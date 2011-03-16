#!/bin/bash

wget http://ftp.gnu.org/gnu/binutils/binutils-2.18.tar.gz
tar -xvf binutils-2.18.tar.gz

INSTALL_DIR=../../tools

./configure --build=x86_64-pc-linux-gnu \
    --host=x86_64-pc-linux-gnu \
    --target=x86_64-pc-linux-gnu \
    --prefix=${INSTALL_DIR}/usr \
    --datadir=${INSTALL_DIR}/usr/share/binutils-data/x86_64-pc-linux-gnu/2.18 \
    --infodir=${INSTALL_DIR}/usr/share/binutils-data/x86_64-pc-linux-gnu/2.18/info \
    --mandir=${INSTALL_DIR}/usr/share/binutils-data/x86_64-pc-linux-gnu/2.18/man \
    --bindir=${INSTALL_DIR}/usr/x86_64-pc-linux-gnu/binutils-bin/2.18 \
    --libdir=${INSTALL_DIR}/usr/lib64/binutils/x86_64-pc-linux-gnu/2.18 \
    --libexecdir=${INSTALL_DIR}/usr/lib64/binutils/x86_64-pc-linux-gnu/2.18 \
    --includedir=${INSTALL_DIR}/usr/lib64/binutils/x86_64-pc-linux-gnu/2.18/include \
    --enable-64-bit-bfd \
    --enable-shared \
    --disable-werror

make
make install
