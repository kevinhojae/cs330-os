#!/bin/bash

# get arguments from command line
while [ "$1" != "" ]; do
    case $1 in
    -m | --mode)
        shift
        mode=$1
        ;;
    -p | --test)
        shift
        test=$1
        ;;
    -c | --command)
        shift
        command=$1
        ;;
    --pc | --put_code)
        shift
        put_codes="-p tests/userprog/$1:$1"
        ;;
    --pf | --put_file)
        shift
        put_files="-p ../../tests/userprog/$1:$1"
        ;;
    --multioom | --novm_folder)
        shift
        novm_folder=$1
        ;;
    esac
    shift
done

# if vm, then put the code from the vm folder, tests/userprog/vm/$1:$1
if [ "$novm_folder" = "true" ]; then
    put_test="-p tests/userprog/no-vm/${test}:${test}"
else
    put_test="-p tests/userprog/${test}:${test}"
fi

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
    echo "pintos --fs-disk filesys.dsk ${put_test} ${put_files} ${put_codes} -- -q -f run \"${command}\""
    pintos --fs-disk filesys.dsk ${put_test} ${put_files} ${put_codes} -- -q -f run "${command}"
fi

# if mode is debug, then run the test with gdb
if [ "$mode" = "debug" ]; then
    echo "pintos --gdb --fs-disk filesys.dsk ${put_test} ${put_files} ${put_codes} -- -q -f run \"${command}\""
    pintos --gdb --fs-disk filesys.dsk ${put_test} ${put_files} ${put_codes} -- -q -f run "${command}"
fi
