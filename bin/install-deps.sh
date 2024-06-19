#!/bin/sh

sudo apt-get update && sudo apt-get install -qy \
  build-essential \
  cmake \
  cppcheck \
  libarmadillo-dev \
  libboost-all-dev \
  libuhd-dev \
  libvulkan-dev \
  python3-numpy \
  python3-pip \
  unzip \
  valgrind \
  wget \
  && \
  pip install sigmf && \
  wget https://sourceforge.net/projects/sigpack/files/sigpack-1.2.7.zip -O sigpack.zip && unzip sigpack.zip && ln -s sigpack-*/sigpack . && \
  git clone https://github.com/google/flatbuffers -b v24.3.25 && \
  git clone https://github.com/nlohmann/json -b v3.11.3 && \
  git clone https://github.com/deepsig/libsigmf -b v1.0.2 && \
  git clone https://github.com/DTolm/VkFFT -b v1.3.4
