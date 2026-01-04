#!/bin/sh

g++ -o src/profile.o -c -g3 src/profile.cpp

cd extern/llama.cpp

#git apply ../../patchllama.diff

rm -fr   buildinstrument
cmake -B buildinstrument -DCMAKE_BUILD_TYPE=Debug -DGGML_CUDA=ON  -DBUILD_SHARED_LIBS=OFF -DBUILD_SHARED_LIBS=OFF -DGGML_CUDA=ON -DLLAMA_NATIVE=ON -DCMAKE_CXX_FLAGS="-finstrument-functions  -rdynamic" -DCMAKE_EXE_LINKER_FLAGS="-rdynamic" 

#sed -i 's|$| ../../../../../src/profile.o|' buildinstrument/tools/cli/CMakeFiles/llama-cli.dir/link.txt
#sed -i '/CXX_FLAGS/s/-finstrument-functions //g' buildinstrument/tools/cli/CMakeFiles/llama-cli.dir/flags.make

cd buildinstrument

make -j



cd ../../../

g++ -o load_and_configure src/load_and_configure.cpp

./load_and_configure

g++ -o src/generated_exclusion.o -c -g3 generated_exclusion.cpp

cd extern/llama.cpp

rm -fr buildinstrument

cmake -B buildinstrument -DCMAKE_BUILD_TYPE=Debug -DGGML_CUDA=ON  -DBUILD_SHARED_LIBS=OFF -DBUILD_SHARED_LIBS=OFF -DGGML_CUDA=ON -DLLAMA_NATIVE=ON -DCMAKE_CXX_FLAGS="-finstrument-functions  -rdynamic" -DCMAKE_EXE_LINKER_FLAGS="-rdynamic" 

sed -i 's|$| ../../../../../src/profile.o ../../../../../src/generated_exclusion.o|' buildinstrument/tools/cli/CMakeFiles/llama-cli.dir/link.txt
sed -i '/CXX_FLAGS/s/-finstrument-functions //g' buildinstrument/tools/cli/CMakeFiles/llama-cli.dir/flags.make

cd buildinstrument

make -j






