#!/bin/sh
#
test_description='Test flux-shell coprocess plugin'

. `dirname $0`/sharness.sh

test_under_flux 4

# Write a userrc that sets the "coprocess" shell option to the JSON object
# provided as the first argument.  Usage: write_rc FILE 'JSON'
write_rc() {
	cat >"$1" <<-EOF
	shell.options["coprocess"] = $2
	EOF
}

# Submit job and wait for output. Set `f58` variable to jobid.
submit_and_wait() {
	f58=$(flux submit "$@") &&
	flux job attach $f58
}

test_expect_success 'coprocess plugin is inert when option is unset' '
	flux run -o verbose=2 true 2>unset.err &&
	test_must_fail grep -i coprocess unset.err
'

test_expect_success 'coprocess launches helper and captures output' '
	write_rc mon.lua "{
	  mon = { command = {\"sh\", \"-c\", \"echo HELLO; sleep 30\"},
	          output = \"mon-{{id}}.log\" }
	}" &&
	submit_and_wait -o userrc=$(pwd)/mon.lua sleep 1 &&
	test -f mon-${f58}.log &&
	grep HELLO mon-${f58}.log
'

test_expect_success 'job with sleeping coprocess still exits promptly' '
	write_rc term.lua "{
	  mon = { command = {\"sleep\", \"300\"},
	          output = \"term-{{id}}.log\" }
	}" &&
	run_timeout 60 flux run -o userrc=$(pwd)/term.lua sleep 1
'

test_expect_success 'short-lived coprocess exits without a kill error' '
	write_rc short.lua "{
	  mon = { command = {\"hostname\"},
	          output = \"short-{{id}}.out\" }
	}" &&
	flux run -o userrc=$(pwd)/short.lua true 2>short.err &&
	test_must_fail grep -i "No such process" short.err &&
	test_must_fail grep -i "coprocess.*kill" short.err
'

test_expect_success 'coprocess that traps SIGTERM is killed by SIGKILL' '
	write_rc trap.lua "{
	  mon = { command = {\"sh\", \"-c\",
	             \"trap \\\"\\\" TERM; while true; do sleep 1; done\"},
	          output = \"trap-{{id}}.log\" }
	}" &&
	run_timeout 60 flux run -o userrc=$(pwd)/trap.lua \
		-o coprocess-kill-timeout=1s sleep 1 2>trap.err &&
	grep -i "sending SIGKILL" trap.err
'

test_expect_success 'invalid coprocess-kill-timeout is rejected' '
	write_rc kt.lua "{
	  mon = { command = {\"true\"}, output = \"kt-{{id}}.log\" }
	}" &&
	test_must_fail flux run -o userrc=$(pwd)/kt.lua \
		-o coprocess-kill-timeout=notfsd true 2>kt.err &&
	grep -i "coprocess-kill-timeout" kt.err
'

test_expect_success 'multi-node coprocess output is aggregated to one file' '
	write_rc agg.lua "{
	  mon = { command = {\"sh\", \"-c\",
	                     \"flux getattr rank; sleep 30\"},
	          output = \"agg-{{id}}.log\" }
	}" &&
	submit_and_wait -N2 -o userrc=$(pwd)/agg.lua sleep 1 &&
	test -f agg-${f58}.log &&
	test $(wc -l <agg-${f58}.log) -eq 2
'

# Output emitted only after SIGTERM must still reach the aggregated file from
# every shell, including followers whose final chunk races shutdown. The
# helper writes a marker line from its SIGTERM handler, then exits.
test_expect_success 'final coprocess output on SIGTERM is not lost' '
	write_rc term_out.lua "{
	  mon = { command = {\"sh\", \"-c\",
		    \"trap \\\"echo BYE{{node.id}}; exit 0\\\" TERM; while true; do sleep 0.1; done\"},
	          output = \"termout-{{id}}.log\" }
	}" &&
	submit_and_wait -N2 -o userrc=$(pwd)/term_out.lua sleep 1 &&
	test -f termout-${f58}.log &&
	grep BYE0 termout-${f58}.log &&
	grep BYE1 termout-${f58}.log
'

test_expect_success 'per-node output template yields one file per node' '
	write_rc pernode.lua "{
	  mon = { command = {\"sh\", \"-c\",
	                     \"echo rank{{node.id}}; sleep 30\"},
	          output = \"pernode-{{id}}-{{node.id}}.log\" }
	}" &&
	submit_and_wait -N2 -o userrc=$(pwd)/pernode.lua sleep 1 &&
	grep rank0 pernode-${f58}-0.log &&
	grep rank1 pernode-${f58}-1.log
'

# With a per-shell template each shell writes its own file directly, so the
# leader does not register the coprocess-write aggregation service. Verify a
# follower still produces its file (it wrote it itself, not via the leader).
test_expect_success 'per-shell template avoids leader output forwarding' '
	write_rc direct.lua "{
	  mon = { command = {\"sh\", \"-c\",
	                     \"echo host-{{node.name}}; sleep 30\"},
	          output = \"direct-{{node.name}}.log\" }
	}" &&
	submit_and_wait -N2 -o userrc=$(pwd)/direct.lua sleep 1 &&
	test -f direct-$(hostname).log &&
	grep host-$(hostname) direct-$(hostname).log
'

test_expect_success 'ranks option restricts launch to a subset of shells' '
	write_rc ranks.lua "{
	  mon = { command = {\"sh\", \"-c\", \"echo hi; sleep 30\"},
	          output = \"ranks-{{id}}-{{node.id}}.log\",
	          ranks = \"0\" }
	}" &&
	submit_and_wait -N2 -o userrc=$(pwd)/ranks.lua sleep 1 &&
	test -f ranks-${f58}-0.log &&
	test_must_fail test -f ranks-${f58}-1.log
'

test_expect_success 'ranks option may exclude the leader shell' '
	write_rc foll.lua "{
	  mon = { command = {\"sh\", \"-c\", \"echo FOLLONLY; sleep 30\"},
	          output = \"foll-{{id}}.log\",
	          ranks = \"1\" }
	}" &&
	submit_and_wait -N2 -o userrc=$(pwd)/foll.lua sleep 1 &&
	test -f foll-${f58}.log &&
	grep FOLLONLY foll-${f58}.log
'

test_expect_success 'output template supports {{node.name}}' '
	write_rc host.lua "{
	  mon = { command = {\"sh\", \"-c\", \"echo hi; sleep 30\"},
	          output = \"host-{{node.name}}.log\" }
	}" &&
	flux run -o userrc=$(pwd)/host.lua sleep 1 &&
	test -f host-$(hostname).log
'

test_expect_success 'command supports mustache templates' '
	write_rc cmdtmpl.lua "{
	  mon = { command = {\"sh\", \"-c\", \"echo id={{id}}; sleep 30\"},
		  output = \"cmdtmpl-{{id}}.log\" }
	}" &&
	submit_and_wait -o userrc=$(pwd)/cmdtmpl.lua sleep 1 &&
	grep "id=${f58}" cmdtmpl-${f58}.log
'

test_expect_success 'multiple co-processes run concurrently' '
	write_rc multi.lua "{
	  a = { command = {\"sh\", \"-c\", \"echo AAA; sleep 30\"},
	        output = \"multi-a-{{id}}.log\" },
	  b = { command = {\"sh\", \"-c\", \"echo BBB; sleep 30\"},
                output = \"multi-b-{{id}}.log\" }
	}" &&
	submit_and_wait -o userrc=$(pwd)/multi.lua sleep 1 &&
	grep AAA multi-a-${f58}.log &&
	grep BBB multi-b-${f58}.log
'

# Two co-processes, both aggregated to their own leader file across two nodes.
# Exercises independent per-co-process EOF accounting on the leader.
test_expect_success 'multiple aggregated co-processes on multiple nodes' '
	write_rc multiagg.lua "{
	  a = { command = {\"sh\", \"-c\", \"echo A{{node.id}}; sleep 30\"},
	        output = \"multiagg-a-{{id}}.log\" },
	  b = { command = {\"sh\", \"-c\", \"echo B{{node.id}}; sleep 30\"},
                output = \"multiagg-b-{{id}}.log\" }
	}" &&
	submit_and_wait -N2 -o userrc=$(pwd)/multiagg.lua sleep 1 &&
	grep A0 multiagg-a-${f58}.log &&
	grep A1 multiagg-a-${f58}.log &&
	grep B0 multiagg-b-${f58}.log &&
	grep B1 multiagg-b-${f58}.log
'

# A follower shell that dies by signal generates a lost-shell exception,
# handled by the coprocess shell.lost handler which drops the lost rank from
# the leader's EOF wait set. A task barrier ensures all shells are up before
# rank 3 kills its own shell, and exit-timeout=none ensures the leader waits
# on the aggregation reference rather than a timeout. Verify the leader
# observes the lost shell and the job then completes.
test_expect_success 'lost follower shell is handled with aggregated output' '
	write_rc lost.lua "{
	  mon = { command = {\"sh\", \"-c\", \"echo COPROC; sleep 300\"},
	          output = \"lost-{{id}}.log\" }
	}" &&
	cat >killshell.sh <<-EOF &&
	#!/bin/sh
	flux pmi barrier
	test \$(flux getattr rank) -eq 3 && kill -9 \$PPID
	sleep 300
	EOF
	chmod +x killshell.sh &&
	id=$(flux submit -N4 --tasks-per-node=1 -o exit-timeout=none \
		-o userrc=$(pwd)/lost.lua ./killshell.sh) &&
	flux job wait-event -Wt 60 -Hp output \
		-m message="shell rank 3 (on $(hostname)): Killed" $id log &&
	flux cancel $id &&
	flux job wait-event -t 60 $id clean
'

test_expect_success 'co-process with no output key uses a default file' '
	write_rc noout.lua "{
	  mon = { command = {\"sh\", \"-c\", \"echo DEFAULTOUT; sleep 30\"} }
	}" &&
	submit_and_wait -o userrc=$(pwd)/noout.lua sleep 1 &&
	test -f coproc-mon-${f58}.log &&
	grep DEFAULTOUT coproc-mon-${f58}.log
'

test_expect_success 'failure to launch a co-process is non-fatal' '
	write_rc badcmd.lua "{
	  mon = { command = {\"/nonexistent/command\"},
	          output = \"badcmd-{{id}}.log\" }
	}" &&
	flux run -o userrc=$(pwd)/badcmd.lua sleep 1 2>badcmd.err &&
	grep -i "launch failed" badcmd.err
'

# A follower that fails to launch its co-process must still release the leader
# from waiting on its output EOF, or the job would hang until kill-timeout.
test_expect_success 'follower launch failure does not hang aggregated job' '
	write_rc badmulti.lua "{
	  mon = { command = {\"/nonexistent/command\"},
	          output = \"badmulti-{{id}}.log\" }
	}" &&
	run_timeout 30 flux run -N2 -o userrc=$(pwd)/badmulti.lua sleep 1
'

test_expect_success 'co-process that exits early is non-fatal' '
	write_rc early.lua "{
	  mon = { command = {\"sh\", \"-c\", \"exit 3\"},
	          output = \"early-{{id}}.log\" }
	}" &&
	flux run -o userrc=$(pwd)/early.lua sleep 1
'

test_expect_success 'invalid coprocess command is rejected' '
	write_rc bad.lua "{ mon = { command = \"notanarray\" } }" &&
	test_must_fail flux run -o userrc=$(pwd)/bad.lua true 2>bad.err &&
	grep -i "command must be" bad.err
'

test_expect_success 'empty coprocess command array is rejected' '
	write_rc empty.lua "{ mon = { command = {} } }" &&
	test_must_fail flux run -o userrc=$(pwd)/empty.lua true 2>empty.err &&
	grep -i "non-empty array" empty.err
'

test_expect_success 'invalid ranks idset is rejected' '
	write_rc badranks.lua "{
	  mon = { command = {\"true\"}, ranks = \"notanidset\" }
	}" &&
	test_must_fail flux run -o userrc=$(pwd)/badranks.lua true 2>badranks.err &&
	grep -i "invalid ranks" badranks.err
'

test_expect_success 'non-object coprocess option is rejected' '
	write_rc notobj.lua "\"nope\"" &&
	test_must_fail flux run -o userrc=$(pwd)/notobj.lua true 2>notobj.err &&
	grep -i "non-empty object" notobj.err
'

test_done
