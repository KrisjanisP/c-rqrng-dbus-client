#! /bin/bash

SCRIPT_DIR=$(dirname $(realpath $0))
cd $SCRIPT_DIR

./compile.sh

sudo cp bin/sd-bus-client $HOME/.local/bin

# check if command is available
if command -v sd-bus-client &> /dev/null; then
    echo "sd-bus-client installed successfully"
else
    echo "Error: $HOME/.local/bin/sd-bus-client not found in PATH"
    exit 1
fi
