# Windows PowerShell install script for Sandbox utility

Write-Host "`n====== Sandbox Windows Installation ======" -ForegroundColor Cyan

# Check if running as administrator
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "This script requires administrator privileges. Please run as administrator." -ForegroundColor Red
    Read-Host "Press Enter to exit"
    exit 1
}

# Helper function to check if a command exists
function Test-CommandExists {
    param ($command)
    $oldPreference = $ErrorActionPreference
    $ErrorActionPreference = 'stop'
    try {
        if (Get-Command $command) { return $true }
    } catch {
        return $false
    } finally {
        $ErrorActionPreference = $oldPreference
    }
}

# Install Chocolatey if not present
if (-not (Test-CommandExists choco)) {
    Write-Host "Chocolatey not found. Installing Chocolatey..." -ForegroundColor Yellow
    Set-ExecutionPolicy Bypass -Scope Process -Force
    [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072
    Invoke-Expression ((New-Object System.Net.WebClient).DownloadString('https://chocolatey.org/install.ps1'))
    # Refresh environment variables
    $env:Path = [System.Environment]::GetEnvironmentVariable("Path","Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path","User")
} else {
    Write-Host "Chocolatey is already installed." -ForegroundColor Green
}

# Install dependencies
Write-Host "Installing dependencies..." -ForegroundColor Blue
choco install -y visualstudio2019buildtools visualstudio2019-workload-vctools cmake git

# Refresh environment variables
$env:Path = [System.Environment]::GetEnvironmentVariable("Path","Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path","User")

# Check for Docker
if (-not (Test-CommandExists docker)) {
    $installDocker = Read-Host "Docker not found. Do you want to install Docker Desktop? (y/n)"
    
    if ($installDocker -eq 'y') {
        Write-Host "Installing Docker Desktop using Chocolatey..." -ForegroundColor Yellow
        choco install -y docker-desktop
        Write-Host "Please launch Docker Desktop and complete the setup." -ForegroundColor Yellow
        Read-Host "Press Enter when Docker is running"
    } else {
        Write-Host "Skipping Docker installation. Note that containerized execution will not be available." -ForegroundColor Yellow
    }
} else {
    Write-Host "Docker is already installed." -ForegroundColor Green
}

# Create build directory
if (-not (Test-Path build)) {
    New-Item -ItemType Directory -Path build | Out-Null
}

# Build the project
Write-Host "Building the Sandbox project..." -ForegroundColor Blue
cmake -S . -B build
cmake --build build --config Release

Write-Host "Build complete!" -ForegroundColor Green

# Ask if user wants to install system-wide
$installSystem = Read-Host "Do you want to install Sandbox system-wide? (y/n)"

if ($installSystem -eq 'y') {
    Write-Host "Installing system-wide..." -ForegroundColor Blue
    cmake --install build
    Write-Host "System-wide installation complete." -ForegroundColor Green
    Write-Host "You can now run 'sandbox' from anywhere." -ForegroundColor Yellow
    
    # Add to PATH if it's not already there
    $binPath = "C:\Program Files\sandbox\bin"
    if (-not ($env:PATH -like "*$binPath*")) {
        Write-Host "Adding installation directory to PATH..." -ForegroundColor Yellow
        [Environment]::SetEnvironmentVariable("PATH", $env:PATH + ";$binPath", [EnvironmentVariableTarget]::Machine)
        $env:PATH += ";$binPath"
    }
} else {
    Write-Host "Skipping system-wide installation." -ForegroundColor Yellow
}

Write-Host "`n====== Installation Complete ======" -ForegroundColor Cyan
Write-Host "The Sandbox utility is now installed!" -ForegroundColor Green

if ($installSystem -eq 'y') {
    Write-Host "To run the sandbox, use: sandbox <program_to_sandbox> [program_args...]" -ForegroundColor Yellow
} else {
    Write-Host "To run the sandbox, use: build\sandbox.exe <program_to_sandbox> [program_args...]" -ForegroundColor Yellow
}

Write-Host "To run with Docker, you'll need to use WSL or Git Bash to run the run_in_container.sh script." -ForegroundColor Yellow

Read-Host "Press Enter to exit"