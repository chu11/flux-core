#!/bin/sh

test_description='Test resource drain/undrain'

. `dirname $0`/sharness.sh

SIZE=4
test_under_flux $SIZE full --test-hosts=fake[0-3]

has_resource_event () {
	flux kvs eventlog get resource.eventlog | awk '{ print $2 }' | grep $1
}

# fake hostnames match the ones set on the broker command line
test_expect_success 'load fake resources' '
	flux module remove sched-simple &&
	flux R encode -r 0-3 -c 0-1 -H fake[0-3] >R &&
	flux resource reload R &&
	flux module load sched-simple
'

test_expect_success 'unload resource module' '
	flux module remove resource
'

test_expect_success 'resource module checkpointed empty drain state' '
	flux kvs get check.resource.drain_state > drain_state1.out &&
	jq -e ".drain_state == {}" < drain_state1.out
'

test_expect_success 'load resource module without truncation' '
	flux module load resource noverify
'

test_expect_success 'drain some nodes' '
	test $(flux resource status -s drain -no {nnodes}) -eq 0 &&
	flux resource drain 1 test_reason_1 &&
	flux resource drain 2-3 test_reason_2_3 &&
	test $(flux resource status -s drain -no {nnodes}) -eq 3 &&
	flux resource drain -no "sort:nodelist {nodelist} {reason}" > drain_status1.out &&
	cat >drain_status1.exp<<-EOT &&
	fake1 test_reason_1
	fake[2-3] test_reason_2_3
	EOT
	test_cmp drain_status1.exp drain_status1.out
'

test_expect_success 'resource eventlog has 2 drain events' '
	test $(has_resource_event drain | wc -l) -eq 2
'

test_expect_success 'unload resource module' '
	flux module remove resource
'

test_expect_success 'resource module checkpointed current drain state' '
	flux kvs get check.resource.drain_state > drain_state2.out &&
	jq -e ".drain_state.\"1\".nodelist == \"fake1\"" < drain_state2.out &&
	jq -e ".drain_state.\"2-3\".nodelist == \"fake[2-3]\"" < drain_state2.out
'

test_expect_success 'load resource module without truncation' '
	flux module load resource noverify
'

test_expect_success 'drain status is identical to before the reload' '
	test $(flux resource status -s drain -no {nnodes}) -eq 3 &&
	flux resource drain -no "sort:nodelist {nodelist} {reason}" > drain_status2.out &&
	cat >drain_status2.exp<<-EOT &&
	fake1 test_reason_1
	fake[2-3] test_reason_2_3
	EOT
	test_cmp drain_status2.exp drain_status2.out
'

test_expect_success 'resource eventlog still has 2 drain events' '
	test $(has_resource_event drain | wc -l) -eq 2
'

test_expect_success 'unload resource module' '
	flux module remove resource
'

test_expect_success 'resource module checkpointed current drain state' '
	flux kvs get check.resource.drain_state > drain_state3.out &&
	jq -e ".drain_state.\"1\".nodelist == \"fake1\"" < drain_state3.out &&
	jq -e ".drain_state.\"2-3\".nodelist == \"fake[2-3]\"" < drain_state3.out
'

test_expect_success 'load resource module with truncation' '
	flux module load resource noverify eventlog-truncate
'

test_expect_success 'drain status is identical to before the reload' '
	test $(flux resource status -s drain -no {nnodes}) -eq 3 &&
	flux resource drain -no "sort:nodelist {nodelist} {reason}" > drain_status3.out &&
	cat >drain_status3.exp<<-EOT &&
	fake1 test_reason_1
	fake[2-3] test_reason_2_3
	EOT
	test_cmp drain_status3.exp drain_status3.out
'

test_expect_success 'resource eventlog truncated, drain events are now gone' '
	test $(has_resource_event drain | wc -l) -eq 0
'

test_expect_success 'reset test state by clearing kvs resource info' '
	flux module remove resource &&
	flux kvs unlink resource.eventlog &&
	flux kvs unlink check.resource.drain_state &&
	flux module load resource noverify eventlog-truncate
'

test_expect_success 'drain a node' '
	test $(flux resource status -s drain -no {nnodes}) -eq 0 &&
	flux resource drain 1 test_reason_1 &&
	test $(flux resource status -s drain -no {nnodes}) -eq 1 &&
	flux resource drain -no "sort:nodelist {nodelist} {reason}" > drain_status4.out &&
	cat >drain_status4.exp<<-EOT &&
	fake1 test_reason_1
	EOT
	test_cmp drain_status4.exp drain_status4.out
'

test_expect_success 'unload resource module' '
	flux module remove resource
'

test_expect_success 'resource module checkpointed current drain state' '
	flux kvs get check.resource.drain_state > drain_state4.out &&
	jq -e ".drain_state.\"1\".nodelist == \"fake1\"" < drain_state4.out
'

# to ensure timestamp is in future, pick timestamp way out
test_expect_success 'resource will replay events after checkpoint' '
	flux kvs put --raw resource.eventlog=- <<-EOT &&
	{"timestamp":4000000000.000000,"name":"drain","context":{"idset":"2","nodelist":"fake2","reason":"test_reason_2","overwrite":0}}
	EOT
	flux module load resource noverify eventlog-truncate
'

test_expect_success 'drain status now includes additional drain from resource.eventlog' '
	test $(flux resource status -s drain -no {nnodes}) -eq 2 &&
	flux resource drain -no "sort:nodelist {nodelist} {reason}" > drain_status5.out &&
	cat >drain_status5.exp<<-EOT &&
	fake1 test_reason_1
	fake2 test_reason_2
	EOT
	test_cmp drain_status5.exp drain_status5.out
'

test_expect_success 'unload resource module' '
	flux module remove resource
'

test_expect_success 'resource module checkpointed current drain state' '
	flux kvs get check.resource.drain_state > drain_state5.out &&
	jq -e ".drain_state.\"1\".nodelist == \"fake1\"" < drain_state5.out &&
	jq -e ".drain_state.\"2\".nodelist == \"fake2\"" < drain_state5.out
'

# to ensure timestamp is in future, pick timestamp way out
test_expect_success 'resource will replay events after checkpoint with bad rank' '
	flux kvs put --raw resource.eventlog=- <<-EOT &&
	{"timestamp":4000000001.000000,"name":"drain","context":{"idset":"0","nodelist":"fake3","reason":"test_reason_3","overwrite":0}}
	EOT
	flux module load resource noverify eventlog-truncate
'

test_expect_success 'drain status now includes additional drain from resource.eventlog' '
	test $(flux resource status -s drain -no {nnodes}) -eq 3 &&
	flux resource drain -no "sort:nodelist {nodelist} {reason}" > drain_status6.out &&
	cat >drain_status6.exp<<-EOT &&
	fake1 test_reason_1
	fake2 test_reason_2
	fake3 test_reason_3
	EOT
	test_cmp drain_status6.exp drain_status6.out
'

test_expect_success 'reset test state by clearing kvs resource info' '
	flux module remove resource &&
	flux kvs unlink resource.eventlog &&
	flux kvs unlink check.resource.drain_state &&
	flux module load resource noverify
'

test_expect_success 'configure truncation by config file' '
	flux config load <<-EOF &&
	[resource]
	eventlog-truncate = true
	EOF
	flux module reload resource noverify
'

test_expect_success 'drain a node' '
	test $(flux resource status -s drain -no {nnodes}) -eq 0 &&
	flux resource drain 1 test_reason_1 &&
	test $(flux resource status -s drain -no {nnodes}) -eq 1 &&
	flux resource drain -no "sort:nodelist {nodelist} {reason}" > drain_status7.out &&
	cat >drain_status7.exp<<-EOT &&
	fake1 test_reason_1
	EOT
	test_cmp drain_status7.exp drain_status7.out
'

test_expect_success 'resource eventlog contains one drain event' '
	test $(has_resource_event drain | wc -l) -eq 1
'

test_expect_success 'reload resource module' '
	flux module reload resource noverify
'

test_expect_success 'drain status has not changed after resource reloaded' '
	test $(flux resource status -s drain -no {nnodes}) -eq 1 &&
	flux resource drain -no "sort:nodelist {nodelist} {reason}" > drain_status8.out &&
	cat >drain_status8.exp<<-EOT &&
	fake1 test_reason_1
	EOT
	test_cmp drain_status8.exp drain_status8.out
'

test_expect_success 'resource eventlog truncated by config, drain events are now gone' '
	test $(has_resource_event drain | wc -l) -eq 0
'

test_expect_success 'configure truncation and truncation history by config file' '
	flux config load <<-EOF &&
	[resource]
	eventlog-truncate = true
	eventlog-truncate-preserve-time = "100d"
	EOF
	flux module reload resource noverify
'

test_expect_success 'reset test and add long ago historical and recent drain data' '
	flux module remove resource &&
	flux kvs unlink resource.eventlog &&
	flux kvs unlink check.resource.drain_state &&
	now=$(date +%s) &&
	flux kvs eventlog append --timestamp=1000.0 resource.eventlog drain \
	     "{\"idset\":\"1\",\"nodelist\":\"fake1\",\"reason\":\"test_reason_1\",\"overwrite\":0}" &&
	flux kvs eventlog append --timestamp=${now} resource.eventlog drain \
	     "{\"idset\":\"2\",\"nodelist\":\"fake2\",\"reason\":\"test_reason_2\",\"overwrite\":0}" &&
	flux module load resource noverify
'

# N.B. note, we cleared the checkpoint of drain state above.  So the
# first time we reload the resource module, there was no checkpoint,
# therefore no drain events will be truncated

test_expect_success 'drain status has two drained nodes' '
	test $(flux resource status -s drain -no {nnodes}) -eq 2 &&
	flux resource drain -no "sort:nodelist {nodelist} {reason}" > drain_status9.out &&
	cat >drain_status9.exp<<-EOT &&
	fake1 test_reason_1
	fake2 test_reason_2
	EOT
	test_cmp drain_status9.exp drain_status9.out
'

test_expect_success 'resource eventlog contains two drain events' '
	test $(has_resource_event drain | wc -l) -eq 2
'

test_expect_success 'reload resource module' '
	flux module reload resource noverify
'

test_expect_success 'drain status still has two drained nodes' '
	test $(flux resource status -s drain -no {nnodes}) -eq 2 &&
	flux resource drain -no "sort:nodelist {nodelist} {reason}" > drain_status10.out &&
	cat >drain_status10.exp<<-EOT &&
	fake1 test_reason_1
	fake2 test_reason_2
	EOT
	test_cmp drain_status10.exp drain_status10.out
'

test_expect_success 'resource eventlog now contains only 1 drain event' '
	test $(has_resource_event drain | wc -l) -eq 1 &&
	flux kvs eventlog get resource.eventlog | grep drain | grep fake2
'

test_done
