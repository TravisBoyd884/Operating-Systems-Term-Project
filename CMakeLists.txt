# Minimum required version of CMake
cmake_minimum_required(VERSION 3.10)

# Name of the project
project(Sandbox C)

# Set the output directory for binaries during development
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

# Detect the operating system
if(APPLE)
  add_definitions(-DMACOS)
  message(STATUS "Configuring for macOS")
elseif(UNIX AND NOT APPLE)
  add_definitions(-DLINUX)
  message(STATUS "Configuring for Linux")
elseif(WIN32)
  add_definitions(-DWINDOWS)
  message(STATUS "Configuring for Windows")
else()
  message(FATAL_ERROR "Unsupported platform. This project currently supports Linux, macOS, and Windows.")
endif()

# Create an executable from the source files
add_executable(sandbox src/sandbox.c)
add_executable(unlink_test src/malicious_program_unlink.c)

# Installation configuration
include(GNUInstallDirs)

# Install binaries to the standard binary installation directory (usually /usr/local/bin on Unix-like systems)
install(TARGETS sandbox unlink_test
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

# Set installation message
message(STATUS "After building, run 'sudo cmake --install build' to install to ${CMAKE_INSTALL_PREFIX}")
message(STATUS "Default install location: ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}")