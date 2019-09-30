#!/bin/sh
#
test_description='Test flux-shell'

. `dirname $0`/sharness.sh

test_under_flux 4 job

flux setattr log-stderr-level 1

TEST_SUBPROCESS_DIR=${FLUX_BUILD_DIR}/src/common/libsubprocess
LPTEST=${SHARNESS_TEST_DIRECTORY}/shell/lptest

hwloc_fake_config='{"0-3":{"Core":2,"cpuset":"0-1"}}'

test_expect_success 'job-shell: load barrier,job-exec,sched-simple modules' '
        #  Add fake by_rank configuration to kvs:
        flux kvs put resource.hwloc.by_rank="$hwloc_fake_config" &&
        flux module load barrier &&
        flux module load -r 0 sched-simple &&
        flux module load -r 0 job-exec
'

test_expect_success 'flux-shell: generate input for stdin input tests' '
       echo "foo" > input_stdin_file &&
       echo "doh" >> input_stdin_file &&
       ${LPTEST} 79 10000 > lptestXXL_input
'

#
# input file tests
#

test_expect_success 'flux-shell: run 1-task input file as stdin job' '
        flux mini run -n1 --input=input_stdin_file \
             ${TEST_SUBPROCESS_DIR}/test_echo -O -n > file0.out &&
        test_cmp input_stdin_file file0.out
'

test_expect_success 'flux-shell: run 2-task input file as stdin job' '
        flux mini run -n2 --input=input_stdin_file --label-io \
             ${TEST_SUBPROCESS_DIR}/test_echo -O -n > file1.out &&
        grep "0: foo" file1.out &&
        grep "0: doh" file1.out &&
        grep "1: foo" file1.out &&
        grep "1: doh" file1.out
'

test_expect_success LONGTEST 'flux-shell: 10K line lptest input works' '
        flux mini run -n1 --input=lptestXXL_input \
             ${TEST_SUBPROCESS_DIR}/test_echo -O -n > file2.out &&
	test_cmp lptestXXL_input file2.out
'

test_expect_success 'flux-shell: input file invalid' '
        ! flux mini run -n1 --input=/foo/bar/baz \
             ${TEST_SUBPROCESS_DIR}/test_echo -O -n > file_err0.out
'

test_expect_success 'job-shell: unload job-exec & sched-simple modules' '
        flux module remove -r 0 job-exec &&
        flux module remove -r 0 sched-simple &&
        flux module remove barrier
'

test_done
