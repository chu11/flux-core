#!/bin/sh

set -x
export PYTHON_VERSION=3.6
proc=`cat /proc/cpuinfo | grep processor | wc -l`
./autogen.sh; ./configure; make clean; make distclean; ./autogen.sh; ./configure; make -j${proc}
git log  --pretty=oneline | head -n 1
