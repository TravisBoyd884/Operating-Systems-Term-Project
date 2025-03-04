# Sandbox - System Call Monitoring Tool

A simple sandbox utility that monitors and blocks potentially harmful system calls.

## Overview

This application monitors child processes using ptrace and prevents them from performing certain operations like deleting files in protected directories.

## Build Instructions

### Prerequisites

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

```bash
# Using locally built version
./bin/sandbox <filepath>

# Using installed version 
sandbox <filepath>
```

Where `<filepath>` is the path to the executable you want to run in the sandbox.

## Features

- Monitors system calls using ptrace
- Blocks unlink operations on protected directories
- Cross-platform support for Linux and macOS (x86_64 and ARM64)

## Testing

The repository includes a test program that attempts to delete files:

```bash
# Test the sandbox with the unlink test program
./bin/sandbox ./bin/unlink_test test/testfile.txt
```
