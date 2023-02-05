#!/bin/sh

export PYTHON_VERSION=3.6
./autogen.sh; ./configure; make clean; make distclean
git log  --pretty=oneline | head -n 1
