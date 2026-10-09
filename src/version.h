#ifndef CLOCK_VERSION_H
#define CLOCK_VERSION_H

/* The firmware version is set in one place: platformio.ini passes it as
 * CLOCK_FW_VERSION (build_flags reach the main component). The fallback only
 * marks builds that bypass platformio.ini. */
#ifndef CLOCK_FW_VERSION
#define CLOCK_FW_VERSION "0.0.0-unknown"
#endif

#define FIRMWARE_NAME "mistral-clock"
#define FIRMWARE_VERSION CLOCK_FW_VERSION

#endif
