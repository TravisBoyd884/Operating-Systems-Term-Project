#!/bin/bash
# Linux install script for Sandbox utility

set -e  # Exit on error

echo -e "\033[1;36m====== Sandbox Linux Installation ======\033[0m"

# Check if a command exists
command_exists() {
  command -v "$1" >/dev/null 2>&1
}

# Install dependencies
echo -e "\033[1;34mInstalling dependencies...\033[0m"

# Detect package manager
if command_exists apt-get; then
  sudo apt-get update
  sudo apt-get install -y build-essential cmake
elif command_exists dnf; then
  sudo dnf install -y gcc gcc-c++ make cmake
elif command_exists yum; then
  sudo yum install -y gcc gcc-c++ make cmake
elif command_exists pacman; then
  sudo pacman -Sy --noconfirm base-devel cmake
else
  echo -e "\033[1;31mUnsupported package manager. Please install build-essential and cmake manually.\033[0m"
  exit 1
fi

# Check for Docker
if ! command_exists docker; then
  echo -e "\033[1;33mDocker not found. Do you want to install Docker? (y/n): \033[0m"
  read install_docker
  
  if [[ "$install_docker" =~ ^[Yy]$ ]]; then
    curl -fsSL https://get.docker.com -o get-docker.sh
    sudo sh get-docker.sh
    sudo usermod -aG docker $USER
    echo -e "\033[1;33mPlease log out and log back in to use Docker without sudo\033[0m"
    
    # Check for Docker Buildx
    if ! docker buildx version >/dev/null 2>&1; then
      echo -e "\033[1;33mDocker Buildx not found. Installing...\033[0m"
      sudo apt-get update
      sudo apt-get install -y docker-buildx-plugin
    fi
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