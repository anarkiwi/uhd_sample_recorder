#!/bin/bash
set -e
bin/build-deps.sh
mkdir -p build && cd build && cmake ../lib && make -j $(nproc) && cd ..
