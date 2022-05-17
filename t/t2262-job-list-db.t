#!/bin/sh

test_description='Test flux job list db'

. $(dirname $0)/job-list/job-list-helper.sh

. $(dirname $0)/sharness.sh

export FLUX_CONF_DIR=$(pwd)
if test -z "${TEST_UNDER_FLUX_ACTIVE}"; then
    STATEDIR=$(mktemp -d)
fi
test_under_flux 4 job -o,-Sstatedir=${STATEDIR}

RPC=${FLUX_BUILD_DIR}/t/request/rpc

QUERYCMD="flux python ${FLUX_SOURCE_DIR}/t/scripts/sqlite-query.py"

fj_wait_event() {
  flux job wait-event --timeout=20 "$@"
}

get_statedir_dbpath() {
    statedir=`flux getattr statedir` &&
    echo ${statedir}/job-db.sqlite
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
            && grep -q "t_inactive = " query.out \
            && grep -q "jobdata = " query.out \
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
get_job_list_values() {
        local id=$(flux job id $1)
        jobdata=`flux job list-ids ${id}`
        list_userid=`echo ${jobdata} | jq ".userid"`
        list_urgency=`echo ${jobdata} | jq ".urgency"`
        list_priority=`echo ${jobdata} | jq ".priority"`
        list_state=`echo ${jobdata} | jq ".state"`
        list_states_mask=`echo ${jobdata} | jq ".states_mask"`
        list_ranks=`echo ${jobdata} | jq ".ranks"`
        list_nnodes=`echo ${jobdata} | jq ".nnodes"`
        list_nodelist=`echo ${jobdata} | jq -r ".nodelist"`
        list_ntasks=`echo ${jobdata} | jq ".ntasks"`
        list_name=`echo ${jobdata} | jq -r ".name"`
        list_waitstatus=`echo ${jobdata} | jq ".waitstatus"`
        list_success=`echo ${jobdata} | jq ".success"`
        list_result=`echo ${jobdata} | jq ".result"`
        list_expiration=`echo ${jobdata} | jq ".expiration"`
        list_annotations=`echo ${jobdata} | jq ".annotations"`
        list_dependencies=`echo ${jobdata} | jq ".dependencies"`
        list_exception_occurred=`echo ${jobdata} | jq ".exception_occurred"`
        list_exception_type=`echo ${jobdata} | jq -r ".exception_type"`
        list_exception_severity=`echo ${jobdata} | jq ".exception_severity"`
        list_exception_note=`echo ${jobdata} | jq -r ".exception_note"`
        list_t_submit=`echo ${jobdata} | jq ".t_submit"`
        list_t_run=`echo ${jobdata} | jq ".t_run"`
        list_t_cleanup=`echo ${jobdata} | jq ".t_cleanup"`
        list_t_inactive=`echo ${jobdata} | jq ".t_inactive"`
}

# get job values from database
# arg1 - jobid
# arg2 - database path
get_db_values() {
        local id=$(flux job id $1)
        local dbpath=$2
        query="select * from jobs where id=$id;"
        ${QUERYCMD} -t 10000 ${dbpath} "${query}" > query.out
        db_jobdata=`grep "jobdata = " query.out | cut -f3- -d' '`
        db_eventlog=`grep "eventlog = " query.out | awk '{print \$3}'`
        db_jobspec=`grep "jobspec = " query.out | awk '{print \$3}'`
        db_R=`grep "R = " query.out | awk '{print \$3}'`
        db_userid=`echo ${jobdata} | jq ".userid"`
        db_urgency=`echo ${jobdata} | jq ".urgency"`
        db_priority=`echo ${jobdata} | jq ".priority"`
        db_state=`echo ${jobdata} | jq ".state"`
        db_states_mask=`echo ${jobdata} | jq ".states_mask"`
        db_ranks=`echo ${jobdata} | jq ".ranks"`
        db_nnodes=`echo ${jobdata} | jq ".nnodes"`
        db_nodelist=`echo ${jobdata} | jq -r ".nodelist"`
        db_ntasks=`echo ${jobdata} | jq ".ntasks"`
        db_name=`echo ${jobdata} | jq -r ".name"`
        db_waitstatus=`echo ${jobdata} | jq ".waitstatus"`
        db_success=`echo ${jobdata} | jq ".success"`
        db_result=`echo ${jobdata} | jq ".result"`
        db_expiration=`echo ${jobdata} | jq ".expiration"`
        db_annotations=`echo ${jobdata} | jq ".annotations"`
        db_dependencies=`echo ${jobdata} | jq ".dependencies"`
        db_exception_occurred=`echo ${jobdata} | jq ".exception_occurred"`
        db_exception_type=`echo ${jobdata} | jq -r ".exception_type"`
        db_exception_severity=`echo ${jobdata} | jq ".exception_severity"`
        db_exception_note=`echo ${jobdata} | jq -r ".exception_note"`
        db_t_submit=`echo ${jobdata} | jq ".t_submit"`
        db_t_run=`echo ${jobdata} | jq ".t_run"`
        db_t_cleanup=`echo ${jobdata} | jq ".t_cleanup"`
        db_t_inactive=`echo ${jobdata} | jq ".t_inactive"`
}

db_compare_data() {
        local id=$(flux job id $1)
        local dbpath=$2
        get_job_list_values ${id}
        get_db_values ${id} ${dbpath}
        if [ "${list_userid}" != "${db_userid}" ] \
            || [ "${list_urgency}" != "${db_urgency}" ] \
            || [ "${list_priority}" != "${db_priority}" ] \
            || [ "${list_state}" != "${db_state}" ] \
            || [ "${list_states_mask}" != "${db_states_mask}" ] \
            || [ "${list_ranks}" != "${db_ranks}" ] \
            || [ "${list_nnodes}" != "${db_nnodes}" ] \
            || [ "${list_nodelist}" != "${db_nodelist}" ] \
            || [ "${list_ntasks}" != "${db_ntasks}" ] \
            || [ "${list_name}" != "${db_name}" ] \
            || [ "${list_waitstatus}" != "${db_waitstatus}" ] \
            || [ "${list_success}" != "${db_success}" ] \
            || [ "${list_result}" != "${db_result}" ] \
            || [ "${list_expiration}" != "${db_expiration}" ] \
            || [ "${list_annotations}" != "${db_annotations}" ] \
            || [ "${list_dependencies}" != "${db_dependencies}" ] \
            || [ "${list_exception_occurred}" != "${db_exception_occurred}" ] \
            || [ "${list_exception_type}" != "${db_exception_type}" ] \
            || [ "${list_exception_severity}" != "${db_exception_severity}" ] \
            || [ "${list_exception_note}" != "${db_exception_note}" ] \
            || [ "${list_t_submit}" != "${db_t_submit}" ] \
            || [ "${list_t_run}" != "${db_t_run}" ] \
            || [ "${list_t_cleanup}" != "${db_t_cleanup}" ] \
            || [ "${list_t_inactive}" != "${db_t_inactive}" ]
        then
            return 1
        fi
        return 0
}

# wait for inactive job list to reach expected count
# arg1 - expected final count
# Usage: wait_inactive_count method target tries
# where method is job-manager, job-list, or job-list-stats (jq required)
wait_inactive_list_count() {
    local target=$1
    local tries=50
    local count
    while test $tries -gt 0; do
        count=$(flux module stats -p jobs.inactive job-list)
        test $count -eq $target && return 0
        sleep 0.25
        tries=$(($tries-1))
    done
    return 1
}

# submit jobs for job list & job-list db testing

test_expect_success 'submit jobs for job list testing' '
        #  Create `hostname` jobspec
        #  N.B. Used w/ `flux job submit` for serial job submission
        #  for efficiency (vs serial `flux mini submit`.
        #
        flux mini submit --dry-run hostname >hostname.json &&
        #
        # submit jobs that will complete
        #
        for i in $(seq 0 3); do
                flux job submit hostname.json >> inactiveids
                fj_wait_event `tail -n 1 inactiveids` clean
        done &&
        #
        #  Currently all inactive ids are "completed"
        #
        tac inactiveids | flux job id > completed.ids &&
        #
        #  Run a job that will fail, copy its JOBID to both inactive and
        #   failed lists.
        #
        ! jobid=`flux mini submit --wait nosuchcommand` &&
        echo $jobid >> inactiveids &&
        flux job id $jobid > failed.ids &&
        touch pending.ids &&
        touch running.ids &&
        tac inactiveids | flux job id > inactive.ids &&
        cat inactive.ids > all.ids &&
        #
        #  Synchronize all expected states
        #
        job_list_wait_states
'

test_expect_success HAVE_JQ 'flux job list inactive jobs (pre purge)' '
        flux job list -s inactive | jq .id > listI.out &&
        test_cmp listI.out inactive.ids
'

test_expect_success HAVE_JQ 'flux job list-inactive jobs (pre purge)' '
        flux job list-inactive | jq .id > list_inactive.out &&
        test_cmp list_inactive.out inactive.ids
'

test_expect_success 'flux job list-ids jobs (pre purge)' '
        for id in `cat inactive.ids`
        do
            flux job list-ids ${id}
        done
'

test_expect_success 'flux job list has all inactive jobs cached' '
        cached=`flux module stats -p jobs.inactive job-list` &&
        test ${cached} -eq $(job_list_state_count inactive)
'

test_expect_success 'job-list db: db stored in statedir' '
        dbpath=$(get_statedir_dbpath) &&
        ls ${dbpath}
'

test_expect_success 'job-list db: has correct number of entries' '
        dbpath=$(get_statedir_dbpath) &&
        entries=$(db_count_entries ${dbpath}) &&
        test ${entries} -eq $(job_list_state_count inactive)
'

test_expect_success 'job-list db: make sure job data looks ok' '
        dbpath=$(get_statedir_dbpath) &&
        for id in `cat list_inactive.out`
        do
            db_check_entries ${id} ${dbpath}
        done
'

test_expect_success HAVE_JQ 'job-list db: make sure job data correct' '
        dbpath=$(get_statedir_dbpath) &&
        for id in `cat list_inactive.out`
        do
            db_compare_data ${id} ${dbpath}
        done
'

test_expect_success 'job-list db: purge 2 jobs' '
        len=$(job_list_state_count inactive) &&
        len=$((len - 2)) &&
        flux job purge --force --num-limit=${len} &&
        mv inactive.ids inactive.ids.orig &&
        head -n ${len} inactive.ids.orig > inactive.ids &&
        wait_inactive_list_count ${len}
'

test_expect_success HAVE_JQ 'flux job list inactive jobs (post purge)' '
        flux job list -s inactive | jq .id > listI2.out &&
        test_cmp listI2.out inactive.ids.orig
'

test_expect_success HAVE_JQ 'flux job list-inactive jobs (post purge)' '
        flux job list-inactive | jq .id > list_inactive2.out &&
        test_cmp list_inactive2.out inactive.ids.orig
'

test_expect_success 'flux job list-ids jobs (post purge)' '
        for id in `cat inactive.ids.orig`
        do
            flux job list-ids ${id}
        done
'

test_expect_success 'job-list db: purge all jobs' '
        len=$(job_list_state_count inactive) &&
        flux job purge --force --num-limit=0 &&
        : > inactive.ids &&
        wait_inactive_list_count 0
'

test_expect_success HAVE_JQ 'flux job list inactive jobs (all purge)' '
        flux job list -s inactive | jq .id > listI3.out &&
        test_cmp listI3.out inactive.ids.orig
'

test_expect_success HAVE_JQ 'flux job list-inactive jobs (all purge)' '
        flux job list-inactive | jq .id > list_inactive3.out &&
        test_cmp list_inactive3.out inactive.ids.orig
'

test_expect_success 'flux job list-ids jobs (all purge)' '
        for id in `cat inactive.ids.orig`
        do
            flux job list-ids ${id}
        done
'

test_expect_success 'reload the job-list module with alternate config' '
        statedir=`flux getattr statedir` &&
        cat >job-list.toml <<EOF &&
[job-list]
dbpath = "${statedir}/testdb.sqlite"
busytimeout = "0.1s"
EOF
        flux config reload &&
        flux module reload job-list
'

test_expect_success 'configured db is there' '
        ls ${statedir}/testdb.sqlite
'

test_expect_success 'flux job list inactive jobs (new db)' '
        count=$(flux job list -s inactive | wc -l) &&
        test ${count} -eq 0
'

test_expect_success 'flux job list-inactive jobs (new db)' '
        count=$(flux job list-inactive | wc -l) &&
        test ${count} -eq 0
'

test_done
