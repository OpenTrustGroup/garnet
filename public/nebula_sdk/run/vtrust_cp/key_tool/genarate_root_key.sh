#!/bin/bash
basepath=$(cd `dirname $0`; pwd)
rm -rf key
mkdir -p key/root_ca/
cd key/root_ca/
mkdir -p demoCA/newcerts
touch demoCA/index.txt
touch demoCA/index.txt.attr
echo '01' > demoCA/serial
cd ../../

################ vTrust CP ROOT CA #########################
echo "genarate vTrust cp root ca key"
##genarate vTrust cp root ca and sign self
openssl genrsa -des3 -out key/root_ca/key_root_ca.key 2048

#pkcs1 to pkcs8
#openssl pkcs8 -topk8 -inform PEM -in key/root_ca/key_root_ca.key -outform pem -nocrypt -out key/root_ca/key_root_ca_privatekey.pem

echo "genarate vTrust cp root ca crt"
openssl req -new -x509 -outform PEM -days 36500 -key key/root_ca/key_root_ca.key -out key/root_ca/key_root_ca.crt
