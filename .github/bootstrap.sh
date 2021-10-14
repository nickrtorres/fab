#!/bin/sh -eux

TDIR=$(mktemp -d)

install_gtest() {
  (
    cd "$TDIR"

    git clone https://github.com/google/googletest.git 
    cd googletest
    mkdir build
    cd build
    cmake .. -DCMAKE_INSTALL_PREFIX="/opt"
    make -j "$(getconf _NPROCESSORS_ONLN)"
    make install
  )
}

main() {
  install_gtest
  rm -rf "$TDIR"
}

main
