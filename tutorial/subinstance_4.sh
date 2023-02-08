#!/bin/sh

start=`date +%s`
flux mini batch -n6 ./job_submit_loop_range.sh 1 250
flux mini batch -n6 ./job_submit_loop_range.sh 251 500
flux mini batch -n6 ./job_submit_loop_range.sh 501 750
flux mini batch -n6 ./job_submit_loop_range.sh 751 1000
flux queue drain
end=`date +%s`
runtime=$((end-start))
echo "Job submissions and runtime took $runtime seconds"

