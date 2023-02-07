#!/bin/sh

batchid1=`flux mini batch -n1 ./loop_iter.sh 1 500`
batchid2=`flux mini batch -n1 ./loop_iter.sh 501 1000`
flux job status ${batchid1} ${batchid2}
