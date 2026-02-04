#!/bin/bash
rm -rf sp
mkdir -p sp/root_ca/
cd sp/root_ca/
mkdir -p demoCA/newcerts
touch demoCA/index.txt
echo '01' > demoCA/serial
cd ../../

################ SP ROOT CA #########################
echo "genarate sp root ca key"
##genarate sp root ca and sign self
openssl genrsa -out sp/root_ca/sp_root_ca.key 2048
#-des3 password goldenriver_sp_root_ca_2018

#pkcs1 to pkcs8
openssl pkcs8 -topk8 -inform PEM -in sp/root_ca/sp_root_ca.key -outform pem -nocrypt -out sp/root_ca/sp_root_ca_privatekey.pem

echo "genarate sp root ca crt"
openssl req -new -x509 -outform PEM -days 10950 -key sp/root_ca/sp_root_ca.key -out sp/root_ca/sp_root_ca.crt
#need sp_root_ca.key password goldenriver_sp_root_ca_2018
#info:
#Country Name (2 letter code) [AU]:cn
#State or Province Name (full name) [Some-State]:liaoning
#Locality Name (eg, city) []:shenyang
#Organization Name (eg, company) [Internet Widgits Pty Ltd]:goldenrivertek
#Organizational Unit Name (eg, section) []:goldenrivertek
#Common Name (e.g. server FQDN or YOUR name) []:gr.sp.root.ca
#Email Address []:

################ SP SIGN CERT #########################
echo "genarate sp sign genkeypair"
mkdir -p sp/sign_cert/
keytool -genkeypair -keyalg RSA -keysize 2048 -sigalg SHA256withRSA -alias sp_sign -keystore  sp/sign_cert/sp_sign.keystore -dname "CN=gr.sp.sign,OU=goldenrivertek,O=goldenrivertek,L=shenyang,ST=liaoning,c=cn"  -validity 10957 -startdate 2018/01/01
#password goldenriver_sp_sign_test

echo "genarate sp sign certreq"
keytool -certreq -keystore  sp/sign_cert/sp_sign.keystore -alias sp_sign -file sp/sign_cert/sp_sign.csr
#password goldenriver_sp_sign_test

echo "genarate sp sign crt"
cd sp/root_ca/
openssl ca -days 10950 -in ../sign_cert/sp_sign.csr -out ../sign_cert/sp_sign.crt -cert sp_root_ca.crt -keyfile sp_root_ca.key
#password goldenriver_sp_root_ca_2018
cd ../../

############### SP SIGN OCSP ###########################
echo "genarate sp sign ocsp data"
mkdir -p sp/sign_cert/ocsp/
openssl ocsp -issuer sp/root_ca/sp_root_ca.crt -cert sp/sign_cert/sp_sign.crt -reqout sp/sign_cert/ocsp/sp_sign_req.der
openssl ocsp -index sp/root_ca/demoCA/index.txt -rsigner sp/root_ca/sp_root_ca.crt -rkey sp/root_ca/sp_root_ca.key -CA sp/root_ca/sp_root_ca.crt -reqin sp/sign_cert/ocsp/sp_sign_req.der -respout sp/sign_cert/ocsp/sp_sign_resp.der
#password goldenriver_sp_root_ca_2018

echo "check sp sign ocsp data"
############### check sp sign ocsp data ###########################
openssl ocsp -respin sp/sign_cert/ocsp/sp_sign_resp.der -CAfile sp/root_ca/sp_root_ca.crt
#need response verify OK
#export pem
java -jar tools/CertTool.jar export_pem_cert --in-file-path sp/sign_cert/sp_sign.crt --out-file-path sp/sign_cert/sp_sign.pem
#export pem
java -jar tools/CertTool.jar ocsp_resp_to_pem --in-file-path sp/sign_cert/ocsp/sp_sign_resp.der --out-file-path sp/sign_cert/ocsp/sp_sign_resp.pem
