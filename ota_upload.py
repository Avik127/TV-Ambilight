#!/usr/bin/env python3
"""
OTA firmware upload for ESP32-CAM Ambilight.

Usage:
    python ota_upload.py                          # uses default binary path
    python ota_upload.py path/to/firmware.bin     # custom binary path

The device must already be running firmware with OTA support (i.e. you
flashed the OTA-capable build at least once over USB).
"""

import sys
import os
import urllib.request
import urllib.error

# ── config ────────────────────────────────────────────────────────────────────
DEVICE_IP = "10.0.0.117"
DEFAULT_BIN = os.path.join(
    os.path.dirname(__file__),
    "esp32_cam_test", "build", "esp32_cam_test.bin"
)
# ─────────────────────────────────────────────────────────────────────────────

bin_path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_BIN

if not os.path.exists(bin_path):
    print(f"ERROR: binary not found: {bin_path}")
    print("Run  idf.py build  inside esp32_cam_test/ first.")
    sys.exit(1)

size = os.path.getsize(bin_path)
print(f"Binary : {bin_path}")
print(f"Size   : {size:,} bytes ({size / 1024:.1f} KB)")
print(f"Target : http://{DEVICE_IP}/ota")
print()

with open(bin_path, "rb") as f:
    data = f.read()

req = urllib.request.Request(
    f"http://{DEVICE_IP}/ota",
    data=data,
    method="POST",
    headers={"Content-Type": "application/octet-stream"},
)

def _is_reboot_signal(exc):
    """Return True if the exception is the ESP32 rebooting mid-response (normal)."""
    msg = str(exc).lower()
    for keyword in ("reset", "forcibly", "timed out", "timeout",
                    "10054", "10060", "connection aborted", "eof"):
        if keyword in msg:
            return True
    cause = getattr(exc, "reason", None) or getattr(exc, "__cause__", None)
    if cause and cause is not exc:
        return _is_reboot_signal(cause)
    return False

try:
    print("Uploading", end="", flush=True)
    with urllib.request.urlopen(req, timeout=120) as resp:
        print()
        body = resp.read().decode()
        print(f"Response: {body}")
        print("Done. Device is rebooting — wait ~5 s then refresh the browser.")
except (urllib.error.URLError, ConnectionResetError, ConnectionAbortedError,
        TimeoutError, OSError) as e:
    print()
    if _is_reboot_signal(e):
        print("Connection closed by device (normal — it rebooted).")
        print("Wait ~5 s then refresh the browser.")
    else:
        print(f"ERROR: {e}")
        sys.exit(1)
