#!/bin/sh -e

if flux run flux kvs link a b; then
   exit 1
fi
