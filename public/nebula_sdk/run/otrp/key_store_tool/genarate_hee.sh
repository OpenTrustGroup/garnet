#!/bin/bash
basepath=$(cd `dirname $0`; pwd)

source $basepath/export_key.sh

rm -rf hee
mkdir -p hee/root_ca/
cd hee/root_ca/
mkdir -p demoCA/newcerts
touch demoCA/index.txt
touch demoCA/index.txt.attr
echo '01' > demoCA/serial
cd ../../

alias="hee_sign"

################ HEE ROOT CA #########################
echo "genarate hee root ca key"
##genarate hee root ca and sign self
openssl genrsa -des3 -out hee/root_ca/hee_root_ca.key 2048

#pkcs1 to pkcs8
#openssl pkcs8 -topk8 -inform PEM -in hee/root_ca/hee_root_ca.key -outform pem -nocrypt -out hee/root_ca/hee_root_ca_privatekey.pem

echo "genarate hee root ca crt"
openssl req -new -x509 -outform PEM -days 36500 -key hee/root_ca/hee_root_ca.key -out hee/root_ca/hee_root_ca.crt

################ HEE SECOND CA #########################
mkdir -p hee/second_ca/
cd hee/second_ca/
mkdir -p demoCA/newcerts
touch demoCA/index.txt
echo '01' > demoCA/serial
cd ../../

echo "genarate hee second ca key"
openssl genrsa -des3 -out hee/second_ca/hee_second_ca.key 2048

#pkcs1 to pkcs8
#openssl pkcs8 -topk8 -inform PEM -in hee/second_ca/hee_second_ca.key -outform pem -nocrypt -out hee/second_ca/hee_second_ca.pem

echo "genarate hee second ca csr"
openssl req -new -days 36500 -key hee/second_ca/hee_second_ca.key -out hee/second_ca/hee_second_ca.csr

echo "genarate hee second ca crt"
cd hee/root_ca/
openssl ca -policy policy_anything -extensions v3_ca -days 36500 -in ../second_ca/hee_second_ca.csr -days 36500 -out ../second_ca/hee_second_ca.crt -cert ../root_ca/hee_root_ca.crt -keyfile ../root_ca/hee_root_ca.key
cd ../../

################ HEE SIGN CERT #########################
echo "genarate hee sign genkeypair"
mkdir -p hee/sign_cert/
keytool -genkeypair -keyalg RSA -keysize 2048 -sigalg SHA256withRSA -alias $alias -keystore  hee/sign_cert/hee_sign.keystore -validity 36500 -startdate 2018/01/01
echo "genarate hee sign certreq"
keytool -certreq -keystore  hee/sign_cert/hee_sign.keystore -alias $alias -file hee/sign_cert/hee_sign.csr

echo "genarate hee sign crt"
cd hee/second_ca/
openssl ca -policy policy_anything -days 36500 -in ../sign_cert/hee_sign.csr -days 36500 -out ../sign_cert/hee_sign.crt -cert hee_second_ca.crt -keyfile hee_second_ca.key
cd ../../

############### HEE SIGN OCSP ###########################
echo "genarate hee sign ocsp data"
mkdir -p hee/sign_cert/ocsp/
openssl ocsp -issuer hee/second_ca/hee_second_ca.crt -cert hee/sign_cert/hee_sign.crt -reqout hee/sign_cert/ocsp/hee_sign_req.der
openssl ocsp -index hee/second_ca/demoCA/index.txt -rsigner hee/second_ca/hee_second_ca.crt -rkey hee/second_ca/hee_second_ca.key -CA hee/second_ca/hee_second_ca.crt -reqin hee/sign_cert/ocsp/hee_sign_req.der -respout hee/sign_cert/ocsp/hee_sign_resp.der

echo "check hee sign ocsp data"
cat hee/second_ca/hee_second_ca.crt hee/root_ca/hee_root_ca.crt >hee/second_ca/hee_second_data.pem
############### check hee sign ocsp data ###########################
openssl ocsp -respin hee/sign_cert/ocsp/hee_sign_resp.der -CAfile hee/second_ca/hee_second_data.pem
#need response verify OK
#export pem
java -jar $basepath/tools/CertTool.jar export_pem_cert --in-file-path hee/sign_cert/hee_sign.crt --out-file-path hee/sign_cert/hee_sign.pem
#export pem
java -jar $basepath/tools/CertTool.jar ocsp_resp_to_pem --in-file-path hee/sign_cert/ocsp/hee_sign_resp.der --out-file-path hee/sign_cert/ocsp/hee_sign_resp.pem

#keystore export key pem
echo "export hee sign private key pem"
read -s -p "Please enter keystore-password:" keystore_password
echo -e "\n"
read -s -p "Please enter key-password:" key_password
java -jar $basepath/tools/CertTool.jar keystore_to_key --in-file-path hee/sign_cert/hee_sign.keystore --alias $alias --keystore-password $keystore_password --key-password $key_password --out-path hee/sign_cert

mkdir -p $basepath/export_key
echo "export hee second ca cert start."
export_cert ${basepath}/hee/second_ca/hee_second_ca.crt $basepath/export_key/hee_ca_cert
echo "export hee second ca cert finish."

echo "export hee cert start."
export_cert ${basepath}/hee/sign_cert/hee_sign.crt $basepath/export_key/hee_cert
echo "export hee cert finish."

echo "export hee private key start."
export_private_key ${basepath}/hee/sign_cert/private_key.pem $basepath/export_key/hee_private_key
echo "export hee private key finish."
