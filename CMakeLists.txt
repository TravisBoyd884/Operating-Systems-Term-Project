# Minimum required version of CMake
cmake_minimum_required(VERSION 3.10)

# Name of the project
project(Sandbox)

# Set the output directory for binaries
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

# Create an executable from the source files
add_executable(sandbox src/sandbox.c)
add_executable(unlink_test src/malicious_program_unlink.c)
