#!/bin/bash

declare -i trials=500

for i in 100 250 500 1000
do
    for j in $(seq 1 $trials)
    do
        param="$(shuf -i 10-100 -n 1) $(shuf -i 1-5 -n 1)"
        # change identifier per client
        ndn-cxx/build/examples/MACconsumer_matrix 1 $i $param "data_with_cache_matrix.dat" 1
        echo $param >> matparams.txt
    done
done
