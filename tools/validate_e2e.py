#!/usr/bin/env python3
"""End-to-end serial validation for the Mistral clock.

Drives the whole functional chain over the USB-Serial/JTAG console with a
single persistent connection:

  A  identity + health (version, heap)
  B  Wi-Fi: scan, add, connect, reboot auto-reconnect, second network
     add/reorder/remove, password never echoed
  B4 offline: remove the network + reboot -> not connected, time not
     synced, last-known weather with its age -> re-add, reconnect, re-sync
  C  themes: dark/light/mid/auto, fb dump of home in each
  D  auto theme across a full day via the debug clock override
     (dawn/day/sunset/night), fb dumps
  D2 DST edges: clock override across both Europe/London transitions
     (GMT->BST, BST->GMT)
  E  weather: forced fetch, location change refetch, default restore,
     weather force override
  F  screensaver: idle-timeout activation, art dump, tap to wake,
     scenario dumps
  G  touch: I2C controller probe, injection walkthrough of
     settings -> location -> back -> home
  H  framebuffer matrix: home x 3 scenes, settings x 3 themes
  I  final state restore + health re-check + password leak scan

The test network must be in range and already saved on the device (phase
B checks that). Its credentials come from the environment and are never
printed: the password is masked in every logged line and the final phase
scans the log for it.

Framebuffer dumps (fb-*.png) and the full log (e2e-log.txt) are written to
the output directory, logs/e2e/ by default (created if missing). Takes about
8 minutes. Exit code 0 = every check passed.

Usage:
  CLOCK_TEST_WIFI_SSID=<ssid> CLOCK_TEST_WIFI_PASS=<pass> \
      python3 tools/validate_e2e.py [--port PORT] [--out DIR] [--log FILE]
  CLOCK_TEST_WIFI_SSID=<ssid> python3 tools/validate_e2e.py --skip-offline
Port: --port, else $CLOCK_PORT, else the single connected Espressif device.
"""
import argparse
import os
import re
import sys
import time
import traceback

import fbdump
from console import PROMPT, REPO, fw_version, open_port, read_until

DEFAULT_OUT = os.path.join(REPO, "logs", "e2e")

# Debug clock override epochs (UTC). 2026-10-08, Europe/London = BST (UTC+1).
E_DAWN, T_DAWN = 1791441000, "07:30"    # 06:30 UTC
E_DAY, T_DAY = 1791457200, "12:00"      # 11:00 UTC
E_SUNSET, T_SUNSET = 1791478500, "17:55"  # 16:55 UTC
E_NIGHT, T_NIGHT = 1791496800, "23:00"  # 22:00 UTC

# DST edges (UTC), Europe/London: spring forward 2026-03-29
# 01:00 UTC (local 01:00 -> 02:00), fall back 2026-10-25 01:00 UTC
# (local 02:00 -> 01:00). Each case: epoch -> exact local time string.
DST_CASES = (
    (1774745999, "2026-03-29 00:59:59 GMT"),   # just before spring forward
    (1774746001, "2026-03-29 02:00:01 BST"),   # just after
    (1792889999, "2026-10-25 01:59:59 BST"),   # just before fall back
    (1792890001, "2026-10-25 01:00:01 GMT"),   # just after
)

# Expected firmware version: CLOCK_FW_VERSION from platformio.ini; if that
# cannot be read, any x.y.z version is accepted.
FW_VERSION = fw_version()
GUEST_SSID = "ClockTestGuest"    # fake AP, never in range
GUEST_PASS = "guest-pass-42"


def _test_net_creds(need_pass):
    """Test-network credentials come from the environment, never from the
    repo. Fail with a clear message when they are missing. The password is
    only needed by the offline phase, which removes and re-adds the network."""
    ssid = os.environ.get("CLOCK_TEST_WIFI_SSID", "").strip()
    passwd = os.environ.get("CLOCK_TEST_WIFI_PASS", "") or None
    if not ssid or (need_pass and not passwd):
        sys.exit("error: set CLOCK_TEST_WIFI_SSID and CLOCK_TEST_WIFI_PASS "
                 "to the test network's credentials (see docs/REFERENCE.md), "
                 "or pass --skip-offline to run without the password. "
                 "They are intentionally not stored in the repo.")
    return ssid, passwd


TEST_SSID = TEST_PASS = None   # set in main() from the environment
OUTDIR = DEFAULT_OUT           # set in main() from --out
RESULTS = []
LOG = None


def log(text=""):
    print(text, flush=True)
    if LOG is not None:
        LOG.write(text + "\n")


def check(name, ok, detail=""):
    RESULTS.append((name, bool(ok)))
    line = f"[{'PASS' if ok else 'FAIL'}] {name}"
    if detail:
        line += f"  ({detail})"
    log(line)
    return ok


def section(title):
    log()
    log(f"--- {title} " + "-" * max(0, 60 - len(title)))


def cmd(ser, text, timeout=15.0, redact=None):
    """Send one console line, return its output (raw, unredacted).

    `redact` is a secret (password) that is masked as **** in everything
    this function logs."""
    ser.write(b"\n")
    read_until(ser, PROMPT, 5)
    ser.write(text.encode() + b"\n")
    out, ok = read_until(ser, PROMPT, timeout)
    s = out.decode("utf-8", "replace")
    shown = text if redact is None else text.replace(redact, "****")
    if not ok:
        log(f"[warn] no prompt after {shown!r}")
    body = s if redact is None else s.replace(redact, "****")
    for ln in body.replace("\r", "").split("\n"):
        if ln.strip():
            log("    " + ln)
    if shown != text:
        log(f"esp> {shown}")
    return s


def wait_cmd(ser, text, needle, timeout, poll=3.0, redact=None):
    """Run `text` repeatedly until its output contains `needle`."""
    end = time.time() + timeout
    out = ""
    while time.time() < end:
        out = cmd(ser, text, timeout=15.0, redact=redact)
        if needle in out:
            return out
        time.sleep(poll)
    return out


def drain_quiet(ser, quiet_s=1.5, cap_s=20.0):
    """Read and discard until the port has been quiet for `quiet_s`."""
    end = time.time() + cap_s
    last = time.time()
    while time.time() < end:
        if ser.read(ser.in_waiting or 1):
            last = time.time()
        elif time.time() - last >= quiet_s:
            return


def dump(ser, name):
    """Framebuffer dump over the live connection -> <output dir>/<name>."""
    path = os.path.join(OUTDIR, name)
    fb = None
    for attempt in range(1, 4):
        # A failed attempt can leave the device streaming the aborted dump
        # (~13 s of base64 at 115200 baud); retrying immediately interleaves
        # two dumps and corrupts both. Drain until the port is quiet first.
        drain_quiet(ser)
        try:
            w, h, fb = fbdump.read_dump(ser, 60.0)
            break
        except Exception as e:  # noqa: BLE001
            log(f"[warn] dump {name} attempt {attempt} failed: {e}")
            time.sleep(1.0)
    if fb is None:
        check(f"fb dump {name}", False, "no clean dump")
        return None
    fbdump.decode(fb, w, h, False).save(path)
    check(f"fb dump {name}", True, f"{w}x{h}")
    return fb


def phase_identity(ser):
    section("A. identity + health")
    out = cmd(ser, "version", timeout=8)
    if FW_VERSION:
        check("version banner", f"mistral-clock v{FW_VERSION} " in out,
              f"v{FW_VERSION} (platformio.ini)")
    else:
        check("version banner",
              re.search(r"mistral-clock v\d+\.\d+\.\d+", out) is not None,
              "x.y.z (platformio.ini version not found)")
    out = cmd(ser, "info", timeout=8)
    heap = 0
    for ln in out.split("\n"):
        if "free heap:" in ln:
            heap = int(ln.split("free heap:")[1].split()[0])
    check("heap at start", heap >= 30000, f"{heap} bytes free")


def phase_wifi(ser):
    section("B. Wi-Fi")
    cmd(ser, "saver off")  # keep the saver out of the way for this phase
    out = cmd(ser, "wifi scan", timeout=30)
    check("scan finds test network", TEST_SSID in out, TEST_SSID)
    out = cmd(ser, "wifi list", timeout=8)
    check("test network saved", TEST_SSID in out, "in saved list")
    out = cmd(ser, "wifi status", timeout=8)
    check("connected", "connected to " + TEST_SSID in out, "")
    check("has IP", re.search(r"\bip \d+\.\d+\.\d+\.\d+", out) is not None, "")

    section("B2. reboot -> auto-reconnect")
    ser.write(b"\n")
    read_until(ser, PROMPT, 5)
    ser.write(b"reboot\n")
    out, _ = read_until(ser, b"ready", 25)
    log("    [boot] " + out.decode("utf-8", "replace").replace("\r", "")
        .strip().split("\n")[-1][:70])
    out = wait_cmd(ser, "wifi status", "connected to " + TEST_SSID, 60)
    check("auto-reconnect after reboot", "connected to " + TEST_SSID in out,
          "no console config after boot")
    out = wait_cmd(ser, "clock", "synced: yes", 60)
    check("SNTP sync after reboot", "synced: yes" in out, "")

    section("B3. second network: add / reorder / remove")
    out = cmd(ser, f"wifi add {GUEST_SSID} {GUEST_PASS}", timeout=10,
              redact=GUEST_PASS)
    check("guest add: fixed password mask", "****" in out, "mask shown")
    check("guest add: password NOT echoed", GUEST_PASS not in out, "no leak")
    out = cmd(ser, "wifi list", timeout=8)
    check("two networks saved", GUEST_SSID in out and TEST_SSID in out,
          "both listed")
    cmd(ser, f"wifi reorder {GUEST_SSID} 1", timeout=8)
    out = cmd(ser, "wifi list", timeout=8)
    check("reorder strict (guest first)",
          out.find(GUEST_SSID) < out.find(TEST_SSID), "order swapped")
    out = cmd(ser, "wifi status", timeout=8)
    check("still on strongest in-range AP",
          "connected to " + TEST_SSID in out, "guest is out of range")
    out = cmd(ser, f"wifi remove {GUEST_SSID}", timeout=8)
    check("guest removed", "removed " + GUEST_SSID in out, "")
    out = cmd(ser, "wifi list", timeout=8)
    check("one network left", TEST_SSID in out and GUEST_SSID not in out,
          "back to 1")
    out = cmd(ser, "wifi status", timeout=8)
    if TEST_PASS:
        check("password never in status", TEST_PASS not in out, "")


def phase_offline(ser):
    section("B4. offline: remove network + reboot -> last known weather")
    out = cmd(ser, f"wifi remove {TEST_SSID}", timeout=8)
    check("offline: network removed", f"removed {TEST_SSID}" in out, "")
    ser.write(b"\n")
    read_until(ser, PROMPT, 5)
    ser.write(b"reboot\n")
    out, _ = read_until(ser, b"ready", 25)
    log("    [boot] " + out.decode("utf-8", "replace").replace("\r", "")
        .strip().split("\n")[-1][:70])
    out = cmd(ser, "wifi status", timeout=8)
    check("offline: not connected", "not connected" in out
          and "connected to" not in out, "")
    out = cmd(ser, "clock", timeout=8)
    check("offline: time not synced", "synced: no" in out, "")
    out = cmd(ser, "weather", timeout=15)
    check("offline: last known weather with age",
          "(showing last known)" in out and "age:" in out
          and "weather:" in out, "")
    out = cmd(ser, f"wifi add {TEST_SSID} {TEST_PASS} 1", timeout=10,
              redact=TEST_PASS)
    check("offline: network re-added (masked)", "****" in out
          and TEST_PASS not in out, "")
    cmd(ser, "wifi connect", timeout=8)
    out = wait_cmd(ser, "wifi status", "connected to " + TEST_SSID, 60)
    check("offline: reconnected", "connected to " + TEST_SSID in out, "")
    out = wait_cmd(ser, "clock", "synced: yes", 60)
    check("offline: SNTP re-synced", "synced: yes" in out, "")
    time.sleep(11)  # the offline `weather` consumed the forced-fetch slot
    out = cmd(ser, "weather", timeout=40)
    check("offline: weather online again", "online:   yes" in out, "")


def phase_themes(ser):
    section("C. themes")
    cmd(ser, "home")
    cmd(ser, "clock override off")
    prev = None
    for name in ("dark", "light", "mid", "auto"):
        out = cmd(ser, f"theme {name}", timeout=8)
        check(f"theme {name} accepted", f"theme: {name}" in out
              or f"theme:   {name}" in out, "")
        fb = dump(ser, f"fb-home-{name}.png")
        if fb is not None and prev is not None:
            check(f"theme {name} changes pixels", fb != prev,
                  f"{sum(a != b for a, b in zip(fb, prev))} bytes differ")
        prev = fb
    out = cmd(ser, "theme", timeout=8)
    check("theme persisted mode = auto", "auto" in out, "")


def phase_auto_day(ser):
    section("D. auto theme across a full day (clock override)")
    for epoch, tstr, name in ((E_DAWN, T_DAWN, "dawn"),
                              (E_DAY, T_DAY, "day"),
                              (E_SUNSET, T_SUNSET, "sunset"),
                              (E_NIGHT, T_NIGHT, "night")):
        cmd(ser, f"clock override {epoch}", timeout=8)
        out = cmd(ser, "clock", timeout=8)
        check(f"override {name} = {tstr} local",
              f"time:   2026-10-08 {tstr}:" in out and "override: active" in out,
              out.split("time:")[1].split("\n")[0].strip()
              if "time:" in out else "?")
        dump(ser, f"fb-auto-{name}.png")
    cmd(ser, "clock override off", timeout=8)
    out = cmd(ser, "clock", timeout=8)
    check("override off, real time synced", "synced: yes" in out
          and "override: active" not in out, "")


def phase_dst(ser):
    section("D2. DST edges (Europe/London, clock override)")
    for epoch, want in DST_CASES:
        cmd(ser, f"clock override {epoch}", timeout=8)
        out = cmd(ser, "clock", timeout=8)
        check(f"DST {want}",
              f"time:   {want} (epoch {epoch})" in out
              and "override: active" in out,
              out.split("time:")[1].split("\n")[0].strip()
              if "time:" in out else "?")
    cmd(ser, "clock override off", timeout=8)
    out = cmd(ser, "clock", timeout=8)
    check("DST: override off, real time synced", "synced: yes" in out
          and "override: active" not in out, "")


ORIG_LOC = None   # the device's location setting, restored in phase_final


def save_location(ser):
    """Remember the location setting ('auto' or 'lat lon name')."""
    global ORIG_LOC
    out = cmd(ser, "loc", timeout=8)
    if "mode:     auto" in out:
        ORIG_LOC = "auto"
        return
    for ln in out.split("\n"):
        if ln.startswith("location: ") and "(" in ln:
            name, rest = ln[len("location: "):].rsplit(" (", 1)
            lat, lon = rest.rstrip(")").split(", ")[:2]
            ORIG_LOC = f"{lat} {lon} {name}"


def restore_location(ser):
    if ORIG_LOC == "auto":
        out = cmd(ser, "loc auto", timeout=10)
    elif ORIG_LOC:
        out = cmd(ser, "loc " + ORIG_LOC, timeout=10)
    else:
        return
    check("location setting restored", "location" in out, ORIG_LOC)


def phase_weather(ser):
    section("E. weather + location")
    cmd(ser, "home", timeout=8)
    save_location(ser)
    cmd(ser, "loc default", timeout=10)   # deterministic start: London
    time.sleep(11)  # the firmware allows one forced fetch per 10 s
    out = cmd(ser, "weather", timeout=40)
    check("weather online", "online:   yes" in out, "")
    check("weather default London", "location: London" in out, "")
    check("weather has temp", "weather:  " in out and "C" in out, "")
    time.sleep(11)
    out = cmd(ser, "loc 48.8566 2.3522 Paris", timeout=10)
    check("loc set Paris", "location set: Paris" in out, "")
    time.sleep(11)
    out = cmd(ser, "weather", timeout=40)
    check("weather refetch after loc change",
          "location: Paris" in out, "loc change triggers a fetch")
    time.sleep(11)
    out = cmd(ser, "loc default", timeout=10)
    check("loc default", "location reset to London" in out, "")
    time.sleep(11)
    out = cmd(ser, "weather", timeout=40)
    check("weather back to London", "location: London" in out, "")
    # automatic location: mode switches, the lookup runs, weather follows
    out = cmd(ser, "loc auto", timeout=10)
    check("loc auto accepted", "location: auto" in out, "")
    out = wait_cmd(ser, "weather", "[auto]", 40, poll=5.0)
    check("weather follows auto location",
          "[auto]" in out and "online:   yes" in out, "")
    out = cmd(ser, "loc default", timeout=10)
    check("manual location ends auto mode",
          "location reset to London" in out, "")
    out = cmd(ser, "weather force rain", timeout=10)
    check("weather force rain", "weather force: rain" in out, "")
    out = cmd(ser, "weather force off", timeout=10)
    check("weather force off", "unknown command" not in out, "")


def phase_saver(ser):
    section("F. screensaver")
    cmd(ser, "home", timeout=8)
    cmd(ser, "saver on", timeout=8)
    cmd(ser, "saver timeout 5", timeout=8)
    out = cmd(ser, "saver", timeout=8)
    check("saver on, timeout 5 s", "saver: on, timeout 5s" in out, "")
    log("    [waiting 9 s with NO serial traffic for the idle timeout]")
    time.sleep(9)
    out = cmd(ser, "saver", timeout=8)
    check("saver active after idle", "screen saver" in out, "idle timeout fired")
    dump(ser, "fb-saver-idle.png")
    tap_expect(ser, 10, 10, "home", "tap wakes saver to home")
    cmd(ser, "theme dark", timeout=8)
    cmd(ser, "saver scenario sleep", timeout=8)
    cmd(ser, "saver now", timeout=8)
    dump(ser, "fb-saver-sleep.png")
    cmd(ser, "home", timeout=8)
    cmd(ser, "theme light", timeout=8)
    cmd(ser, "saver scenario loaf", timeout=8)
    cmd(ser, "saver now", timeout=8)
    dump(ser, "fb-saver-loaf.png")
    cmd(ser, "home", timeout=8)
    cmd(ser, "saver scenario auto", timeout=8)
    cmd(ser, "theme auto", timeout=8)
    cmd(ser, "saver timeout 120", timeout=8)


def screen_of(ser):
    """Current screen via the saver status line: home / saver / other."""
    out = cmd(ser, "saver", timeout=8)
    if "screen saver" in out:
        return "saver"
    if "screen home" in out:
        return "home"
    return "other"


def tap_expect(ser, x, y, want, name, hold=250):
    """Inject a tap at glass (x,y) and verify the screen changed to `want`.

    A simulated tap can be swallowed whole when the LVGL task is briefly
    busy (scene repaint / display flush) inside the injection window; the
    250 ms hold spans several ticks and one retry covers the rare miss.
    """
    got = "?"
    for attempt in (1, 2):
        cmd(ser, f"touch inject {x} {y} {hold}", timeout=10)
        time.sleep(0.6)
        got = screen_of(ser)
        if got == want:
            detail = f"({x},{y}) -> screen {got}"
            if attempt == 2:
                detail += " [needed retry]"
            check(f"tap {name}", True, detail)
            return True
    check(f"tap {name}", False, f"({x},{y}) wanted screen {want}, got {got}")
    return False


def phase_touch(ser):
    section("G. touch")
    out = cmd(ser, "touch i2c", timeout=10)
    check("AXS5106L responds on I2C", "AXS5106L responding at 0x63" in out,
          "controller alive")
    check("i2c/driver views agree", "driver: idle" in out, "")
    out = cmd(ser, "touch raw", timeout=8)
    check("touch raw idle", "idle" in out, "")
    cmd(ser, "home", timeout=8)
    cmd(ser, "theme dark", timeout=8)
    check("walkthrough starts on home", screen_of(ser) == "home", "")
    tap_expect(ser, 270, 14, "other", "SETTINGS opens settings")
    fb_set1 = dump(ser, "fb-touch-settings.png")
    tap_expect(ser, 286, 13, "home", "BACK returns home")
    tap_expect(ser, 270, 14, "other", "SETTINGS reopens settings")
    fb_set2 = dump(ser, "fb-touch-settings2.png")
    if fb_set1 is not None and fb_set2 is not None:
        check("settings via touch deterministic",
              fb_set1 == fb_set2, "two touch-opened dumps identical")
    tap_expect(ser, 172, 107, "other", "LOC opens city list")
    dump(ser, "fb-touch-loc.png")
    tap_expect(ser, 286, 13, "other", "BACK loc -> settings")
    tap_expect(ser, 286, 13, "home", "BACK settings -> home")
    check("walkthrough ends on home", screen_of(ser) == "home", "")


def phase_matrix(ser):
    section("H. framebuffer matrix (scenes x themes)")
    cmd(ser, "home", timeout=8)
    cmd(ser, f"clock override {E_DAY}", timeout=8)  # deterministic day phase
    for scene in ("city", "nature", "window"):
        cmd(ser, f"scene {scene}", timeout=8)
        dump(ser, f"fb-scene-{scene}.png")
    cmd(ser, "scene auto", timeout=8)
    cmd(ser, "clock override off", timeout=8)
    for name in ("dark", "light", "mid"):
        cmd(ser, f"theme {name}", timeout=8)
        cmd(ser, "settings", timeout=8)
        dump(ser, f"fb-settings-{name}.png")
        cmd(ser, "home", timeout=8)
    cmd(ser, "theme auto", timeout=8)


def phase_final(ser, logpath):
    section("I. final state + summary")
    restore_location(ser)
    cmd(ser, "theme auto", timeout=8)
    cmd(ser, "scene auto", timeout=8)
    cmd(ser, "weather force off", timeout=8)
    cmd(ser, "clock override off", timeout=8)
    cmd(ser, "saver on", timeout=8)
    cmd(ser, "saver timeout 120", timeout=8)
    cmd(ser, "home", timeout=8)
    out = cmd(ser, "wifi status", timeout=8)
    check("final: connected", "connected to " + TEST_SSID in out, "")
    out = cmd(ser, "clock", timeout=8)
    check("final: time synced", "synced: yes" in out, "")
    out = cmd(ser, "weather", timeout=40)
    check("final: weather online", "online:   yes" in out, "")
    # The forced fetch keeps mbedTLS buffers allocated for a few seconds
    # (a temporary dip in free heap). Poll until it has settled so the
    # health check sees the steady-state heap, not the dip.
    heap = 0
    for _ in range(10):
        out = cmd(ser, "info", timeout=8)
        for ln in out.split("\n"):
            if "free heap:" in ln:
                heap = int(ln.split("free heap:")[1].split()[0])
        if heap >= 30000:
            break
        time.sleep(2)
    check("final: heap healthy", heap >= 30000, f"{heap} bytes free")
    LOG.flush()
    with open(logpath, encoding="utf-8") as f:
        logtext = f.read()
    check("log contains no Wi-Fi password",
          (not TEST_PASS or TEST_PASS not in logtext)
          and GUEST_PASS not in logtext, "")
    passed = sum(1 for _, ok in RESULTS if ok)
    failed = sum(1 for _, ok in RESULTS if not ok)
    log()
    log(f"=== {passed} passed, {failed} failed, {len(RESULTS)} checks ===")
    return failed == 0


PHASES = (phase_identity, phase_wifi, phase_offline, phase_themes,
          phase_auto_day, phase_dst, phase_weather, phase_saver, phase_touch,
          phase_matrix)


def main():
    global LOG, OUTDIR, TEST_SSID, TEST_PASS
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial port (default: $CLOCK_PORT or auto-detect)")
    ap.add_argument("--out", default=DEFAULT_OUT,
                    help="output directory for the log and PNG dumps "
                         "(default: logs/e2e in the repo)")
    ap.add_argument("--log", help="log file (default: <out>/e2e-log.txt)")
    ap.add_argument("--skip-offline", action="store_true",
                    help="skip the offline phase (remove network, reboot, "
                         "re-add); CLOCK_TEST_WIFI_PASS is then not needed")
    args = ap.parse_args()
    TEST_SSID, TEST_PASS = _test_net_creds(need_pass=not args.skip_offline)
    phases = [p for p in PHASES
              if not (args.skip_offline and p is phase_offline)]
    OUTDIR = args.out
    logpath = args.log or os.path.join(OUTDIR, "e2e-log.txt")
    os.makedirs(OUTDIR, exist_ok=True)
    os.makedirs(os.path.dirname(os.path.abspath(logpath)), exist_ok=True)
    ser = open_port(args.port)
    ok = False
    with open(logpath, "w", buffering=1, encoding="utf-8") as LOG:
        log("validate_e2e.py  " + time.strftime("%Y-%m-%d %H:%M:%S UTC",
                                                 time.gmtime()))
        log(f"port {ser.port}   out {OUTDIR}   log {logpath}")
        try:
            time.sleep(0.3)
            ser.reset_input_buffer()
            ser.write(b"\n")
            read_until(ser, PROMPT, 8)
            for phase in phases:
                phase(ser)
            ok = phase_final(ser, logpath)
        except Exception:  # noqa: BLE001
            log(traceback.format_exc())
        finally:
            ser.close()
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
