#!/bin/sh

start=`date +%s`
for myjob in `ls my_job_scripts/myjob*.sh`
do
    #echo $myjob
    flux mini submit ${myjob}
done
end=`date +%s`
runtime=$((end-start))
echo "Job submissions took $runtime seconds"

flux queue drain
end=`date +%s`
runtime=$((end-start))
echo "Job submission and runtime took $runtime seconds"
