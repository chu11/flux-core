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
