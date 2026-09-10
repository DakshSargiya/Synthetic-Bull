#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

DEPS_DIR="deps"

# ── 1. Clone / update dependencies ────────────────────────────────
if [ ! -d "$DEPS_DIR/uWebSockets" ]; then
  echo "⬇  Cloning uWebSockets …"
  mkdir -p "$DEPS_DIR"
  git clone --recurse-submodules https://github.com/uNetworking/uWebSockets.git "$DEPS_DIR/uWebSockets"
else
  echo "✓  uWebSockets already present"
fi

if [ ! -f "$DEPS_DIR/json.hpp" ]; then
  echo "⬇  Downloading nlohmann/json single-header …"
  mkdir -p "$DEPS_DIR/nlohmann"
  curl -sL https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp \
       -o "$DEPS_DIR/nlohmann/json.hpp"
else
  echo "✓  nlohmann/json already present"
fi

# ── 2. Build uSockets static library ─────────────────────────────
USOCKETS_DIR="$DEPS_DIR/uWebSockets/uSockets"
if [ ! -f "$USOCKETS_DIR/uSockets.a" ]; then
  echo "🔨 Building uSockets …"
  make -C "$USOCKETS_DIR" -j"$(nproc)"
else
  echo "✓  uSockets.a already built"
fi

# ── 3. Compile the backend ────────────────────────────────────────
echo "🔨 Compiling synthetic-bull …"
g++ -std=c++17 -O2 \
    -I"$DEPS_DIR/uWebSockets/src" \
    -I"$DEPS_DIR/uWebSockets/uSockets/src" \
    -I"$DEPS_DIR" \
    src/main.cpp src/OrderBook.cpp src/GBMGenerator.cpp src/Exchange.cpp \
    "$USOCKETS_DIR/uSockets.a" \
    -lpthread -lssl -lcrypto -lz \
    -o synthetic-bull

echo "✅ Build complete → ./synthetic-bull"
