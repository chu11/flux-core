#!/bin/bash
#
#  Build fluxorama image with builddir mounted as /usr/src for testing.
#

PROJECT=flux-core
WORKDIR=/usr/src
MOUNT_HOME_ARGS="--volume=$HOME:/home/$USER -e HOME"
JOBS=2

declare -r prog=${0##*/}
die() { echo -e "$prog: $0"; exit 1; }

declare -r long_opts="help,no-home,rebuild,jobs:"
declare -r short_opts="hrj:"
declare -r usage="
Usage: $prog [OPTIONS]\n\
Build fluxorama system test docker image for CI builds\n\
\n\
Options:\n\
 -h, --help                    Display this message\n\
 -j, --jobs=N                  Value for make -j (default=$JOBS)\n\
     --rebuild                 Rebuild base fluxorama image from source\n\
     --no-home                 Skip mounting the host home directory\n\
"

# check if running in OSX
if [[ "$(uname)" == "Darwin" ]]; then
    # BSD getopt
    GETOPTS=`/usr/bin/getopt $short_opts -- $*`
else
    # GNU getopt
    GETOPTS=`/usr/bin/getopt -u -o $short_opts -l $long_opts -n $prog -- $@`
    if [[ $? != 0 ]]; then
        die "$usage"
    fi
    eval set -- "$GETOPTS"
fi

while true; do
    case "$1" in
      -h|--help)     echo -ne "$usage";          exit 0  ;;
      -j|--jobs)     JOBS="$2";                  shift 2 ;;
      --rebuild)     REBUILD_BASE_IMAGE=t;       shift   ;;
      --no-home)     MOUNT_HOME_ARGS="";         shift   ;;
      --)            shift; break;                       ;;
      *)             die "Invalid option '$1'\n$usage"   ;;
    esac
done

TOP=$(git rev-parse --show-toplevel 2>&1) \
    || die "not inside $PROJECT git repository!"
which docker >/dev/null \
    || die "unable to find docker binary!"

. ${TOP}/src/test/checks-lib.sh

if test "$REBUILD_BASE_IMAGE" = "t"; then
    checks_group "Rebuilding fluxrm/flux-core:centos8 from source" \
      $TOP/src/test/docker/docker-run-checks.sh \
        -j $JOBS \
        -i centos8 \
        -t fluxrm/flux-core:centos8 \
        --install-only
fi

checks_group "Building system image for user $USER $(id -u) group=$(id -g)" \
  docker build \
    --build-arg USER=$USER \
    --build-arg UID=$(id -u) \
    --build-arg GID=$(id -g) \
    -t fluxorama:systest \
    --target=systest \
    src/test/docker/fluxorama \
    || die "docker build failed"

docker run -d --rm \
    --hostname=fluxorama \
    --workdir=$WORKDIR \
    $MOUNT_HOME_ARGS \
    --volume=$TOP:$WORKDIR \
    --volume=/sys/fs/cgroup:/sys/fs/cgroup:ro \
    --tmpfs=/run \
    --cap-add SYS_PTRACE \
    --name=flux-system-test-$$ \
    fluxorama:systest

docker exec -ti -u $USER:$GID \
    -e CC=$CC \
    -e CXX=$CXX \
    -e LDFLAGS=$LDFLAGS \
    -e CFLAGS=$CFLAGS \
    -e CPPFLAGS=$CPPFLAGS \
    -e USER=$USER \
    -e CI=$CI \
    -e PS1=$PS1 \
    -e HOME=/home/$USER \
    -e XDG_RUNTIME_DIR="/run/user/996" \
    -e DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/996/bus \
    -w $WORKDIR \
    flux-system-test-$$ /bin/bash

docker exec -ti flux-system-test-$$ shutdown -r now


# vi: ts=4 sw=4 expandtab
