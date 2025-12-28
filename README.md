# profiling-llamacpp
using GCC -finstrument-functions option to do performance profile llama C++

This project enables function-level profiling using GCC's -finstrument-functions.
It provides detailed insights into execution flow, helping developers analyze performance, debug,
and optimize large-scale models like LLaMA for NLP tasks.

## Start

git clone https://github.com/ucas-linux/profiling-llamacpp.git

cd profiling-llamacpp

git submodule update --init --recursive

## Build

./build-binary.sh


