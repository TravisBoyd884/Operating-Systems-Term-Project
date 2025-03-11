#!/bin/bash
# macOS install script for Sandbox utility

set -e  # Exit on error

echo -e "\033[1;36m====== Sandbox macOS Installation ======\033[0m"

# Check if a command exists
command_exists() {
  command -v "$1" >/dev/null 2>&1
}

# Install dependencies
echo -e "\033[1;34mInstalling dependencies...\033[0m"

# Check for Xcode Command Line Tools
if ! command_exists xcode-select; then
  echo -e "\033[1;33mXcode command line tools not found, installing...\033[0m"
  xcode-select --install
  echo "Please complete the Xcode installation process before continuing."
  read -p "Press enter when Xcode tools installation is complete..."
fi

# Check for Homebrew
if ! command_exists brew; then
  echo -e "\033[1;33mHomebrew not found, installing...\033[0m"
  /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
  
  # Add Homebrew to PATH
  if [[ -f ~/.zshrc ]]; then
    echo 'eval "$(/opt/homebrew/bin/brew shellenv)"' >> ~/.zshrc
    eval "$(/opt/homebrew/bin/brew shellenv)"
  elif [[ -f ~/.bash_profile ]]; then
    echo 'eval "$(/opt/homebrew/bin/brew shellenv)"' >> ~/.bash_profile
    eval "$(/opt/homebrew/bin/brew shellenv)"
  fi
fi

# Install CMake
brew install cmake

# Check for Docker
if ! command_exists docker; then
  echo -e "\033[1;33mDocker not found. Do you want to install Docker Desktop? (y/n): \033[0m"
  read install_docker
  
  if [[ "$install_docker" =~ ^[Yy]$ ]]; then
    echo -e "\033[1;33mDownloading Docker Desktop for Mac...\033[0m"
    brew install --cask docker
    echo -e "\033[1;33mPlease launch Docker Desktop from your Applications folder.\033[0m"
    read -p "Press enter when Docker is running..."
  else
    echo -e "\033[1;33mSkipping Docker installation. Note that containerized execution will not be available.\033[0m"
  fi
else
  echo -e "\033[1;32mDocker is already installed.\033[0m"
fi

# Build the project
echo -e "\033[1;34mBuilding the Sandbox project...\033[0m"

# Create build directory if it doesn't exist
mkdir -p build

# Configure and build
cmake -S . -B build
cmake --build build

echo -e "\033[1;32mBuild complete!\033[0m"

# Ask if user wants to install system-wide
echo -e "\033[1;33mDo you want to install Sandbox system-wide? (requires sudo) (y/n): \033[0m"
read install_system

if [[ "$install_system" =~ ^[Yy]$ ]]; then
  echo -e "\033[1;34mInstalling system-wide...\033[0m"
  sudo cmake --install build
  echo -e "\033[1;32mSystem-wide installation complete.\033[0m"
  echo -e "\033[1;33mYou can now run 'sandbox' from anywhere.\033[0m"
else
  echo -e "\033[1;33mSkipping system-wide installation.\033[0m"
fi

echo -e "\033[1;36m====== Installation Complete ======\033[0m"
echo -e "\033[1;32mThe Sandbox utility is now installed!\033[0m"

if [[ "$install_system" =~ ^[Yy]$ ]]; then
  echo -e "\033[1;33mTo run the sandbox, use: sandbox <program_to_sandbox> [program_args...]\033[0m"
  echo -e "\033[1;33mTo run with Docker container, use: sandcon <program_to_sandbox> [program_args...]\033[0m"
else
  echo -e "\033[1;33mTo run the sandbox, use: ./build/sandbox <program_to_sandbox> [program_args...]\033[0m"
  echo -e "\033[1;33mTo run with Docker container, use: ./run_in_container.sh <program_to_sandbox> [program_args...]\033[0m"
fi