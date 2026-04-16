"""
get_version.py — PlatformIO pre-build script

Injects two build flags automatically:
  -D FW_VERSION_STR="<git describe output>"
  -D OTA_PASSWORD="<from secrets.h or random>"

Run by PlatformIO before compilation via:
  extra_scripts = pre:get_version.py
"""

import subprocess
import re
import secrets
import string

Import("env")

# --- FW_VERSION_STR from git tag ---
try:
    version = subprocess.check_output(
        ["git", "describe", "--tags", "--always"],
        stderr=subprocess.DEVNULL
    ).decode().strip()
    # Strip leading 'v' if present (v1.2.0 -> 1.2.0)
    if version.startswith("v"):
        version = version[1:]
except Exception:
    version = "unknown"

env.Append(CPPDEFINES=[("FW_VERSION_STR", env.StringifyMacro(version))])
print(f"  FW_VERSION_STR = {version}")

# --- OTA_PASSWORD fallback ---
# Check if OTA_PASSWORD is already defined (from secrets.h or build_flags).
# If not, generate a random one so OTA is protected but unreachable
# until the user sets a real password.
defines = env.get("CPPDEFINES", [])
has_ota_pw = any(
    (isinstance(d, tuple) and d[0] == "OTA_PASSWORD") or d == "OTA_PASSWORD"
    for d in defines
)

if not has_ota_pw:
    # Check if secrets.h defines it (simple grep — avoids C preprocessing)
    try:
        with open("src/secrets.h", "r") as f:
            content = f.read()
        has_ota_pw = bool(re.search(r"^\s*#\s*define\s+OTA_PASSWORD\s", content, re.MULTILINE))
    except FileNotFoundError:
        pass

if not has_ota_pw:
    random_pw = ''.join(secrets.choice(string.ascii_letters + string.digits) for _ in range(24))
    env.Append(CPPDEFINES=[("OTA_PASSWORD", env.StringifyMacro(random_pw))])
    print(f"  OTA_PASSWORD = <random> (set OTA_PASSWORD in secrets.h to control)")
