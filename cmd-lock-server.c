/* $Id: cmd-lock-server.c,v 1.12 2011/04/06 22:24:00 nicm Exp $ */

/*
 * Copyright (c) 2008 Nicholas Marriott <nicm@users.sourceforge.net>
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

#include <pwd.h>
#include <string.h>
#include <unistd.h>

#include "tmux.h"

/*
 * Lock commands.
 */

int	cmd_lock_server_exec(struct cmd *, struct cmd_ctx *);

const struct cmd_entry cmd_lock_server_entry = {
	"lock-server", "lock",
	"", 0, 0,
	"",
	0,
	NULL,
	NULL,
	cmd_lock_server_exec
};

const struct cmd_entry cmd_lock_session_entry = {
	"lock-session", "locks",
	"t:", 0, 0,
	CMD_TARGET_SESSION_USAGE,
	0,
	NULL,
	NULL,
	cmd_lock_server_exec
};

const struct cmd_entry cmd_lock_client_entry = {
	"lock-client", "lockc",
	"t:", 0, 0,
	CMD_TARGET_CLIENT_USAGE,
	0,
	NULL,
	NULL,
	cmd_lock_server_exec
};

/* ARGSUSED */
int
cmd_lock_server_exec(struct cmd *self, unused struct cmd_ctx *ctx)
{
	struct args	*args = self->args;
	struct client	*c;
	struct session	*s;

	if (self->entry == &cmd_lock_server_entry)
		server_lock();
	else if (self->entry == &cmd_lock_session_entry) {
		if ((s = cmd_find_session(ctx, args_get(args, 't'), 0)) == NULL)
			return (-1);
		server_lock_session(s);
	} else {
		if ((c = cmd_find_client(ctx, args_get(args, 't'))) == NULL)
			return (-1);
		server_lock_client(c);
	}
	recalculate_sizes();

	return (0);
}
