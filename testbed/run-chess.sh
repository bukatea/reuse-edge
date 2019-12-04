#!/bin/bash

declare -i trials=20

all=("0.3" "0.5" "0.7")
for i in "${all[@]}"
do
    for j in 8 10 12
    do
        for k in $(seq 1 $trials)
        do
            # change identifier per client
            ndn-cxx/build/examples/MACconsumer_chess 1 "$i" $j "data_with_cache_chess.dat"
        done
    done
done
