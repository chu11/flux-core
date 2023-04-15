#!/bin/sh

source ./fluxdirs.sh

for dir in ${FLUXDIRS}
do
    cd $dir >& /dev/null
    if [ $? -eq 0 ]
    then
        echo "***************"
        echo "* Pushing $dir"
        echo "***************"
        if git branch | grep master
        then
            mainbranchname="master"
        else
            mainbranchname="main"
        fi
        git checkout ${mainbranchname}
        git push --tags origin ${mainbranchname}:${mainbranchname}
        git pull
        cd ..
    else
        echo "* DID NOT PUSH in $dir"
    fi
done
