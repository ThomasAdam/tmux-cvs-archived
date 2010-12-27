/* $Id: cmd-choose-session.c,v 1.15 2009/11/14 17:56:39 tcunha Exp $ */

/*
 * Copyright (c) 2009 Nicholas Marriott <nicm@users.sourceforge.net>
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

#include <ctype.h>

#include "tmux.h"

/*
 * Enter choice mode to choose a session.
 */

int	cmd_choose_session_exec(struct cmd *, struct cmd_ctx *);

void	cmd_choose_session_callback(void *, int);
void	cmd_choose_session_free(void *);

const struct cmd_entry cmd_choose_session_entry = {
	"choose-session", NULL,
	CMD_TARGET_WINDOW_USAGE " [template]",
	CMD_ARG01, "",
	cmd_target_init,
	cmd_target_parse,
	cmd_choose_session_exec,
	cmd_target_free,
	cmd_target_print
};

struct cmd_choose_session_data {
	struct client	*client;
	char   		*template;
};

int
cmd_choose_session_exec(struct cmd *self, struct cmd_ctx *ctx)
{
	struct cmd_target_data		*data = self->data;
	struct cmd_choose_session_data	*cdata;
	struct winlink			*wl;
	u_int			 	 cur;

	if (ctx->curclient == NULL) {
		ctx->error(ctx, "must be run interactively");
		return (-1);
	}

	if ((wl = cmd_find_window(ctx, data->target, NULL)) == NULL)
		return (-1);

	if (window_pane_set_mode(wl->window->active, &window_choose_mode) != 0)
		return (0);

	cur = window_choose_item_data(ctx, wl, WIN_CHOOSE_SESSIONS);

	cdata = xmalloc(sizeof *cdata);
	if (data->arg != NULL)
		cdata->template = xstrdup(data->arg);
	else
		cdata->template = xstrdup("switch-client -t '%%'");
	cdata->client = ctx->curclient;
	cdata->client->references++;

	window_choose_ready(wl->window->active,
	    cur, cmd_choose_session_callback, cmd_choose_session_free, cdata);

	return (0);
}

void
cmd_choose_session_callback(void *data, int idx)
{
	struct cmd_choose_session_data	*cdata = data;
	struct session			*s;
	struct cmd_list			*cmdlist;
	struct cmd_ctx			 ctx;
	char				*template, *cause;

	if (idx == -1)
		return;
	if (cdata->client->flags & CLIENT_DEAD)
		return;

	if ((u_int) idx > ARRAY_LENGTH(&sessions) - 1)
		return;
	s = ARRAY_ITEM(&sessions, idx);
	if (s == NULL)
		return;
	template = cmd_template_replace(cdata->template, s->name, 1);

	if (cmd_string_parse(template, &cmdlist, &cause) != 0) {
		if (cause != NULL) {
			*cause = toupper((u_char) *cause);
			status_message_set(cdata->client, "%s", cause);
			xfree(cause);
		}
		xfree(template);
		return;
	}
	xfree(template);

	ctx.msgdata = NULL;
	ctx.curclient = cdata->client;

	ctx.error = key_bindings_error;
	ctx.print = key_bindings_print;
	ctx.info = key_bindings_info;

	ctx.cmdclient = NULL;

	cmd_list_exec(cmdlist, &ctx);
	cmd_list_free(cmdlist);
}

void
cmd_choose_session_free(void *data)
{
	struct cmd_choose_session_data	*cdata = data;

	cdata->client->references--;
	xfree(cdata->template);
	xfree(cdata);
}
