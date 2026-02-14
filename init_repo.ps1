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

# --- Initial commit ---
git commit -m "Initial commit: HALMET Marine Engine & Tank Monitor

PlatformIO project for Hat Labs HALMET board monitoring a
Volvo Penta MD7A engine via NMEA 2000 / Signal K.

Included:
  platformio.ini              - Build config, library deps
  include/halmet_config.h     - Pin assignments and defaults
  include/BilgeFan.h/.cpp     - Bilge fan purge state machine
  include/RpmSensor.h/.cpp    - Alternator W-terminal RPM counter
  include/OneWireSensors.h/.cpp - DS18B20 1-Wire chain
  include/N2kSenders.h/.cpp   - NMEA 2000 PGN helpers
  src/main.cpp                - Application entry point
  README.md                   - Wiring & configuration guide
  HALMET_Design_Specification.md - Full electrical & SW design doc
"

Write-Host "`nDone. Repository initialised with initial commit." -ForegroundColor Green
git log --oneline
