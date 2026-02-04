#!/bin/bash
################ TSM SIGN CERT #########################
echo "genarate tsm sign genkeypair"
mkdir -p tsm/sign_cert2/
keytool -genkeypair -keyalg RSA -keysize 2048 -sigalg SHA256withRSA -alias tsm_sign -keystore  tsm/sign_cert2/tsm_sign.keystore -dname "CN=gr.tsm.sign2,OU=goldenrivertek,O=goldenrivertek,L=shenyang,ST=liaoning,c=cn"  -validity 10957 -startdate 2018/01/01
#password goldenriver_tsm_sign_test2

echo "genarate tsm sign certreq"
keytool -certreq -keystore  tsm/sign_cert2/tsm_sign.keystore -alias tsm_sign -file tsm/sign_cert2/tsm_sign.csr
#password goldenriver_tsm_sign_test2

echo "genarate tsm sign crt"
cd tsm/root_ca/
openssl ca -days 10950 -extensions SAN -config <(cat /usr/lib/ssl/openssl.cnf <(printf "[SAN]\nsubjectAltName=DNS:test2.tsm.goldenrivertek.com")) -in ../sign_cert2/tsm_sign.csr -out ../sign_cert2/tsm_sign.crt -cert tsm_root_ca.crt -keyfile tsm_root_ca.key
#password goldenriver_tsm_root_ca_2018
cd ../../
#/usr/lib/ssl/openssl.cnf

############### TSM SIGN OCSP ###########################
echo "genarate tsm sign ocsp data"
mkdir -p tsm/sign_cert2/ocsp/
openssl ocsp -issuer tsm/root_ca/tsm_root_ca.crt -cert tsm/sign_cert2/tsm_sign.crt -reqout tsm/sign_cert2/ocsp/tsm_sign_req.der
openssl ocsp -index tsm/root_ca/demoCA/index.txt -rsigner tsm/root_ca/tsm_root_ca.crt -rkey tsm/root_ca/tsm_root_ca.key -CA tsm/root_ca/tsm_root_ca.crt -reqin tsm/sign_cert2/ocsp/tsm_sign_req.der -respout tsm/sign_cert2/ocsp/tsm_sign_resp.der
#password goldenriver_tsm_root_ca_2018

echo "check tsm sign ocsp data"
############### check tsm sign ocsp data ###########################
openssl ocsp -respin tsm/sign_cert2/ocsp/tsm_sign_resp.der -CAfile tsm/root_ca/tsm_root_ca.crt
#need response verify OK
#export pem
java -jar tools/CertTool.jar export_pem_cert --in-file-path tsm/sign_cert2/tsm_sign.crt --out-file-path tsm/sign_cert2/tsm_sign.pem
#export pem
java -jar tools/CertTool.jar ocsp_resp_to_pem --in-file-path tsm/sign_cert2/ocsp/tsm_sign_resp.der --out-file-path tsm/sign_cert2/ocsp/tsm_sign_resp.pem
