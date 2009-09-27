/* $Id: server-fn.c,v 1.91 2009/09/25 17:51:39 tcunha Exp $ */

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

#include <string.h>
#include <time.h>
#include <unistd.h>

#include "tmux.h"

void
server_fill_environ(struct session *s, struct environ *env)
{
	char		 tmuxvar[MAXPATHLEN], *term;
	u_int		 idx;

	if (session_index(s, &idx) != 0)
		fatalx("session not found");
	xsnprintf(tmuxvar, sizeof tmuxvar,
	    "%s,%ld,%u", socket_path, (long) getpid(), idx);
	environ_set(env, "TMUX", tmuxvar);

	term = options_get_string(&s->options, "default-terminal");
	environ_set(env, "TERM", term);
}

void
server_write_error(struct client *c, const char *msg)
{
	struct msg_print_data	printdata;

	strlcpy(printdata.msg, msg, sizeof printdata.msg);
	server_write_client(c, MSG_ERROR, &printdata, sizeof printdata);
}

void
server_write_client(
    struct client *c, enum msgtype type, const void *buf, size_t len)
{
	struct imsgbuf	*ibuf = &c->ibuf;

	if (c->flags & CLIENT_BAD)
		return;
	log_debug("writing %d to client %d", type, c->ibuf.fd);
	imsg_compose(ibuf, type, PROTOCOL_VERSION, -1, -1, (void *) buf, len);
}

void
server_write_session(
    struct session *s, enum msgtype type, const void *buf, size_t len)
{
	struct client	*c;
	u_int		 i;

	for (i = 0; i < ARRAY_LENGTH(&clients); i++) {
		c = ARRAY_ITEM(&clients, i);
		if (c == NULL || c->session == NULL)
			continue;
		if (c->session == s)
			server_write_client(c, type, buf, len);
	}
}

void
server_redraw_client(struct client *c)
{
	c->flags |= CLIENT_REDRAW;
}

void
server_status_client(struct client *c)
{
	c->flags |= CLIENT_STATUS;
}

void
server_redraw_session(struct session *s)
{
	struct client	*c;
	u_int		 i;

	for (i = 0; i < ARRAY_LENGTH(&clients); i++) {
		c = ARRAY_ITEM(&clients, i);
		if (c == NULL || c->session == NULL)
			continue;
		if (c->session == s)
			server_redraw_client(c);
	}
}

void
server_status_session(struct session *s)
{
	struct client	*c;
	u_int		 i;

	for (i = 0; i < ARRAY_LENGTH(&clients); i++) {
		c = ARRAY_ITEM(&clients, i);
		if (c == NULL || c->session == NULL)
			continue;
		if (c->session == s)
			server_status_client(c);
	}
}

void
server_redraw_window(struct window *w)
{
	struct client	*c;
	u_int		 i;

	for (i = 0; i < ARRAY_LENGTH(&clients); i++) {
		c = ARRAY_ITEM(&clients, i);
		if (c == NULL || c->session == NULL)
			continue;
		if (c->session->curw->window == w)
			server_redraw_client(c);
	}
	w->flags |= WINDOW_REDRAW;
}

void
server_status_window(struct window *w)
{
	struct session	*s;
	u_int		 i;

	/*
	 * This is slightly different. We want to redraw the status line of any
	 * clients containing this window rather than any where it is the
	 * current window.
	 */

	for (i = 0; i < ARRAY_LENGTH(&sessions); i++) {
		s = ARRAY_ITEM(&sessions, i);
		if (s != NULL && session_has(s, w))
			server_status_session(s);
	}
}

void
server_lock(struct client *client, struct session *session)
{
	struct client		*c;
	const char		*cmd;
	size_t			 cmdlen;
	struct msg_lock_data	 lockdata;
	u_int			 i;

	for (i = 0; i < ARRAY_LENGTH(&clients); i++) {
		c = ARRAY_ITEM(&clients, i);
		if (c == NULL || c->session == NULL)
			continue;
		if (c->flags & CLIENT_SUSPENDED)
			continue;

		if ((client != NULL && client != c) ||
		    (session != NULL && c->session != session))
			/* We're either locking clients or sessions depending
			 * on how we're called.
			 */
			continue;

		cmd = options_get_string(&c->session->options, "lock-command");
		cmdlen = strlcpy(lockdata.cmd, cmd, sizeof lockdata.cmd);
		if (cmdlen >= sizeof lockdata.cmd)
			return;

		tty_stop_tty(&c->tty);
		tty_raw(&c->tty, tty_term_string(c->tty.term, TTYC_SMCUP));
		tty_raw(&c->tty, tty_term_string(c->tty.term, TTYC_CLEAR));

		c->flags |= CLIENT_SUSPENDED;
		server_write_client(c, MSG_LOCK, &lockdata, sizeof lockdata);
	}
}

void
server_kill_window(struct window *w)
{
	struct session	*s;
	struct winlink	*wl;
	u_int		 i;
	
	for (i = 0; i < ARRAY_LENGTH(&sessions); i++) {
		s = ARRAY_ITEM(&sessions, i);
		if (s == NULL || !session_has(s, w))
			continue;
		if ((wl = winlink_find_by_window(&s->windows, w)) == NULL)
			continue;
		
		if (session_detach(s, wl))
			server_destroy_session(s);
		else
			server_redraw_session(s);
	}
}

int
server_link_window(
    struct winlink *srcwl, struct session *dst, int dstidx,
    int killflag, int selectflag, char **cause)
{
	struct winlink	*dstwl;

	dstwl = NULL;
	if (dstidx != -1)
		dstwl = winlink_find_by_index(&dst->windows, dstidx);
	if (dstwl != NULL) {
		if (dstwl->window == srcwl->window)
			return (0);
		if (killflag) {
			/*
			 * Can't use session_detach as it will destroy session
			 * if this makes it empty.
			 */
			session_alert_cancel(dst, dstwl);
			winlink_stack_remove(&dst->lastw, dstwl);
			winlink_remove(&dst->windows, dstwl);

			/* Force select/redraw if current. */
			if (dstwl == dst->curw)
				selectflag = 1;
		}
	}

	if (dstidx == -1)
		dstidx = -1 - options_get_number(&dst->options, "base-index");
	dstwl = session_attach(dst, srcwl->window, dstidx, cause);
	if (dstwl == NULL)
		return (-1);

	if (!selectflag)
		server_status_session(dst);
	else {
		session_select(dst, dstwl->idx);
		server_redraw_session(dst);
	}

	return (0);
}

void
server_unlink_window(struct session *s, struct winlink *wl)
{
	if (session_detach(s, wl))
		server_destroy_session(s);
	else
		server_redraw_session(s);
}

void
server_destroy_session(struct session *s)
{
	struct client	*c;
	u_int		 i;
	
	for (i = 0; i < ARRAY_LENGTH(&clients); i++) {
		c = ARRAY_ITEM(&clients, i);
		if (c == NULL || c->session != s)
			continue;
		c->session = NULL;
		server_write_client(c, MSG_EXIT, NULL, 0);
	}
}

void
server_set_identify(struct client *c)
{
	struct timeval	tv;
	int		delay;

	delay = options_get_number(&c->session->options, "display-panes-time");
	tv.tv_sec = delay / 1000;
	tv.tv_usec = (delay % 1000) * 1000L;

	if (gettimeofday(&c->identify_timer, NULL) != 0)
		fatal("gettimeofday failed");
	timeradd(&c->identify_timer, &tv, &c->identify_timer);

	c->flags |= CLIENT_IDENTIFY;
	c->tty.flags |= (TTY_FREEZE|TTY_NOCURSOR);
	server_redraw_client(c);
}

void
server_clear_identify(struct client *c)
{
	if (c->flags & CLIENT_IDENTIFY) {
		c->flags &= ~CLIENT_IDENTIFY;
		c->tty.flags &= ~(TTY_FREEZE|TTY_NOCURSOR);
		server_redraw_client(c);
	}
}
