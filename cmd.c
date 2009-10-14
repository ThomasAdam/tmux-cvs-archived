/* $Id: cmd.c,v 1.123 2009/10/12 00:35:08 tcunha Exp $ */

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
#include <sys/time.h>

#include <fnmatch.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tmux.h"

const struct cmd_entry *cmd_table[] = {
	&cmd_attach_session_entry,
	&cmd_bind_key_entry,
	&cmd_break_pane_entry,
	&cmd_choose_client_entry,
	&cmd_choose_session_entry,
	&cmd_choose_window_entry,
	&cmd_clear_history_entry,
	&cmd_clock_mode_entry,
	&cmd_command_prompt_entry,
	&cmd_confirm_before_entry,
	&cmd_copy_buffer_entry,
	&cmd_copy_mode_entry,
	&cmd_delete_buffer_entry,
	&cmd_detach_client_entry,
	&cmd_display_message_entry,
	&cmd_display_panes_entry,
	&cmd_down_pane_entry,
	&cmd_find_window_entry,
	&cmd_has_session_entry,
	&cmd_if_shell_entry,
	&cmd_kill_pane_entry,
	&cmd_kill_server_entry,
	&cmd_kill_session_entry,
	&cmd_kill_window_entry,
	&cmd_last_window_entry,
	&cmd_link_window_entry,
	&cmd_list_buffers_entry,
	&cmd_list_clients_entry,
	&cmd_list_commands_entry,
	&cmd_list_keys_entry,
	&cmd_list_panes_entry,
	&cmd_list_sessions_entry,
	&cmd_list_windows_entry,
	&cmd_load_buffer_entry,
	&cmd_lock_client_entry,
	&cmd_lock_server_entry,
	&cmd_lock_session_entry,
	&cmd_move_window_entry,
	&cmd_new_session_entry,
	&cmd_new_window_entry,
	&cmd_next_layout_entry,
	&cmd_next_window_entry,
	&cmd_paste_buffer_entry,
	&cmd_pipe_pane_entry,
	&cmd_previous_layout_entry,
	&cmd_previous_window_entry,
	&cmd_refresh_client_entry,
	&cmd_rename_session_entry,
	&cmd_rename_window_entry,
	&cmd_resize_pane_entry,
	&cmd_respawn_window_entry,
	&cmd_rotate_window_entry,
	&cmd_run_shell_entry,
	&cmd_save_buffer_entry,
	&cmd_select_layout_entry,
	&cmd_select_pane_entry,
	&cmd_select_prompt_entry,
	&cmd_select_window_entry,
	&cmd_send_keys_entry,
	&cmd_send_prefix_entry,
	&cmd_server_info_entry,
	&cmd_set_buffer_entry,
	&cmd_set_environment_entry,
	&cmd_set_option_entry,
	&cmd_set_window_option_entry,
	&cmd_show_buffer_entry,
	&cmd_show_environment_entry,
	&cmd_show_options_entry,
	&cmd_show_window_options_entry,
	&cmd_source_file_entry,
	&cmd_split_window_entry,
	&cmd_start_server_entry,
	&cmd_suspend_client_entry,
	&cmd_swap_pane_entry,
	&cmd_swap_window_entry,
	&cmd_switch_client_entry,
	&cmd_unbind_key_entry,
	&cmd_unlink_window_entry,
	&cmd_up_pane_entry,
	NULL
};

struct session	*cmd_newest_session(struct sessions *);
struct client	*cmd_newest_client(void);
struct client	*cmd_lookup_client(const char *);
struct session	*cmd_lookup_session(const char *, int *);
struct winlink	*cmd_lookup_window(struct session *, const char *, int *);
int		 cmd_lookup_index(struct session *, const char *, int *);

int
cmd_pack_argv(int argc, char **argv, char *buf, size_t len)
{
	size_t	arglen;
	int	i;

	*buf = '\0';
	for (i = 0; i < argc; i++) {
		if (strlcpy(buf, argv[i], len) >= len)
			return (-1);
		arglen = strlen(argv[i]) + 1;
		buf += arglen;
		len -= arglen;
	}

	return (0);
}

int
cmd_unpack_argv(char *buf, size_t len, int argc, char ***argv)
{
	int	i;
	size_t	arglen;

	if (argc == 0)
		return (0);
	*argv = xcalloc(argc, sizeof **argv);

	buf[len - 1] = '\0';
	for (i = 0; i < argc; i++) {
		if (len == 0) {
			cmd_free_argv(argc, *argv);
			return (-1);
		}

		arglen = strlen(buf) + 1;
		(*argv)[i] = xstrdup(buf);
		buf += arglen;
		len -= arglen;
	}

	return (0);
}

void
cmd_free_argv(int argc, char **argv)
{
	int	i;

	if (argc == 0)
		return; 
	for (i = 0; i < argc; i++) {
		if (argv[i] != NULL)
			xfree(argv[i]);
	}
	xfree(argv);
}

struct cmd *
cmd_parse(int argc, char **argv, char **cause)
{
	const struct cmd_entry **entryp, *entry;
	struct cmd	        *cmd;
	char			 s[BUFSIZ];
	int			 opt, ambiguous = 0;

	*cause = NULL;
	if (argc == 0) {
		xasprintf(cause, "no command");
		return (NULL);
	}

	entry = NULL;
	for (entryp = cmd_table; *entryp != NULL; entryp++) {
		if ((*entryp)->alias != NULL &&
		    strcmp((*entryp)->alias, argv[0]) == 0) {
			ambiguous = 0;
			entry = *entryp;
			break;
		}

		if (strncmp((*entryp)->name, argv[0], strlen(argv[0])) != 0)
			continue;
		if (entry != NULL)
			ambiguous = 1;
		entry = *entryp;

		/* Bail now if an exact match. */
		if (strcmp(entry->name, argv[0]) == 0)
			break;
	}
	if (ambiguous)
		goto ambiguous;
	if (entry == NULL) {
		xasprintf(cause, "unknown command: %s", argv[0]);
		return (NULL);
	}

	optreset = 1;
	optind = 1;
	if (entry->parse == NULL) {
		while ((opt = getopt(argc, argv, "")) != -1) {
			switch (opt) {
			default:
				goto usage;
			}
		}
		argc -= optind;
		argv += optind;
		if (argc != 0)
			goto usage;
	}

	cmd = xmalloc(sizeof *cmd);
	cmd->entry = entry;
	cmd->data = NULL;
	if (entry->parse != NULL) {
		if (entry->parse(cmd, argc, argv, cause) != 0) {
			xfree(cmd);
			return (NULL);
		}
	}
	return (cmd);

ambiguous:
	*s = '\0';
	for (entryp = cmd_table; *entryp != NULL; entryp++) {
		if (strncmp((*entryp)->name, argv[0], strlen(argv[0])) != 0)
			continue;
		if (strlcat(s, (*entryp)->name, sizeof s) >= sizeof s)
			break;
		if (strlcat(s, ", ", sizeof s) >= sizeof s)
			break;
	}
	s[strlen(s) - 2] = '\0';
	xasprintf(cause, "ambiguous command: %s, could be: %s", argv[0], s);
	return (NULL);

usage:
	xasprintf(cause, "usage: %s %s", entry->name, entry->usage);
	return (NULL);
}

int
cmd_exec(struct cmd *cmd, struct cmd_ctx *ctx)
{
	return (cmd->entry->exec(cmd, ctx));
}

void
cmd_free(struct cmd *cmd)
{
	if (cmd->data != NULL && cmd->entry->free != NULL)
		cmd->entry->free(cmd);
	xfree(cmd);
}

size_t
cmd_print(struct cmd *cmd, char *buf, size_t len)
{
	if (cmd->entry->print == NULL) {
		return (xsnprintf(buf, len, "%s", cmd->entry->name));
	}
	return (cmd->entry->print(cmd, buf, len));
}

/*
 * Figure out the current session. Use: 1) the current session, if the command
 * context has one; 2) the session containing the pty of the calling client, if
 * any 3) the session specified in the TMUX variable from the environment (as
 * passed from the client); 3) the newest session.
 */
struct session *
cmd_current_session(struct cmd_ctx *ctx)
{
	struct msg_command_data	*data = ctx->msgdata;
	struct client		*c = ctx->cmdclient;
	struct session		*s;
	struct sessions		 ss;
	struct winlink		*wl;
	struct window_pane	*wp;
	u_int			 i;
	int			 found;

	if (ctx->curclient != NULL && ctx->curclient->session != NULL)
		return (ctx->curclient->session);

	/*
	 * If the name of the calling client's pty is know, build a list of the
	 * sessions that contain it and if any choose either the first or the
	 * newest.
	 */
	if (c != NULL && c->tty.path != NULL) {
		ARRAY_INIT(&ss);
		for (i = 0; i < ARRAY_LENGTH(&sessions); i++) {
			if ((s = ARRAY_ITEM(&sessions, i)) == NULL)
				continue;
			found = 0;
			RB_FOREACH(wl, winlinks, &s->windows) {
				TAILQ_FOREACH(wp, &wl->window->panes, entry) {
					if (strcmp(wp->tty, c->tty.path) == 0) {
						found = 1;
						break;
					}
				}
				if (found)
					break;
			}
			if (found)
				ARRAY_ADD(&ss, s);
		}

		s = cmd_newest_session(&ss);
		ARRAY_FREE(&ss);
		if (s != NULL)
			return (s);
	}

	/* Use the session from the TMUX environment variable. */
	if (data != NULL && data->pid != -1) {
		if (data->pid != getpid())
			return (NULL);
		if (data->idx > ARRAY_LENGTH(&sessions))
			return (NULL);
		if ((s = ARRAY_ITEM(&sessions, data->idx)) == NULL)
			return (NULL);
		return (s);
	}

	return (cmd_newest_session(&sessions));
}

/* Find the newest session. */
struct session *
cmd_newest_session(struct sessions *ss)
{
	struct session	*s, *snewest;
	struct timeval	*tv = NULL;
	u_int		 i;

	snewest = NULL;
	for (i = 0; i < ARRAY_LENGTH(ss); i++) {
		if ((s = ARRAY_ITEM(ss, i)) == NULL)
			continue;

		if (tv == NULL || timercmp(&s->tv, tv, >)) {
			snewest = s;
			tv = &s->tv;
		}
	}

	return (snewest);
}

/* Find the newest client. */
struct client *
cmd_newest_client(void)
{
	struct client	*c, *cnewest;
	struct timeval	*tv = NULL;
	u_int		 i;

	cnewest = NULL;
	for (i = 0; i < ARRAY_LENGTH(&clients); i++) {
		if ((c = ARRAY_ITEM(&clients, i)) == NULL)
			continue;
		if (c->session == NULL)
			continue;

		if (tv == NULL || timercmp(&c->tv, tv, >)) {
			cnewest = c;
			tv = &c->tv;
		}
	}

	return (cnewest);
}

/* Find the target client or report an error and return NULL. */
struct client *
cmd_find_client(struct cmd_ctx *ctx, const char *arg)
{
	struct client	*c;
	struct session	*s;
	char		*tmparg;
	size_t		 arglen;
	u_int		 i;

	/* A NULL argument means the current client. */
	if (arg == NULL) {
		if (ctx->curclient != NULL)
			return (ctx->curclient);
		/*
		 * No current client set. Find the current session and see if
		 * it has only one client.
		 */
		s = cmd_current_session(ctx);
		if (s != NULL) {
			c = NULL;
			for (i = 0; i < ARRAY_LENGTH(&clients); i++) {
				if (ARRAY_ITEM(&clients, i)->session == s) {
					if (c != NULL)
						break;
					c = ARRAY_ITEM(&clients, i);
				}
			}
			if (i == ARRAY_LENGTH(&clients) && c != NULL)
				return (c);
		}
		return (cmd_newest_client());
	}
	tmparg = xstrdup(arg);

	/* Trim a single trailing colon if any. */
	arglen = strlen(tmparg);
	if (arglen != 0 && tmparg[arglen - 1] == ':')
		tmparg[arglen - 1] = '\0';

	/* Find the client, if any. */
	c = cmd_lookup_client(tmparg);

	/* If no client found, report an error. */
	if (c == NULL)
		ctx->error(ctx, "client not found: %s", tmparg);

	xfree(tmparg);
	return (c);
}

/*
 * Lookup a client by device path. Either of a full match and a match without a
 * leading _PATH_DEV ("/dev/") is accepted.
 */
struct client *
cmd_lookup_client(const char *name)
{
	struct client	*c;
	const char	*path;
	u_int		 i;

	for (i = 0; i < ARRAY_LENGTH(&clients); i++) {
		if ((c = ARRAY_ITEM(&clients, i)) == NULL)
			continue;
		if ((path = c->tty.path) == NULL)
			continue;

		/* Check for exact matches. */
		if (strcmp(name, path) == 0)
			return (c);

		/* Check without leading /dev if present. */
		if (strncmp(path, _PATH_DEV, (sizeof _PATH_DEV) - 1) != 0)
			continue;
		if (strcmp(name, path + (sizeof _PATH_DEV) - 1) == 0)
			return (c);
	}

	return (NULL);
}

/* Lookup a session by name. If no session is found, NULL is returned. */
struct session *
cmd_lookup_session(const char *name, int *ambiguous)
{
	struct session	*s, *sfound;
	u_int		 i;

	*ambiguous = 0;

	/*
	 * Look for matches. Session names must be unique so an exact match
	 * can't be ambigious and can just be returned.
	 */
	sfound = NULL;
	for (i = 0; i < ARRAY_LENGTH(&sessions); i++) {	
		if ((s = ARRAY_ITEM(&sessions, i)) == NULL)
			continue;

		/* Check for an exact match and return it if found. */
		if (strcmp(name, s->name) == 0)
			return (s);
		
		/* Then check for pattern matches. */
		if (strncmp(name, s->name, strlen(name)) == 0 ||
		    fnmatch(name, s->name, 0) == 0) {
			if (sfound != NULL) {
				*ambiguous = 1;
				return (NULL);
			}
			sfound = s;
		}
	}
		
 	return (sfound);
}

/*
 * Lookup a window or return -1 if not found or ambigious. First try as an
 * index and if invalid, use fnmatch or leading prefix. Return NULL but fill in
 * idx if the window index is a valid number but there is now window with that
 * index.
 */
struct winlink *
cmd_lookup_window(struct session *s, const char *name, int *ambiguous)
{
	struct winlink	*wl, *wlfound;
	const char	*errstr;
	u_int		 idx;

	*ambiguous = 0;

	/* First see if this is a valid window index in this session. */
	idx = strtonum(name, 0, INT_MAX, &errstr);
	if (errstr == NULL) {
		if ((wl = winlink_find_by_index(&s->windows, idx)) != NULL)
			return (wl);
	}
		
	/* Look for exact matches, error if more than one. */
	wlfound = NULL;
	RB_FOREACH(wl, winlinks, &s->windows) {
		if (strcmp(name, wl->window->name) == 0) {
			if (wlfound != NULL) {
				*ambiguous = 1;
				return (NULL);
			}
			wlfound = wl;
		}
	}
	if (wlfound != NULL)
		return (wlfound);

	/* Now look for pattern matches, again error if multiple. */
	wlfound = NULL;
	RB_FOREACH(wl, winlinks, &s->windows) {
		if (strncmp(name, wl->window->name, strlen(name)) == 0 ||
		    fnmatch(name, wl->window->name, 0) == 0) {
			if (wlfound != NULL) {
				*ambiguous = 1;
				return (NULL);
			}
			wlfound = wl;
		}
	}	
	if (wlfound != NULL)
		return (wlfound);

	return (NULL);
}

/*
 * Find a window index - if the window doesn't exist, check if it is a
 * potential index and return it anyway.
 */
int
cmd_lookup_index(struct session *s, const char *name, int *ambiguous)
{
	struct winlink	*wl;
	const char	*errstr;
	u_int		 idx;

	if ((wl = cmd_lookup_window(s, name, ambiguous)) != NULL)
		return (wl->idx);
	if (*ambiguous)
		return (-1);

	idx = strtonum(name, 0, INT_MAX, &errstr);
	if (errstr == NULL)
		return (idx);

	return (-1);
}

/* Find the target session or report an error and return NULL. */
struct session *
cmd_find_session(struct cmd_ctx *ctx, const char *arg)
{
	struct session	*s;
	struct client	*c;
	char		*tmparg;
	size_t		 arglen;
	int		 ambiguous;

	/* A NULL argument means the current session. */
	if (arg == NULL)
		return (cmd_current_session(ctx));
	tmparg = xstrdup(arg);

	/* Trim a single trailing colon if any. */
	arglen = strlen(tmparg);
	if (arglen != 0 && tmparg[arglen - 1] == ':')
		tmparg[arglen - 1] = '\0';

	/* Find the session, if any. */
	s = cmd_lookup_session(tmparg, &ambiguous);

	/* If it doesn't, try to match it as a client. */
	if (s == NULL && (c = cmd_lookup_client(tmparg)) != NULL)
		s = c->session;

	/* If no session found, report an error. */
	if (s == NULL) {
		if (ambiguous)
			ctx->error(ctx, "more than one session: %s", tmparg);
		else
			ctx->error(ctx, "session not found: %s", tmparg);
	}

	xfree(tmparg);
	return (s);
}

/* Find the target session and window or report an error and return NULL. */
struct winlink *
cmd_find_window(struct cmd_ctx *ctx, const char *arg, struct session **sp)
{
	struct session	*s;
	struct winlink	*wl;
	const char	*winptr;
	char		*sessptr = NULL;
	int		 ambiguous = 0;

	/*
	 * Find the current session. There must always be a current session, if
	 * it can't be found, report an error.
	 */
	if ((s = cmd_current_session(ctx)) == NULL) {
		ctx->error(ctx, "can't establish current session");
		return (NULL);
	}

	/* A NULL argument means the current session and window. */
	if (arg == NULL) {
		if (sp != NULL)
			*sp = s;
		return (s->curw);
	}

	/* Time to look at the argument. If it is empty, that is an error. */
	if (*arg == '\0')
		goto not_found;

	/* Find the separating colon and split into window and session. */
	winptr = strchr(arg, ':');
	if (winptr == NULL)
		goto no_colon;
	winptr++;	/* skip : */
	sessptr = xstrdup(arg);
	*strchr(sessptr, ':') = '\0';

	/* Try to lookup the session if present. */
	if (*sessptr != '\0') {
		if ((s = cmd_lookup_session(sessptr, &ambiguous)) == NULL)
			goto no_session;
	}
	if (sp != NULL)
		*sp = s;

	/*
	 * Then work out the window. An empty string is the current window,
	 * otherwise try to look it up in the session.
	 */
	if (*winptr == '\0')
		wl = s->curw;
	else if ((wl = cmd_lookup_window(s, winptr, &ambiguous)) == NULL)
		goto not_found;
	
	if (sessptr != NULL)
		xfree(sessptr);
	return (wl);

no_colon:
	/* No colon in the string, first try as a window then as a session. */
	if ((wl = cmd_lookup_window(s, arg, &ambiguous)) == NULL) {
		if (ambiguous)
			goto not_found;
		if ((s = cmd_lookup_session(arg, &ambiguous)) == NULL)
			goto no_session;
		wl = s->curw;
	}

	if (sp != NULL)
		*sp = s;

	return (wl);

no_session:
	if (ambiguous)
		ctx->error(ctx, "multiple sessions: %s", arg);
	else
		ctx->error(ctx, "session not found: %s", arg);
	if (sessptr != NULL)
		xfree(sessptr);
	return (NULL);

not_found:
	if (ambiguous)
		ctx->error(ctx, "multiple windows: %s", arg);
	else
		ctx->error(ctx, "window not found: %s", arg);
	if (sessptr != NULL)
		xfree(sessptr);
	return (NULL);
}

/*
 * Find the target session and window index, whether or not it exists in the
 * session. Return -2 on error or -1 if no window index is specified. This is
 * used when parsing an argument for a window target that may not exist (for
 * example if it is going to be created).
 */
int
cmd_find_index(struct cmd_ctx *ctx, const char *arg, struct session **sp)
{
	struct session	*s;
	const char	*winptr;
	char		*sessptr = NULL;
	int		 idx, ambiguous = 0;

	/*
	 * Find the current session. There must always be a current session, if
	 * it can't be found, report an error.
	 */
	if ((s = cmd_current_session(ctx)) == NULL) {
		ctx->error(ctx, "can't establish current session");
		return (-2);
	}

	/* A NULL argument means the current session and "no window" (-1). */
	if (arg == NULL) {
		if (sp != NULL)
			*sp = s;
		return (-1);
	}

	/* Time to look at the argument. If it is empty, that is an error. */
	if (*arg == '\0')
		goto not_found;

	/* Find the separating colon. If none, assume the current session. */
	winptr = strchr(arg, ':');
	if (winptr == NULL)
		goto no_colon;
	winptr++;	/* skip : */
	sessptr = xstrdup(arg);
	*strchr(sessptr, ':') = '\0';

	/* Try to lookup the session if present. */
	if (sessptr != NULL && *sessptr != '\0') {
		if ((s = cmd_lookup_session(sessptr, &ambiguous)) == NULL)
			goto no_session;
	}
	if (sp != NULL)
		*sp = s;

	/*
	 * Then work out the window. An empty string is a new window otherwise
	 * try to look it up in the session.
	 */
	if (*winptr == '\0')
		 idx = -1;
	else if ((idx = cmd_lookup_index(s, winptr, &ambiguous)) == -1) {
		if (ambiguous)
			goto not_found;
		ctx->error(ctx, "invalid index: %s", arg);
		idx = -2;
	}
	
	if (sessptr != NULL)
		xfree(sessptr);
	return (idx);

no_colon:
	/* No colon in the string, first try as a window then as a session. */
	if ((idx = cmd_lookup_index(s, arg, &ambiguous)) == -1) {
		if (ambiguous)
			goto not_found;
		if ((s = cmd_lookup_session(arg, &ambiguous)) == NULL)
			goto no_session;
		idx = -1;
	}

	if (sp != NULL)
		*sp = s;

	return (idx);

no_session:
	if (ambiguous)
 		ctx->error(ctx, "multiple sessions: %s", arg);
	else
		ctx->error(ctx, "session not found: %s", arg);
	if (sessptr != NULL)
		xfree(sessptr);
	return (-2);

not_found:
	if (ambiguous)
		ctx->error(ctx, "multiple windows: %s", arg);
	else
		ctx->error(ctx, "window not found: %s", arg);
	if (sessptr != NULL)
		xfree(sessptr);
	return (-2);
}

/*
 * Find the target session, window and pane number or report an error and
 * return NULL. The pane number is separated from the session:window by a .,
 * such as mysession:mywindow.0.
 */
struct winlink *
cmd_find_pane(struct cmd_ctx *ctx,
    const char *arg, struct session **sp, struct window_pane **wpp)
{
	struct session	*s;
	struct winlink	*wl;
	const char	*period;
	char		*winptr, *paneptr;
	const char	*errstr;
	u_int		 idx;

	/* Get the current session. */
	if ((s = cmd_current_session(ctx)) == NULL) {
       		ctx->error(ctx, "can't establish current session");
		return (NULL);
	}
	if (sp != NULL)
		*sp = s;

	/* A NULL argument means the current session, window and pane. */
	if (arg == NULL) {
		*wpp = s->curw->window->active;
		return (s->curw);
	}

	/* Look for a separating period. */
	if ((period = strrchr(arg, '.')) == NULL)
		goto no_period;

	/* Pull out the window part and parse it. */
	winptr = xstrdup(arg);
	winptr[period - arg] = '\0';
	if (*winptr == '\0')
		wl = s->curw;
	else if ((wl = cmd_find_window(ctx, winptr, sp)) == NULL)
		goto error;

	/* Find the pane section and look it up. */
	paneptr = winptr + (period - arg) + 1;
	if (*paneptr == '\0')
		*wpp = wl->window->active;
	else {
		idx = strtonum(paneptr, 0, INT_MAX, &errstr);
		if (errstr != NULL) {
			ctx->error(ctx, "pane %s: %s", errstr, paneptr);
			goto error;
		}
		*wpp = window_pane_at_index(wl->window, idx);
		if (*wpp == NULL) {
			ctx->error(ctx, "no such pane: %u", idx);
			goto error;
		}
	}

	xfree(winptr);
	return (wl);

no_period:
	/* Try as a pane number alone. */
	idx = strtonum(arg, 0, INT_MAX, &errstr);
	if (errstr != NULL)
		goto lookup_window;

	/* Try index in the current session and window. */
	if ((*wpp = window_pane_at_index(s->curw->window, idx)) == NULL)
		goto lookup_window;
	
	return (s->curw);

lookup_window:
	/* Try as a window and use the active pane. */
	if ((wl = cmd_find_window(ctx, arg, sp)) != NULL)
		*wpp = wl->window->active;
	return (wl);
	
error:
	xfree(winptr);
	return (NULL);
}

/* Replace the first %% or %idx in template by s. */
char *
cmd_template_replace(char *template, const char *s, int idx)
{
	char	 ch;
	char	*buf, *ptr;
	int	 replaced;
	size_t	 len;

	if (strstr(template, "%") == NULL)
		return (xstrdup(template));

	buf = xmalloc(1);
	*buf = '\0';
	len = 0;
	replaced = 0;

	ptr = template;
	while (*ptr != '\0') {
		switch (ch = *ptr++) {
		case '%':
			if (*ptr < '1' || *ptr > '9' || *ptr - '0' != idx) {
				if (*ptr != '%' || replaced)
					break;
				replaced = 1;
			}
			ptr++;

			len += strlen(s);
			buf = xrealloc(buf, 1, len + 1);
			strlcat(buf, s, len + 1);
			continue;
		}
		buf = xrealloc(buf, 1, len + 2);
		buf[len++] = ch;
		buf[len] = '\0';
	}

	return (buf);
}
