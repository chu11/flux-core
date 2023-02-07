#!/bin/sh

start=`date +%s`
flux mini batch -n4 ./job_submit_loop_range.sh 1 500
flux mini batch -n4 ./job_submit_loop_range.sh 501 1000
flux queue drain
end=`date +%s`
runtime=$((end-start))
echo "Job submissions and runtime took $runtime seconds"
