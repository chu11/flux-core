#!/bin/sh

test_description='flux-mini batch specific tests'

. $(dirname $0)/sharness.sh


# Start an instance with 16 cores across 4 ranks
export TEST_UNDER_FLUX_CORES_PER_RANK=4
if test -z "${TEST_UNDER_FLUX_ACTIVE}"; then
    STATEDIR=$(mktemp -d)
fi
test_under_flux 4 job -o,-Sstatedir=${STATEDIR}

flux setattr log-stderr-level 1

NCORES=$(flux kvs get resource.R | flux R decode --count=core)
test ${NCORES} -gt 4 && test_set_prereq MULTICORE

test_expect_success 'create generic test batch script' '
	cat <<-EOF >batch-script.sh
	#!/bin/sh
	ncores=\$(flux resource list -s all -no {ncores})
	nnodes=\$(flux resource list -s all -no {nnodes})
	printf "size=%d nodes=%d\n" \$(flux getattr size) \$nnodes
	flux mini run -n \$ncores hostname
	EOF
'
test_expect_success HAVE_JQ 'flux-mini batch copies script into jobspec' '
	flux mini batch -n1 --dry-run batch-script.sh | \
		jq -j .attributes.system.batch.script > script.sh &&
	test_cmp batch-script.sh script.sh
'
test_expect_success HAVE_JQ 'flux-mini batch takes a script on stdin' '
	flux mini batch -n1 --dry-run < batch-script.sh | \
		jq -j .attributes.system.batch.script > script-stdin.sh &&
	test_cmp batch-script.sh script.sh
'
test_expect_success HAVE_JQ 'flux mini batch --wrap option works' '
	flux mini batch -n1 --dry-run --wrap foo bar baz | \
		jq -j .attributes.system.batch.script >script-wrap.out &&
	cat <<-EOF >script-wrap.expected &&
	#!/bin/sh
	foo bar baz
	EOF
	test_cmp script-wrap.expected script-wrap.out
'
test_expect_success HAVE_JQ 'flux mini batch --wrap option works on stdin' '
	printf "foo\nbar\nbaz\n" | \
	    flux mini batch -n1 --dry-run --wrap | \
		jq -j .attributes.system.batch.script >stdin-wrap.out &&
	cat <<-EOF >stdin-wrap.expected &&
	#!/bin/sh
	foo
	bar
	baz
	EOF
	test_cmp stdin-wrap.expected stdin-wrap.out
'
test_expect_success 'flux-mini batch fails for binary file' '
	test_expect_code 1 flux mini batch -n1 $(which hostname)
'
test_expect_success 'flux-mini batch fails for file without she-bang' '
	cat <<-EOF >invalid-script.sh &&
	flux mini run hostname
	EOF
	test_expect_code 1 flux mini batch -n1 invalid-script.sh
'
test_expect_success 'flux-mini batch fails if -N > -n' '
	test_expect_code 1 flux mini batch -N4 -n1 --wrap hostname
'
test_expect_success HAVE_JQ 'flux-mini batch -N2 requests 2 nodes exclusively' '
	flux mini batch -N2 --wrap --dry-run hostname | \
		jq -S ".resources[0]" | \
		jq -e ".type == \"node\" and .exclusive"
'
test_expect_success HAVE_JQ 'flux-mini batch --exclusive works' '
	flux mini batch -N1 -n1 --exclusive --wrap --dry-run hostname | \
		jq -S ".resources[0]" | \
		jq -e ".type == \"node\" and .exclusive"
'
test_expect_success NO_ASAN 'flux-mini batch: submit a series of jobs' '
	id1=$(flux mini batch --flags=waitable -n1 batch-script.sh) &&
	id2=$(flux mini batch --flags=waitable -n4 batch-script.sh) &&
	id3=$(flux mini batch --flags=waitable -N2 -n4 batch-script.sh) &&
	flux resource list &&
	flux jobs &&
	id4=$(flux mini batch --flags=waitable -N2 -n2 --exclusive batch-script.sh) &&
	id5=$(flux mini batch --flags=waitable -N2 batch-script.sh) &&
	run_timeout 180 flux job wait --verbose --all
'
test_expect_success NO_ASAN 'flux-mini batch: job results are expected' '
	test_debug "grep . flux-*.out" &&
	grep "size=1 nodes=1" flux-${id1}.out &&
	grep "size=1 nodes=1" flux-${id2}.out &&
	grep "size=2 nodes=2" flux-${id3}.out &&
	grep "size=2 nodes=2" flux-${id4}.out &&
	grep "size=2 nodes=2" flux-${id5}.out
'
test_expect_success MULTICORE 'flux-mini batch: exclusive flag worked' '
	test $(flux job info ${id4} R | flux R decode --count=core) -gt 2 &&
	test $(flux job info ${id5} R | flux R decode --count=core) -gt 2
'

test_expect_success 'flux-mini batch: --output=kvs directs output to kvs' '
	id=$(flux mini batch -n1 --flags=waitable --output=kvs batch-script.sh) &&
	run_timeout 60 flux job attach $id > kvs-output.log 2>&1 &&
	test_debug "cat kvs-output.log" &&
	grep "size=1 nodes=1" kvs-output.log
'
test_expect_success 'flux-mini batch: --broker-opts works' '
	id=$(flux mini batch -n1 --flags=waitable \
	     --broker-opts=-v batch-script.sh) &&
	id2=$(flux mini batch -n1 --flags=waitable \
	     --broker-opts=-v,-v batch-script.sh) &&
	run_timeout 60 flux job wait $id &&
	test_debug "cat flux-${id}.out" &&
	grep "boot: rank=0 size=1" flux-${id}.out &&
	run_timeout 60 flux job wait $id2 &&
	grep "boot: rank=0 size=1" flux-${id2}.out &&
	grep "entering event loop" flux-${id2}.out
'
test_done
