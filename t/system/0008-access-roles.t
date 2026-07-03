#
#  Verify [access.roles] admin assignment end-to-end in a system instance.
#
#  The test runs as a guest (id -u != security.owner).  We reconfigure the
#  running instance (as the instance owner, via sudo) to grant the admin role
#  to this guest user, then verify the connector-local module stamps
#  FLUX_ROLE_ADMIN (0x8) onto the guest's credential -- exercising the real
#  client_authenticate() -> roleconf_match() path that cannot be reached from
#  a single-user test instance.  The config is restored on completion so other
#  system tests are unaffected.
#
#  Credential bits: FLUX_ROLE_USER=0x2, FLUX_ROLE_LOCAL=0x4, FLUX_ROLE_ADMIN=0x8
#
test_expect_success 'running as a guest (not the instance owner)' '
	test $(flux getattr security.owner) -ne $(id -u)
'
test_expect_success 'guest is an ordinary user before admin role is configured' '
	flux ping --count=1 --userid broker >ping-before.out &&
	test_debug "cat ping-before.out" &&
	grep -q "userid=$(id -u) rolemask=0x6" ping-before.out
'
test_expect_success 'configure admin role for this guest user' '
	cleanup "sudo flux config reload" &&
	flux config get |
	    jq ".access.roles.admin.users = [\"$(id -un)\"]" |
	    sudo flux config load &&
	flux config get | jq .access
'
test_expect_success 'guest in admin role is assigned FLUX_ROLE_ADMIN' '
	flux ping --count=1 --userid broker >ping-admin.out &&
	test_debug "cat ping-admin.out" &&
	grep -q "userid=$(id -u) rolemask=0xe" ping-admin.out
'
#
#  A different guest (user1), not listed in the admin role, must still get an
#  ordinary guest credential while the admin role is configured for us.
#
test_expect_success 'have a second guest user' '
	sudo -u user1 id && test_set_prereq HAVE_USER1
'
test_expect_success HAVE_USER1 'other guest is not assigned admin' '
	sudo -u user1 flux ping --count=1 --userid broker >ping-user1.out &&
	test_debug "cat ping-user1.out" &&
	grep -q "userid=$(id -u user1) rolemask=0x6" ping-user1.out
'
