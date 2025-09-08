#!/bin/sh
#

test_description='Test that Flux can launch Flux'

. `dirname $0`/sharness.sh

# Size the session to one more than the number of cores, minimum of 4
SIZE=$(test_size_large)
test_under_flux ${SIZE}
echo "# $0: flux session size will be ${SIZE}"

ARGS="-Sbroker.rc1_path= -Sbroker.rc3_path="
test_expect_success "flux can run flux instance as a job" '
	run_timeout 60 flux run -n1 -N1 \
		flux start ${ARGS} flux getattr size >size.out &&
	echo 1 >size.exp &&
	test_cmp size.exp size.out
'

test_expect_success 'flux subinstance sets uri job memo' '
	jobid=$(flux batch -n1 --wrap sleep 300) &&
	flux job wait-event -t 60 ${jobid} memo &&
	flux jobs -no {user.uri} ${jobid} > uri.memo &&
	grep ^ssh:// uri.memo &&
	flux cancel $jobid &&
	flux job wait-event $jobid clean
'

test_expect_success "flux --parent works in subinstance" '
	id=$(flux submit \
		flux start ${ARGS} flux --parent kvs put test=ok) &&
	flux job attach $id &&
	flux job info $id guest.test > guest.test &&
	cat <<-EOF >guest.test.exp &&
	ok
	EOF
	test_cmp guest.test.exp guest.test
'

test_expect_success "flux --parent --parent works in subinstance" '
	id=$(flux batch -n1 --wrap \
		flux run flux start ${ARGS} \
		flux --parent --parent kvs put test=ok) &&
	flux job attach $id &&
	flux job info $id guest.test > guest2.test &&
	cat <<-EOF >guest2.test.exp &&
	ok
	EOF
	test_cmp guest2.test.exp guest2.test
'

test_expect_success "flux --root works in subinstance" '
	id=$(flux batch -n1 --wrap --error=/dev/null \
		flux run flux start ${ARGS} \
		flux --root getattr instance-level) &&
	flux job wait-event -vt 600 $id clean &&
	test_debug "cat flux-${id}.out" &&
	test "$(cat flux-${id}.out)" -eq 0
'

test_expect_success 'flux --root returns current instance at depth 0' '
	test $(flux --root getattr instance-level) -eq 0
'

test_expect_success "instance-level attribute = 0 in new standalone instance" '
	flux start ${ARGS} flux getattr instance-level >level_new.out &&
	echo 0 >level_new.exp &&
	test_cmp level_new.exp level_new.out
'

test_expect_success "instance-level attribute = 0 in test instance" '
	flux getattr instance-level >level0.out &&
	echo 0 >level0.exp &&
	test_cmp level0.exp level0.out
'

test_expect_success "instance-level attribute = 1 in first subinstance" '
	flux run flux start ${ARGS} \
		flux getattr instance-level >level1.out &&
	echo 1 >level1.exp &&
	test_cmp level1.exp level1.out
'

test_expect_success "instance-level attribute = 2 in second subinstance" '
	flux run flux start \
		flux run flux start ${ARGS} \
		flux getattr instance-level >level2.out &&
	echo 2 >level2.exp &&
	test_cmp level2.exp level2.out
'

test_expect_success "flux sets jobid attribute" '
	id=$(flux submit \
		flux start ${ARGS} flux getattr jobid) &&
	echo "$id" >jobid.exp &&
	flux job attach $id >jobid.out &&
	test_cmp jobid.exp jobid.out
'
test_expect_success 'flux can launch multiple brokers per node' '
	flux alloc -x -N2 -o per-resource.count=2 \
		flux resource info
'
test_expect_success 'flux can launch multiple brokers per node (R lookup fallback)' '
	flux alloc -N2 --exclusive -o per-resource.count=2 \
		--conf=resource.no-update-watch=true \
		flux resource info
'
test_expect_success 'broker flux jobid-path=/' '
	val=$(flux getattr jobid-path) &&
	test $val = /
'
test_expect_success 'child broker jobid-path=/id' '
	val=$(flux run flux start ${ARGS} flux getattr jobid-path) &&
	test $val = /$(flux job last)
'
test_expect_success 'job FLUX_JOB_ID_PATH=/id' '
	val=$(flux run printenv FLUX_JOB_ID_PATH) &&
	test $val = /$(flux job last)
'
test_expect_success 'flux_open("/") works at top-level' '
	flux python -c "import flux; print(flux.Flux(\"/\").attr_get(\"instance-level\"));"
'
test_expect_success 'flux_open(3) accepts path-like URIs: "/", "../.." etc' '
	cat <<-EOF >flux_open.py &&
	import unittest
	import flux
	import sys
	from collections import namedtuple

	Test = namedtuple("Test", ["arg", "level"])
	tests = [
	    Test(None, 3),
	    Test(".", 3),
	    Test("./", 3),
	    Test("..", 2),
	    Test("../", 2),
	    Test("./..", 2),
	    Test("../..", 1),
	    Test("..//..", 1),
	    Test("../../", 1),
	    Test(".././..", 1),
	    Test("../../..", 0),
	    Test("../../../..", 0),
	    Test("../../../../../../../", 0),
	    Test("/", 0),
	]

	class TestFluxOpen(unittest.TestCase):
	    def test_pathlike_uris(self):
	        for test in tests:
	            l = flux.Flux(test.arg).attr_get("instance-level")
	            self.assertEqual(int(l), test.level)

	    def test_pathlike_bad_arg(self):
	        for arg in ("/.", "//", "../f", "../f/.."):
	            with self.assertRaises(OSError, msg=f"arg={arg}"):
	                h = flux.Flux(arg)
	                print(h.attr_get("instance-level"))
	unittest.main(testRunner=unittest.TextTestRunner(verbosity=2))
	EOF
	flux alloc -n1 flux alloc -n1 flux alloc -n1 flux python ./flux_open.py
'
test_done
