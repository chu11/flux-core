#!/bin/bash -e

flux module load content-sqlite

flux module remove kvs-watch
flux module remove kvs

flux dump --checkpoint issue4222.tar

echo 1
flux content flush
echo 2
flux content dropcache
echo 3
flux module remove content-sqlite
echo 4

sqlitepath=$(flux getattr rundir)/content.sqlite
mv $sqlitepath $sqlitepath.bak

echo 5
flux module load content-sqlite

flux restore --checkpoint issue4222.tar

flux module load kvs
flux module load kvs-watch

flux kvs namespace create issue4222ns
flux kvs put --namespace=issue4222ns a=1

flux module remove content-sqlite
