#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

g++ -std=c++17 -O0 -g \
    orderbook_tests.cpp ../src/OrderBook.cpp \
    -o /tmp/orderbook_tests

/tmp/orderbook_tests
