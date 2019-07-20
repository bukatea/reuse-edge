#!/bin/bash

# destination MAC address URI
declare -r dest="ether://[b8:27:eb:70:17:37]"
# local out interface URI
declare -r srciface="dev://enp0s25"

# sets up routes in NFD for CN
nfdc face create $dest local $srciface
nfdc route add /edge-compute/requester $dest
