#!/bin/bash

make clean
make
cd build

# Filename for the script containing test commands
FILE="../scripts/test_commands.txt"

# Test name extracted from the command line argument
TEST_NAME=$1

# Mode extracted from the command line argument
MODE=$2

# Grep the file for the appropriate line containing the command
COMMAND=$(grep -w "run $TEST_NAME" $FILE)

if [ "$MODE" == "-m" ]; then
    if [ "$3" == "run" ]; then
        # Run mode: modify command as per the requirement
        COMMAND=$(echo "$COMMAND" | sed -e 's/ < \/dev\/null.*//')
        COMMAND="pintos $COMMAND"
        echo "Executing: $COMMAND"
        eval $COMMAND
    elif [ "$3" == "debug" ]; then
        COMMAND=$(echo "$COMMAND" | sed -e 's/ -T [^ ]*//' -e 's/ < \/dev\/null.*//')
        COMMAND="pintos --gdb $COMMAND"
        echo "Executing: $COMMAND"
        $COMMAND
    elif [ "$3" == "test" ]; then
        # Extract the path for the Perl command
        TEST_PATH=$(echo "$COMMAND" | grep -oP '(?<=2> ).*(?=\.errors)')
        TEST_PATH="${TEST_PATH%.errors}"

        # Test mode: modify command as per the requirement
        COMMAND="pintos $COMMAND"
        echo "Executing: $COMMAND"
        eval $COMMAND

        # Check if the output file is created
        if [ -f "$TEST_PATH.output" ]; then
            # Running the Perl script
            TEST_COMMAND="perl -I../.. ../../$TEST_PATH.ck $TEST_PATH $TEST_PATH.result"
            echo "Executing: $TEST_COMMAND"
            eval $TEST_COMMAND
        else
            echo "Error: Output file $TEST_PATH.output not found."
        fi
    else
        echo "Unknown mode: $3"
    fi
else
    echo "Usage: bash run.sh <test name> -m <mode>"
    echo "Modes: test, debug"
fi
