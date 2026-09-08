/**
 * @file kbd.c
 *
 * @copyright GPLv2
 * Copyright (C) 2018-2026 Armin Luntzer (armin.luntzer@univie.ac.at)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * @brief terminal keyboard monitor of the SpaceWire bridge: toggles the
 *        PUS debug output when 'd' or 'D' is pressed
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <unistd.h>
#include <termios.h>

#include <pthread.h>

#include <spw_bridge.h>
#include <kbd.h>


static struct termios kbd_old_term;
static bool kbd_term_saved;


static void kbd_restore_term(void)
{
	if (kbd_term_saved)
		tcsetattr(STDIN_FILENO, TCSANOW, &kbd_old_term);
}


static void *kbd_monitor(void *ptr)
{
	uint8_t ch;

	struct termios kbd_new_term;

	struct bridge_cfg *cfg;


	cfg = (struct bridge_cfg *)ptr;

	if (!isatty(STDIN_FILENO))
		return NULL;

	if (tcgetattr(STDIN_FILENO, &kbd_old_term))
		return NULL;

	kbd_term_saved = true;

	/* switch to single-key mode, keep everything else unchanged */
	kbd_new_term = kbd_old_term;
	kbd_new_term.c_lflag &= (tcflag_t)~(ICANON | ECHO);
	kbd_new_term.c_cc[VMIN] = 1;
	kbd_new_term.c_cc[VTIME] = 0;
	if (tcsetattr(STDIN_FILENO, TCSANOW, &kbd_new_term))
		return NULL;

	while (read(STDIN_FILENO, &ch, 1) == 1) {
		if (ch != 'd' && ch != 'D')
			continue;

		cfg->pus_debug = !cfg->pus_debug;
		printf("PUS debug %s\n", cfg->pus_debug ? "enabled" : "disabled");
	}

	kbd_restore_term();

	return NULL;
}


/**
 * @brief start the keyboard monitor
 *
 * @param cfg the bridge configuration; the PUS debug flag is toggled when
 *	      the user presses 'd' or 'D'
 *
 * @note the monitor thread is detached, only acts when stdin is connected
 *	 to a terminal, and restores the terminal settings on exit
 */

void kbd_start(struct bridge_cfg *cfg)
{
	int ret;

	pthread_t th;


	if ((ret = pthread_create(&th, NULL, kbd_monitor, cfg))) {
		fprintf(stderr, "kbd monitor: %s\n", strerror(ret));
		return;
	}

	pthread_detach(th);
}


/**
 * @brief restore the terminal settings changed by the keyboard monitor
 *
 * @note called by the application once the main loop winds down
 */

void kbd_stop(void)
{
	kbd_restore_term();
}