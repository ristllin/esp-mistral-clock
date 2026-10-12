#!/usr/bin/env python3
"""Pin the RADIUS CA certificate of a WPA2-Enterprise network on the clock.

The clock only joins an enterprise network (`wifi add-eap`) once the CA that
signed the RADIUS server certificate is pinned: without it the credentials
would go to any server that answers. This sends the certificate (PEM or DER
file) as base64 chunks over the console (`wifi ca <ssid> begin|add|save`).

Getting the CA: ask the network's IT, or capture the chain a laptop is shown
when it first joins (e.g. macOS: the certificate prompt, or the profile's
trust settings). Check the fingerprint printed here against a trusted source.

Usage: wifi_ca.py [--port PORT] <ssid> <cert.pem|cert.der>
Port: --port, else $CLOCK_PORT, else the single connected Espressif device.
"""
import argparse
import base64
import hashlib
import re
import sys
import time

from console import drain, find_port, open_port, read_until

CHUNK = 64      # base64 characters per line: well under the 256-byte RX buffer


def load_der(path):
    data = open(path, "rb").read()
    m = re.search(rb"-----BEGIN CERTIFICATE-----(.+?)-----END CERTIFICATE-----",
                  data, re.S)
    return base64.b64decode(b"".join(m.group(1).split())) if m else data


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial port (default: $CLOCK_PORT or auto-detect)")
    ap.add_argument("ssid")
    ap.add_argument("cert", help="CA certificate, PEM or DER (first PEM block is used)")
    args = ap.parse_args()
    if '"' in args.ssid or "\\" in args.ssid:
        sys.exit("SSIDs with '\"' or '\\' are not supported by this tool")

    der = load_der(args.cert)
    print("SHA-256 fingerprint:", hashlib.sha256(der).hexdigest().upper())
    b64 = base64.b64encode(der).decode()
    ser = open_port(find_port(args.port))
    time.sleep(0.3)
    drain(ser, 0.5, echo=False)

    def cmd(line, needle, timeout=5):
        ser.write(line.encode() + b"\r\n")
        out, found = read_until(ser, needle, timeout)
        if not found:
            sys.exit(f"no '{needle.decode()}' after: {line[:40]}...\n"
                     + out.decode(errors="replace"))
        return out

    ssid = f'"{args.ssid}"'
    cmd(f"wifi ca {ssid} begin", b"ok 0")
    sent = 0
    for i in range(0, len(b64), CHUNK):
        sent += len(b64[i:i + CHUNK])
        cmd(f"wifi ca {ssid} add {b64[i:i + CHUNK]}", f"ok {sent}".encode())
    out = cmd(f"wifi ca {ssid} save", b"CA pinned", timeout=10)
    print(out.decode(errors="replace").strip())
    return 0


if __name__ == "__main__":
    sys.exit(main())
