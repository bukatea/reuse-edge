#!/bin/bash

# $1 : name of process to log
# $2 : duration between cpu/mem updates + top execution time
# to find the top execution time for precise intervals between logging, run
#     time top -b -n 2 -d "$2" -p $(pidof $1) | tail -1 | awk '{print $9 " " $10}'
# in the shell
# $3 : file to write, in append mode

while true
do
    # logs cpu/mem/unix-timestamp every $2 + top execution time seconds
    echo $(top -b -n 2 -d "$2" -p $(pidof $1) | tail -1 | awk '{print $9 " " $10}') $(($(date +'%s * 1000 + %-N / 1000000'))) | tee -a "$3"
done
