#!/bin/bash
rm -rf hee
mkdir -p hee/root_ca/
cd hee/root_ca/
mkdir -p demoCA/newcerts
touch demoCA/index.txt
echo '01' > demoCA/serial
cd ../../

################ HEE ROOT CA #########################
echo "genarate hee root ca key"
##genarate hee root ca and sign self
openssl genrsa -out hee/root_ca/hee_root_ca.key 2048

#pkcs1 to pkcs8
openssl pkcs8 -topk8 -inform PEM -in hee/root_ca/hee_root_ca.key -outform pem -nocrypt -out hee/root_ca/hee_root_ca_privatekey.pem

echo "genarate hee root ca crt"
openssl req -new -x509 -outform PEM -days 10950 -key hee/root_ca/hee_root_ca.key -out hee/root_ca/hee_root_ca.crt
#info:
#Country Name (2 letter code) [AU]:cn
#State or Province Name (full name) [Some-State]:liaoning
#Locality Name (eg, city) []:shenyang
#Organization Name (eg, company) [Internet Widgits Pty Ltd]:goldenrivertek
#Organizational Unit Name (eg, section) []:goldenrivertek
#Common Name (e.g. server FQDN or YOUR name) []:gr.hee.root.ca
#Email Address []:


################ HEE SECOND CA #########################
mkdir -p hee/second_ca/
cd hee/second_ca/
mkdir -p demoCA/newcerts
touch demoCA/index.txt
echo '01' > demoCA/serial
cd ../../

echo "genarate hee second ca key"
openssl genrsa -out hee/second_ca/hee_second_ca.key 2048

#pkcs1 to pkcs8
openssl pkcs8 -topk8 -inform PEM -in hee/second_ca/hee_second_ca.key -outform pem -nocrypt -out hee/second_ca/hee_second_ca.pem

echo "genarate hee second ca csr"
openssl req -new -days 10957 -key hee/second_ca/hee_second_ca.key -out hee/second_ca/hee_second_ca.csr
#Country Name (2 letter code) [AU]:cn
#State or Province Name (full name) [Some-State]:liaoning
#Locality Name (eg, city) []:shenyang
#Organization Name (eg, company) [Internet Widgits Pty Ltd]:goldenrivertek
#Organizational Unit Name (eg, section) []:goldenrivertek
#Common Name (e.g. server FQDN or YOUR name) []:gr.hee.second.ca
#Email Address []:

#A challenge password dSiTXCvBrKePft25bHTa
#An optional company name []:

echo "genarate hee second ca crt"
cd hee/root_ca/
openssl ca -extensions v3_ca -days 10950 -in ../second_ca/hee_second_ca.csr -days 10957 -out ../second_ca/hee_second_ca.crt -cert ../root_ca/hee_root_ca.crt -keyfile ../root_ca/hee_root_ca.key
cd ../../
#openssl req -new -x509 -days 10950 -key hee/second_ca/hee_second_ca.key -out hee/second_ca/hee_second_ca.crt
############### HEE SECOND CA OCSP ###########################
#echo "genarate second ca ocsp data"
#mkdir -p hee/second_ca/ocsp/
#openssl ocsp -issuer hee/root_ca/hee_root_ca.crt -cert hee/second_ca/hee_second_ca.crt -reqout hee/second_ca/ocsp/hee_second_ca_req.der
#openssl ocsp -index hee/root_ca/demoCA/index.txt -rsigner hee/root_ca/hee_root_ca.crt -rkey hee/root_ca/hee_root_ca.key -CA hee/root_ca/hee_root_ca.crt -reqin hee/second_ca/#ocsp/hee_second_ca_req.der -respout hee/second_ca/ocsp/hee_second_ca_resp.der

#echo "check second ca ocsp data"
############### check second ca ocsp data ###########################
#openssl ocsp -respin hee/second_ca/ocsp/hee_second_ca_resp.der -CAfile hee/root_ca/hee_root_ca.crt
#need response verify OK
#export pem
#java -jar tools/CertTool.jar export_pem_cert --in-file-path hee/second_ca/hee_second_ca.crt --out-file-path hee/second_ca/hee_second_ca.pem
#export pem
#java -jar tools/CertTool.jar ocsp_resp_to_pem --in-file-path hee/second_ca/ocsp/hee_second_ca_resp.der --out-file-path hee/second_ca/ocsp/hee_second_ca_resp.pem


################ HEE SIGN CERT #########################
echo "genarate hee sign genkeypair"
mkdir -p hee/sign_cert/
keytool -genkeypair -keyalg RSA -keysize 2048 -sigalg SHA256withRSA -alias hee_sign -keystore  hee/sign_cert/hee_sign.keystore -dname "CN=gr.hee.sign,OU=goldenrivertek,O=goldenrivertek,L=shenyang,ST=liaoning,c=cn"  -validity 10957 -startdate 2018/01/01
#password goldenriver_hee_sign_test

echo "genarate hee sign certreq"
keytool -certreq -keystore  hee/sign_cert/hee_sign.keystore -alias hee_sign -file hee/sign_cert/hee_sign.csr
#password goldenriver_hee_sign_test

echo "genarate hee sign crt"
cd hee/second_ca/
openssl ca -days 10950 -in ../sign_cert/hee_sign.csr -days 10957 -out ../sign_cert/hee_sign.crt -cert hee_second_ca.crt -keyfile hee_second_ca.key
cd ../../

############### HEE SIGN OCSP ###########################
echo "genarate hee sign ocsp data"
mkdir -p hee/sign_cert/ocsp/
openssl ocsp -issuer hee/second_ca/hee_second_ca.crt -cert hee/sign_cert/hee_sign.crt -reqout hee/sign_cert/ocsp/hee_sign_req.der
openssl ocsp -index hee/second_ca/demoCA/index.txt -rsigner hee/second_ca/hee_second_ca.crt -rkey hee/second_ca/hee_second_ca.key -CA hee/second_ca/hee_second_ca.crt -reqin hee/sign_cert/ocsp/hee_sign_req.der -respout hee/sign_cert/ocsp/hee_sign_resp.der -text

echo "check hee sign ocsp data"
cat hee/second_ca/hee_second_ca.crt hee/root_ca/hee_root_ca.crt >hee/second_ca/hee_second_data.pem
############### check hee sign ocsp data ###########################
openssl ocsp -respin hee/sign_cert/ocsp/hee_sign_resp.der -CAfile hee/second_ca/hee_second_data.pem
#need response verify OK
#export pem
java -jar tools/CertTool.jar export_pem_cert --in-file-path hee/sign_cert/hee_sign.crt --out-file-path hee/sign_cert/hee_sign.pem
#export pem
java -jar tools/CertTool.jar ocsp_resp_to_pem --in-file-path hee/sign_cert/ocsp/hee_sign_resp.der --out-file-path hee/sign_cert/ocsp/hee_sign_resp.pem

#keystore export key
echo "export hee sign private key pem"
java -jar tools/CertTool.jar keystore_to_key --in-file-path hee/sign_cert/hee_sign.keystore --out-path hee/sign_cert/ --alias hee_sign --keystore-password goldenriver_hee_sign_test --key-password goldenriver_hee_sign_test
#hee private to jwk
echo "export hee sign private key jwk"
openssl rsa -in hee/sign_cert/private_key.pem -out hee/sign_cert/hee_sign_private_key_pkcs1.pem
pem-jwk hee/sign_cert/hee_sign_private_key_pkcs1.pem > hee/sign_cert/hee_sign_private_key.jwk
