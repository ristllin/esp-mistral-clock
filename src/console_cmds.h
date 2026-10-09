#ifndef CLOCK_CONSOLE_CMDS_H
#define CLOCK_CONSOLE_CMDS_H

/* Panel debug helpers (console_cmds.c) and `fb dump`
 * (fbdump.c), registered by console_init(). */
void console_register_commands(void);
int fbdump_cmd(int argc, char **argv);

#endif
