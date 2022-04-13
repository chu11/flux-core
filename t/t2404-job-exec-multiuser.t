#!/bin/sh

test_description='Sanity checks for job-exec multiuser exec'

. $(dirname $0)/sharness.sh

flux version | grep -q libflux-security && test_set_prereq FLUX_SECURITY

if ! test_have_prereq FLUX_SECURITY; then
	skip_all='skipping multiuser exec tests, libflux-security or IMP not found'
	test_done
fi

IMP=${SHARNESS_TEST_SRCDIR}/job-exec/imp.sh
IMP_FAIL=${SHARNESS_TEST_SRCDIR}/job-exec/imp-fail.sh

#  Configure dummy IMP
if ! test -d conf.d; then
	mkdir conf.d
	cat <<-EOF >conf.d/exec.toml
	[exec]
	imp = "${IMP}"
	EOF
fi

export FLUX_CONF_DIR=$(pwd)/conf.d
if test -z "${TEST_UNDER_FLUX_ACTIVE}"; then
    STATEDIR=$(mktemp -d)
fi
test_under_flux 2 job -o,-Sstatedir=${STATEDIR}

test_expect_success 'job-exec: module configured to use IMP' '
	flux dmesg | grep "using imp path ${IMP}"
'
test_expect_success 'job-exec: job as instance owner works' '
	test "$(id -u)" = "$(flux mini run id -u)"
'

SIGN_AS=${SHARNESS_TEST_SRCDIR}/scripts/sign-as.py
#  Use flux_sign_wrap_as(3) to create a fake sign-wrapped jobspec
#  which will pass ingest signature check when submitted with
#  FLUX_HANDLE_USERID.
#
#  This will trigger the job-exec module to run job under our fake IMP.
#
test_expect_success HAVE_JQ 'job-exec: job as guest tries to run IMP' '
	FAKE_USERID=42 &&
	flux mini run --dry-run -n1 id -u | \
		flux python ${SIGN_AS} ${FAKE_USERID} > job.signed &&
	id=$(FLUX_HANDLE_USERID=${FAKE_USERID} \
		flux job submit --flags=signed job.signed) &&
	flux job attach ${id} &&
	flux job list-ids ${id} > ${id}.json &&
	jq -e ".userid == 42" < ${id}.json &&
	flux job attach ${id} 2>&1 | grep "test-imp: Running.*$(flux job id ${id})"
'
test_expect_success HAVE_JQ 'job-exec: large jobspec does not get truncated' '
	(FAKE_USERID=42 &&
		for i in `seq 0 2048`; \
			do export ENV${i}=xxxxxyyyyyyyyyzzzzzzz; \
		done &&
		flux mini run --dry-run -n1 id -u | \
			flux python ${SIGN_AS} ${FAKE_USERID} > job.signed &&
		id=$(FLUX_HANDLE_USERID=${FAKE_USERID} \
		flux job submit --flags=signed job.signed) &&
		flux job attach ${id} &&
		actual=imp-$(flux job id $id).input &&
		test_debug "echo expecting J of size $(wc -c < job.signed)B" &&
		test_debug "echo input to IMP was $(wc -c < $actual)B" &&
		jq -j .J ${actual} > J.input &&
		test_cmp job.signed J.input
	)
'
test_expect_success HAVE_JQ,NO_ASAN 'job-exec: kill multiuser job uses the IMP' '
	FAKE_USERID=42 &&
	flux mini run --dry-run -n2 -N2 sleep 1000 | \
		flux python ${SIGN_AS} ${FAKE_USERID} > sleep-job.signed &&
	id=$(FLUX_HANDLE_USERID=${FAKE_USERID} \
		flux job submit --flags=signed sleep-job.signed) &&
	flux job list-ids ${id} > ${id}.json &&
	jq -e ".userid == 42" < ${id}.json &&
	flux job wait-event -p guest.exec.eventlog -vt 30 ${id} shell.start &&
	flux job cancel ${id} &&
	test_expect_code 143 run_timeout 30 flux job status -v ${id} &&
	flux dmesg | grep "test-imp: Kill .*signal 15"
'

#  Configure failing IMP
test_expect_success 'job-exec: reconfig with failing dummy IMP' '
	cat <<-EOF >conf.d/exec.toml
	[exec]
	imp = "${IMP_FAIL}"
	EOF
'

test_expect_success 'job-exec: reconfig and reload module' '
	flux config reload &&
	flux module reload -f job-exec
'
test_expect_success 'job-exec: IMP failure on one rank terminates job' '
	FAKE_USERID=42 &&
	id=$(FLUX_HANDLE_USERID=${FAKE_USERID} \
		flux job submit --flags=signed sleep-job.signed) &&
	flux job wait-event -vt 20 ${id}  clean &&
	test_must_fail_or_be_terminated flux job attach -vEX ${id}
'


test_done
