/* $Id: cmd-list-windows.c,v 1.44 2010/12/06 21:56:32 nicm Exp $ */

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

#include <unistd.h>

#include "tmux.h"

/*
 * List windows on given session.
 */

int	cmd_list_windows_exec(struct cmd *, struct cmd_ctx *);

const struct cmd_entry cmd_list_windows_entry = {
	"list-windows", "lsw",
	CMD_TARGET_SESSION_USAGE,
	0, "",
	cmd_target_init,
	cmd_target_parse,
	cmd_list_windows_exec,
	cmd_target_free,
	cmd_target_print
};

int
cmd_list_windows_exec(struct cmd *self, struct cmd_ctx *ctx)
{
	struct cmd_target_data	*data = self->data;
	struct session		*s;
	struct winlink		*wl;
	char			*layout;

	if ((s = cmd_find_session(ctx, data->target)) == NULL)
		return (-1);

	RB_FOREACH(wl, winlinks, &s->windows) {
		layout = layout_dump(wl->window);
		ctx->print(ctx, "%d: %s [%ux%u] [layout %s]%s",
		    wl->idx, wl->window->name, wl->window->sx, wl->window->sy,
		    layout, wl == s->curw ? " (active)" : "");
		xfree(layout);
	}

	return (0);
}
