#!/bin/sh

for i in `seq 1 1000`
do
    cp template.sh myjob${i}.sh
done
