#!/usr/bin/env sh

install_dir=$INTERFACE_DIR/plugins/argparse
mkdir -p $install_dir
cd $install_dir

git clone https://github.com/p-ranav/argparse .

mkdir build
cd build
cmake -DARGPARSE_BUILD_SAMPLES=on -DARGPARSE_BUILD_TESTS=on .. || echo "Cmake failed. Make sure that you have the module installed"
make -j20
