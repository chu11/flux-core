#!/bin/sh

source ./fluxdirs.sh

for dir in ${FLUXDIRS}
do
    cd $dir >& /dev/null
    if [ $? -eq 0 ]
    then
        echo "***************"
        echo "* Pulling $dir"
        echo "***************"
        if git branch | grep master
        then
            mainbranchname="master"
        else
            mainbranchname="main"
        fi
        git checkout ${mainbranchname}
        if [ $? -eq 0 ]
        then
            git pull upstreammaster ${mainbranchname}:${mainbranchname}
        else
            echo "* DID NOT PULL in $dir - cant checkout master"
        fi
        cd ..
    else
        echo "* NO $dir directory"
    fi
done
