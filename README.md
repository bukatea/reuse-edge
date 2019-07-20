# reuse-edge
This repository contains a testbed-deployable application-level implementation of compute reuse for edge networks, as described in the seminal SEC paper *ICedge*.

Currently, we have three applications implementing reuse: matrix multiplication (shortened to "matrix"), face detection on a simulated camera feed (shortened to "simcamera"), and a chess optimal move algorithm (shortened to "chess").
These applications consist of two files (for now): a consumer source file and a CN source file. They describe how consumers send requests for computation and how CNs respond to them and provide computation services.
Each source file is heavily commented. To write new applications, use any of the existing applications as a template.

For further implementation details, refer to the MobileEdgeCom paper *A Case*.

## Installation Instructions
**Make sure to follow this part very carefully or the testbed will not work as intended!**

Currently, the tested CN OS's are: *Ubuntu 18.04*.

&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;The tested consumer OS's are: *Raspbian Stretch*

Prerequisites (unless stated, on both Pi's and CN's):
1. ndn-cxx version 0.6.5 (on routers as well)
2. NFD version 0.6.5 (on routers as well)
3. libjpeg and libpng
4. cmake >= 3.8.2
5. g++ >= 8 for C++17

### CN
To start, clone this repository into home or preferred directory using `git clone https://github.com/bukatea/reuse-edge.git`. The commands below assume the repo was cloned to home.

First, we need to replace the wscript in ndn-cxx and ndn-cxx/examples with the reuse-edge version, as we've added new libraries (Eigen, dlib, and Goldfish) and ndn-cxx needs to be aware of those as it compiles examples.
Run
```
cp ~/reuse-edge/src/CN/wscript ~/ndn-cxx
cp ~/reuse-edge/src/examples/wscript ~/ndn-cxx/examples
```
To copy the wscripts over. **Note that the wscript assumes the name of the user is `nsol`. To change this in the wscript, find all instances of `nsol` and replace them with your user name. Furthermore, if you cloned to a directory other than home, also change all lines containing `nsol` to match your installed directory.**

After this, copy the .cpp files contained in [reuse-edge/src/CN](../master/src/CN) to the examples folder of ndn-cxx. For now, do not re-`./waf configure` yet.

Now, we need to move on to compiling external libraries. All of the prerequisites can be installed via a package manager (for Debian-based, use `apt`; for Fedora-based, `yum`). The only libraries to compile manually from [reuse-edge/external](../master/external) are dlib and Goldfish. Eigen is a header-only library, so there is nothing to compile there. **However, make sure to copy the eigen folder from reuse-edge/external to ndn-cxx, using e.g. `cp -r ~/reuse-edge/external/eigen ~/ndn-cxx`.**

When waf looks for dlib and Goldfish when a `./waf configure` is called, it will look for *static* libraries in two directories: reuse-edge/external/dlib/examples/build/dlib_build and reuse-edge/external/Goldfish/build respectively. If those libdlib.a and libengine.a files do not exist, then waf does not let you compile the reuse applications.

To compile dlib, run
```
cd ~/reuse-edge/external/dlib/examples
mkdir build && cd build
cmake .. # note that depending on the processor, YOU WILL WANT TO ENABLE SSE2 SSE4, or AVX instructions because it MAKES THE DETECTION faster; see below for more info
cmake --build .
```
To discover which instructions to enable in the above cmake command, run an `lscpu | grep 'sse2\|sse4\|avx'` on the machine. If more than one are matched, choose sse4 over sse2, avx over sse4.

Once you have figured out which instructions to enable, replace the `cmake ..` from above with `cmake -DUSE_SSE2_INSTRUCTIONS=ON ..` for sse2, `cmake -DUSE_SSE4_INSTRUCTIONS=ON ..` for sse4, and `cmake -DUSE_AVX_INSTRUCTIONS=ON ..` for avx. Make sure that you choose **only one of** sse2, sse4, or avx.

Now, there should be libdlib.a in reuse-edge/external/dlib/examples/build/dlib_build.

To compile Goldfish, run
```
cd ~/reuse-edge/external/Goldfish
mkdir build && cd build
# Make sure here that your g++ version is at least 8 for C++17 features. If not, select what to use explicitly, e.g.
export CXX=/opt/gcc-8.1.0/bin/g++-8.1.0
export CC=/opt/gcc-8.1.0/bin/gcc-8.1.0
cmake .. -DCMAKE_BUILD_TYPE=Release
make
```

Now, there should be a libengine.a in reuse-edge/external/Goldfish/build.

Run
```
cd ~/ndn-cxx
./waf configure
./waf
./waf install
```
to make the the reuse application executables in the ndn-cxx/build/examples.

Finally, run
```
cp ~/reuse-edge/src/CN/route_edge-compute.sh ~
cp ~/reuse-edge/testbed/log_cpu.sh ~
```
to copy the NFD routing script and CPU logging script to home. **Every time you restart NFD, you must run the routing script once to set up the correct static routes.**

### Consumers
Clone the repository into home in the same way.

Copy the wscripts into the ndn-cxx and ndn-cxx/examples directory with
```
cp ~/reuse-edge/src/consumer/wscript ~/ndn-cxx
cp ~/reuse-edge/src/examples/wscript ~/ndn-cxx/examples
```
This wscript assumes that the name of the user is `pi`. Again, if your consumer user name is different or your clone directory is different from home, change all instances (and directories) of `pi` to your user name.

Copy the .cpp files contained in [reuse-edge/src/consumer](../master/src/consumer) to the examples folder of ndn-cxx.

Prerequisites should be installed. Again make sure that Eigen is copied into the ndn-cxx directory. For compiling dlib and Goldfish, follow the same process as the CN. Configure, compile, and install using waf. If using Ubuntu, make sure to `sudo ldconfig` afterward.

For the script files, run
```
cp ~/reuse-edge/src/consumer/route_edge-compute.sh ~
cp ~/reuse-edge/testbed/run* ~ # test scripts for matrix, simcamera, and chess
cp ~/reuse-edge/testbed/captures/* ~/ndn-cxx/build/examples # this is so that full camera captures are available when running the simcamera application
```
Again, make sure to run the routing script every time NFD is restarted.

### Routers/Forwarding Nodes
Clone the repository into home.

Simply copy the [reuse-edge/src/routers/access](../master/src/routers/access) route script for access routers (first-hop from consumer) and [reuse-edge/src/routers/single](../master/src/routers/single) route script for all other routers.
```
cp ~/reuse-edge/src/routers/access/route_edge-compute.sh ~
# or
cp ~/reuse-edge/src/routers/single/route_edge-compute.sh ~
```

## Past Submissions
| Type | Name | Modules | Status |
| --- | --- | --- | --- |
| Conference | ACM/IEEE SEC 2019 - *ICedge: When Edge Computing Meets Information-Centric Networking* | matrix only | pending |
| Workshop | IEEE GLOBECOM MobileEdgeCom 2019 - *A Case for Compute Reuse in Future Edge Systems: An Empirical Study* | matrix, simcamera, and chess | pending |

## TO-DOs
- [ ] Git submodules for external libraries
- [ ] Switch naive prodreceived-based counting method for smarter bool-based counting method when responding to interests
- [ ] Remove possible redundancy of checking reuse table for matrix twice in [MAC_matrix.cpp](../master/src/CN/MAC_matrix.cpp)
- [ ] Add proper debug statements instead of printing to `cout` for everything
- [ ] Add hashing (*i.e.* no send) functionality for no reuse (reuse already has it)
