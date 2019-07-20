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
1. ndn-cxx version 0.6.5
2. NFD version 0.6.5
3. libjpeg and libpng
4. cmake >= 3.8.2
5. g++ >= 8 for C++17

### CN
To start, clone this repository into home or preferred directory using `git clone https://github.com/bukatea/reuse-edge.git`. The commands below assume the repo was cloned to home.

First, we need to replace the wscript in ndn-cxx with the reuse-edge version, as we've added new libraries (Eigen, dlib, and Goldfish) and ndn-cxx needs to be aware of those as it compiles examples.
Run
```
cp ~/reuse-edge/src/CN/wscript ~/ndn-cxx
```
To copy the wscript over. **Note that the wscript assumes the name of the user is `nsol`. To change this in the wscript, find all instances of `nsol` and replace them with your user name. Furthermore, if you cloned to a directory other than home, also change all lines containing `nsol` to match your installed directory.**

After this, copy the .cpp files contained in [src/CN](../master/src/CN) to the examples folder of ndn-cxx. For now, do not re-`./waf configure` yet.

Now, we need to move on to compiling external libraries. All of the prerequisites can be installed via a package manager (for Debian-based, use `apt`; for Fedora-based, `yum`). Eigen is a header-only library, so there is nothing to compile there. However, for dlib and Goldfish, there are source files that need to be compiled.

## Past Submissions
| Type | Name | Modules | Status |
| --- | --- | --- | --- |
| Conference | ACM/IEEE SEC 2019 - *ICedge: When Edge Computing Meets Information-Centric Networking* | matrix only | pending |
| Workshop | IEEE GLOBECOM MobileEdgeCom 2019 - *A Case for Compute Reuse in Future Edge Systems: An Empirical Study* | matrix, simcamera, and chess | pending |

## TO-DOs
- [ ] Switch naive prodreceived-based counting method for smarter bool-based counting method when responding to interests
- [ ] Remove possible redundancy of checking reuse table for matrix twice in [MAC_matrix.cpp](../master/src/CN/MAC_matrix.cpp)
- [ ] Add proper debug statements instead of printing to `cout` for everything
- [ ] Add hashing (*i.e.* no send) functionality for no reuse (reuse already has it)
