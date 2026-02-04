#!/bin/bash
basepath=$(cd `dirname $0`; pwd)

source $basepath/export_key.sh

rm -rf tfw
mkdir -p tfw/root_ca/
cd tfw/root_ca/
mkdir -p demoCA/newcerts
touch demoCA/index.txt
touch demoCA/index.txt.attr
echo '01' > demoCA/serial
cd ../../

alias="tfw_sign"

################ TFW ROOT CA #########################
echo "genarate tfw root ca key"
##genarate tfw root ca and sign self
openssl genrsa -des3 -out tfw/root_ca/tfw_root_ca.key 2048

#pkcs1 to pkcs8
#openssl pkcs8 -topk8 -inform PEM -in tfw/root_ca/tfw_root_ca.key -outform pem -nocrypt -out tfw/root_ca/tfw_root_ca_privatekey.pem

echo "genarate tfw root ca crt"
openssl req -new -x509 -outform PEM -days 36500 -key tfw/root_ca/tfw_root_ca.key -out tfw/root_ca/tfw_root_ca.crt

################ TFW SIGN CERT #########################
echo "genarate tfw sign genkeypair"
mkdir -p tfw/sign_cert/
keytool -genkeypair -keyalg RSA -keysize 2048 -sigalg SHA256withRSA -alias $alias -keystore  tfw/sign_cert/tfw_sign.keystore -validity 36500 -startdate 2018/01/01

echo "genarate tfw sign certreq"
keytool -certreq -keystore  tfw/sign_cert/tfw_sign.keystore -alias $alias -file tfw/sign_cert/tfw_sign.csr

echo "genarate tfw sign crt"
cd tfw/root_ca/
openssl ca -policy policy_anything -days 36500 -in ../sign_cert/tfw_sign.csr -out ../sign_cert/tfw_sign.crt -cert tfw_root_ca.crt -keyfile tfw_root_ca.key
cd ../../

############### TFW SIGN OCSP ###########################
echo "genarate tfw sign ocsp data"
mkdir -p tfw/sign_cert/ocsp/
openssl ocsp -issuer tfw/root_ca/tfw_root_ca.crt -cert tfw/sign_cert/tfw_sign.crt -reqout tfw/sign_cert/ocsp/tfw_sign_req.der
openssl ocsp -index tfw/root_ca/demoCA/index.txt -rsigner tfw/root_ca/tfw_root_ca.crt -rkey tfw/root_ca/tfw_root_ca.key -CA tfw/root_ca/tfw_root_ca.crt -reqin tfw/sign_cert/ocsp/tfw_sign_req.der -respout tfw/sign_cert/ocsp/tfw_sign_resp.der

echo "check tfw sign ocsp data"
############### check tfw sign ocsp data ###########################
openssl ocsp -respin tfw/sign_cert/ocsp/tfw_sign_resp.der -CAfile tfw/root_ca/tfw_root_ca.crt
#need response verify OK
#export pem
java -jar $basepath/tools/CertTool.jar export_pem_cert --in-file-path tfw/sign_cert/tfw_sign.crt --out-file-path tfw/sign_cert/tfw_sign.pem
#export pem
java -jar $basepath/tools/CertTool.jar ocsp_resp_to_pem --in-file-path tfw/sign_cert/ocsp/tfw_sign_resp.der --out-file-path tfw/sign_cert/ocsp/tfw_sign_resp.pem

#keystore export key
echo "export tfw sign private key pem"
read -s -p "Please enter keystore-password:" keystore_password
echo -e "\n"
read -s -p "Please enter key-password:" key_password
java -jar $basepath/tools/CertTool.jar keystore_to_key --in-file-path tfw/sign_cert/tfw_sign.keystore --alias $alias --keystore-password $keystore_password --key-password $key_password --out-path tfw/sign_cert

mkdir -p $basepath/export_key
echo "export tfw cert start."
export_cert ${basepath}/tfw/sign_cert/tfw_sign.crt $basepath/export_key/tfw_cert
echo "export tfw cert finish."

echo "export tfw private key."
export_private_key ${basepath}/tfw/sign_cert/private_key.pem $basepath/export_key/tfw_private_key
echo "export tfw private key finish."
