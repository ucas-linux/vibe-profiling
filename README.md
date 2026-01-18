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


## profile pytorch

cd extern/pytorch

git submodule update --init --recursive

python3 -m venv ~/venvs/torch-src

source ~/venvs/torch-src/bin/activate

python -m pip install -U pip setuptools wheel

export CMAKE_BUILD_TYPE=Debug
export CMAKE_GENERATOR=Ninja
export USE_CUDA=0
export USE_ROCM=0
export USE_CUFILE=0
export USE_NCCL=0
export USE_DISTRIBUTED=0
export CMAKE_CXX_FLAGS=-finstrument-functions
export BUILT_TEST=0

python -m pip install --no-build-isolation -v -e .




