#!/bin/bash

# Check if at least one argument is provided
if [ $# -lt 1 ]; then
  echo "Usage: $0 <program_to_sandbox> [program_args...]"
  echo "Example: $0 /bin/rm /path/to/file.txt"
  exit 1
fi

# Determine if this is the development version or installed version
SCRIPT_NAME=$(basename "$0")
if [ "$SCRIPT_NAME" = "sandcon" ]; then
  # System-wide installed version
  DOCKERFILE_PATH="/usr/local/share/sandbox/Dockerfile"
  # Check if the Dockerfile exists in the expected location
  if [ ! -f "$DOCKERFILE_PATH" ]; then
    # Try alternative locations if the default one doesn't exist
    POSSIBLE_PATHS=(
      "/usr/share/sandbox/Dockerfile"
      "/usr/local/share/sandbox/Dockerfile"
      "/opt/sandbox/Dockerfile"
    )
    
    for PATH_OPTION in "${POSSIBLE_PATHS[@]}"; do
      if [ -f "$PATH_OPTION" ]; then
        DOCKERFILE_PATH="$PATH_OPTION"
        break
      fi
    done
    
    # If we still can't find it, use a temporary directory and create the Dockerfile
    if [ ! -f "$DOCKERFILE_PATH" ]; then
      echo "Warning: Could not find installed Dockerfile. Creating a temporary one."
      TEMP_DIR=$(mktemp -d)
      DOCKERFILE_PATH="$TEMP_DIR/Dockerfile"
      cat > "$DOCKERFILE_PATH" << 'EOF'
FROM ubuntu:22.04

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    && rm -rf /var/lib/apt/lists/*

# Create a working directory
WORKDIR /app

# Set the entrypoint to the sandbox binary
ENTRYPOINT ["sandbox"]

# Default to help message if no command is provided
CMD ["--help"]
EOF
    fi
  fi
  
  DOCKERFILE_DIR=$(dirname "$DOCKERFILE_PATH")
else
  # Development version
  SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" &> /dev/null && pwd)"
  DOCKERFILE_DIR="$SCRIPT_DIR"
fi

# Build the Docker image
echo "Building Docker image from $DOCKERFILE_DIR..."
docker build -t sandbox-container "$DOCKERFILE_DIR" || {
  echo "Docker build failed. Please check the Dockerfile and ensure Docker is running."
  exit 1
}

# Get the program and arguments
PROGRAM="$1"
shift
PROGRAM_ARGS=("$@")

# If the program is a relative path or just a program name, we need to find its absolute path
if [[ "$PROGRAM" != /* ]]; then
  # Check if the program is in the current directory
  if [[ -f "./$PROGRAM" ]]; then
    PROGRAM="$(pwd)/$PROGRAM"
  else
    # Try to find the program in PATH
    WHICH_PROGRAM=$(which "$PROGRAM" 2>/dev/null)
    if [[ -n "$WHICH_PROGRAM" ]]; then
      PROGRAM="$WHICH_PROGRAM"
    else
      echo "Error: Program '$1' not found in current directory or PATH"
      exit 1
    fi
  fi
fi

# Check if the program exists
if [[ ! -f "$PROGRAM" ]]; then
  echo "Error: Program '$PROGRAM' does not exist"
  exit 1
fi

# Verify program is executable
if [[ ! -x "$PROGRAM" ]]; then
  echo "Warning: Program '$PROGRAM' is not executable, attempting to make it executable"
  chmod +x "$PROGRAM" || {
    echo "Error: Unable to make '$PROGRAM' executable"
    exit 1
  }
fi

# Create a volume mount for the program directory
PROGRAM_DIR=$(dirname "$PROGRAM")
PROGRAM_NAME=$(basename "$PROGRAM")

# Debug output to identify the issue
echo "Program path: $PROGRAM"
echo "Program directory: $PROGRAM_DIR"
echo "Program name: $PROGRAM_NAME"
echo "Working directory: $(pwd)"
echo "Arguments: ${PROGRAM_ARGS[@]}"

# Run the Docker container with SYS_PTRACE capability
# Mount the program directory and current directory
echo "Running sandbox in container with program: $PROGRAM ${PROGRAM_ARGS[@]}"
docker run --rm -it \
  --cap-add=SYS_PTRACE \
  --security-opt seccomp=unconfined \
  -v "$PROGRAM_DIR:$PROGRAM_DIR:ro" \
  -v "$(pwd):$(pwd)" \
  -w "$(pwd)" \
  --name sandbox-instance \
  sandbox-container "$PROGRAM" "${PROGRAM_ARGS[@]}"