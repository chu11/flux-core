#!/bin/sh
#

test_description='Verify that fixed issues remain fixed'

. `dirname $0`/sharness.sh

if test_have_prereq ASAN; then
    skip_all='skipping issues tests under AddressSanitizer'
    test_done
fi

SIZE=4
if test -z "${TEST_UNDER_FLUX_ACTIVE}"; then
    STATEDIR=$(mktemp -d)
fi
test_under_flux ${SIZE} full -o,-Sstatedir=${STATEDIR}
echo "# $0: flux session size will be ${SIZE}"

if test -z "$T4000_ISSUES_GLOB"; then
    T4000_ISSUES_GLOB="*"
fi

for testscript in ${FLUX_SOURCE_DIR}/t/issues/${T4000_ISSUES_GLOB}; do
    testname=`basename $testscript`
    prereqs=$(sed -n 's/^.*test-prereqs: \(.*\).*$/\1/gp' $testscript)
    test_expect_success  "$prereqs" $testname "run_timeout 120 $testscript"
done

test_done
