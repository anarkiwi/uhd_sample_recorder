#!/bin/sh

sudo apt-get update && sudo apt-get install -qy \
  build-essential \
  cmake \
  cppcheck \
  git \
  libboost-all-dev \
  libuhd-dev \
  python3-pip \
  valgrind \
  && \
  pip install --break-system-packages sigmf && \
  git clone https://github.com/google/flatbuffers -b v24.3.25 && \
  git clone https://github.com/nlohmann/json -b v3.11.3 && \
  git clone https://github.com/deepsig/libsigmf -b v1.0.2
