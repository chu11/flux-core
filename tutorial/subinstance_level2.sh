#!/bin/sh
# filename: subinstance_level2.sh
id=`flux mini submit --job-name=Level2 -N1 sleep 60`
flux job status ${id}
