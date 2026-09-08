#!/bin/bash
set -e
mkdir -p flatbuffers/build
cd flatbuffers/build
cmake ..
make -j $(nproc)
sudo make install
cd ../..
mkdir -p json/build
cd json/build
cmake ..
make -j $(nproc)
sudo make install
cd ../..
mkdir -p libsigmf/build
cd libsigmf/build
cmake -DUSE_SYSTEM_JSON=ON -DUSE_SYSTEM_FLATBUFFERS=ON ..
make -j $(nproc)
sudo make install
cd ../..
