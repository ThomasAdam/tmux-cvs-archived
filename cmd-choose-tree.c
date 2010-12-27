/* $id$ */

/*
 * Copyright (c) 2010 Thomas Adam <thomas@xteddy.org>
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

/* Construct a tree of sessions and their windows to choose from. */
int	cmd_choose_tree_exec(struct cmd *, struct cmd_ctx *);
void	cmd_choose_tree_callback(void *, int);
void	cmd_choose_tree_free(void *);

const struct cmd_entry cmd_choose_tree_entry = {
	"choose-tree", NULL,
	CMD_TARGET_WINDOW_USAGE,
	CMD_ARG01, "",
	cmd_target_init,
	cmd_target_parse,
	cmd_choose_tree_exec,
	cmd_target_free,
	cmd_target_print
};

struct cmd_choose_tree_data {
	struct client	*client;
	struct session	*session;
	char   		*window_template;
	char		*session_template;
};

int
cmd_choose_tree_exec(struct cmd *self, struct cmd_ctx *ctx)
{
	struct cmd_target_data		*data = self->data;
	struct cmd_choose_tree_data	*cdata;
	struct winlink			*wl;
	u_int				 cur_win;

	if (ctx->curclient == NULL) {
		ctx->error(ctx, "must be run interactively");
		return (-1);
	}

	if ((wl = cmd_find_window(ctx, data->target, NULL)) == NULL)
		return (-1);

	if (window_pane_set_mode(wl->window->active, &window_choose_mode) != 0)
		return (0);

	cur_win = window_choose_item_data(ctx, wl, WIN_CHOOSE_WINDOWS_SESSIONS);

	/* TA:  FIXME - currently broken. */
	cdata = xmalloc(sizeof *cdata);
	cdata->session_template = xstrdup("switch-client -t '%%'");
	cdata->window_template = xstrdup("select-window -t '%%'");
	
	cdata->client = ctx->curclient;
	cdata->client->references++;
	cdata->session = ctx->curclient->session;
	cdata->session->references++;

	window_choose_ready(wl->window->active,
	    cur_win, cmd_choose_tree_callback, cmd_choose_tree_free, cdata);

	return (0);
}

void
cmd_choose_tree_callback(void *data, int idx)
{
	struct cmd_choose_tree_data	*cdata = data;
	struct cmd_list			*cmdlist;
	struct cmd_ctx			 ctx;
	char				*target, *template, *cause;

	if (idx == -1)
		return;
	if (cdata->client->flags & CLIENT_DEAD)
		return;
	if (cdata->session->flags & SESSION_DEAD)
		return;
	if (cdata->client->session != cdata->session)
		return;

	xasprintf(&target, "%s:%d", cdata->session->name, idx);
	template = cmd_template_replace(cdata->window_template, target, 1);
	xfree(target);

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
cmd_choose_tree_free(void *data)
{
	struct cmd_choose_tree_data	*cdata = data;

	cdata->session->references--;
	cdata->client->references--;
	xfree(cdata->window_template);
	xfree(cdata->session_template);
	xfree(cdata);
}
