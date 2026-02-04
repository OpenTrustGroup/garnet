#!/bin/bash
basepath=$(cd `dirname $0`; pwd)

source $basepath/export_key.sh

rm -rf tsm
mkdir -p tsm/root_ca/
cd tsm/root_ca/
mkdir -p demoCA/newcerts
touch demoCA/index.txt
touch demoCA/index.txt.attr
echo '01' > demoCA/serial
cd ../../

alias="tsm_sign"
tsmid=test.tsm.goldenrivertek.com
if [ -n "$1" ]; then
    tsmid=$1
fi

echo "tsmid:"$tsmid

################ TSM ROOT CA #########################
echo "genarate tsm root ca key"
##genarate tsm root ca and sign self
openssl genrsa -des3 -out tsm/root_ca/tsm_root_ca.key 2048

#pkcs1 to pkcs8
#openssl pkcs8 -topk8 -inform PEM -in tsm/root_ca/tsm_root_ca.key -outform pem -nocrypt -out tsm/root_ca/tsm_root_ca_privatekey.pem

echo "genarate tsm root ca crt"
openssl req -new -x509 -outform PEM -days 36500 -key tsm/root_ca/tsm_root_ca.key -out tsm/root_ca/tsm_root_ca.crt

################ TSM SIGN CERT #########################
echo "genarate tsm sign genkeypair"
mkdir -p tsm/sign_cert/
keytool -genkeypair -keyalg RSA -keysize 2048 -sigalg SHA256withRSA -alias $alias -keystore  tsm/sign_cert/tsm_sign.keystore -validity 36500 -startdate 2018/01/01

echo "genarate tsm sign certreq"
keytool -certreq -keystore  tsm/sign_cert/tsm_sign.keystore -alias $alias -file tsm/sign_cert/tsm_sign.csr

echo "genarate tsm sign crt"
cd tsm/root_ca/
openssl ca -policy policy_anything -days 36500 -extensions SAN -config <(cat /usr/lib/ssl/openssl.cnf <(printf "[SAN]\nsubjectAltName=DNS:$tsmid")) -in ../sign_cert/tsm_sign.csr -out ../sign_cert/tsm_sign.crt -cert tsm_root_ca.crt -keyfile tsm_root_ca.key
cd ../../
#/usr/lib/ssl/openssl.cnf

############### TSM SIGN OCSP ###########################
echo "genarate tsm sign ocsp data"
mkdir -p tsm/sign_cert/ocsp/
openssl ocsp -issuer tsm/root_ca/tsm_root_ca.crt -cert tsm/sign_cert/tsm_sign.crt -reqout tsm/sign_cert/ocsp/tsm_sign_req.der
openssl ocsp -index tsm/root_ca/demoCA/index.txt -rsigner tsm/root_ca/tsm_root_ca.crt -rkey tsm/root_ca/tsm_root_ca.key -CA tsm/root_ca/tsm_root_ca.crt -reqin tsm/sign_cert/ocsp/tsm_sign_req.der -respout tsm/sign_cert/ocsp/tsm_sign_resp.der

echo "check tsm sign ocsp data"
############### check tsm sign ocsp data ###########################
openssl ocsp -respin tsm/sign_cert/ocsp/tsm_sign_resp.der -CAfile tsm/root_ca/tsm_root_ca.crt
#need response verify OK
#export pem
java -jar $basepath/tools/CertTool.jar export_pem_cert --in-file-path tsm/sign_cert/tsm_sign.crt --out-file-path tsm/sign_cert/tsm_sign.pem
#export pem
java -jar $basepath/tools/CertTool.jar ocsp_resp_to_pem --in-file-path tsm/sign_cert/ocsp/tsm_sign_resp.der --out-file-path tsm/sign_cert/ocsp/tsm_sign_resp.pem
#export privite key pem
read -s -p "Please enter keystore-password:" keystore_password
echo -e "\n"
read -s -p "Please enter key-password:" key_password
java -jar $basepath/tools/CertTool.jar keystore_to_key --in-file-path tsm/sign_cert/tsm_sign.keystore --alias $alias --keystore-password $keystore_password --key-password $key_password --out-path tsm/sign_cert

mkdir -p $basepath/export_key
echo "export tsm root cert."
export_cert ${basepath}/tsm/root_ca/tsm_root_ca.crt $basepath/export_key/tsm_root_cert
echo "export tsm root cert finish."
