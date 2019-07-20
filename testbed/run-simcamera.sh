#!/bin/bash

declare -i trials=5
all=("0.7" "0.8" "0.9")
pics=("cam1.png" "doublecam1.png")

choose() {
    if [ "$1" = "cam1.png" ]
    then
        echo 1000
    else
        echo 2000
    fi
}

for i in "${all[@]}"
do
    for h in "${pics[@]}"
    do
        for j in $(seq 1 $trials)
        do
            # change identifier per client
            ndn-cxx/build/examples/MACconsumer_simcamera 1 "$i" "$(choose $h)" "ndn-cxx/build/examples/$h" "data_with_cache_simcamera.dat"
#            ndn-cxx/build/examples/MACconsumer_simcamera 1 "$i" "$(choose $h)" "ndn-cxx/build/examples/$h" "data_no_cache_simcamera.dat"
        done
    done
done
