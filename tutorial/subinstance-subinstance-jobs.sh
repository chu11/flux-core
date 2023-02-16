#!/bin/sh

batchid1=`flux mini batch -n4 ./subinstance-jobs.sh`
batchid2=`flux mini batch -n4 ./subinstance-jobs.sh`
flux job status ${batchid1} ${batchid2}
