#!/bin/sh

source ./fluxdirs.sh

for dir in ${FLUXDIRS}
do
    echo "***************"
    echo "* Clone out $dir"
    echo "***************"
    cd $dir >& /dev/null
    if [ $? -eq 0 ]
    then
        echo "already cloned $dir"
    else
        git clone git@github.com:chu11/${dir}.git
        cd $dir
        git remote add upstreammaster git@github.com:flux-framework/${dir}.git
    fi
    cd ..
done
