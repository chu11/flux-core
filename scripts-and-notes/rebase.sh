#!/bin/sh

git checkout master
for var in "$@"
do
    git checkout $var
    git rebase master
    git checkout master
done
