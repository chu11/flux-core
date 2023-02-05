#!/bin/sh

export PYTHON_VERSION=3.6
./autogen.sh; ./configure; make clean; make distclean; ./autogen.sh; ./configure --enable-sanitizer=address; make -j16; make -j16 check
git log  --pretty=oneline | head -n 1
