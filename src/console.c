/* Console: own REPL over the native USB-Serial/JTAG port.
 *
 * esp_console's linenoise REPL is not used: it ignores empty lines and
 * offers no hook to show a short menu when a terminal opens. This REPL
 * prints the menu at start and on every empty line, echoes input
 * byte-by-byte (terminals on serial devices normally have local echo off)
 * and runs everything through esp_console_run(), so every
 * menu action is also a one-line command and `help` lists them all.
 *
 * Line endings: CR, LF and CRLF are all accepted as exactly one line ending
 * (the VFS is set to CRLF mode and the byte loop de-duplicates the pair), so
 * scripted "\r\n" does not produce a phantom empty line + menu reprint.
 *
 * Menu triggers (see README "Console" for the documented behaviour):
 *   - at boot, and whenever the USB host connection appears
 *     (usb_serial_jtag_is_connected() false->true transition);
 *   - on every empty line;
 *   - on the first input after >= 3 s of console silence;
 *   - when the host starts polling our CDC IN endpoint (drain-edge task
 *     below). The ESP32-C6 USB-Serial/JTAG peripheral surfaces no port-open
 *     / DTR-RTS event to software (CDC line-state requests are absorbed by
 *     the hardware), but the host only sends IN tokens to our data endpoint
 *     while a terminal has the port open, so "host started draining" is a
 *     usable terminal-opened edge. "First input after silence" is kept as a
 *     fallback for hosts that poll differently.
 *
 * The USB-Serial/JTAG driver/VFS setup mirrors esp_console_new_repl_usb_
 * serial_jtag exactly, which keeps esptool auto-reset (DTR/RTS) working. */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_console.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "hal/usb_serial_jtag_ll.h"

#include "console.h"
#include "console_cmds.h"
#include "version.h"
#include "saver.h"
#include "scene.h"
#include "theme_mgr.h"
#include "time_mgr.h"
#include "touch.h"
#include "ui_settings.h"
#include "weather_mgr.h"
#include "wifi_mgr.h"

#define CONSOLE_PROMPT "esp> "
#define CONSOLE_LINE_MAX 512
#define CONSOLE_IDLE_ARM_MS 3000   /* silence before the menu is armed */

static const char *TAG = "console";

void console_print_menu(void)
{
    printf("--- %s v%s ---\n", FIRMWARE_NAME, FIRMWARE_VERSION);
    printf(" wifi:  scan | add <ssid> <pass> [prio] | remove <ssid> | list | reorder <ssid> <n> | status | connect\n");
    printf(" theme: dark | light | mid | auto\n");
    printf(" wx:    weather | loc | loc auto | loc <lat> <lon> <name> | loc default | weather force <cond>|off\n");
    printf(" scene: scene [city|nature|window|auto]\n");
    printf(" saver: saver [on|off|now|timeout <s>|scenario <name>]\n");
    printf(" misc:  settings | fb dump | touch inject <x> <y> [hold_ms] | brightness <0-100>\n");
    printf(" time:  clock | clock override <epoch|off>\n");
    printf(" debug: version | orient | rawfill | lcdreg | reboot\n");
    printf(" 'help' lists all commands\n");
}

static void print_banner_menu(void)
{
    printf("\r\n%s v%s ready - %s console\r\n", FIRMWARE_NAME,
           FIRMWARE_VERSION, CONFIG_IDF_TARGET);
    console_print_menu();
}

static int menu_cmd(int argc, char **argv)
{
    (void)argc; (void)argv;
    console_print_menu();
    return 0;
}

static int version_cmd(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("%s v%s (%s, %d MHz, %d KB flash)\n", FIRMWARE_NAME, FIRMWARE_VERSION,
           CONFIG_IDF_TARGET, CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
           CONFIG_ESPTOOLPY_FLASHSIZE_8MB ? 8192 : 4096);
    return 0;
}

static int reboot_cmd(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("rebooting...\n");
    fflush(stdout);
    esp_restart();
    return 0;
}

/* --- terminal-open detection (drain edge) --------------------------------
 * TOKEN_REC_IN_EP1 is a raw status bit the IDF USB-Serial/JTAG driver never
 * enables or clears, so polling it from here is race-free with the driver.
 * The host polls our CDC IN endpoint only while a terminal has the port
 * open; when the port closes, the tokens stop. */
static volatile uint32_t s_in_tokens;   /* IN tokens seen on EP1 */
static volatile bool s_port_open;       /* host is draining EP1 */
static TickType_t s_last_token_tick;

static int usbmon_cmd(int argc, char **argv)
{
    (void)argc; (void)argv;
    uint32_t raw = usb_serial_jtag_ll_get_intraw_mask();
    printf("usb host attached (SOF):    %s\n",
           usb_serial_jtag_is_connected() ? "yes" : "no");
    printf("port open (host polling IN): %s\n", s_port_open ? "yes" : "no");
    printf("IN tokens seen on EP1:       %u\n", (unsigned)s_in_tokens);
    printf("TOKEN_REC_IN_EP1 raw bit:    %d\n",
           (raw & USB_SERIAL_JTAG_INTR_TOKEN_REC_IN_EP1) ? 1 : 0);
    return 0;
}

static void register_core_commands(void)
{
    const esp_console_cmd_t commands[] = {
        {.command = "menu", .help = "show the command menu", .hint = NULL, .func = &menu_cmd},
        {.command = "version", .help = "firmware version and chip info", .hint = NULL, .func = &version_cmd},
        {.command = "reboot", .help = "restart the device", .hint = NULL, .func = &reboot_cmd},
        {.command = "usbmon", .help = "USB-Serial/JTAG host/port-open monitor state", .hint = NULL, .func = &usbmon_cmd},
    };
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&commands[i]));
    }
}

/* --- line editor -------------------------------------------------------
 * Byte-wise input with per-byte echo. The password argument of
 * `wifi add <ssid> <pass>` is not echoed: a fixed "****" is printed once
 * instead (passwords are never printed or logged); the real line still
 * reaches esp_console_run. */

static char s_line[CONSOLE_LINE_MAX];
static size_t s_len;
static bool s_saw_cr;              /* de-duplicate CRLF */
static TickType_t s_last_input;
static bool s_menu_armed;          /* menu prints on the next input byte */
static bool s_host_seen;           /* host connection already announced */

/* Is the cursor inside the password token of `wifi add <ssid> <pass>`?
 * Tokens: 0=wifi 1=add 2=ssid 3=pass (and beyond, masked as well). The line
 * is tokenized with the same state machine as esp_console_split_argv
 * (spaces, double quotes, backslash escapes), so no spelling of the command
 * that the console accepts can bypass the mask. */
static bool cursor_in_password(void)
{
    enum { SP, ARG, QUOTED } st = SP;
    bool esc = false;
    char t[2][6] = { "", "" };     /* first two tokens, truncated */
    size_t tl = 0;
    int tok = -1;
    for (size_t i = 0; i < s_len; i++) {
        char c = s_line[i];
        int out = -1;
        if (esc) {                       /* only \\, \" and "\ " survive */
            if (c == '\\' || c == '"' || c == ' ') { out = c; }
            esc = false;
        } else if (st == SP) {
            if (c == ' ') { continue; }
            tok++;
            tl = 0;
            if (c == '"') { st = QUOTED; }
            else if (c == '\\') { st = ARG; esc = true; }
            else { st = ARG; out = c; }
        } else if (c == '\\') {
            esc = true;
        } else if ((st == QUOTED && c == '"') || (st == ARG && c == ' ')) {
            st = SP;                     /* a closing quote ends the token */
        } else {
            out = c;
        }
        if (out >= 0 && tok < 2 && tl < sizeof(t[0]) - 1) {
            t[tok][tl++] = (char)out;
            t[tok][tl] = '\0';
        }
    }
    int cur = (st == SP) ? tok + 1 : tok;   /* token the next byte joins */
    return cur >= 3 && !strcmp(t[0], "wifi") && !strcmp(t[1], "add");
}

static void submit_line(void)
{
    printf("\r\n");
    if (s_len == 0) {
        console_print_menu();
    } else {
        s_line[s_len] = '\0';
        int ret = 0;
        esp_err_t err = esp_console_run(s_line, &ret);
        if (err == ESP_ERR_NOT_FOUND) {
            /* name only: the arguments may hold a mistyped password */
            const char *cmd = s_line + strspn(s_line, " ");
            printf("unknown command: %.*s (try 'help')\n",
                   (int)strcspn(cmd, " "), cmd);
        } else if (err != ESP_OK) {
            printf("error: %s\n", esp_err_to_name(err));
        }
        /* the line may hold a `wifi add` password: do not keep it around */
        memset(s_line, 0, s_len);
    }
    s_len = 0;
    printf(CONSOLE_PROMPT);
    fflush(stdout);
}

static void handle_byte(int c)
{
    saver_notify_console();   /* console activity resets the saver idle timer */
    if (c == '\r') {
        s_saw_cr = true;
        submit_line();
        return;
    }
    if (c == '\n') {
        if (s_saw_cr) {            /* LF of a CRLF pair: one line ending */
            s_saw_cr = false;
            return;
        }
        submit_line();
        return;
    }
    s_saw_cr = false;
    if (c == 0x7F || c == 0x08) {  /* backspace */
        if (s_len > 0) {
            s_len--;
            printf("\b \b");
            fflush(stdout);
        }
        return;
    }
    if (c < 0x20 || c > 0x7E) {    /* other control bytes: ignore */
        return;
    }
    if (s_len >= CONSOLE_LINE_MAX - 1) { return; }
    bool was_pass = cursor_in_password();
    s_line[s_len++] = (char)c;
    bool mask = cursor_in_password();
    if (mask && !was_pass) {
        /* fixed mask printed once when the password token starts:
         * per-character '*' would leak the password length */
        printf("****");
    } else if (!mask) {
        putchar(c);
    }
    fflush(stdout);
}

/* One non-blocking byte from stdin, or -1 when nothing is available. */
static int read_byte_nb(void)
{
    unsigned char c;
    int r = read(fileno(stdin), &c, 1);
    return r == 1 ? (int)c : -1;
}

static void repl_task(void *arg)
{
    (void)arg;

    print_banner_menu();
    printf(CONSOLE_PROMPT);
    fflush(stdout);
    s_last_input = xTaskGetTickCount();
    s_host_seen = usb_serial_jtag_is_connected();

    while (1) {
        int c = read_byte_nb();
        if (c < 0) {
            if (!s_host_seen && usb_serial_jtag_is_connected()) {
                /* host just attached (e.g. USB power-only boot) */
                s_host_seen = true;
                print_banner_menu();
                printf(CONSOLE_PROMPT);
                fflush(stdout);
            } else if (!s_menu_armed &&
                       xTaskGetTickCount() - s_last_input >
                           pdMS_TO_TICKS(CONSOLE_IDLE_ARM_MS)) {
                /* quiet console: the next keypress shows the menu first,
                 * which is what a freshly opened terminal needs */
                s_menu_armed = true;
            }
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        s_last_input = xTaskGetTickCount();
        if (s_menu_armed) {
            s_menu_armed = false;
            print_banner_menu();
            printf(CONSOLE_PROMPT);
            fflush(stdout);
            /* The keypress that woke the menu was CR/LF (e.g. the user
             * pressed Enter on a fresh terminal): the menu above already
             * answered it, so swallow the line ending instead of letting
             * submit_line() print the menu a second time. */
            if (c == '\r') {
                s_saw_cr = true;
                continue;
            }
            if (c == '\n') {
                continue;
            }
        }
        handle_byte(c);
    }
}

static void drain_edge_task(void *arg)
{
    (void)arg;
    /* The boot menu (repl_task) already greets a port that is open from
     * reset: if IN tokens are already flowing when we start, count that
     * session as greeted. Otherwise arm for the first open. */
    vTaskDelay(pdMS_TO_TICKS(500));
    bool menu_shown;
    if (usb_serial_jtag_ll_get_intraw_mask() &
        USB_SERIAL_JTAG_INTR_TOKEN_REC_IN_EP1) {
        usb_serial_jtag_ll_clr_intsts_mask(
            USB_SERIAL_JTAG_INTR_TOKEN_REC_IN_EP1);
        menu_shown = true;
        s_port_open = true;
        s_last_token_tick = xTaskGetTickCount();
    } else {
        menu_shown = false;
    }
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(200));
        uint32_t raw = usb_serial_jtag_ll_get_intraw_mask();
        if (raw & USB_SERIAL_JTAG_INTR_TOKEN_REC_IN_EP1) {
            usb_serial_jtag_ll_clr_intsts_mask(
                USB_SERIAL_JTAG_INTR_TOKEN_REC_IN_EP1);
            s_in_tokens++;
            s_last_token_tick = xTaskGetTickCount();
            s_port_open = true;
            /* Greet a freshly opened terminal, but never mid-line and not
             * twice in a row. */
            if (!menu_shown && s_len == 0 &&
                xTaskGetTickCount() - s_last_input > pdMS_TO_TICKS(1000)) {
                menu_shown = true;
                print_banner_menu();
                printf(CONSOLE_PROMPT);
                fflush(stdout);
                s_last_input = xTaskGetTickCount();
                s_menu_armed = false;
            }
        } else if (s_port_open &&
                   xTaskGetTickCount() - s_last_token_tick >
                       pdMS_TO_TICKS(3000)) {
            s_port_open = false;    /* port closed: re-arm for next open */
            menu_shown = false;
        }
    }
}

void console_init(void)
{
    /* Same setup as esp_console_new_repl_usb_serial_jtag, minus linenoise.
     * RX line endings CRLF: the VFS folds a CRLF pair into one LF; the REPL
     * byte loop treats CR, LF and CRLF as one line ending in any case. */
    usb_serial_jtag_vfs_set_rx_line_endings(ESP_LINE_ENDINGS_CRLF);
    usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);
    fcntl(fileno(stdout), F_SETFL, 0);
    fcntl(fileno(stdin), F_SETFL, O_NONBLOCK);

    usb_serial_jtag_driver_config_t usb_cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    esp_err_t err = usb_serial_jtag_driver_install(&usb_cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "usb_serial_jtag_driver_install: %s", esp_err_to_name(err));
    }
    usb_serial_jtag_vfs_use_driver();

    esp_console_config_t console_cfg = {
        .max_cmdline_length = CONSOLE_LINE_MAX,
        .max_cmdline_args = 64,
        .heap_alloc_caps = MALLOC_CAP_DEFAULT,
        .hint_color = 39,
        .hint_bold = 0,
    };
    err = esp_console_init(&console_cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_console_init: %s", esp_err_to_name(err));
    }

    register_core_commands();
    console_register_commands();
    touch_register_commands();
    wifi_mgr_register_commands();
    time_mgr_register_commands();
    weather_mgr_register_commands();
    ui_settings_register_commands();
    theme_mgr_register_commands();
    scene_register_commands();
    saver_register_commands();
    /* `help` lists every registered command with its usage */
    ESP_ERROR_CHECK(esp_console_register_help_command());

    xTaskCreate(repl_task, "console_repl", 3584, NULL, 2, NULL);
    xTaskCreate(drain_edge_task, "console_drain", 2048, NULL, 2, NULL);
}
