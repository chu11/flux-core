#!/bin/sh

set -x
export PYTHON_VERSION=3.6

while getopts j: flag
do
    case "${flag}" in
        j) proc=${OPTARG};;
    esac
done

if test -z "${proc}"; then
    proc=`cat /proc/cpuinfo | grep processor | wc -l`
    proc=$((${proc} / 2))
fi
./autogen.sh; ./configure; make clean; make distclean; ./autogen.sh; ./configure; make -j${proc}
rm -f machine.xml
hwloc-ls --of xml >machine.xml
FLUX_HWLOC_XMLFILE=$(pwd)/machine.xml make -j${proc} check
git log  --pretty=oneline | head -n 1
