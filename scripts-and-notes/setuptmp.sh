#!/bin/sh

cd /tmp/achu
rm -rf flux-core
git clone git@github.com:chu11/flux-core.git
cp ~achu/chaos/git/flux-framework/flux-core/rebuild.sh /tmp/achu/flux-core
cd /tmp/achu/flux-core
./rebuild.sh
