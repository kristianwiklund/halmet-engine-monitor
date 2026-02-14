# init_repo.ps1  —  Run once to initialise the git repository and make the first commit.
# Usage: right-click → "Run with PowerShell"  (or:  pwsh .\init_repo.ps1)

$ErrorActionPreference = "Stop"
$repoDir = $PSScriptRoot

Write-Host "`n=== HALMET git repository initialisation ===" -ForegroundColor Cyan

Set-Location $repoDir

# --- Configure git identity (local to this repo only) ---
git init
git config user.name  "HALMET Dev"
git config user.email "dev@halmet-project.local"

# --- Stage everything ---
git add -A

# --- Single initial commit containing all files in their corrected state ---
git commit -m "Initial commit: HALMET Marine Engine & Tank Monitor

PlatformIO project for Hat Labs HALMET board monitoring a
Volvo Penta MD7A engine via NMEA 2000 / Signal K.

Files:
  platformio.ini                 - Build config, library deps
  include/halmet_config.h        - Pin assignments and defaults
  include/BilgeFan.h/.cpp        - Bilge fan purge state machine
  include/RpmSensor.h/.cpp       - Alternator W-terminal RPM counter
  include/OneWireSensors.h/.cpp  - DS18B20 1-Wire chain
  include/N2kSenders.h/.cpp      - NMEA 2000 PGN helpers
  src/main.cpp                   - Application entry point
  README.md                      - Wiring, config, troubleshooting guide
  HALMET_Design_Specification.md - Full electrical and SW design doc

Tank logic: single tank, two Gobius Pro sensors.
  A2 = below-3/4 threshold, A3 = below-1/4 threshold.
  Combined into one PGN 127505 with midpoint level estimates.

platformio.ini: correct library names and platform.
  - Platform: pioarduino fork required for SensESP v3 / Arduino Core 3.x.
    Official espressif32 frozen at Core 2.0.17, incompatible with SensESP v3.
  - NMEA2000: registry name is ttlappalainen/NMEA2000-library (hyphen).
    ttlappalainen/NMEA2000 does not exist; version @^4.19 is also invalid.
  - NMEA2000_esp32: not in PlatformIO registry; pulled via GitHub URL.
  - Partition table: default_8MB.csv (HALMET has 8 MB flash).
  - Added build_flags: USE_ESP_IDF_LOG, CORE_DEBUG_LEVEL (required by SensESP v3)."

Write-Host "`nDone. Repository initialised." -ForegroundColor Green
git log --oneline
