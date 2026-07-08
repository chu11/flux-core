#!/bin/sh

test_description='Test job-manager private mode'

. $(dirname $0)/sharness.sh

test_under_flux 1 job

flux version | grep -q libflux-security && test_set_prereq FLUX_SECURITY

owner_uid=$(id -u)
other_uid=$((owner_uid + 100))

submit_as_guest()
{
	flux run --dry-run "$@" | \
	  flux python ${SHARNESS_TEST_SRCDIR}/scripts/sign-as.py $other_uid \
	    >job.signed
	FLUX_HANDLE_USERID=$other_uid flux job submit --flags=signed job.signed
}

set_userid() {
	export FLUX_HANDLE_USERID=$1
	export FLUX_HANDLE_ROLEMASK=0x2
}

# Simulate a guest that has been assigned the admin role (USER|ADMIN).
set_admin() {
	export FLUX_HANDLE_USERID=$1
	export FLUX_HANDLE_ROLEMASK=0xa
}

unset_userid() {
	unset FLUX_HANDLE_USERID
	unset FLUX_HANDLE_ROLEMASK
}

enable_private_mode() {
	flux config load <<-EOT
	[access]
	private-mode = true
	EOT
}

disable_private_mode() {
	flux config load </dev/null
}

job_manager_getinfo() {
	flux python -c "import flux; print(flux.Flux().rpc('job-manager.getinfo',{}).get_str())"
}

job_manager_getattr() {
	flux python -c "import flux; print(flux.Flux().rpc(\"job-manager.getattr\",{\"id\":"$1",\"attrs\":[\"$2\"]}).get_str())"
}

test_expect_success 'submit a job as instance owner' '
	jobid=$(flux submit true)
'
test_expect_success 'save jobid as integer' '
	flux job id --to=dec $jobid >jobid_int.out
'
test_expect_success FLUX_SECURITY 'submit a job as guest user' '
	guestid=$(submit_as_guest --urgency=hold hostname) &&
	flux job id --to=dec $guestid >guestid.out
'

test_expect_success 'private mode off: guest can call stats-get' '
	set_userid $other_uid &&
	test_when_finished unset_userid &&
	flux module stats job-manager
'
test_expect_success 'private mode off: guest can call getinfo' '
	set_userid $other_uid &&
	test_when_finished unset_userid &&
	job_manager_getinfo
'
test_expect_success FLUX_SECURITY 'private mode off: guest getattr on own job' '
	set_userid $other_uid &&
	test_when_finished unset_userid &&
	job_manager_getattr $(cat guestid.out) jobspec
'
test_expect_success 'private mode off: guest gets EINVAL for unknown job' '
	set_userid $other_uid &&
	test_when_finished unset_userid &&
	test_must_fail job_manager_getattr 1 jobspec 2>err.out &&
	grep -i "unknown job" err.out
'
test_expect_success 'private mode off: guest gets EPERM for other user job' '
	set_userid $other_uid &&
	test_when_finished unset_userid &&
	test_must_fail job_manager_getattr $(cat jobid_int.out) jobspec 2>err.out &&
	grep -i "guests can only access their own jobs" err.out
'

test_expect_success 'enable private mode' '
	enable_private_mode
'
test_expect_success 'private mode on: guest stats-get fails with EPERM' '
	set_userid $other_uid &&
	test_when_finished unset_userid &&
	test_must_fail flux module stats job-manager 2>private-stats.err &&
	test_debug "cat private-stats.err" &&
	grep "not permitted" private-stats.err
'
test_expect_success 'private mode on: owner stats-get succeeds' '
	flux module stats job-manager >/dev/null
'
test_expect_success 'private mode on: guest getinfo fails with EPERM' '
	set_userid $other_uid &&
	test_when_finished unset_userid &&
	test_must_fail job_manager_getinfo 2>private-getinfo.err &&
	test_debug "cat private-getinfo.err" &&
	grep "not permitted" private-getinfo.err
'
test_expect_success 'private mode on: owner getinfo succeeds' '
	job_manager_getinfo
'
test_expect_success 'private mode on: guest gets EPERM for unknown job (not EINVAL)' '
	set_userid $other_uid &&
	test_when_finished unset_userid &&
	test_must_fail job_manager_getattr 1 jobspec 2>err.out &&
	test_must_fail grep -i "unknown job" err.out
'
test_expect_success 'private mode on: guest gets EPERM for another users job' '
	set_userid $other_uid &&
	test_when_finished unset_userid &&
	test_must_fail job_manager_getattr $(cat jobid_int.out) jobspec \
		2>private-getattr.err &&
	test_debug "cat private-getattr.err" &&
	grep "not permitted" private-getattr.err
'
test_expect_success FLUX_SECURITY 'private mode on: guest getattr on own job' '
	set_userid $other_uid &&
	test_when_finished unset_userid &&
	job_manager_getattr $(cat guestid.out) jobspec
'
test_expect_success 'private mode on: owner getattr succeeds' '
	job_manager_getattr $(cat jobid_int.out) jobspec
'
test_expect_success 'private mode on: admin stats-get succeeds' '
	set_admin $other_uid &&
	test_when_finished unset_userid &&
	flux module stats job-manager >/dev/null
'
test_expect_success 'private mode on: admin getinfo succeeds' '
	set_admin $other_uid &&
	test_when_finished unset_userid &&
	job_manager_getinfo
'
test_expect_success 'private mode on: admin gets EINVAL for unknown job (not EPERM)' '
	set_admin $other_uid &&
	test_when_finished unset_userid &&
	test_must_fail job_manager_getattr 1 jobspec 2>admin-unknown.err &&
	grep -i "unknown job" admin-unknown.err
'
# The admin role only exempts a guest from private mode (listing and stats);
# it does not grant read access to another user's job data.
test_expect_success 'private mode on: admin cannot getattr another user job' '
	set_admin $other_uid &&
	test_when_finished unset_userid &&
	test_must_fail job_manager_getattr $(cat jobid_int.out) jobspec
'
# The admin role confers elevated *visibility*, not write access: an admin
# must not be able to mutate another user's job.  Use a running owner job so
# the credential check is reached (inactive jobs fail earlier with ENOENT).
test_expect_success 'submit a running owner job for admin mutate tests' '
	sleepid=$(flux submit sleep 300) &&
	flux job wait-event --timeout=20 $sleepid start
'
test_expect_success 'admin cannot cancel another user job' '
	set_admin $other_uid &&
	test_when_finished unset_userid &&
	test_must_fail flux cancel $sleepid 2>admin-cancel.err &&
	grep -i "not permitted\|only.*their own" admin-cancel.err
'
test_expect_success 'admin cannot change urgency of another user job' '
	set_admin $other_uid &&
	test_when_finished unset_userid &&
	test_must_fail flux job urgency $sleepid 31 2>admin-urg.err &&
	grep -i "not permitted\|only.*their own" admin-urg.err
'
test_expect_success 'admin cannot raise an exception on another user job' '
	set_admin $other_uid &&
	test_when_finished unset_userid &&
	test_must_fail flux job raise --severity=0 --type=test $sleepid \
		2>admin-raise.err &&
	grep -i "not permitted\|only.*their own" admin-raise.err
'
test_expect_success 'cancel running owner job from admin mutate tests' '
	flux cancel $sleepid &&
	flux job wait-event --timeout=20 $sleepid clean
'

test_expect_success 'disable private mode' '
	disable_private_mode
'
test_expect_success 'private mode off: guest can call stats-get again' '
	set_userid $other_uid &&
	test_when_finished unset_userid &&
	flux module stats job-manager
'
test_expect_success 'private mode off: guest can call getinfo again' '
	set_userid $other_uid &&
	test_when_finished unset_userid &&
	job_manager_getinfo
'
test_expect_success 'private mode off: guest gets EINVAL for unknown job again' '
	set_userid $other_uid &&
	test_when_finished unset_userid &&
	test_must_fail job_manager_getattr 1 jobspec 2>err.out &&
	grep -i "unknown job" err.out
'
test_expect_success 'private mode off: guest gets EPERM for other user job again' '
	set_userid $other_uid &&
	test_when_finished unset_userid &&
	test_must_fail job_manager_getattr $(cat jobid_int.out) jobspec 2>err.out &&
	grep -i "guests can only access their own jobs" err.out
'
test_expect_success FLUX_SECURITY \
'private mode off: guest getattr on own job still works' '
	set_userid $other_uid &&
	test_when_finished unset_userid &&
	job_manager_getattr $(cat guestid.out) jobspec
'


test_done
