#!/bin/bash

# nexthop-to-CN MAC address URI
declare -r toCN="ether://[b8:27:eb:70:17:37]"
# nexthop-to-CN out interface URI
declare -r outiface="dev://eth0"
# consumer MAC address URI(s)
declare -ra cons=("ether://[b8:27:eb:44:c4:90]" "ether://[b8:27:eb:d9:8a:2d]")
# consumer in interface URI
declare -r iniface="dev://eth0"

# sets up routes in NFD for access router

# to CN routes
nfdc face create $toCN local $outiface 
nfdc route add /edge-compute/computer $toCN



# consumer routes
nfdc face create ${cons[0]} local $iniface
# change identifiers of requesters as needed
nfdc route add /edge-compute/requester/1 ${cons[0]}

nfdc face create ${cons[1]} local $iniface
# change identifiers of requesters as needed
nfdc route add /edge-compute/requester/2 ${cons[1]}

# add more consumers
