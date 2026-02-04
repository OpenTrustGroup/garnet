#!/bin/bash
#push file to qemu
#The first parameter is the source file path to be pushed.
#The second parameter is the target path.

basepath=$(cd `dirname $0`; pwd)

$basepath/push_qemu.sh $basepath/new_test_key/export_key /root/ree/secmgr_test_key
