#!/bin/sh
#

test_description='Test flux-kvs commit sync.'

. `dirname $0`/kvs/kvs-helper.sh

. `dirname $0`/sharness.sh

RPC=${FLUX_BUILD_DIR}/t/request/rpc

SIZE=1
test_under_flux ${SIZE} minimal

TESTNAMESPACE="testnamespace"

checkpoint_get() {
        jq -j -c -n  "{key:\"$1\"}" | $RPC content.checkpoint-get
}

test_expect_success 'load content module' '
	flux module load content
'

test_expect_success 'load content-sqlite and kvs and add some data' '
        flux module load content-sqlite &&
        flux module load kvs &&
        flux kvs put a=1 &&
        flux kvs put b=2
'

test_expect_success 'kvs: no checkpoint of kvs-primary should exist yet' '
        test_must_fail checkpoint_get kvs-primary
'

test_expect_success 'kvs: put some data to kvs and sync it (flux kvs put)' '
        flux kvs put --blobref --sync c=3 > syncblob.out
'

test_expect_success 'kvs: checkpoint of kvs-primary should exist now' '
        checkpoint_get kvs-primary
'

test_expect_success 'kvs: checkpoint of kvs-primary should match rootref' '
        checkpoint_get kvs-primary | jq -r .value[0].rootref > checkpoint.out &&
        test_cmp syncblob.out checkpoint.out
'

test_expect_success 'kvs: put some data to kvs and sync it (flux kvs sync)' '
        flux kvs put --blobref d=4 > syncblob2.out &&
        flux kvs sync
'

test_expect_success 'kvs: checkpoint of kvs-primary should match rootref' '
        checkpoint_get kvs-primary | jq -r .value[0].rootref > checkpoint2.out &&
        test_cmp syncblob2.out checkpoint2.out
'

test_expect_success 'kvs: sync fails against non-primary namespace' '
        flux kvs namespace create ${TESTNAMESPACE} &&
        flux kvs put --namespace=${TESTNAMESPACE} a=10 &&
        test_must_fail flux kvs put --namespace=${TESTNAMESPACE} --sync b=11
'

test_expect_success 'kvs: unload content-sqlite' '
	flux module remove content-sqlite
'

test_expect_success 'kvs: sync without backing store fails' '
        test_must_fail flux kvs put --sync d=4
'

test_expect_success 'kvs: no pending requests at end of tests before module removal' '
	pendingcount=$(flux module stats -p pending_requests kvs) &&
	test $pendingcount -eq 0
'

test_expect_success 'kvs: unload kvs' '
	flux module remove kvs
'

test_expect_success 'remove content module' '
	flux module remove content
'

test_done
