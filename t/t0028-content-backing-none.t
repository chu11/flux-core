#!/bin/sh

test_description='Test broker content checkpoint w/o backing module'

. `dirname $0`/sharness.sh

# Size the session to one more than the number of cores, minimum of 4
SIZE=$(test_size_large)
test_under_flux ${SIZE} minimal
echo "# $0: flux session size will be ${SIZE}"

RPC=${FLUX_BUILD_DIR}/t/request/rpc

checkpoint_put() {
    o="{key:\"$1\",value:{version:1,rootref:\"$2\",timestamp:2.2}}"
    jq -j -c -n  ${o} | $RPC content.checkpoint-put
}
checkpoint_get() {
    jq -j -c -n  "{key:\"$1\"}" | $RPC content.checkpoint-get
}

test_expect_success HAVE_JQ 'checkpoint-get fails, no checkpoints yet' '
    checkpoint_put foo bar
'

test_expect_success HAVE_JQ 'checkpoint-put foo w/ rootref bar' '
    checkpoint_put foo bar
'

test_expect_success HAVE_JQ 'checkpoint-get foo returned rootref bar' '
    echo bar >rootref.exp &&
    checkpoint_get foo | jq -r .value | jq -r .rootref >rootref.out &&
    test_cmp rootref.exp rootref.out
'

test_expect_success 'flux-dump --checkpoint with missing checkpoint fails' '
        test_must_fail flux dump --checkpoint foo.tar
'

test_expect_success 'load kvs and create some kvs data' '
        flux module load kvs &&
        flux kvs put a=1 &&
        flux kvs put b=foo
'

test_expect_success 'unload kvs' '
        flux module remove kvs
'

test_expect_success 'dump default=kvs-primary checkpoint works' '
        flux dump --checkpoint foo.tar
'

test_expect_success 'restore content' '
        flux restore --checkpoint foo.tar
'

test_expect_success 'reload kvs' '
        flux module load kvs
'

test_expect_success 'verify KVS content restored' '
        test $(flux kvs get a) = "1"
        test $(flux kvs get b) = "foo"
'

test_expect_success 'unload kvs' '
        flux module remove kvs
'

test_done
