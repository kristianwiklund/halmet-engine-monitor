# fix_littlefs_penv.ps1
# Deletes PlatformIO's penv so it is rebuilt cleanly on the next build.
# Run from any directory: pwsh .\fix_littlefs_penv.ps1

$ErrorActionPreference = "Stop"

$penvPath = "$env:USERPROFILE\.platformio\penv"

Write-Host ""
Write-Host "=== PlatformIO penv clean rebuild ===" -ForegroundColor Cyan
Write-Host ""

if (-not (Test-Path $penvPath)) {
    Write-Host "penv not found at: $penvPath" -ForegroundColor Yellow
    Write-Host "Nothing to do — PlatformIO will create it fresh on next build."
    exit 0
}

Write-Host "Removing: $penvPath" -ForegroundColor Yellow
Remove-Item -Recurse -Force $penvPath
Write-Host "Done. penv removed." -ForegroundColor Green

Write-Host ""
Write-Host "Next steps:" -ForegroundColor Cyan
Write-Host "  1. Re-open VS Code (or your PlatformIO IDE)"
Write-Host "  2. Run your build as normal"
Write-Host "  3. PlatformIO will recreate the penv and reinstall all"
Write-Host "     dependencies compiled against the correct Python version."
Write-Host ""
