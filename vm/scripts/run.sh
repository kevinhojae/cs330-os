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
COMMAND=$(grep "run $TEST_NAME" $FILE)

# if debug, add "pintos --gdb --" to the command e.g. bash scripts/run.sh args-none --debug, 
if [ "$2" == "--debug" ]; then
    COMMAND="pintos --gdb $COMMAND"
else
    COMMAND="pintos $COMMAND"
fi

# Execute the command
echo "Executing: $COMMAND"
$COMMAND
