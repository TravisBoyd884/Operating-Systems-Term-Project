@echo off
:: Windows install script for Sandbox utility

echo [36m====== Sandbox Windows Installation ======[0m

:: Check if running as administrator
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo [31mThis script requires administrator privileges. Please run as administrator.[0m
    pause
    exit /b 1
)

:: Check for Chocolatey
where choco >nul 2>&1
if %errorlevel% neq 0 (
    echo [33mChocolatey not found. Installing Chocolatey...[0m
    @powershell -NoProfile -ExecutionPolicy Bypass -Command "iex ((New-Object System.Net.WebClient).DownloadString('https://chocolatey.org/install.ps1'))"
    :: Refresh environment variables
    call refreshenv
) else (
    echo [32mChocolatey is already installed.[0m
)

:: Install dependencies
echo [34mInstalling dependencies...[0m
choco install -y visualstudio2019buildtools visualstudio2019-workload-vctools cmake git

:: Refresh environment variables
call refreshenv

:: Check for Docker
where docker >nul 2>&1
if %errorlevel% neq 0 (
    echo [33mDocker not found. Do you want to install Docker Desktop? (y/n):[0m
    set /p install_docker=
    
    if /i "%install_docker%"=="y" (
        echo [33mInstalling Docker Desktop using Chocolatey...[0m
        choco install -y docker-desktop
        echo [33mPlease launch Docker Desktop and complete the setup.[0m
        echo [33mPress any key when Docker is running...[0m
        pause >nul
    ) else (
        echo [33mSkipping Docker installation. Note that containerized execution will not be available.[0m
    )
) else (
    echo [32mDocker is already installed.[0m
)

:: Create build directory
if not exist build mkdir build

:: Build the project
echo [34mBuilding the Sandbox project...[0m
cmake -S . -B build
cmake --build build --config Release

echo [32mBuild complete![0m

:: Ask if user wants to install system-wide
echo [33mDo you want to install Sandbox system-wide? (y/n):[0m
set /p install_system=

if /i "%install_system%"=="y" (
    echo [34mInstalling system-wide...[0m
    cmake --install build
    echo [32mSystem-wide installation complete.[0m
    echo [33mYou can now run 'sandbox' from anywhere.[0m
    
    :: Add to PATH if it's not already there
    echo [33mAdding installation directory to PATH...[0m
    setx PATH "%PATH%;C:\Program Files\sandbox\bin" /M
) else (
    echo [33mSkipping system-wide installation.[0m
)

echo [36m====== Installation Complete ======[0m
echo [32mThe Sandbox utility is now installed![0m

if /i "%install_system%"=="y" (
    echo [33mTo run the sandbox, use: sandbox <program_to_sandbox> [program_args...][0m
) else (
    echo [33mTo run the sandbox, use: build\sandbox.exe <program_to_sandbox> [program_args...][0m
)

echo [33mTo run with Docker, you'll need to use WSL or Git Bash to run the run_in_container.sh script.[0m

pause