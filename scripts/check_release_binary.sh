#!/bin/bash

set -e

# Parse arguments
while [[ $# -gt 0 ]]; do
  case $1 in
    --binary)
      DUCKDB_BINARY="$2"
      shift 2
      ;;
    --prev-version)
      PREV_VERSION="$2"
      shift 2
      ;;
    --current-version)
      CURRENT_VERSION="$2"
      shift 2
      ;;
    --current-hash)
      CURRENT_HASH="$2"
      shift 2
      ;;
    --repo-root)
      REPO_ROOT="$2"
      shift 2
      ;;
    --platform)
      EXTENSION_PLATFORM="$2"
      shift 2
      ;;
    *)
      echo "Unknown argument: $1"
      exit 1
      ;;
  esac
done

if [ -z "$EXTENSION_PLATFORM" ]; then
  OS="$(uname -s)"
  ARCH="$(uname -m)"
  case "$OS" in
    Linux)
      case "$ARCH" in
        x86_64) EXTENSION_PLATFORM="linux_amd64" ;;
        aarch64|arm64) EXTENSION_PLATFORM="linux_arm64" ;;
        *) echo "Unsupported Linux architecture: $ARCH"; exit 1 ;;
      esac
      ;;
    Darwin)
      case "$ARCH" in
        x86_64) EXTENSION_PLATFORM="osx_amd64" ;;
        arm64) EXTENSION_PLATFORM="osx_arm64" ;;
        *) echo "Unsupported macOS architecture: $ARCH"; exit 1 ;;
      esac
      ;;
    MSYS*|MINGW*|CYGWIN*)
      case "$ARCH" in
        x86_64) EXTENSION_PLATFORM="windows_amd64" ;;
        aarch64|arm64) EXTENSION_PLATFORM="windows_arm64" ;;
        *) echo "Unsupported Windows architecture: $ARCH"; exit 1 ;;
      esac
      ;;
    *)
      echo "Unsupported OS: $OS"
      exit 1
      ;;
  esac
  echo "Detected platform: $EXTENSION_PLATFORM"
fi

if [ -z "$CURRENT_VERSION" ]; then
  echo "Usage: $0 --current-version <version> [--platform <platform>] [--binary <path>] [--prev-version <version>]"
  exit 1
fi

if [ -z "$DUCKDB_BINARY" ]; then
  echo "Binary not provided. Downloading latest for platform: $EXTENSION_PLATFORM"
  mkdir -p /tmp
  
  # Determine download URL based on platform
  if [[ "$EXTENSION_PLATFORM" == "osx_arm64" || "$EXTENSION_PLATFORM" == "osx_amd64" ]]; then
    URL="https://artifacts.duckdb.org/latest/duckdb-binaries-osx.zip"
    ZIP="/tmp/duckdb-binaries-osx.zip"
  elif [[ "$EXTENSION_PLATFORM" == "linux_amd64" ]]; then
    URL="https://artifacts.duckdb.org/latest/duckdb-binaries-linux-amd64.zip"
    ZIP="/tmp/duckdb-binaries-linux-amd64.zip"
  elif [[ "$EXTENSION_PLATFORM" == "linux_arm64" ]]; then
    URL="https://artifacts.duckdb.org/latest/duckdb-binaries-linux-arm64.zip"
    ZIP="/tmp/duckdb-binaries-linux-arm64.zip"
  elif [[ "$EXTENSION_PLATFORM" == "windows_amd64" || "$EXTENSION_PLATFORM" == "windows_arm64" ]]; then
    URL="https://artifacts.duckdb.org/latest/duckdb-binaries-windows.zip"
    ZIP="/tmp/duckdb-binaries-windows.zip"
  else
    echo "Unknown platform for automatic download: $EXTENSION_PLATFORM"
    exit 1
  fi

  curl -L -o "$ZIP" "$URL"
  unzip -o "$ZIP" -d /tmp
  
  # The artifacts zip contains more zips, we need to extract the CLI zip
  if [[ "$EXTENSION_PLATFORM" == "osx_arm64" || "$EXTENSION_PLATFORM" == "osx_amd64" ]]; then
    unzip -o /tmp/duckdb_cli-osx-universal.zip -d /tmp
    DUCKDB_BINARY="/tmp/duckdb"
  elif [[ "$EXTENSION_PLATFORM" == "linux_amd64" ]]; then
    unzip -o /tmp/duckdb_cli-linux-amd64.zip -d /tmp
    DUCKDB_BINARY="/tmp/duckdb"
  elif [[ "$EXTENSION_PLATFORM" == "linux_arm64" ]]; then
    unzip -o /tmp/duckdb_cli-linux-arm64.zip -d /tmp
    DUCKDB_BINARY="/tmp/duckdb"
  elif [[ "$EXTENSION_PLATFORM" == "windows_amd64" ]]; then
    unzip -o /tmp/duckdb_cli-windows-amd64.zip -d /tmp
    DUCKDB_BINARY="/tmp/duckdb.exe"
  elif [[ "$EXTENSION_PLATFORM" == "windows_arm64" ]]; then
    unzip -o /tmp/duckdb_cli-windows-arm64.zip -d /tmp
    DUCKDB_BINARY="/tmp/duckdb.exe"
  fi

  if [[ "$EXTENSION_PLATFORM" != "windows_amd64" && "$EXTENSION_PLATFORM" != "windows_arm64" ]]; then
    chmod +x "$DUCKDB_BINARY"
  fi
fi

if [ -z "$PREV_VERSION" ]; then
  PREV_VERSION="v1.2.0"
fi

PYTHON_EXE="python3"
echo "Setting up venv for DuckDB $PREV_VERSION..."
python3 -m venv venv

# Determine python executable in venv
if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" || "$OSTYPE" == "win32" ]]; then
  PYTHON_VENV="./venv/Scripts/python"
else
  PYTHON_VENV="./venv/bin/python3"
fi

# Install previous version
$PYTHON_VENV -m pip install duckdb=="$PREV_VERSION"
$PYTHON_VENV -c "import duckdb;print(f'Installed DuckDB version: {duckdb.query(\"pragma version\").fetchone()[0]}')"

# Run the python check script
CMD="$PYTHON_EXE scripts/check_release_binary.py \
  --binary \"$DUCKDB_BINARY\" \
  --current-version \"$CURRENT_VERSION\" \
  --platform \"$EXTENSION_PLATFORM\""

if [ -n "$CURRENT_HASH" ]; then
  CMD="$CMD --current-hash \"$CURRENT_HASH\""
fi

if [ -n "$REPO_ROOT" ]; then
  CMD="$CMD --repo-root \"$REPO_ROOT\""
fi

if [ -n "$PREV_VERSION" ]; then
  CMD="$CMD --prev-version \"$PREV_VERSION\" --python-venv \"$PYTHON_VENV\""
fi

eval $CMD
