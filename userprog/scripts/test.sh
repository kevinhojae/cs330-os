#!/bin/bash


# bash ./scripts/test.sh -t args-single -c 'args-single onearg'
# pintos --fs-disk=10 -p tests/userprog/args-single:args-single -- -q -f run 'args-single onearg'

# get arguments from command line
while getopts ":t:c:" opt; do
  case ${opt} in
    t )
      test=$OPTARG
      ;;
    c )
      command=$OPTARG
      ;;
    \? )
      echo "Usage: bash scripts/test.sh -t test -c command"
      ;;
  esac
done

# rebuild
cd /root/cs330-os/userprog
make clean
make

# run pintos
cd /root/cs330-os/userprog/build
pintos --fs-disk=10 -p tests/userprog/${test}:${test} -- -q -f run ${command}