#!/bin/bash
rm -rf tsmb
mkdir -p tsmb/root_ca/
cd tsmb/root_ca/
mkdir -p demoCA/newcerts
touch demoCA/index.txt
echo '01' > demoCA/serial
cd ../../

################ TSM ROOT CA #########################
echo "genarate tsm root ca key"
##genarate tsm root ca and sign self
openssl genrsa -out tsmb/root_ca/tsm_root_ca.key 2048
#-des3 password goldenriver_tsmb

#pkcs1 to pkcs8
openssl pkcs8 -topk8 -inform PEM -in tsmb/root_ca/tsm_root_ca.key -outform pem -nocrypt -out tsmb/root_ca/tsm_root_ca_privatekey.pem

echo "genarate tsm root ca crt"
openssl req -new -x509 -outform PEM -days 10950 -key tsmb/root_ca/tsm_root_ca.key -out tsmb/root_ca/tsm_root_ca.crt
#need tsm_root_ca.key password goldenriver_tsmb
#info:
#Country Name (2 letter code) [AU]:cn
#State or Province Name (full name) [Some-State]:liaoning
#Locality Name (eg, city) []:shenyang
#Organization Name (eg, company) [Internet Widgits Pty Ltd]:goldenrivertek
#Organizational Unit Name (eg, section) []:goldenrivertek
#Common Name (e.g. server FQDN or YOUR name) []:gr.tsmb.root.ca
#Email Address []:

################ TSM SIGN CERT #########################
echo "genarate tsm sign genkeypair"
mkdir -p tsmb/sign_cert/
keytool -genkeypair -keyalg RSA -keysize 2048 -sigalg SHA256withRSA -alias tsm_sign -keystore  tsmb/sign_cert/tsm_sign.keystore -dname "CN=gr.tsm.sign,OU=goldenrivertek,O=goldenrivertek,L=shenyang,ST=liaoning,c=cn"  -validity 10957 -startdate 2018/01/01
#password goldenriver_tsmb

echo "genarate tsm sign certreq"
keytool -certreq -keystore  tsmb/sign_cert/tsm_sign.keystore -alias tsm_sign -file tsmb/sign_cert/tsm_sign.csr
#password goldenriver_tsmb

echo "genarate tsm sign crt"
cd tsmb/root_ca/
openssl ca -days 10950 -extensions SAN -config <(cat /usr/lib/ssl/openssl.cnf <(printf "[SAN]\nsubjectAltName=DNS:test.tsmb.goldenrivertek.com")) -in ../sign_cert/tsm_sign.csr -out ../sign_cert/tsm_sign.crt -cert tsm_root_ca.crt -keyfile tsm_root_ca.key
#password goldenriver_tsmb
cd ../../
#/usr/lib/ssl/openssl.cnf

############### TSM SIGN OCSP ###########################
echo "genarate tsm sign ocsp data"
mkdir -p tsmb/sign_cert/ocsp/
openssl ocsp -issuer tsmb/root_ca/tsm_root_ca.crt -cert tsmb/sign_cert/tsm_sign.crt -reqout tsmb/sign_cert/ocsp/tsm_sign_req.der
openssl ocsp -index tsmb/root_ca/demoCA/index.txt -rsigner tsmb/root_ca/tsm_root_ca.crt -rkey tsmb/root_ca/tsm_root_ca.key -CA tsmb/root_ca/tsm_root_ca.crt -reqin tsmb/sign_cert/ocsp/tsm_sign_req.der -respout tsmb/sign_cert/ocsp/tsm_sign_resp.der
#password goldenriver_tsmb

echo "check tsm sign ocsp data"
############### check tsm sign ocsp data ###########################
openssl ocsp -respin tsmb/sign_cert/ocsp/tsm_sign_resp.der -CAfile tsmb/root_ca/tsm_root_ca.crt
#need response verify OK
#export pem
java -jar tools/CertTool.jar export_pem_cert --in-file-path tsmb/sign_cert/tsm_sign.crt --out-file-path tsmb/sign_cert/tsm_sign.pem
#export pem
java -jar tools/CertTool.jar ocsp_resp_to_pem --in-file-path tsmb/sign_cert/ocsp/tsm_sign_resp.der --out-file-path tsmb/sign_cert/ocsp/tsm_sign_resp.pem
