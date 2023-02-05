#!/bin/sh

flux mini bulksubmit my_job_scripts/myjob{}.sh ::: $(seq 1 1000)
