#!/bin/bash
#push file to device
#param1 is src folder path
#param2 is dst folder path

adb root
adb remount
adb shell mkdir -p $2

function read_dir(){
for file in `ls $1`
do
 if [ -d $1"/"$file ]
 then
 read_dir $1"/"$file $2
 else
     adb push $1"/"$file $2
 fi
done
}
read_dir $1 $2
