#!/bin/sh

for myjob in `ls my_job_scripts/myjob*.sh`
do
    #echo $myjob
    flux mini submit ${myjob}
done
