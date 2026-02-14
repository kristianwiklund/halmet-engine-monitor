@echo off
REM  init_repo.bat  —  Double-click to initialise git repo and make first commit.
REM  Requires Git for Windows: https://git-scm.com/download/win

cd /d "%~dp0"

echo === HALMET git repository initialisation ===

git init
git config user.name  "HALMET Dev"
git config user.email "dev@halmet-project.local"

git add -A

git commit -m "Initial commit: HALMET Marine Engine & Tank Monitor"

echo.
echo Done. Repository initialised.
git log --oneline
pause
