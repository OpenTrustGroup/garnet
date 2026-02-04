#!/bin/bash
basepath=$(cd `dirname $0`; pwd)

USAGE='usage: Generate provision spaik json file.\n
         --tsm_id Trusted Application Manager/Trusted Service Manager id\n
         --sp_id Service Provider id\n
         -h Show help.\n'
#debug: --tsm_id test.tsm.goldenrivertek.com --sp_id debug_spid

echo "Genarate spaik start."

# parse the arguments of scripts
ARGS=`getopt -o h --long tsm_id:,sp_id: -n 'genarate_spaik.sh' -- "$@"`
if [ $? != 0 ]; then
    echo "Arguments are error."
    exit 1
fi
#echo $ARGS

eval set -- "${ARGS}"

while true
do
    case "$1" in
        --tsm_id)
            tsm_id_p=$2
            shift 2
            ;;
        --sp_id)
            sp_id_p=$2
            shift 2
            ;;
        --)
            shift
            break
            ;;
        -h)
            help
            exit 0
            ;;
        *)
            help
            exit 1
            ;;
    esac
done

if [ -z "$tsm_id_p" ]; then
    echo "--tsm_id parameter cannot be empty"
    echo "$in_key_folder_path"
    echo -e ${USAGE}
    exit 1
fi

if [ -z "$sp_id_p" ]; then
    echo "--sp_id parameter cannot be empty"
    echo -e ${USAGE}
    exit 1
fi

export_private_key(){
if [ -z "$1" ]; then
    echo "Export key failed. File: $1 not exist."
    exit 1
fi

if [ -z "$2" ]; then
    echo "Export key failed. Out path null."
    exit 1
fi

rm -rf $2
cp $1 $2

}

rm -rf spaik
mkdir -p spaik

echo "genarate spaik private key"
openssl genrsa -out spaik/spaik_private_key.pem 2048

openssl rsa -in spaik/spaik_private_key.pem -pubout -out spaik/spaik_public_key.pem

java -jar $basepath/../provision_tool/secmgr_provision_tool.jar create_spaik --tsm_id $tsm_id_p --sp_id $sp_id_p --spaik_private_key_pem_path spaik/spaik_private_key.pem --spaik_public_key_pem_path spaik/spaik_public_key.pem --out_file_path spaik/spaik.json

openssl pkcs8 -topk8 -inform PEM -in spaik/spaik_private_key.pem -outform pem -nocrypt -out spaik/spaik_private_key_pkcs8.pem

mkdir -p $basepath/export_key
export_private_key ${basepath}/spaik/spaik.json $basepath/export_key/spaik_provision

echo "Genarate spaik finish."
