#!/bin/bash

# nexthop-to-CN MAC address URI
declare -r toCN="ether://[00:21:5a:10:9d:64]"
# nexthop-to-CN out interface URI
declare -r outiface="dev://eth0"
# nexthop-to-consumer MAC address URI
declare -r tocons="ether://[b8:27:eb:ba:e6:3a]"
# nexthop-to-consumer in interface URI
declare -r iniface="dev://eth0"

# sets up routes in NFD for a general router

# to CN routes
nfdc face create $toCN local $outiface
nfdc route add /edge-compute/computer $toCN



# to consumer routes
nfdc face create $tocons local $iniface
nfdc route add /edge-compute/requester $tocons
