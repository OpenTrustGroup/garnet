#!/bin/bash
basepath=$(cd `dirname $0`; pwd)


################ SIGN KEY #########################
echo "genarate sign genkeypair"
mkdir -p key/sign/

##genarate sign key self
openssl genrsa -des3 -out key/sign/key_sign.key 2048

#pkcs1 to pkcs8
openssl pkcs8 -topk8 -inform PEM -in key/sign/key_sign.key -outform pem -nocrypt -out key/sign/key_sign_privatekey.pem

echo "genarate sign crt"
openssl req -new -x509 -outform PEM -days 36500 -key key/sign/key_sign.key -out key/sign/key_sign.crt
openssl x509 -in key/sign/key_sign.crt -pubkey  -noout > key/sign/key_sign_publickey.pem
