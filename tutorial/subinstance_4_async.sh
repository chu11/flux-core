#!/bin/sh

start=`date +%s`
flux mini batch -n6 ./job_submit_async_range.sh 1 2500
flux mini batch -n6 ./job_submit_async_range.sh 2501 5000
flux mini batch -n6 ./job_submit_async_range.sh 5001 7500
flux mini batch -n6 ./job_submit_async_range.sh 7501 10000
flux queue drain
end=`date +%s`
runtime=$((end-start))
echo "Job submissions and runtime took $runtime seconds"

