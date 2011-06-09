#!/bin/bash

wget http://gcc-uk.internet.bs/releases/gcc-4.2.4/gcc-4.2.4.tar.bz2
tar -xvf gcc-4.2.4.tar.bz2

INSTALL_DIR=$(readlink -f ../../tools)

mkdir build_dir-gcc-4.2.4
cd build_dir-gcc-4.2.4

../gcc-4.2.4/configure \
    --prefix=${INSTALL_DIR}/usr \
    --bindir=${INSTALL_DIR}/usr/x86_64-pc-linux-gnu/gcc-bin/4.2.4 \
    --includedir=${INSTALL_DIR}/usr/lib/gcc/x86_64-pc-linux-gnu/4.2.4/include \
    --datadir=${INSTALL_DIR}/usr/share/gcc-data/x86_64-pc-linux-gnu/4.2.4 \
    --mandir=${INSTALL_DIR}/usr/share/gcc-data/x86_64-pc-linux-gnu/4.2.4/man \
    --infodir=${INSTALL_DIR}/usr/share/gcc-data/x86_64-pc-linux-gnu/4.2.4/info \
    --with-gxx-include-dir=${INSTALL_DIR}/usr/lib/gcc/x86_64-pc-linux-gnu/4.2.4/include/g++-v4 \
    --host=x86_64-pc-linux-gnu \
    --build=x86_64-pc-linux-gnu \
    --enable-multilib \
    --enable-checking=release --disable-libgcj --enable-languages=c,c++ \
    --enable-shared --enable-threads=posix --enable-__cxa_atexit

make -j16
make install
