#!/bin/sh
test_description='Test flux queue command'

. $(dirname $0)/sharness.sh

if test -z "${TEST_UNDER_FLUX_ACTIVE}"; then
    STATEDIR=$(mktemp -d)
fi
test_under_flux 1 full -o,-Sstatedir=${STATEDIR}

flux setattr log-stderr-level 1

LIST_JOBS=${FLUX_BUILD_DIR}/t/job-manager/list-jobs

test_expect_success 'flux-queue: unknown sub-command fails with usage message' '
	test_must_fail flux queue wrongsubcmd 2>usage.out &&
	grep Usage: usage.out
'

test_expect_success 'flux-queue: missing sub-command fails with usage message' '
	test_must_fail flux queue 2>usage2.out &&
	grep Usage: usage2.out
'

test_expect_success 'flux-queue: disable with no reason fails' '
	test_must_fail flux queue submit disable 2>usage3.out &&
	grep Usage: usage3.out
'

test_expect_success 'flux-queue: enable with extra free args fails' '
	test_must_fail flux queue enable xyz 2>usage4.out &&
	grep Usage: usage4.out
'

test_expect_success 'flux-queue: status with extra free args fails' '
	test_must_fail flux queue status xyz 2>usage5.out &&
	grep Usage: usage5.out
'

test_expect_success 'flux-queue: drain with extra free args fails' '
	test_must_fail flux queue drain xyz 2>usage6.out &&
	grep Usage: usage6.out
'

test_expect_success 'flux-queue: idle with extra free args fails' '
	test_must_fail flux queue idle xyz 2>usage7.out &&
	grep Usage: usage7.out
'

test_expect_success 'flux-queue: status with bad broker connection fails' '
	! FLUX_URI=/wrong flux queue status
'

test_expect_success 'flux-queue: disable with bad broker connection fails' '
	! FLUX_URI=/wrong flux queue disable foo
'

test_expect_success 'flux-queue: enable with bad broker connection fails' '
	! FLUX_URI=/wrong flux queue enable
'

test_expect_success 'flux-queue: drain with bad broker connection fails' '
	! FLUX_URI=/wrong flux queue drain
'

test_expect_success 'flux-queue: idle with bad broker connection fails' '
	! FLUX_URI=/wrong flux queue idle
'

test_expect_success 'flux-queue: disable works' '
        flux queue disable system is fubar
'

test_expect_success 'flux-queue: job submit fails with queue disabled' '
        test_must_fail flux mini run /bin/true
'

test_expect_success 'flux-queue: enable works' '
        flux queue enable
'

test_expect_success 'flux-queue: flux mini run works after enable' '
        run_timeout 60 flux mini run /bin/true
'

test_expect_success 'flux-queue: stop with bad broker connection fails' '
	! FLUX_URI=/wrong flux queue stop
'

test_expect_success 'flux-queue: start with bad broker connection fails' '
	! FLUX_URI=/wrong flux queue start
'

test_expect_success 'flux-queue: start with extra free args fails' '
	test_must_fail flux queue start xyz 2>start_xargs.out &&
	grep Usage: start_xargs.out
'

test_expect_success 'flux-queue: stop works' '
	flux queue stop my unique message
'

test_expect_success 'flux-queue: status reports reason for stop' '
	flux queue status 2>status.out &&
	cat <<-EOT >status.exp &&
	flux-queue: Job submission is enabled
	flux-queue: Scheduling is disabled: my unique message
	EOT
	test_cmp status.exp status.out
'

test_expect_success 'flux-queue: submit 3 jobs' '
	for i in $(seq 1 3); do flux mini submit /bin/true; done
'

test_expect_success 'flux-queue: start scheduling' '
	flux queue start
'

test_expect_success 'flux-queue: queue empties out' '
	run_timeout 60 flux queue drain
'

test_expect_success 'flux-queue: start long job that uses all cores' '
	ncores=$(flux resource list -s up -no {ncores}) &&
	id=$(flux mini submit -n ${ncores} sleep 600) &&
	flux job wait-event ${id} start &&
	echo ${id} >longjob
'

test_expect_success 'flux-queue: submit a job and make sure alloc sent' '
	id=$(flux mini submit --flags debug /bin/true) &&
	flux job wait-event ${id} debug.alloc-request
'

test_expect_success 'flux-queue: stop canceled alloc request' '
	flux queue stop -v 2>stop.err &&
	grep "flux-queue: 1 alloc requests pending to scheduler" stop.err
'

test_expect_success 'flux-queue: start scheduling and cancel long job' '
	flux queue start &&
	flux job cancel $(cat longjob)
'

test_expect_success 'flux-queue: queue empties out' '
	flux queue drain
'

test_expect_success 'flux-queue: unload scheduler' '
	flux module remove sched-simple
'

test_expect_success 'flux-queue: submit a job to ping scheduler' '
	flux mini submit --flags debug /bin/true
'

wait_for_sched_offline() {
	local n=$1
	for try in $(seq 1 $n); do
		echo Check queue status for offline, try ${try} of $n 2>&1
		flux queue status 2>&1 | grep disabled && return
	done
}

test_expect_success 'flux-queue: queue says scheduling disabled' '
	wait_for_sched_offline 10 &&
	flux queue status 2>sched_stat.err &&
	cat <<-EOT >sched_stat.exp &&
	flux-queue: Job submission is enabled
	flux-queue: Scheduling is disabled: Scheduler is offline
	EOT
	test_cmp sched_stat.exp sched_stat.err
'

test_expect_success 'flux-queue: queue contains 1 active job' '
	COUNT=$(${LIST_JOBS} | wc -l) &&
	test ${COUNT} -eq 1
'

test_expect_success 'flux-queue: load scheduler' '
	flux module load sched-simple mode=limited=1
'

test_expect_success 'flux-queue: queue says scheduling is enabled' '
	flux queue status 2>sched_stat2.err &&
	cat <<-EOT >sched_stat2.exp &&
	flux-queue: Job submission is enabled
	flux-queue: Scheduling is enabled
	EOT
	test_cmp sched_stat2.exp sched_stat2.err
'

test_expect_success 'flux-queue: job in queue ran' '
	run_timeout 30 flux queue drain
'

test_expect_success 'flux-queue: submit a long job that uses all cores' '
	ncores=$(flux resource list -s up -no {ncores}) &&
	flux mini submit -n ${ncores} sleep 600
'

test_expect_success 'flux-queue: submit 2 more jobs' '
	flux mini submit /bin/true &&
	flux mini submit /bin/true
'

test_expect_success 'flux-queue: there are 3 active jobs' '
	COUNT=$(${LIST_JOBS} | wc -l) &&
	test ${COUNT} -eq 3
'

test_expect_success 'flux-queue: queue status -v shows expected counts' '
	flux queue status -v 2>stat.err &&
	cat <<-EOT >stat.exp &&
	flux-queue: Job submission is enabled
	flux-queue: Scheduling is enabled
	flux-queue: 1 alloc requests queued
	flux-queue: 1 alloc requests pending to scheduler
	flux-queue: 0 free requests pending to scheduler
	flux-queue: 1 running jobs
	EOT
	test_cmp stat.exp stat.err
'

test_expect_success 'flux-queue: stop queue and cancel long job' '
	flux queue stop &&
	flux job cancelall -f -S RUN
'

test_expect_success 'flux-queue: queue becomes idle' '
	run_timeout 30 flux queue idle
'

test_expect_success 'flux-queue: queue status -v shows expected counts' '
	flux queue status -v 2>stat2.err &&
	cat <<-EOT >stat2.exp &&
	flux-queue: Job submission is enabled
	flux-queue: Scheduling is disabled
	flux-queue: 2 alloc requests queued
	flux-queue: 0 alloc requests pending to scheduler
	flux-queue: 0 free requests pending to scheduler
	flux-queue: 0 running jobs
	EOT
	test_cmp stat2.exp stat2.err
'

test_expect_success 'flux-queue: start queue and drain' '
	flux queue start &&
	run_timeout 30 flux queue drain
'


runas_guest() {
        local userid=$(($(id -u)+1))
        FLUX_HANDLE_USERID=$userid FLUX_HANDLE_ROLEMASK=0x2 "$@"
}

test_expect_success 'flux-queue: status allowed for guest' '
	runas_guest flux queue status
'

test_expect_success 'flux-queue: stop denied for guest' '
	test_must_fail runas_guest flux queue stop 2>guest_stop.err &&
	cat <<-EOT >guest_alloc.exp &&
	flux-queue: alloc-admin: Request requires owner credentials
	EOT
	test_cmp guest_alloc.exp guest_stop.err
'

test_expect_success 'flux-queue: start denied for guest' '
	test_must_fail runas_guest flux queue start 2>guest_start.err &&
	test_cmp guest_alloc.exp guest_start.err
'

test_expect_success 'flux-queue: disable denied for guest' '
	test_must_fail runas_guest flux queue disable foo 2>guest_dis.err &&
	cat <<-EOT >guest_submit.exp &&
	flux-queue: submit-admin: Request requires owner credentials
	EOT
	test_cmp guest_submit.exp guest_dis.err
'

test_expect_success 'flux-queue: enable denied for guest' '
	test_must_fail runas_guest flux queue enable 2>guest_ena.err &&
	test_cmp guest_submit.exp guest_ena.err
'

test_expect_success 'flux-queue: drain denied for guest' '
	test_must_fail runas_guest flux queue drain 2>guest_drain.err &&
	cat <<-EOT >guest_drain.exp &&
	flux-queue: drain: Request requires owner credentials
	EOT
	test_cmp guest_drain.exp guest_drain.err
'

test_expect_success 'flux-queue: idle denied for guest' '
	test_must_fail runas_guest flux queue idle 2>guest_idle.err &&
	cat <<-EOT >guest_idle.exp &&
	flux-queue: idle: Request requires owner credentials
	EOT
	test_cmp guest_idle.exp guest_idle.err
'


test_done
