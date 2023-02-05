#!/bin/sh

flux queue list

flux config load <<-EOT
[policy.limits]
duration = "4h"
job-size.max.nnodes = 10
job-size.min.nnodes = 2
job-size.max.ncores = -1
job-size.min.ncores = -1

EOT

flux queue list

echo "===================="

flux R encode -r 0-3 -p batch:0-1 -p debug:2-3 | tr -d '\n' | flux kvs put -r resource.R=-

flux config load <<-EOT
[policy.jobspec.defaults.system]
queue = "batch"
duration = "1h"

[policy.limits]
duration = "4h"
job-size.max.nnodes = 10
job-size.min.nnodes = 2
job-size.max.ncores = -1
job-size.min.ncores = 1

[queues.batch]
requires = [ "batch" ]
policy.jobspec.defaults.system.duration = "2h"

[queues.debug]
requires = [ "debug" ]

[queues.debug.policy.limits]
duration = "30m"
job-size.max.nnodes = 8
job-size.min.nnodes = 1
job-size.max.ncores = 8
job-size.min.ncores = 1
job-size.max.ngpus = 8
job-size.min.ngpus = 1

EOT

flux module unload sched-simple
flux module reload resource
flux module load sched-simple

flux queue list

flux resource list
