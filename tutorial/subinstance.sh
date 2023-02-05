#!/bin/sh

batchid1=`flux mini batch -n1 ./loop1to500.sh`
batchid2=`flux mini batch -n1 ./loop501to1000.sh`
flux job status ${batchid1} ${batchid2}
