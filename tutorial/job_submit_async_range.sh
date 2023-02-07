#!/bin/sh

flux mini submit --wait --cc="$1-$2" "my_job_scripts/myjob{cc}.sh"

