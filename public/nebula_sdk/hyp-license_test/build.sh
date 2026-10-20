#!/bin/bash

set -e

rm -f ${current_path}/hyp-license_test

current_path=$(pwd)
cd ${current_path}/src/hyp-license_test
make clean
make
cp out/hyp-license_test ${current_path}/

