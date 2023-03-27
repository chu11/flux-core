#!/bin/sh

jobid1=`flux mini submit -n1 --job-name=Level2 sleep inf`
jobid2=`flux mini submit -n1 --job-name=Level2 sleep inf`
flux job status ${jobid1} ${jobid2}
