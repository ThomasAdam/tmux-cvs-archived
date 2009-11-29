/* $Id: cfg.c,v 1.24 2009/11/28 14:50:36 tcunha Exp $ */

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
#include <sys/stat.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "tmux.h"

/*
 * Config file parser. Pretty quick and simple, each line is parsed into a
 * argv array and executed as a command.
 */

void printflike2 cfg_print(struct cmd_ctx *, const char *, ...);
void printflike2 cfg_error(struct cmd_ctx *, const char *, ...);

char	 *cfg_cause;

/* ARGSUSED */
void printflike2
cfg_print(unused struct cmd_ctx *ctx, unused const char *fmt, ...)
{
}

/* ARGSUSED */
void printflike2
cfg_error(unused struct cmd_ctx *ctx, const char *fmt, ...)
{
	va_list	ap;

	va_start(ap, fmt);
	xvasprintf(&cfg_cause, fmt, ap);
	va_end(ap);
}

int
load_cfg(const char *path, struct cmd_ctx *ctxin, char **cause)
{
	FILE		*f;
	u_int		 n;
	char		*buf, *line, *ptr;
	size_t		 len;
	struct cmd_list	*cmdlist;
	struct cmd_ctx	 ctx;

	if ((f = fopen(path, "rb")) == NULL) {
		xasprintf(cause, "%s: %s", path, strerror(errno));
		return (1);
	}
	n = 0;

	line = NULL;
	while ((buf = fgetln(f, &len))) {
		if (buf[len - 1] == '\n')
			buf[len - 1] = '\0';
		else {
			line = xrealloc(line, 1, len + 1);
			memcpy(line, buf, len);
			line[len] = '\0';
			buf = line;
		}
		n++;

		if (cmd_string_parse(buf, &cmdlist, cause) != 0) {
			if (*cause == NULL)
				continue;
			goto error;
		}
		if (cmdlist == NULL)
			continue;
		cfg_cause = NULL;

		if (ctxin == NULL) {
			ctx.msgdata = NULL;
			ctx.curclient = NULL;
			ctx.cmdclient = NULL;
		} else {
			ctx.msgdata = ctxin->msgdata;
			ctx.curclient = ctxin->curclient;
			ctx.cmdclient = ctxin->cmdclient;
		}

		ctx.error = cfg_error;
		ctx.print = cfg_print;
		ctx.info = cfg_print;
		ctx.depth = 0;

		cfg_cause = NULL;
		cmd_list_exec(cmdlist, &ctx);
		cmd_list_free(cmdlist);
		if (cfg_cause != NULL) {
			*cause = cfg_cause;
			goto error;
		}
	}
	if (line != NULL)
		xfree(line);
	fclose(f);

	return (0);

error:
	if (line != NULL)
		xfree(line);
	fclose(f);

	xasprintf(&ptr, "%s: %s at line %u", path, *cause, n);
	xfree(*cause);
	*cause = ptr;
	return (1);
}
