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

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "tmux.h"

SPLAY_GENERATE(key_bindings, key_binding, entry, key_bindings_cmp);

struct key_bindings	key_bindings;
struct key_bindings	dead_key_bindings;

int
key_bindings_cmp(struct key_binding *bd1, struct key_binding *bd2)
{
	int	key1, key2;
	int	key3, key4;

	key1 = bd1->key & ~KEYC_PREFIX;
	key2 = bd2->key & ~KEYC_PREFIX;

	key3 = bd1->key2 & ~KEYC_PREFIX;
	key4 = bd2->key2 & ~KEYC_PREFIX;

	if (key1 != key2)
		return (key1 - key2);

	if (key3 != key4)
		return (key3 - key4);

	if ((bd1->key & KEYC_PREFIX && !(bd2->key & KEYC_PREFIX)) ||
		(bd1->key2 & KEYC_PREFIX && !(bd2->key2 & KEYC_PREFIX)))
		return (-1);

	if ((bd2->key & KEYC_PREFIX && !(bd1->key & KEYC_PREFIX)) ||
		(bd1->key2 & KEYC_PREFIX && !(bd1->key2 & KEYC_PREFIX)))
		return (1);
	return (0);
}

struct key_binding *
key_bindings_lookup(int key)
{
	struct key_binding	bd;

	bd.key = key;

	return (SPLAY_FIND(key_bindings, &key_bindings, &bd));
}

void
key_bindings_add(int key, int key2, int has_second_key,
		int can_repeat, struct cmd_list *cmdlist)
{
	struct key_binding	*bd;

	key_bindings_remove(key);
	key_bindings_remove(key2);
		    
	bd = xmalloc(sizeof *bd);
	bd->key = key;
	bd->key2 = key2;

	log_debug2( "Adding key2: %d and is second key: %d",
			key2, has_second_key);

	SPLAY_INSERT(key_bindings, &key_bindings, bd);
	
	bd->can_repeat = can_repeat;
	bd->has_second_key = has_second_key;
	bd->cmdlist = cmdlist;
}

void
key_bindings_remove(int key)
{
	struct key_binding	*bd;

	if ((bd = key_bindings_lookup(key)) == NULL)
		return;
	SPLAY_REMOVE(key_bindings, &key_bindings, bd);
	SPLAY_INSERT(key_bindings, &dead_key_bindings, bd);
}

void
key_bindings_clean(void)
{
	struct key_binding	*bd;

	while (!SPLAY_EMPTY(&dead_key_bindings)) {
		bd = SPLAY_ROOT(&dead_key_bindings);
		SPLAY_REMOVE(key_bindings, &dead_key_bindings, bd);
		cmd_list_free(bd->cmdlist);
		xfree(bd);
	}
}

void
key_bindings_init(void)
{
	static const struct {
		int			 key;
		int			 key2;
		int			 has_second_key;
		int			 can_repeat;
		const struct cmd_entry	*entry;
	} table[] = {
		{ ' ',		KEYC_NONE,0,		0, &cmd_next_layout_entry },
		{ '!',		KEYC_NONE,0, 		0, &cmd_break_pane_entry },
		{ '"',		KEYC_NONE,0,		0, &cmd_split_window_entry },	
		{ '%',		KEYC_NONE,0,		0, &cmd_split_window_entry },	
		{ '#',		KEYC_NONE,0, 		0, &cmd_list_buffers_entry },
		{ '&',		KEYC_NONE,0, 		0, &cmd_confirm_before_entry },
		{ ',',		KEYC_NONE,0, 		0, &cmd_command_prompt_entry },
		{ '-',		KEYC_NONE,0, 		0, &cmd_delete_buffer_entry },
		{ '.',		KEYC_NONE,0, 		0, &cmd_command_prompt_entry },
		{ '0',		KEYC_NONE,0, 		0, &cmd_select_window_entry },
		{ '1',		KEYC_NONE,0, 		0, &cmd_select_window_entry },
		{ '2',		KEYC_NONE,0,		0, &cmd_select_window_entry },
		{ '3',		KEYC_NONE,0,		0, &cmd_select_window_entry },
		{ '4',		KEYC_NONE,0,		0, &cmd_select_window_entry },
		{ '5',		KEYC_NONE,0,		0, &cmd_select_window_entry },
		{ '6',		KEYC_NONE,0,		0, &cmd_select_window_entry },
		{ '7',		KEYC_NONE,0,		0, &cmd_select_window_entry },
		{ '8',		KEYC_NONE,0,		0, &cmd_select_window_entry },
		{ '9',		KEYC_NONE,0,		0, &cmd_select_window_entry },
		{ ':',		KEYC_NONE,0,		0, &cmd_command_prompt_entry },
		{ '=',		KEYC_NONE,0,		0, &cmd_scroll_mode_entry },
		{ '?',		KEYC_NONE,0,		0, &cmd_list_keys_entry },
		{ '[',		KEYC_NONE,0,		0, &cmd_copy_mode_entry },
		{ '\'',		KEYC_NONE,0,		0, &cmd_select_prompt_entry },
		{ '\032',	KEYC_NONE,0, /* C-z */	0, &cmd_suspend_client_entry },
		{ ']',		KEYC_NONE,0,		0, &cmd_paste_buffer_entry },
		{ 'c',  	'c',1,			0, &cmd_new_window_entry },
		{ 'd',		KEYC_NONE,0,		0, &cmd_detach_client_entry },
		{ 'f',		KEYC_NONE,0,		0, &cmd_command_prompt_entry },
		{ 'i',		KEYC_NONE,0,		0, &cmd_display_message_entry },
		{ 'l',		KEYC_NONE,0,		0, &cmd_last_window_entry },
		{ 'n',		KEYC_NONE,0,		0, &cmd_next_window_entry },
		{ 'o',		KEYC_NONE,0,		0, &cmd_down_pane_entry },
		{ 'p',		KEYC_NONE,0,		0, &cmd_previous_window_entry },
		{ 'r',		KEYC_NONE,0,		0, &cmd_refresh_client_entry },
		{ 's',		KEYC_NONE,0,		0, &cmd_choose_session_entry },
		{ 't',		KEYC_NONE,0,		0, &cmd_clock_mode_entry },
		{ 'w',		KEYC_NONE,0,		0, &cmd_choose_window_entry },
		{ 'x',		KEYC_NONE,0,		0, &cmd_confirm_before_entry },
		{ '{',		KEYC_NONE,0,		0, &cmd_swap_pane_entry },
		{ '}',		KEYC_NONE,0,		0, &cmd_swap_pane_entry },
		{ '\002', KEYC_NONE,0,			0, &cmd_send_prefix_entry },
		{ '1' | KEYC_ESCAPE, KEYC_NONE,0,	0, &cmd_select_layout_entry },
		{ '2' | KEYC_ESCAPE, KEYC_NONE,0,	0, &cmd_select_layout_entry },
		{ '3' | KEYC_ESCAPE, KEYC_NONE,0,	0, &cmd_select_layout_entry },
		{ '4' | KEYC_ESCAPE, KEYC_NONE,0,	0, &cmd_select_layout_entry },
		{ KEYC_PPAGE,	     KEYC_NONE,0, 	0, &cmd_scroll_mode_entry },
		{ 'n' | KEYC_ESCAPE, KEYC_NONE,0, 	0, &cmd_next_window_entry },
		{ 'p' | KEYC_ESCAPE, KEYC_NONE,0, 	0, &cmd_previous_window_entry },
		{ KEYC_UP,	     KEYC_NONE,0,	0, &cmd_up_pane_entry },
		{ KEYC_DOWN,	     KEYC_NONE,0,	0, &cmd_down_pane_entry },
		{ KEYC_UP | KEYC_ESCAPE,    KEYC_NONE,0,  1, &cmd_resize_pane_entry },
		{ KEYC_DOWN | KEYC_ESCAPE,  KEYC_NONE,0,  1, &cmd_resize_pane_entry },
		{ KEYC_LEFT | KEYC_ESCAPE,  KEYC_NONE,0,  1, &cmd_resize_pane_entry },
		{ KEYC_RIGHT | KEYC_ESCAPE, KEYC_NONE,0,  1, &cmd_resize_pane_entry },
		{ KEYC_UP | KEYC_CTRL,	    KEYC_NONE,0,  1, &cmd_resize_pane_entry },
		{ KEYC_DOWN | KEYC_CTRL,    KEYC_NONE,0,  1, &cmd_resize_pane_entry },	
		{ KEYC_LEFT | KEYC_CTRL,    KEYC_NONE,0,  1, &cmd_resize_pane_entry },
		{ KEYC_RIGHT | KEYC_CTRL,   KEYC_NONE,0,  1, &cmd_resize_pane_entry },
		{ 'o' | KEYC_ESCAPE,	    KEYC_NONE,0,  0, &cmd_rotate_window_entry },
		{ '\017',	            KEYC_NONE,0,  0, &cmd_rotate_window_entry },
	};
	u_int		 i;
	struct cmd	*cmd;
	struct cmd_list	*cmdlist;

	SPLAY_INIT(&key_bindings);

	for (i = 0; i < nitems(table); i++) {
		cmdlist = xmalloc(sizeof *cmdlist);
		TAILQ_INIT(cmdlist);

		cmd = xmalloc(sizeof *cmd);
		cmd->entry = table[i].entry;
		cmd->data = NULL;
		if (cmd->entry->init != NULL)
			cmd->entry->init(cmd, table[i].key);
		TAILQ_INSERT_HEAD(cmdlist, cmd, qentry);

		key_bindings_add(table[i].key | KEYC_PREFIX, table[i].key2, 
		    table[i].has_second_key, table[i].can_repeat, cmdlist);
	}
}

void
key_bindings_free(void)
{
	struct key_binding	*bd;

	key_bindings_clean();
	while (!SPLAY_EMPTY(&key_bindings)) {
		bd = SPLAY_ROOT(&key_bindings);
		SPLAY_REMOVE(key_bindings, &key_bindings, bd);
		cmd_list_free(bd->cmdlist);
		xfree(bd);
	}
}

void printflike2
key_bindings_error(struct cmd_ctx *ctx, const char *fmt, ...)
{
	va_list	ap;
	char   *msg;

	va_start(ap, fmt);
	xvasprintf(&msg, fmt, ap);
	va_end(ap);

	*msg = toupper((u_char) *msg);
 	status_message_set(ctx->curclient, "%s", msg);
	xfree(msg);
}

void printflike2
key_bindings_print(struct cmd_ctx *ctx, const char *fmt, ...)
{
	struct winlink	*wl = ctx->cursession->curw;
	va_list		 ap;

	if (wl->window->active->mode != &window_more_mode)
		window_pane_reset_mode(wl->window->active);
	window_pane_set_mode(wl->window->active, &window_more_mode);

	va_start(ap, fmt);
	window_more_vadd(wl->window->active, fmt, ap);
	va_end(ap);
}

void printflike2
key_bindings_info(struct cmd_ctx *ctx, const char *fmt, ...)
{
	va_list	ap;
	char   *msg;

	if (be_quiet)
		return;

	va_start(ap, fmt);
	xvasprintf(&msg, fmt, ap);
	va_end(ap);

	*msg = toupper((u_char) *msg);
 	status_message_set(ctx->curclient, "%s", msg);
	xfree(msg);
}

void
key_bindings_dispatch(struct key_binding *bd, struct client *c)
{
	struct cmd_ctx	 	 ctx;

	ctx.msgdata = NULL;
	ctx.cursession = c->session;
	ctx.curclient = c;

	ctx.error = key_bindings_error;
	ctx.print = key_bindings_print;
	ctx.info = key_bindings_info;

	ctx.cmdclient = NULL;

	cmd_list_exec(bd->cmdlist, &ctx);
}
