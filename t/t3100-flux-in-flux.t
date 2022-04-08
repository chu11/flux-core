#!/bin/sh
#

test_description='Test that Flux can launch Flux'

. `dirname $0`/sharness.sh

# Size the session to one more than the number of cores, minimum of 4
SIZE=$(test_size_large)

if test -z "${TEST_UNDER_FLUX_ACTIVE}"; then
    STATEDIR=$(mktemp -d)
fi
test_under_flux ${SIZE} full -o,-Sstatedir=${STATEDIR}

echo "# $0: flux session size will be ${SIZE}"

ARGS="-o,-Sbroker.rc1_path=,-Sbroker.rc3_path="
test_expect_success "flux can run flux instance as a job" '
        STATEDIR=$(mktemp -d) &&
	run_timeout 10 flux mini run -n1 -N1 \
		flux start ${ARGS} -o,--setattr=statedir=${STATEDIR} flux getattr size >size.out &&
	echo 1 >size.exp &&
	test_cmp size.exp size.out
'

test_expect_success 'flux subinstance sets uri job memo' '
	jobid=$(flux mini batch -n1 --wrap sleep 300) &&
	flux job wait-event -t 60 ${jobid} memo &&
	flux jobs -no {user.uri} ${jobid} > uri.memo &&
	grep ^ssh:// uri.memo &&
	flux job cancel $jobid &&
	flux job wait-event $jobid clean
'

test_expect_success "flux --parent works in subinstance" '
        STATEDIR=$(mktemp -d) &&
	id=$(flux mini submit \
		flux start ${ARGS} -o,--setattr=statedir=${STATEDIR} flux --parent kvs put test=ok) &&
	flux job attach $id &&
	flux job info $id guest.test > guest.test &&
	cat <<-EOF >guest.test.exp &&
	ok
	EOF
	test_cmp guest.test.exp guest.test
'

test_expect_success "flux --parent --parent works in subinstance" '
        STATEDIR1=$(mktemp -d) &&
        STATEDIR2=$(mktemp -d) &&
	id=$(flux mini submit \
		flux start ${ARGS} -o,--setattr=statedir=${STATEDIR1} \
		flux start ${ARGS} -o,--setattr=statedir=${STATEDIR2} flux --parent --parent kvs put test=ok) &&
	flux job attach $id &&
	flux job info $id guest.test > guest2.test &&
	cat <<-EOF >guest2.test.exp &&
	ok
	EOF
	test_cmp guest2.test.exp guest2.test
'

test_expect_success "instance-level attribute = 0 in new standalone instance" '
        STATEDIR=$(mktemp -d) &&
	flux start ${ARGS} -o,--setattr=statedir=${STATEDIR} flux getattr instance-level >level_new.out &&
	echo 0 >level_new.exp &&
	test_cmp level_new.exp level_new.out
'

test_expect_success "instance-level attribute = 0 in test instance" '
	flux getattr instance-level >level0.out &&
	echo 0 >level0.exp &&
	test_cmp level0.exp level0.out
'

test_expect_success "instance-level attribute = 1 in first subinstance" '
        STATEDIR=$(mktemp -d) &&
	flux mini run flux start ${ARGS} -o,--setattr=statedir=${STATEDIR} \
		flux getattr instance-level >level1.out &&
	echo 1 >level1.exp &&
	test_cmp level1.exp level1.out
'

test_expect_success "instance-level attribute = 2 in second subinstance" '
        STATEDIR1=$(mktemp -d) &&
        STATEDIR2=$(mktemp -d) &&
	flux mini run flux start -o,--setattr=statedir=${STATEDIR1} \
		flux mini run flux start ${ARGS} -o,--setattr=statedir=${STATEDIR2} \
		flux getattr instance-level >level2.out &&
	echo 2 >level2.exp &&
	test_cmp level2.exp level2.out
'

test_expect_success "flux sets jobid attribute" '
        STATEDIR=$(mktemp -d) &&
	id=$(flux mini submit \
		flux start ${ARGS} -o,--setattr=statedir=${STATEDIR} flux getattr jobid) &&
	echo "$id" >jobid.exp &&
	flux job attach $id >jobid.out &&
	test_cmp jobid.exp jobid.out
'

test_done
