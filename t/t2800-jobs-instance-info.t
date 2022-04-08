#!/bin/sh

test_description='Test flux jobs command with instance.* attributes'

. $(dirname $0)/sharness.sh

if test -z "${TEST_UNDER_FLUX_ACTIVE}"; then
    STATEDIR=$(mktemp -d)
fi
test_under_flux 4 job -o,-Sstatedir=${STATEDIR}

export FLUX_URI_RESOLVE_LOCAL=t
waitfile="${SHARNESS_TEST_SRCDIR}/scripts/waitfile.lua"

#  Launch some test instance jobs with varying characteristics
#
#  1. Launch a job that exits immediately so we have a completed instance
#
#  2. Launch an instance job that then submits 4 sleep jobs, touches a
#     file so we know when the jobs have been submitted, and waits for
#     all jobs to complete (essentially forever)
#
#  3. Start a normal job to ensure instance info is blank for non-instance
#     jobs.
#
test_expect_success 'start a set of Flux instances' '
        STATEDIR1=$(mktemp -d) &&
        STATEDIR2=$(mktemp -d) &&
	id=$(flux mini submit flux start -o,--setattr=statedir=${STATEDIR1} /bin/true) &&
	id2=$(flux mini submit -n2 -c1 flux start -o,--setattr=statedir=${STATEDIR2} \
		"flux mini run /bin/false ; \
		 flux mini submit --cc=1-4 sleep 300 && \
		 touch ready && \
		 flux queue idle") &&
	flux mini submit sleep 600 &&
	flux job wait-event $id clean &&
	$waitfile -t 60 ready
'
test_expect_success 'flux-jobs can get instance info' "
	flux jobs -ao '{id.f58:>12} {instance.stats:^25} {instance.utilization!P:>5} {instance.gpu_utilization!P:>5}' > jobs.out &&
	test_debug 'cat jobs.out'
"
test_expect_success 'flux-jobs -o {instance.stats} worked' '
	grep F:1 jobs.out
'
test_expect_success 'flux-jobs {instance.utilization!P} works as expected' "
	flux jobs -ano \
		'{instance.utilization!P},{instance.utilization!P:h}' \
		> jobs-P.out &&
	cat >jobs-P.expected <<-EOF &&
	,-
	100%,100%
	,-
	EOF
	test_cmp jobs-P.expected jobs-P.out
"
test_expect_success 'flux-jobs instance fields empty for completed job' '
	grep $id jobs.out > completed.out &&
	grep "$id  *$" completed.out
'
test_expect_success 'flux-jobs instance headers work' '
	cat >headers.expected <<-EOF &&
	       JOBID           STATS           CORE%  GPU%
	EOF
	head -n1 jobs.out >headers &&
	test_cmp headers.expected headers
'
test_expect_success 'flux-jobs {instance.stats.total} works' '
	test $(flux jobs -no {instance.stats.total} $id2) = 5
'
test_expect_success 'flux-jobs {instance.progress} works' '
	test $(flux jobs -no {instance.progress:.1f} $id2) = 0.2
'
test_done
