#!/bin/bash

wget http://gcc-uk.internet.bs/releases/gcc-4.3.0/gcc-4.3.0.tar.bz2
tar -xvf gcc-4.3.0.tar.bz2

INSTALL_DIR=../../tools

mkdir build_dir
cd build_dir

../gcc-4.3.0/configure \
    --prefix=${INSTALL_DIR}/usr \
    --bindir=${INSTALL_DIR}/usr/x86_64-pc-linux-gnu/gcc-bin/4.3.0 \
    --includedir=${INSTALL_DIR}/usr/lib/gcc/x86_64-pc-linux-gnu/4.3.0/include \
    --datadir=${INSTALL_DIR}/usr/share/gcc-data/x86_64-pc-linux-gnu/4.3.0 \
    --mandir=${INSTALL_DIR}/usr/share/gcc-data/x86_64-pc-linux-gnu/4.3.0/man \
    --infodir=${INSTALL_DIR}/usr/share/gcc-data/x86_64-pc-linux-gnu/4.3.0/info \
    --with-gxx-include-dir=${INSTALL_DIR}/usr/lib/gcc/x86_64-pc-linux-gnu/4.3.0/include/g++-v4 \
    --host=x86_64-pc-linux-gnu \
    --build=x86_64-pc-linux-gnu \
    --enable-multilib \
    --enable-checking=release --disable-libgcj --enable-languages=c,c++ \
    --enable-shared --enable-threads=posix --enable-__cxa_atexit

make -j5
make install
