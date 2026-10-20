## Overview
This program is a hypervisor activation testing program. After the program runs successfully, it will activate the supervisor
Due to its sensitive key content, it cannot be made public and can only be used for internal development and testing purposes by relevant personnel

## Get code:
Obtain code through the get_code.sh script (code is subject to permission control, some personnel have permissions)

## Directory structure:
Google Test relies on the Google Test Library
Hyp-lice_test is a testing program

## Compile:
Execute the build.sh script in the directory

## Run app:
Successful compilation will generate the executable program hyp-lice_test in the hyp-license_test/hyp-lice_test/out folder
Push the program to the device
For example, scp - o StrictHostKeyChecking=no under QEMU/ hyp-license_test  root@192.168.3.53 :/ data/hyp-license_test
