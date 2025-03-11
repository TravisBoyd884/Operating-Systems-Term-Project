#!/bin/bash
# Universal installer script that detects OS and calls the appropriate installer

set -e  # Exit on error

echo -e "\033[1;36m====== Sandbox Installer ======\033[0m"

# Detect OS
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
  echo -e "\033[1;34mDetected OS: Linux\033[0m"
  # Run Linux installer
  bash ./installers/install_linux.sh
elif [[ "$OSTYPE" == "darwin"* ]]; then
  echo -e "\033[1;34mDetected OS: macOS\033[0m"
  # Run macOS installer
  bash ./installers/install_macos.sh
elif [[ "$OSTYPE" == "cygwin" ]] || [[ "$OSTYPE" == "msys" ]] || [[ "$OSTYPE" == "win32" ]]; then
  echo -e "\033[1;34mDetected OS: Windows\033[0m"
  echo -e "\033[1;33mPlease run one of these installers directly:\033[0m"
  echo -e "\033[1;33m- installers\\install_windows.bat (for Command Prompt)\033[0m"
  echo -e "\033[1;33m- installers\\install_windows.ps1 (for PowerShell)\033[0m"
else
  echo -e "\033[1;31mUnsupported operating system: $OSTYPE\033[0m"
  echo -e "\033[1;31mThis script supports Linux, macOS, and Windows (via cygwin/msys).\033[0m"
  exit 1
fi