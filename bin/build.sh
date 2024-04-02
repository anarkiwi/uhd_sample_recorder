#!/bin/bash
set -e
cd VkFFT && mkdir build && cd build && CMAKE_BUILD_TYPE=Release cmake -DGLSLANG_GIT_TAG=13.0.0 .. && make -j $(nproc) && cd ../..
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
mkdir build && cd build && cmake ../lib && make -j $(nproc) && cd ..
