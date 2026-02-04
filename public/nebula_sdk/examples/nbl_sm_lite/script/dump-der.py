# Copyright 2020 GoldenRiver Technology Co., Ltd. All rights reserved.

# Convert "pri.der" including private key and "pub.der" including public key into c source.
# Cannot get bits of actual key by Python, so only export bytes count.

from __future__ import print_function
from asn1crypto.keys import ECPrivateKey
from asn1crypto.keys import PublicKeyInfo

def to_hex_str(name, array):
    # translate to hex string
    pk_hex = format(array, '064x')

    # split by pairs of characters
    hex_bytes = ["0x" + pk_hex[i:i + 2] for i in range(0, len(pk_hex), 2)]

    return "const uint8_t " + name + "[32] = {" + ", ".join(hex_bytes) + "};"

# private key
def pk_to_c_array(name, pk_der):
    # parse der format
    pk = ECPrivateKey.load(pk_der)

    # extract private key
    pk_native = pk['private_key'].native

    # make string C array declaration
    return to_hex_str(name, pk_native)

# public key
def public_to_c_array(name_x, name_y, der):
    key = PublicKeyInfo.load(der)
    print("//SM2 Public key: algorithm=%s, curve=%s, key size=%d(bytes)" % (key.algorithm, key.curve[1], key.byte_size))

    public_key = key['public_key']

    x,y = public_key.to_coords()
    str_x = to_hex_str(name_x, x)
    str_y = to_hex_str(name_y, y)

    return str_x + "\n" + str_y

with open("pri.der", "rb") as f:
    print(pk_to_c_array("private_key", f.read()))

with open("pub.der", "rb") as f:
    print(public_to_c_array("public_key_x", "public_key_y", f.read()))
