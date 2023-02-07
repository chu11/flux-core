#!/bin/sh

for i in `seq $1 $2`
do
    # echo $i
    flux mini submit my_job_scripts/myjob${i}.sh > /dev/null
done

flux queue drain

