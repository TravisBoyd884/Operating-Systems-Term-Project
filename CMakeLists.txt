# Minimum required version of CMake
cmake_minimum_required(VERSION 3.10)

# Name of the project
project(Sandbox C)

# Set the output directory for binaries during development
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

# Detect the operating system and select the appropriate source file
if(APPLE)
  set(SANDBOX_SOURCE src/sandbox_macos.c)
  add_definitions(-DMACOS)
  message(STATUS "Configuring for macOS")
elseif(UNIX AND NOT APPLE)
  set(SANDBOX_SOURCE src/sandbox_linux.c)
  add_definitions(-DLINUX)
  message(STATUS "Configuring for Linux")
elseif(WIN32)
  set(SANDBOX_SOURCE src/sandbox_windows.c)
  add_definitions(-DWINDOWS)
  message(STATUS "Configuring for Windows")
else()
  message(FATAL_ERROR "Unsupported platform. This project currently supports Linux, macOS, and Windows.")
endif()

# Create an executable from the platform-specific source file
add_executable(sandbox ${SANDBOX_SOURCE})

# Create the test executables
add_executable(unlink_test src/malicious_unlink.c)
add_executable(file_operations_test src/malicious_file_operations.c)

# Installation configuration
include(GNUInstallDirs)

# Install binaries to the standard binary installation directory (usually /usr/local/bin on Unix-like systems)
install(TARGETS sandbox unlink_test file_operations_test
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

# Install the container script as 'sandcon'
install(FILES ${CMAKE_SOURCE_DIR}/run_in_container.sh 
        RENAME sandcon
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

# Install Dockerfile next to the executable for container builds
install(FILES ${CMAKE_SOURCE_DIR}/Dockerfile
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/sandbox
        PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)

# Set installation message
message(STATUS "After building, run 'sudo cmake --install build' to install to ${CMAKE_INSTALL_PREFIX}")
message(STATUS "Default install location: ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}")
message(STATUS "Dockerfile will be installed to: ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATAROOTDIR}/sandbox")
