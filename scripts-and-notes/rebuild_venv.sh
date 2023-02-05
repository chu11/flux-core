#!/bin/sh

./autogen.sh; ./configure; make clean; make distclean; ./autogen.sh; ./configure; make -j16; make -j16 check
git log  --pretty=oneline | head -n 1
