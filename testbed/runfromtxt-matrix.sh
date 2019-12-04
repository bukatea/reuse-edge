#!/bin/bash

declare -i trials=500
declare -i counter=1

for i in 100 250 500 1000
do
    for j in $(seq 1 $trials)
    do
        param="$(cat matparams.txt | head -$counter | tail -1)"
        # change identifier per client
        ndn-cxx/build/examples/MACconsumer_matrix 1 $i $param "data_no_cache_matrix.dat" 0
        counter+=1
    done
done
