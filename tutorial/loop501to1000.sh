#!/bin/sh

for i in `seq 501 1000`
do
    flux mini submit my_job_scripts/myjob${i}.sh > /dev/null
done

flux queue drain
