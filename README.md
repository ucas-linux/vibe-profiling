# vibe-profiling
using GCC -finstrument-functions option to do performance profile various of C/C++ programs

This project enables function-level profiling using GCC's -finstrument-functions.
It provides detailed insights into execution flow, helping developers analyze performance, debug,
and optimize large-scale models like LLaMA for NLP tasks.

## Start

git clone https://github.com/ucas-linux/vibe-profiling.git

cd vibe-profiling

git submodule update --init --recursive

## Build
sudo apt-get install elfutils libdw-dev


./build-binary.sh


