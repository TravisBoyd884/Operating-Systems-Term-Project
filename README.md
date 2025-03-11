# Sandbox - System Call Monitoring Tool

A security sandbox utility that monitors and controls potentially harmful system calls.

## Overview

This application monitors child processes using ptrace (on Linux/macOS) or process monitoring (on Windows) and controls access to file operations. When a monitored program attempts to perform file operations like opening, reading, writing, or deleting files, the sandbox intercepts these actions and prompts the user to allow or deny them.

## Key Features

- Monitors file system operations: open, read, write, and delete
- Interactive prompting for security decisions
- Cross-platform support for Linux, macOS, and Windows
- Docker container support for isolated execution
- Focuses monitoring on user-specified directories

## Build Instructions

### Prerequisites

- For execution only:
  - Docker Engine
  - Docker Buildx (for building Docker images)
- For building from source:
  - CMake 3.10 or higher
  - C compiler (GCC or Clang)

### Building the Application

```bash
# Configure the build
cmake -S . -B build

# Build the application
cmake --build build
```

The binaries will be created in the `bin/` directory.

### Installing System-wide

To install the application to your system path (usually `/usr/local/bin`):

```bash
# Install the application (may require sudo)
sudo cmake --install build
```

After installation, you can run `sandbox` from anywhere in your terminal.

## Usage

### Basic Usage

```bash
# Using locally built version
./bin/sandbox <program_to_sandbox> [program_args...]

# Using installed version 
sandbox <program_to_sandbox> [program_args...]
```

Where `<program_to_sandbox>` is the path to the executable you want to run in the sandbox.

### Containerized Execution

For an additional layer of isolation, you can run the sandbox inside a Docker container:

```bash
# If using the development version:
# First, make sure the script is executable
chmod +x run_in_container.sh

# Run the sandbox in a container
./run_in_container.sh <program_to_sandbox> [program_args...]

# If installed system-wide:
sandcon <program_to_sandbox> [program_args...]
```

The containerization script will:
1. Build the sandbox Docker image based on Ubuntu
2. Create a Docker container with appropriate privileges for ptrace
3. Mount your current directory into the container
4. Run the sandbox with your specified program inside the container

This provides additional isolation from your host system and can be used to sandbox potentially dangerous programs.

## Test Programs

The repository includes test programs to demonstrate sandbox capabilities:

```bash
# Test file deletion monitoring
./bin/sandbox ./bin/unlink_test ./protected_directory/test_file.txt

# Test file read/write/open monitoring
./bin/sandbox ./bin/file_operations_test ./protected_directory/test_file.txt
```

## Implementation Details

- Linux: Uses ptrace to intercept system calls
- macOS: Uses ptrace with platform-specific adaptations
- Windows: Uses process creation flags and simulated monitoring
- Docker: Uses Ubuntu container with special permissions for ptrace functionality

## Educational Value

This project demonstrates several key operating systems concepts:
1. System call interception and the user/kernel boundary
2. Process isolation and security mechanisms
3. Containerization as an extension of OS protection models
4. Cross-platform differences in system call interfaces
5. Real-time process monitoring and intervention techniques
