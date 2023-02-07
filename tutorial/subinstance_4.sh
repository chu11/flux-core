#!/bin/sh

batchid1=`flux mini batch -n1 ./loop_iter.sh 1 250`
batchid2=`flux mini batch -n1 ./loop_iter.sh 251 500`
batchid3=`flux mini batch -n1 ./loop_iter.sh 501 750`
batchid4=`flux mini batch -n1 ./loop_iter.sh 751 1000`
flux job status ${batchid1} ${batchid2} ${batchid3} ${batchid4}
