#!/bin/bash
git clone git@github.com:maidsafe/MaidSafe.git
cd MaidSafe
git submodule update --init
git checkout next
git pull
git submodule foreach 'git checkout next && git pull'
mkdir release_build
cd release_build
cmake .. -G $1 -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
cd ..
mkdir debug_build
cd debug_build
cmake .. -G $1 -DCMAKE_BUILD_TYPE=Debug
cmake --build . --config Debug
exit