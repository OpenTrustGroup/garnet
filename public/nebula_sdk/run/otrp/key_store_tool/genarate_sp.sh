#!/bin/bash
basepath=$(cd `dirname $0`; pwd)
rm -rf sp
mkdir -p sp/root_ca/
cd sp/root_ca/
mkdir -p demoCA/newcerts
touch demoCA/index.txt
touch demoCA/index.txt.attr
echo '01' > demoCA/serial
cd ../../

alias="sp_sign"

################ SP ROOT CA #########################
echo "genarate sp root ca key"
##genarate sp root ca and sign self
openssl genrsa -des3 -out sp/root_ca/sp_root_ca.key 2048

#pkcs1 to pkcs8
#openssl pkcs8 -topk8 -inform PEM -in sp/root_ca/sp_root_ca.key -outform pem -nocrypt -out sp/root_ca/sp_root_ca_privatekey.pem

echo "genarate sp root ca cert"
openssl req -new -x509 -outform PEM -days 36500 -key sp/root_ca/sp_root_ca.key -out sp/root_ca/sp_root_ca.crt
################ SP SIGN CERT #########################
echo "genarate sp sign keypair"
mkdir -p sp/sign_cert/
keytool -genkeypair -keyalg RSA -keysize 2048 -sigalg SHA256withRSA -alias $alias -keystore  sp/sign_cert/sp_sign.keystore -validity 36500 -startdate 2018/01/01

echo "genarate sp sign certreq"
keytool -certreq -keystore  sp/sign_cert/sp_sign.keystore -alias $alias -file sp/sign_cert/sp_sign.csr

echo "genarate sp sign cert"
cd sp/root_ca/
openssl ca -policy policy_anything -days 36500 -in ../sign_cert/sp_sign.csr -out ../sign_cert/sp_sign.crt -cert sp_root_ca.crt -keyfile sp_root_ca.key
cd ../../

############### SP SIGN OCSP ###########################
echo "genarate sp sign ocsp data"
mkdir -p sp/sign_cert/ocsp/
openssl ocsp -issuer sp/root_ca/sp_root_ca.crt -cert sp/sign_cert/sp_sign.crt -reqout sp/sign_cert/ocsp/sp_sign_req.der
openssl ocsp -index sp/root_ca/demoCA/index.txt -rsigner sp/root_ca/sp_root_ca.crt -rkey sp/root_ca/sp_root_ca.key -CA sp/root_ca/sp_root_ca.crt -reqin sp/sign_cert/ocsp/sp_sign_req.der -respout sp/sign_cert/ocsp/sp_sign_resp.der

echo "check sp sign ocsp data"
############### check sp sign ocsp data ###########################
openssl ocsp -respin sp/sign_cert/ocsp/sp_sign_resp.der -CAfile sp/root_ca/sp_root_ca.crt
#need response verify OK
#export pem
java -jar $basepath/tools/CertTool.jar export_pem_cert --in-file-path sp/sign_cert/sp_sign.crt --out-file-path sp/sign_cert/sp_sign.pem
#export pem
java -jar $basepath/tools/CertTool.jar ocsp_resp_to_pem --in-file-path sp/sign_cert/ocsp/sp_sign_resp.der --out-file-path sp/sign_cert/ocsp/sp_sign_resp.pem
#export privite key pem
read -s -p "Please enter keystore-password:" keystore_password
echo -e "\n"
read -s -p "Please enter key-password:" key_password
java -jar $basepath/tools/CertTool.jar keystore_to_key --in-file-path sp/sign_cert/sp_sign.keystore --alias $alias --keystore-password $keystore_password --key-password $key_password --out-path sp/sign_cert
