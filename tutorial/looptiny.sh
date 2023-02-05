#!/bin/sh

for i in `seq 1 10`
do
    flux mini submit my_job_scripts/myjob${i}.sh
done
