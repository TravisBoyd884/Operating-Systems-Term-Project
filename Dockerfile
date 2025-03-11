FROM ubuntu:22.04

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    && rm -rf /var/lib/apt/lists/*

# Create a working directory
WORKDIR /app

# Copy the source files
COPY . .

# Clean any previous build artifacts
RUN rm -rf /app/build /app/bin

# Build the sandbox and test programs
RUN mkdir -p /app/build /app/bin && \
    cd /app && \
    cmake -S . -B /app/build && \
    cmake --build /app/build

# Set the entrypoint to the sandbox binary
# Try the locally built version first, fall back to system installed one if available
ENTRYPOINT ["sh", "-c", "if [ -f /app/bin/sandbox ]; then exec /app/bin/sandbox \"$@\"; else exec sandbox \"$@\"; fi", "--"]

# Default to help message if no command is provided
CMD ["--help"]