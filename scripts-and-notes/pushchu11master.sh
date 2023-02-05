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
