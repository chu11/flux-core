#!/bin/sh

jobid1=`flux mini submit -n1 sleep inf`
jobid2=`flux mini submit -n1 sleep inf`
jobid3=`flux mini submit -n1 sleep inf`
jobid4=`flux mini submit -n1 sleep inf`
flux job status ${jobid1} ${jobid2} ${jobid3} ${jobid4}
