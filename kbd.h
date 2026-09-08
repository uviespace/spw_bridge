/**
 * @file kbd.h
 *
 * @brief public interface of the keyboard monitor: toggles the PUS debug
 *        output when 'd' or 'D' is pressed
 */

#ifndef KBD_H
#define KBD_H

#include <spw_bridge.h>


/**
 * @brief start the keyboard monitor
 *
 * @param cfg the bridge configuration; the PUS debug flag is toggled when
 *	      the user presses 'd' or 'D'
 *
 * @note the monitor thread is detached, only acts when stdin is connected
 *	 to a terminal, and restores the terminal settings on exit
 */

void kbd_start(struct bridge_cfg *cfg);


/**
 * @brief restore the terminal settings changed by the keyboard monitor
 *
 * @note called by the application once the main loop winds down
 */

void kbd_stop(void);


#endif /* KBD_H */