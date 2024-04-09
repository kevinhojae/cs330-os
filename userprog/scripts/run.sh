#!/bin/bash

# bash ./scripts/test.sh -t args-single -c 'args-single onearg'
# pintos --fs-disk=10 -p tests/userprog/args-single:args-single -- -q -f run 'args-single onearg'

# take put arguments as list
# e.g. bash ./scripts/test.sh -t args-multiple -c 'args-multiple onearg twoarg threearg' -p ../../userprog/build/tests/userprog/args-multiple -p ../../userprog/build/tests/userprog/args-multiple -p ../../userprog/build/tests/userprog/args-multiple

# get arguments from command line
while getopts "m:t:c:f:p:" opt; do
    case ${opt} in
    m)
        mode=$OPTARG
        ;;
    t)
        test=$OPTARG
        ;;
    c)
        command=$OPTARG
        ;;
    f)
        put_files="-p ../../tests/userprog/$OPTARG:$OPTARG"
        ;;
    p)
        put_codes="-p tests/userprog/$OPTARG:$OPTARG"
        ;;
    \?)
        echo "Usage: bash scripts/test.sh -t test -c command"
        ;;
    esac
done

# rebuild
cd /root/cs330-os/userprog/build
make clean

cd /root/cs330-os/userprog
make clean
make

# run pintos
cd /root/cs330-os/userprog/build
pintos-mkdisk filesys.dsk 10

# if mode is test, then run the test
if [ "$mode" = "test" ]; then
    echo "pintos --fs-disk filesys.dsk -p tests/userprog/${test}:${test} ${put_files}${put_codes} -- -q -f run \"${command}\""
    pintos --fs-disk filesys.dsk -p tests/userprog/${test}:${test} ${put_files}${put_codes} -- -q -f run "${command}"
fi

# if mode is debug, then run the test with gdb
if [ "$mode" = "debug" ]; then
    echo "pintos --gdb --fs-disk filesys.dsk -p tests/userprog/${test}:${test} ${put_files}${put_codes} -- -q -f run \"${command}\""
    pintos --gdb --fs-disk filesys.dsk -p tests/userprog/${test}:${test} ${put_files}${put_codes} -- -q -f run "${command}"
fi
