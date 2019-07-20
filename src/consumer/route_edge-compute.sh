#!/bin/bash

# destination MAC address URI
declare -r dest="ether://[b8:27:eb:ba:e6:3a]"
# local out interface URI
declare -r srciface="dev://eth0"

# sets up routes in NFD for consumer
nfdc face create $dest local $srciface
nfdc route add /edge-compute/computer $dest

