#ifndef CLOCK_CONSOLE_H
#define CLOCK_CONSOLE_H

/* USB-Serial/JTAG console: menu + one-line command framework.
 * console_init() installs the USB-Serial/JTAG driver, switches the VFS to it,
 * registers all command modules and spawns the REPL task. The REPL shows the
 * menu when a terminal session starts and on every empty line. */
void console_init(void);
void console_print_menu(void);

#endif
