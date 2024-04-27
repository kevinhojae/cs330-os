#!/bin/bash

make clean
make
cd build

# bash scripts/run.sh args-none

# Filename for the script containing test commands
FILE="../scripts/test_commands.txt"

# Test name extracted from the command line argument
TEST_NAME=$1

# Grep the file for the appropriate line containing the command
COMMAND=$(grep -o "pintos.*run $TEST_NAME" $FILE)

# Execute the command
echo "Executing: $COMMAND"
$COMMAND
