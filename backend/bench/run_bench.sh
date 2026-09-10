#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

g++ -std=c++17 -O2 \
    latency_bench.cpp ../src/OrderBook.cpp \
    -o /tmp/latency_bench

/tmp/latency_bench "${1:-200000}"
