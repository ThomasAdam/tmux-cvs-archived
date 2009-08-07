/* $Id$ */

/*
 * Copyright (c) 2007 Nicholas Marriott <nicm@users.sourceforge.net>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <pwd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "tmux.h"

#ifdef DEBUG
/* DragonFly uses an OpenBSD-like malloc() since 1.6 */
#if defined(__OpenBSD__) || defined(__DragonFly__)
const char	*malloc_options = "AFGJPX";
#endif
#ifdef __FreeBSD__
const char	*_malloc_options = "AJX";
#endif
#endif

volatile sig_atomic_t sigwinch;
volatile sig_atomic_t sigterm;
volatile sig_atomic_t sigcont;
volatile sig_atomic_t sigchld;
volatile sig_atomic_t sigusr1;
volatile sig_atomic_t sigusr2;

char		*cfg_file;
struct options	 global_s_options;	/* session options */
struct options	 global_w_options;	/* window options */

int		 server_locked;
u_int		 password_failures;
char		*server_password;
time_t		 server_activity;

int		 debug_level;
int		 be_quiet;
time_t		 start_time;
char		*socket_path;

__dead void	 usage(void);
char 		*makesockpath(const char *);
int		 prepare_unlock(enum msgtype *, void **, size_t *, int);
int		 prepare_cmd(enum msgtype *, void **, size_t *, int, char **);

#ifndef HAVE_PROGNAME
char      *__progname = (char *) "tmux";
#endif

__dead void
usage(void)
{
	fprintf(stderr,
	    "usage: %s [-28dqUuv] [-f file] [-L socket-name] [-S socket-path]\n"
	    "            [command [flags]]\n",
	    __progname);
	exit(1);
}

void
logfile(const char *name)
{
	char	*path;

	log_close();
	if (debug_level > 0) {
		xasprintf(
		    &path, "%s-%s-%ld.log", __progname, name, (long) getpid());
		log_open_file(debug_level, path);
		xfree(path);
	}
}

void
sighandler(int sig)
{
	int	saved_errno;

	saved_errno = errno;
	switch (sig) {
	case SIGWINCH:
		sigwinch = 1;
		break;
	case SIGTERM:
		sigterm = 1;
		break;
	case SIGCHLD:
		sigchld = 1;
		break;
	case SIGCONT:
		sigcont = 1;
		break;
	case SIGUSR1:
		sigusr1 = 1;
		break;
	case SIGUSR2:
		sigusr2 = 1;
		break;
	}
	errno = saved_errno;
}

void
siginit(void)
{
	struct sigaction	 act;

	memset(&act, 0, sizeof act);
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_RESTART;

	act.sa_handler = SIG_IGN;
	if (sigaction(SIGPIPE, &act, NULL) != 0)
		fatal("sigaction failed");
	if (sigaction(SIGINT, &act, NULL) != 0)
		fatal("sigaction failed");
	if (sigaction(SIGTSTP, &act, NULL) != 0)
		fatal("sigaction failed");
	if (sigaction(SIGQUIT, &act, NULL) != 0)
		fatal("sigaction failed");

	act.sa_handler = sighandler;
	if (sigaction(SIGWINCH, &act, NULL) != 0)
		fatal("sigaction failed");
	if (sigaction(SIGTERM, &act, NULL) != 0)
		fatal("sigaction failed");
	if (sigaction(SIGCHLD, &act, NULL) != 0)
		fatal("sigaction failed");
	if (sigaction(SIGUSR1, &act, NULL) != 0)
		fatal("sigaction failed");
	if (sigaction(SIGUSR2, &act, NULL) != 0)
		fatal("sigaction failed");
}

void
sigreset(void)
{
	struct sigaction act;

	memset(&act, 0, sizeof act);
	sigemptyset(&act.sa_mask);

	act.sa_handler = SIG_DFL;
	if (sigaction(SIGPIPE, &act, NULL) != 0)
		fatal("sigaction failed");
	if (sigaction(SIGUSR1, &act, NULL) != 0)
		fatal("sigaction failed");
	if (sigaction(SIGUSR2, &act, NULL) != 0)
		fatal("sigaction failed");
	if (sigaction(SIGINT, &act, NULL) != 0)
		fatal("sigaction failed");
	if (sigaction(SIGTSTP, &act, NULL) != 0)
		fatal("sigaction failed");
	if (sigaction(SIGQUIT, &act, NULL) != 0)
		fatal("sigaction failed");
	if (sigaction(SIGWINCH, &act, NULL) != 0)
		fatal("sigaction failed");
	if (sigaction(SIGTERM, &act, NULL) != 0)
		fatal("sigaction failed");
	if (sigaction(SIGCHLD, &act, NULL) != 0)
		fatal("sigaction failed");
}

char *
makesockpath(const char *label)
{
	char		base[MAXPATHLEN], *path;
	struct stat	sb;
	u_int		uid;

	uid = getuid();
	xsnprintf(base, MAXPATHLEN, "%s/%s-%d", _PATH_TMP, __progname, uid);

	if (mkdir(base, S_IRWXU) != 0 && errno != EEXIST)
		return (NULL);

	if (lstat(base, &sb) != 0)
		return (NULL);
	if (!S_ISDIR(sb.st_mode)) {
		errno = ENOTDIR;
		return (NULL);
	}
	if (sb.st_uid != uid || (sb.st_mode & (S_IRWXG|S_IRWXO)) != 0) {
		errno = EACCES;
		return (NULL);
	}

	xasprintf(&path, "%s/%s", base, label);
	return (path);
}

int
prepare_unlock(enum msgtype *msg, void **buf, size_t *len, int argc)
{
	static struct msg_unlock_data	 unlockdata;
	char				*pass;

	if (argc != 0) {
		log_warnx("can't specify a command when unlocking");
		return (-1);
	}
	
	if ((pass = getpass("Password: ")) == NULL)
		return (-1);

	if (strlen(pass) >= sizeof unlockdata.pass) {
		log_warnx("password too long");
		return (-1);
	}
		
	strlcpy(unlockdata.pass, pass, sizeof unlockdata.pass);
	memset(pass, 0, strlen(pass));

	*buf = &unlockdata;
	*len = sizeof unlockdata;

	*msg = MSG_UNLOCK;
	return (0);
}

int
prepare_cmd(enum msgtype *msg, void **buf, size_t *len, int argc, char **argv)
{
	static struct msg_command_data	 cmddata;

	client_fill_session(&cmddata);
	
	cmddata.argc = argc;
	if (cmd_pack_argv(argc, argv, cmddata.argv, sizeof cmddata.argv) != 0) {
		log_warnx("command too long");
		return (-1);
	}

	*buf = &cmddata;
	*len = sizeof cmddata;

	*msg = MSG_COMMAND;
	return (0);
}

int
main(int argc, char **argv)
{
	struct client_ctx	 cctx;
	struct cmd_list		*cmdlist;
 	struct cmd		*cmd;
	struct pollfd	 	 pfd;
	enum msgtype		 msg;
	struct hdr	 	 hdr;
	struct passwd		*pw;
	struct msg_print_data	 printdata;
	char			*s, *path, *label, *home, *cause;
	char			 cwd[MAXPATHLEN];
	void			*buf;
	size_t			 len;
	int	 		 retcode, opt, flags, unlock, cmdflags = 0;

	unlock = flags = 0;
	label = path = NULL;
        while ((opt = getopt(argc, argv, "28df:L:qS:uUv")) != -1) {
                switch (opt) {
		case '2':
			flags |= IDENTIFY_256COLOURS;
			flags &= ~IDENTIFY_88COLOURS;
			break;
		case '8':
			flags |= IDENTIFY_88COLOURS;
			flags &= ~IDENTIFY_256COLOURS;
			break;
		case 'f':
			if (cfg_file)
				xfree(cfg_file);
			cfg_file = xstrdup(optarg);
			break;
		case 'L':
			if (label != NULL)
				xfree(label);
			label = xstrdup(optarg);
			break;
		case 'S':
			if (path != NULL)
				xfree(path);
			path = xstrdup(optarg);
			break;
		case 'q':
			be_quiet = 1;
			break;
		case 'u':
			flags |= IDENTIFY_UTF8;
			break;
		case 'U':
			unlock = 1;
			break;
		case 'd':
			flags |= IDENTIFY_HASDEFAULTS;
			break;
		case 'v':
			debug_level++;
			break;
                default:
			usage();
                }
        }
	argc -= optind;
	argv += optind;

	log_open_tty(debug_level);
	siginit();

	if (!(flags & IDENTIFY_UTF8)) {
		/*
		 * If the user has set whichever of LC_ALL, LC_CTYPE or LANG
		 * exist (in that order) to contain UTF-8, it is a safe
		 * assumption that either they are using a UTF-8 terminal, or
		 * if not they know that output from UTF-8-capable programs may
		 * be wrong.
		 */
		if ((s = getenv("LC_ALL")) == NULL) {
			if ((s = getenv("LC_CTYPE")) == NULL)
				s = getenv("LANG");
		}
		if (s != NULL && strcasestr(s, "UTF-8") != NULL)
			flags |= IDENTIFY_UTF8;
	}

	options_init(&global_s_options, NULL);
	options_set_number(&global_s_options, "bell-action", BELL_ANY);
	options_set_number(&global_s_options, "buffer-limit", 9);
	options_set_string(&global_s_options, "default-command", "%s", "");
	options_set_string(&global_s_options, "default-terminal", "screen");
	options_set_number(&global_s_options, "display-time", 750);
	options_set_number(&global_s_options, "history-limit", 2000);
	options_set_number(&global_s_options, "lock-after-time", 0);
	options_set_number(&global_s_options, "message-attr", GRID_ATTR_REVERSE);
	options_set_number(&global_s_options, "message-bg", 3);
	options_set_number(&global_s_options, "message-fg", 0);
	options_set_number(&global_s_options, "prefix", '\002');
	options_set_number(&global_s_options, "repeat-time", 500);
	options_set_number(&global_s_options, "set-remain-on-exit", 0);
	options_set_number(&global_s_options, "set-titles", 0);
	options_set_number(&global_s_options, "status", 1);
	options_set_number(&global_s_options, "status-attr", GRID_ATTR_REVERSE);
	options_set_number(&global_s_options, "status-bg", 2);
	options_set_number(&global_s_options, "status-fg", 0);
	options_set_number(&global_s_options, "status-interval", 15);
	options_set_number(&global_s_options, "status-keys", MODEKEY_EMACS);
	options_set_number(&global_s_options, "status-justify", 0);
	options_set_string(&global_s_options, "status-left", "[#S]");
	options_set_number(&global_s_options, "status-left-attr", 0);
	options_set_number(&global_s_options, "status-left-fg", 8);
	options_set_number(&global_s_options, "status-left-bg", 8);
	options_set_number(&global_s_options, "status-left-length", 10);
	options_set_number(&global_s_options, "status-right-length", 40);
	options_set_number(&global_s_options, "status-right-attr", 0);
	options_set_number(&global_s_options, "status-right-fg", 8);
	options_set_number(&global_s_options, "status-right-bg", 8);
 	options_set_string(
	    &global_s_options, "status-right", "\"#22T\" %%H:%%M %%d-%%b-%%y");
	if (flags & IDENTIFY_UTF8)
		options_set_number(&global_s_options, "status-utf8", 1);
	else
		options_set_number(&global_s_options, "status-utf8", 0);
	options_set_number(&global_s_options, "visual-activity", 0);
	options_set_number(&global_s_options, "visual-bell", 0);
	options_set_number(&global_s_options, "visual-content", 0);

	options_init(&global_w_options, NULL);
	options_set_number(&global_w_options, "aggressive-resize", 0);
	options_set_number(&global_w_options, "automatic-rename", 1);
	options_set_number(&global_w_options, "clock-mode-colour", 4);
	options_set_number(&global_w_options, "clock-mode-style", 1);
	options_set_number(&global_w_options, "force-height", 0);
	options_set_number(&global_w_options, "force-width", 0);
	options_set_number(&global_w_options, "main-pane-width", 81);
	options_set_number(&global_w_options, "main-pane-height", 24);
	options_set_number(&global_w_options, "mode-attr", GRID_ATTR_REVERSE);
	options_set_number(&global_w_options, "mode-bg", 3);
	options_set_number(&global_w_options, "mode-fg", 0);
	options_set_number(&global_w_options, "mode-keys", MODEKEY_EMACS);
	options_set_number(&global_w_options, "mode-mouse", 1);
	options_set_number(&global_w_options, "monitor-activity", 0);
	options_set_string(&global_w_options, "monitor-content", "%s", "");
	if (flags & IDENTIFY_UTF8)
		options_set_number(&global_w_options, "utf8", 1);
	else
		options_set_number(&global_w_options, "utf8", 0);
	options_set_number(&global_w_options, "window-status-attr", 0);
	options_set_number(&global_w_options, "window-status-bg", 8);
	options_set_number(&global_w_options, "window-status-fg", 8);
	options_set_number(&global_w_options, "window-status-current-attr", 0);
	options_set_number(&global_w_options, "window-status-current-bg", 8);
	options_set_number(&global_w_options, "window-status-current-fg", 8);
	options_set_number(&global_w_options, "xterm-keys", 0);
 	options_set_number(&global_w_options, "remain-on-exit", 0);

	if (cfg_file == NULL) {
		home = getenv("HOME");
		if (home == NULL || *home == '\0') {
			pw = getpwuid(getuid());
			if (pw != NULL)
				home = pw->pw_dir;
		}
		xasprintf(&cfg_file, "%s/%s", home, DEFAULT_CFG);
		if (access(cfg_file, R_OK) != 0) {
			xfree(cfg_file);
			cfg_file = NULL;
		}
	} else {
		if (access(cfg_file, R_OK) != 0) {
			log_warn("%s", cfg_file);
			exit(1);
		}
	}
	
	if (label == NULL)
		label = xstrdup("default");
	if (path == NULL && (path = makesockpath(label)) == NULL) {
		log_warn("can't create socket");
		exit(1);
	}
	xfree(label);

	if (getcwd(cwd, sizeof cwd) == NULL) {
		pw = getpwuid(getuid());
		if (pw->pw_dir != NULL && *pw->pw_dir != '\0')
			strlcpy(cwd, pw->pw_dir, sizeof cwd);
		else
			strlcpy(cwd, "/", sizeof cwd);
	}
	options_set_string(&global_s_options, "default-path", "%s", cwd);

	if (unlock) {
		if (prepare_unlock(&msg, &buf, &len, argc) != 0)
			exit(1);
	} else {
		if (prepare_cmd(&msg, &buf, &len, argc, argv) != 0)
			exit(1);
	}

	if (unlock)
		cmdflags &= ~CMD_STARTSERVER;
	else if (argc == 0)
		cmdflags |= CMD_STARTSERVER;
	else {
		/*
		 * It sucks parsing the command string twice (in client and
		 * later in server) but it is necessary to get the start server
		 * flag.
		 */
		if ((cmdlist = cmd_list_parse(argc, argv, &cause)) == NULL) {
			log_warnx("%s", cause);
			exit(1);
		}
		cmdflags &= ~CMD_STARTSERVER;
		TAILQ_FOREACH(cmd, cmdlist, qentry) {
			if (cmd->entry->flags & CMD_STARTSERVER) {
				cmdflags |= CMD_STARTSERVER;
				break;
			}
		}
		cmd_list_free(cmdlist);
	}

 	memset(&cctx, 0, sizeof cctx);
	if (client_init(path, &cctx, cmdflags, flags) != 0)
		exit(1);
	xfree(path);

	client_write_server(&cctx, msg, buf, len);
	memset(buf, 0, len);

	retcode = 0;
	for (;;) {
		pfd.fd = cctx.srv_fd;
		pfd.events = POLLIN;
		if (BUFFER_USED(cctx.srv_out) > 0)
			pfd.events |= POLLOUT;

		if (poll(&pfd, 1, INFTIM) == -1) {
			if (errno == EAGAIN || errno == EINTR)
				continue;
			fatal("poll failed");
		}

		if (buffer_poll(&pfd, cctx.srv_in, cctx.srv_out) != 0)
			goto out;

	restart:
		if (BUFFER_USED(cctx.srv_in) < sizeof hdr)
			continue;
		memcpy(&hdr, BUFFER_OUT(cctx.srv_in), sizeof hdr);
		if (BUFFER_USED(cctx.srv_in) < (sizeof hdr) + hdr.size)
			continue;
		buffer_remove(cctx.srv_in, sizeof hdr);

		switch (hdr.type) {
		case MSG_EXIT:
		case MSG_SHUTDOWN:
			goto out;
		case MSG_ERROR:
			retcode = 1;
			/* FALLTHROUGH */
		case MSG_PRINT:
			if (hdr.size < sizeof printdata)
				fatalx("bad MSG_PRINT size");
			buffer_read(cctx.srv_in, &printdata, sizeof printdata);

			printdata.msg[(sizeof printdata.msg) - 1] = '\0';
			log_info("%s", printdata.msg);
			goto restart;
		case MSG_READY:
			retcode = client_main(&cctx);
			goto out;
		default:
			fatalx("unexpected command");
		}
	}

out:
	options_free(&global_s_options);
	options_free(&global_w_options);

	close(cctx.srv_fd);
	buffer_destroy(cctx.srv_in);
	buffer_destroy(cctx.srv_out);

	return (retcode);
}
