#!/bin/sh

test_description='Tests for job-archive2'

. $(dirname $0)/sharness.sh

export FLUX_CONF_DIR=$(pwd)
test_under_flux 4 job

ARCHIVE2DIR=`pwd`
ARCHIVE2DB="${ARCHIVE2DIR}/jobarchive2.db"

QUERYCMD="flux python ${FLUX_SOURCE_DIR}/t/scripts/sqlite-query.py"

fj_wait_event() {
  flux job wait-event --timeout=20 "$@"
}

# wait for job to be in specific state in job-list module
# arg1 - jobid
# arg2 - job state
wait_jobid_state() {
        local jobid=$(flux job id $1)
        local state=$2
        local i=0
        while ! flux job list --states=${state} | grep $jobid > /dev/null \
               && [ $i -lt 50 ]
        do
                sleep 0.1
                i=$((i + 1))
        done
        if [ "$i" -eq "50" ]
        then
            return 1
        fi
        return 0
}

# wait for job to be stored in job archive2
# arg1 - jobid
# arg2 - database path
wait_db() {
        local jobid=$(flux job id $1)
        local dbpath=$2
        local i=0
        query="select id from jobs;"
        while ! ${QUERYCMD} -t 100 ${dbpath} "${query}" | grep $jobid > /dev/null \
               && [ $i -lt 50 ]
        do
                sleep 0.1
                i=$((i + 1))
        done
        if [ "$i" -eq "50" ]
        then
            return 1
        fi
        return 0
}

# count number of entries in database
# arg1 - database path
db_count_entries() {
        local dbpath=$1
        query="select id from jobs;"
        count=`${QUERYCMD} -t 10000 ${dbpath} "${query}" | grep "^id =" | wc -l`
        echo $count
}

# verify entries stored into job archive2
# arg1 - jobid
# arg2 - database path
db_check_entries() {
        local id=$(flux job id $1)
        local dbpath=$2
        query="select * from jobs where id=$id;"
        ${QUERYCMD} -t 10000 ${dbpath} "${query}" > query.out
        if grep -q "^id = " query.out \
            && grep -q "userid = " query.out \
            && grep -q "urgency = " query.out \
            && grep -q "priority = " query.out \
            && grep -q "state = " query.out \
            && grep -q "states_mask = " query.out \
            && grep -q "ranks = " query.out \
            && grep -q "nnodes = " query.out \
            && grep -q "nodelist = " query.out \
            && grep -q "ntasks = " query.out \
            && grep -q "name = " query.out \
            && grep -q "waitstatus = " query.out \
            && grep -q "success = " query.out \
            && grep -q "result = " query.out \
            && grep -q "expiration = " query.out \
            && grep -q "annotations = " query.out \
            && grep -q "dependencies = " query.out \
            && grep -q "exception_occurred = " query.out \
            && grep -q "exception_type = " query.out \
            && grep -q "exception_severity = " query.out \
            && grep -q "exception_note = " query.out \
            && grep -q "t_submit = " query.out \
            && grep -q "t_run = " query.out \
            && grep -q "t_cleanup = " query.out \
            && grep -q "t_inactive = " query.out \
            && grep -q "eventlog = " query.out \
            && grep -q "jobspec = " query.out \
            && grep -q "R = " query.out
        then
            return 0
        fi
        return 1
}

# get job values from database
# arg1 - jobid
# arg2 - database path
get_db_values() {
        local id=$(flux job id $1)
        local dbpath=$2
        query="select * from jobs where id=$id;"
        ${QUERYCMD} -t 10000 ${dbpath} "${query}" > query.out
        userid=`grep "userid = " query.out | awk '{print \$3}'`
        urgency=`grep "urgency = " query.out | awk '{print \$3}'`
        priority=`grep "priority = " query.out | awk '{print \$3}'`
        state=`grep "state = " query.out | awk '{print \$3}'`
        states_mask=`grep "states_mask = " query.out | awk '{print \$3}'`
        ranks=`grep "ranks = " query.out | awk '{print \$3}'`
        nnodes=`grep "nnodes = " query.out | awk '{print \$3}'`
        nodelist=`grep "nodelist = " query.out | awk '{print \$3}'`
        ntasks=`grep "ntasks = " query.out | awk '{print \$3}'`
        name=`grep "name = " query.out | awk '{print \$3}'`
        waitstatus=`grep "waitstatus = " query.out | awk '{print \$3}'`
        success=`grep "success = " query.out | awk '{print \$3}'`
        result=`grep "result = " query.out | awk '{print \$3}'`
        expiration=`grep "expiration = " query.out | awk '{print \$3}'`
        annotations=`grep "annotations = " query.out | awk '{print \$3}'`
        dependencies=`grep "dependencies = " query.out | awk '{print \$3}'`
        exception_occurred=`grep "exception_occurred = " query.out | awk '{print \$3}'`
        exception_type=`grep "exception_type = " query.out | awk '{print \$3}'`
        exception_severity=`grep "exception_severity = " query.out | awk '{print \$3}'`
        exception_note=`grep "exception_note = " query.out | cut -f3- -d' '`
        t_submit=`grep "t_submit = " query.out | awk '{print \$3}'`
        t_run=`grep "t_run = " query.out | awk '{print \$3}'`
        t_cleanup=`grep "t_cleanup = " query.out | awk '{print \$3}'`
        t_inactive=`grep "t_inactive = " query.out | awk '{print \$3}'`
        eventlog=`grep "eventlog = " query.out | awk '{print \$3}'`
        jobspec=`grep "jobspec = " query.out | awk '{print \$3}'`
        R=`grep "R = " query.out | awk '{print \$3}'`
}

# check database values (job ran)
# arg1 - jobid
# arg2 - database path
db_check_values_run() {
        local id=$(flux job id $1)
        local dbpath=$2
        get_db_values $id $dbpath
        if [ -z "$userid" ] \
            || [ -z "$urgency" ] \
            || [ -z "$priority" ] \
            || [ -z "$state" ] \
            || [ -z "$states_mask" ] \
            || [ -z "$ranks" ] \
            || [ "$nnodes" == "0" ] \
            || [ -z "$nodelist" ] \
            || [ -z "$ntasks" ] \
            || [ -z "$name" ] \
            || [ "$waitstatus" == "-1" ] \
            || [ -z "$success" ] \
            || [ -z "$result" ] \
            || [ -z "$expiration" ] \
            || [ "$t_submit" == "0.0" ] \
            || [ "$t_run" == "0.0" ] \
            || [ "$t_cleanup" == "0.0" ] \
            || [ "$t_inactive" == "0.0" ] \
            || [ -z "$eventlog" ] \
            || [ -z "$jobspec" ] \
            || [ -z "$R" ]
        then
            return 1
        fi
        return 0
}

# check database values (job ran good)
# arg1 - jobid
# arg2 - database path
db_check_values_run_success() {
        db_check_values_run $1 $2
        if [ $? -ne 0 ]; then
            return 1
        fi
        if [ "$exception_occurred" != "0" ] \
            || [ -n "$exception_type" ] \
            || [ "$exception_severity" != "-1" ] \
            || [ -n "$exception_note" ]
        then
            return 1
        fi
        return 0
}

# check database values (job ran fail)
# arg1 - jobid
# arg2 - database path
db_check_values_run_fail() {
        db_check_values_run $1 $2
        if [ $? -ne 0 ]; then
            return 1
        fi
        if [ "$exception_occurred" != "1" ] \
            || [ -z "$exception_type" ] \
            || [ "$exception_severity" == "-1" ] \
            || [ -z "$exception_note" ]
        then
            return 1
        fi
        return 0
}

# check database values (job did not run)
# arg1 - jobid
# arg2 - database path
db_check_values_no_run() {
        local id=$(flux job id $1)
        local dbpath=$2
        get_db_values $id $dbpath
        if [ -z "$userid" ] \
            || [ -z "$urgency" ] \
            || [ -z "$priority" ] \
            || [ -z "$state" ] \
            || [ -z "$states_mask" ] \
            || [ -n "$ranks" ] \
            || [ "$nnodes" != "0" ] \
            || [ -n "$nodelist" ] \
            || [ -z "$ntasks" ] \
            || [ -z "$name" ] \
            || [ "$waitstatus" != "-1" ] \
            || [ "$success" != "0" ] \
            || [ -z "$result" ] \
            || [ -z "$expiration" ] \
            || [ "$t_submit" == "0.0" ] \
            || [ "$t_run" != "0.0" ] \
            || [ "$t_cleanup" == "0.0" ] \
            || [ "$t_inactive" == "0.0" ] \
            || [ -z "$eventlog" ] \
            || [ -z "$jobspec" ] \
            || [ -n "$R" ]
        then
            return 1
        fi
        return 0
}

# check database values (job did not run - canceled)
# arg1 - jobid
# arg2 - database path
db_check_values_no_run_canceled() {
        db_check_values_no_run $1 $2
        if [ $? -ne 0 ]; then
            return 1
        fi
        if [ "$exception_occurred" != "1" ] \
            || [ "$exception_type" != "cancel" ] \
            || [ "$exception_severity" != "0" ] \
            || [ -n "$exception_note" ]
        then
            return 1
        fi
        return 0
}

# check database values (job did not run - w/ exception note)
# arg1 - jobid
# arg2 - database path
db_check_values_no_run_exception() {
        db_check_values_no_run $1 $2
        if [ $? -ne 0 ]; then
            return 1
        fi
        if [ "$exception_occurred" != "1" ] \
            || [ -z "$exception_type" ] \
            || [ "$exception_severity" != "0" ] \
            || [ -z "$exception_note" ]
        then
            return 1
        fi
        return 0
}

test_expect_success 'job-archive2: load module without specifying period, should fail' '
        test_must_fail flux module load job-archive2
'

test_expect_success 'job-archive2: setup config file' '
        cat >archive2.toml <<EOF &&
[archive2]
period = "0.5s"
dbpath = "${ARCHIVE2DB}"
busytimeout = "0.1s"
EOF
	flux config reload
'

test_expect_success 'job-archive2: load module' '
        flux module load job-archive2
'

test_expect_success 'job-archive2: stores inactive job info (job good)' '
        jobid=`flux mini submit hostname` &&
        fj_wait_event $jobid clean &&
        wait_jobid_state $jobid inactive &&
        wait_db $jobid ${ARCHIVE2DB} &&
        db_check_entries $jobid ${ARCHIVE2DB} &&
        db_check_values_run_success $jobid ${ARCHIVE2DB}
'

test_expect_success 'job-archive2: stores inactive job info (job fail)' '
        jobid=`flux mini submit nosuchcommand` &&
        fj_wait_event $jobid clean &&
        wait_jobid_state $jobid inactive &&
        wait_db $jobid ${ARCHIVE2DB} &&
        db_check_entries $jobid ${ARCHIVE2DB} &&
        db_check_values_run_fail $jobid ${ARCHIVE2DB}
'

# to ensure job canceled before we run, we submit a job to eat up all
# resources first.
test_expect_success 'job-archive2: stores inactive job info (job cancel)' '
        jobid1=`flux mini submit -N4 -n8 sleep 500` &&
        fj_wait_event $jobid1 start &&
        jobid2=`flux mini submit hostname` &&
        fj_wait_event $jobid2 submit &&
        flux job cancel $jobid2 &&
        fj_wait_event $jobid2 clean &&
        flux job cancel $jobid1 &&
        fj_wait_event $jobid1 clean &&
        wait_jobid_state $jobid2 inactive &&
        wait_db $jobid2 ${ARCHIVE2DB} &&
        db_check_entries $jobid2 ${ARCHIVE2DB} &&
        db_check_values_no_run_canceled $jobid2 ${ARCHIVE2DB}
'

test_expect_success 'job-archive2: stores inactive job info (resources)' '
        jobid=`flux mini submit -N1000 -n1000 hostname` &&
        fj_wait_event $jobid clean &&
        wait_jobid_state $jobid inactive &&
        wait_db $jobid ${ARCHIVE2DB} &&
        db_check_entries $jobid ${ARCHIVE2DB} &&
        db_check_values_no_run_exception $jobid ${ARCHIVE2DB}
'

test_expect_success 'job-archive2: all jobs stored' '
        count=`db_count_entries ${ARCHIVE2DB}` &&
        test $count -eq 5
'

test_expect_success 'job-archive2: reload module' '
        flux module reload job-archive2
'

test_expect_success 'job-archive2: doesnt restore old data' '
        count=`db_count_entries ${ARCHIVE2DB}` &&
        test $count -eq 5
'

test_expect_success 'job-archive2: stores more inactive job info' '
        jobid1=`flux mini submit hostname` &&
        jobid2=`flux mini submit hostname` &&
        fj_wait_event $jobid1 clean &&
        fj_wait_event $jobid2 clean &&
        wait_jobid_state $jobid1 inactive &&
        wait_jobid_state $jobid2 inactive &&
        wait_db $jobid1 ${ARCHIVE2DB} &&
        wait_db $jobid2 ${ARCHIVE2DB} &&
        db_check_entries $jobid1 ${ARCHIVE2DB} &&
        db_check_entries $jobid2 ${ARCHIVE2DB} &&
        db_check_values_run $jobid1 ${ARCHIVE2DB} &&
        db_check_values_run $jobid2 ${ARCHIVE2DB}
'

test_expect_success 'job-archive2: all jobs stored' '
        count=`db_count_entries ${ARCHIVE2DB}` &&
        test $count -eq 7
'

# we don't check values in module stats b/c it can be racy w/ polling
test_expect_success 'job-archive2: get module stats' '
        flux module stats job-archive2
'

test_expect_success 'job-archive2: unload module' '
        flux module unload job-archive2
'

test_expect_success 'job-archive2: db exists after module unloaded' '
        count=`db_count_entries ${ARCHIVE2DB}` &&
        test $count -eq 7
'

test_expect_success 'job-archive2: setup config file without dbpath' '
        cat >archive2.toml <<EOF &&
[archive2]
period = "0.5s"
busytimeout = "0.1s"
EOF
	flux config reload
'

test_expect_success 'job-archive2: load module failure, statedir not set' '
        test_must_fail flux module load job-archive2
'

test_expect_success 'job-archive2: setup config file without period' '
        cat >archive2.toml <<EOF &&
[archive2]
dbpath = "${ARCHIVE2DB}"
busytimeout = "0.1s"
EOF
	flux config reload
'

test_expect_success 'job-archive2: load module failure, period not set' '
        test_must_fail flux module load job-archive2
'

test_expect_success 'job-archive2: setup config file with illegal period' '
        cat >archive2.toml <<EOF &&
[archive2]
period = "-10.5x"
dbpath = "${ARCHIVE2DB}"
busytimeout = "0.1s"
EOF
	flux config reload
'

test_expect_success 'job-archive2: load module failure, period illegal' '
        test_must_fail flux module load job-archive2
'

# rc1 doesn't load `job-archive2`, nix these tests

# test_expect_success 'job-archive2: setup config file with only period' '
#         cat >archive2.toml <<EOF &&
# [archive2]
# period = "0.5s"
# EOF
# 	flux config reload
# '

# test_expect_success 'job-archive2: launch flux with statedir set' '
#         flux start -o,--setattr=statedir=$(pwd) /bin/true
# '

# test_expect_success 'job-archive2: job-archive2 setup in statedir' '
#         ls $(pwd)/job-archive2.sqlite
# '

test_done
