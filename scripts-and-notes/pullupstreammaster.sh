#!/bin/sh

DIRS="\
  flux-core \
  flux-sched \
  flux-security \
  rfc \
  flux-workflow-examples \
  flux-framework.github.io \
  flux-hierarchy \
  flux-scalability \
  flux-docs \
  flux-accounting \
  flux-k8s \
  flux-k8s-orchestrator \
  flux-pmix \
  flux-pam \
  planning \
"

for dir in ${DIRS}
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
