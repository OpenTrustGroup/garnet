#!/bin/bash
#push file to qemu
#The first parameter is the source file path to be pushed.
#The second parameter is the target path.

basepath=$(cd `dirname $0`; pwd)

function read_dir(){
for file in `ls $1`
do
 if [ -d $1"/"$file ]
 then
 read_dir $1"/"$file $2 $3
 else
    if [ -z "$3" ]; then
      $basepath/../../../run/install-file-in-qemu.sh -e $1"/"$file -o$2
    else
      $basepath/../../../run/install-file-in-qemu.sh -e $1"/"$file -o$2 -a$3
    fi
 fi
done
}
read_dir $1 $2 $3
