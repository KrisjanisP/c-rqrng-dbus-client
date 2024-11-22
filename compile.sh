#! /bin/bash

SCRIPT_DIR=$(dirname "$0")

pushd "$SCRIPT_DIR"
mkdir -p bin
gcc sd-bus-client.c -o bin/sd-bus-client $(pkg-config --cflags --libs libsystemd)
popd
