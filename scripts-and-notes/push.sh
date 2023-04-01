#!/bin/sh

git checkout master
for var in "$@"
do
    target=`echo $var | sed 's/\(.*\)_try.*/\1/'`
    git push -f origin $var:$target
done
