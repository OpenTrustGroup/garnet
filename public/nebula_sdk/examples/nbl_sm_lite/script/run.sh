#!/bin/bash

# Copyright 2020 GoldenRiver Technology Co., Ltd. All rights reserved.

# 1. Create private and public key of SM2 by openssl (version>=1.1.1)
# 2. Use Python script convert "der" files to c source.
#    Users should install python package "asn1crypto".
#    sudo pip install asn1crypto

#1. Create private key
openssl ecparam -genkey -name SM2 -genkey -out pri.pem

#2. Conver pem to der (binary format)
openssl ec -in pri.pem -outform der -out pri.der
cat pri.pem | openssl ec -pubout -out pub.der -outform DER

#3. Create corresponding c source.
python dump-der.py
