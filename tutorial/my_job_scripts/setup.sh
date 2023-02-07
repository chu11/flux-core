#!/bin/sh

for i in `seq 1 10000`
do
    cp template.sh myjob${i}.sh
done
