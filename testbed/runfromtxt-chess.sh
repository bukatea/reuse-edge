#!/bin/bash

declare -i trials=20
declare -i counter=1

all=("0.3" "0.5" "0.7")
for i in "${all[@]}"
do
    for j in 8 10 12
    do
        for k in $(seq 1 $trials)
        do
            # change identifier per client
            ndn-cxx/build/examples/MACconsumer_chess 1 "$i" $j "data_no_cache_chess.dat" "chessparams.txt" $counter
            counter+=1
        done
    done
done
