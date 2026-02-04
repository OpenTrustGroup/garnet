#!/bin/bash
rm -rf tfw
mkdir -p tfw/root_ca/
cd tfw/root_ca/
mkdir -p demoCA/newcerts
touch demoCA/index.txt
echo '01' > demoCA/serial
cd ../../

################ TFW ROOT CA #########################
echo "genarate tfw root ca key"
##genarate tfw root ca and sign self
openssl genrsa -out tfw/root_ca/tfw_root_ca.key 2048
#-des3 password goldenriver_tfw_root_ca_2018

#pkcs1 to pkcs8
openssl pkcs8 -topk8 -inform PEM -in tfw/root_ca/tfw_root_ca.key -outform pem -nocrypt -out tfw/root_ca/tfw_root_ca_privatekey.pem

echo "genarate tfw root ca crt"
openssl req -new -x509 -outform PEM -days 10950 -key tfw/root_ca/tfw_root_ca.key -out tfw/root_ca/tfw_root_ca.crt
#need tfw_root_ca.key password goldenriver_tfw_root_ca_2018
#info:
#Country Name (2 letter code) [AU]:cn
#State or Province Name (full name) [Some-State]:liaoning
#Locality Name (eg, city) []:shenyang
#Organization Name (eg, company) [Internet Widgits Pty Ltd]:goldenrivertek
#Organizational Unit Name (eg, section) []:goldenrivertek
#Common Name (e.g. server FQDN or YOUR name) []:gr.tfw.root.ca
#Email Address []:

################ TFW SIGN CERT #########################
echo "genarate tfw sign genkeypair"
mkdir -p tfw/sign_cert/
keytool -genkeypair -keyalg RSA -keysize 2048 -sigalg SHA256withRSA -alias tfw_sign -keystore  tfw/sign_cert/tfw_sign.keystore -dname "CN=gr.tfw.sign,OU=goldenrivertek,O=goldenrivertek,L=shenyang,ST=liaoning,c=cn"  -validity 10957 -startdate 2018/01/01
#password goldenriver_tfw_sign_test

echo "genarate tfw sign certreq"
keytool -certreq -keystore  tfw/sign_cert/tfw_sign.keystore -alias tfw_sign -file tfw/sign_cert/tfw_sign.csr
#password goldenriver_tfw_sign_test

echo "genarate tfw sign crt"
cd tfw/root_ca/
openssl ca -days 10950 -in ../sign_cert/tfw_sign.csr -out ../sign_cert/tfw_sign.crt -cert tfw_root_ca.crt -keyfile tfw_root_ca.key
#password goldenriver_tfw_root_ca_2018
cd ../../

############### TFW SIGN OCSP ###########################
echo "genarate tfw sign ocsp data"
mkdir -p tfw/sign_cert/ocsp/
openssl ocsp -issuer tfw/root_ca/tfw_root_ca.crt -cert tfw/sign_cert/tfw_sign.crt -reqout tfw/sign_cert/ocsp/tfw_sign_req.der
openssl ocsp -index tfw/root_ca/demoCA/index.txt -rsigner tfw/root_ca/tfw_root_ca.crt -rkey tfw/root_ca/tfw_root_ca.key -CA tfw/root_ca/tfw_root_ca.crt -reqin tfw/sign_cert/ocsp/tfw_sign_req.der -respout tfw/sign_cert/ocsp/tfw_sign_resp.der
#password goldenriver_tfw_root_ca_2018

echo "check tfw sign ocsp data"
############### check tfw sign ocsp data ###########################
openssl ocsp -respin tfw/sign_cert/ocsp/tfw_sign_resp.der -CAfile tfw/root_ca/tfw_root_ca.crt
#need response verify OK
#export pem
java -jar tools/CertTool.jar export_pem_cert --in-file-path tfw/sign_cert/tfw_sign.crt --out-file-path tfw/sign_cert/tfw_sign.pem
#export pem
java -jar tools/CertTool.jar ocsp_resp_to_pem --in-file-path tfw/sign_cert/ocsp/tfw_sign_resp.der --out-file-path tfw/sign_cert/ocsp/tfw_sign_resp.pem

#keystore export key
echo "export tfw sign private key pem"
java -jar tools/CertTool.jar keystore_to_key --in-file-path tfw/sign_cert/tfw_sign.keystore --out-path tfw/sign_cert/ --alias tfw_sign --keystore-password goldenriver_tfw_sign_test --key-password goldenriver_tfw_sign_test
