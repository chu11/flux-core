#!/bin/sh
# filename: subinstance_level1.sh
id1=`flux mini submit --job-name=Level1 -N1 sleep 60`
id2=`flux mini batch -N2 ./subinstance_level2.sh`
flux job status ${id1} ${id2}
