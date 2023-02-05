#!/bin/sh

rm -rf local
cd flux-core
make clean ; make distclean ; ./autogen.sh ; ./configure --prefix=/g/g0/achu/chaos/git/flux-framework/local ; make -j16 ; make install
setenv PKG_CONFIG_PATH /g/g0/achu/chaos/git/flux-framework/local/lib/pkgconfig
cd ../flux-sched
make clean ; make distclean ; ./autogen.sh ; ./configure --prefix=/g/g0/achu/chaos/git/flux-framework/local ; make -j16; make -j16 check; make install
