#!/bin/bash

# Check what should work so far in src/common on macos

die() {
    echo "$(basename $0): $@" >&2
    exit 1
}

CONF_SCRIPT=scripts/configure-macos.sh

test -f $CONF_SCRIPT || die "please run from the top level of the source tree"
test -f configure || die "please run $CONF_SCRIPT first"

if make -j4 -C src check ; then
    cat >&2 <<-EOT
=============================================
* Well the unit tests that worked before on macos still work!
* However, please note that the macos port of flux-core is incomplete.
* Search for 'macos' at https://github.com/flux-framework/flux-core/issues
* for portability issues that still need to be resolved.
=============================================
EOT
else
    ./src/test/checks-annotate.sh
    die "check failed"
fi

