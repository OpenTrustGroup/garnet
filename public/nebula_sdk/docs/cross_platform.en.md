# Vtrust cross platform                {#vtrust_cross_platform_en}
This Documentation introduces the use of cross-platform products
## Run example
1. Build `$SDK_HOME/Examples` : Change current directory to `$SDK_HOME/Examples`. Run `make` command.The folder `$SDK_HOME/Examples/*/out` will be created and contains intermediate files(*.o) and target program(*.so/*.a/ELF/FAR).
2. Start QEMU: Change current directory to `$SDK_HOME`. Run `run/run-qemu.sh`.
    **[During the first run, SSH will be start and ask users to confirm keys, please input "yes".]**.
   `run/run-qemu.sh` will start a virtual network for QEMU and will use "sudo" command, so please input current user's password.
    Because sudo will remember this input, so this password will be required to input at a short time.
3. Running the `debug.sh` file in the CA directory will automatically run the sample CA and TA. If the APP runs successfully, it will display "Hello World" in the TEE log. If you need to run on a real device, you can use the `debug.sh -d`.

## Specific steps for cross-platform APP development
  debug app use the `debug.sh` script. The following is an introduction to the detailed development steps.

#### 1.Write micropython scripts.
  Example script directory: `$SDK_HOME/Examples/vtrust_cp/vtrust_cp_ca/data/script.py`

  Cross-platform mictopython API description:
  mp_obj_t cp_get_data()
  The parameters set when calling nbl_run_ta from CA can be obtained through the `vtrust.cp.get_data()` function of the micropython script.

  mp_obj_t cp_get_data_size()
  This function `vtrust.cp.get_data_size()` can get the length of data set when nbl_run_ta is called in CA.

  Please refer to the official micropython documentation and sample code for more information.

#### 2.Generate keypair
  A pair of asymmetric secret keys generated in the release environment are mainly used for the signature and verification of the micropython script in the previous step.
  In the debugging environment, the SDK has prepared a set of debugging keys. It is not necessary to generate a secret key to use the debug.sh script.

  1.Generate keypair
  `$SDK_HOME/run/vtrust_cp/key_tool/genarate_sign_key.sh`script is used to generate a pair of asymmetric keys. Please refer to the readme document in the directory for more information.
  The SDK has prepared a set of debugging keys`$SDK_HOME/run/vtrust_cp/key_tool/debug_key`.

  2.Provide the generated public key key_sign_publickey.pem to Goldenriver Technology to obtain a signed public key data.
  The SDK has prepared a set of debugging keys`$SDK_HOME/Examples/vtrust_cp/vtrust_cp_ca/data/sign_publickey_data`

#### 3.Sign script
  Sign the micropython script. IIt can guarantee that the running script is correct.
  The debug.sh script in the debugging environment can automatically use the debug key to sign the script.
  The script signature tool is in`$SDK_HOME/run/vtrust_cp/sign_tool/vtrust_sign_script.sh`
  The specific parameters are as follows:
  `--data_path` The path of the script file that needs to be signed
  `--private_key_pem_path` The private key required for signing. Generated in the previous step.
  `--out_file` The output path of the file after signing.
  `-h` help
  The signature data in the example is `$SDK_HOME/Examples/vtrust_cp/vtrust_cp_ca/data/script_sign`

#### 4.Coding CA
  Cidubg CA to load micropython scripts, signatures and related secret keys, etc. Call the nbl_run_ta function to pass the corresponding parameters. Please refer to example `$SDK_HOME/Examples/vtrust_cp/vtrust_cp_ca`。

  Interface Description：
typedef struct nbl_cp_req_s
{
  char *mp_script;            //The micropython script to be executed
  void* mp_param;             //Parameters required in micropython script execution
  size_t mp_param_size;       //The length of the parameter required in the execution of the micropython script
  void *mp_script_sign;       //Signature of micropython script
  size_t mp_script_sign_size; //The length of the micropython script signature
  void *sign_publickey;       //The micropython script signature public key. The public key needs to be signed by the root certificate
  size_t sign_publickey_size; //The length of the micropython script signature public key

} __attribute__ ((packed)) nbl_cp_req_t;

typedef struct nbl_cp_res_s
{
   int32_t status;            //Return status of execution result
   void* data;                //Return data
   size_t data_size;          //The length of the returned data
} __attribute__ ((packed)) nbl_cp_res_t;

//Run the security APP and execute the micropython script, req_data is the request data, res_data is the return data.
int nbl_run_ta(nbl_cp_req_t *req_data, nbl_cp_res_t *res_data);

#### 5.Run TA
  TA image file in `$SDK_HOME/Examples/vtrust_cp/vtrust_cp_ca/ta_image`.

  You can use the provision_create_ha_by_config.sh script to run on the simulator. The -q parameter is required. Examples are as follows:
    `$SDK_PATH/run/otrp/provision_tool/provision_create_ha_by_config.sh -c $SDK_PATH/run/otrp/key_store_test -e $EXAMPLE_PATH/ta_image/vtrust_cp_ta -o $EXAMPLE_PATH/ta_json -q ""`

  The provision_create_ha_by_config.sh script can also be used to run on a real device, which requires the -d parameter, example parameter
    `$SDK_PATH/run/otrp/provision_tool/provision_create_ha_by_config.sh -c $SDK_PATH/run/otrp/key_store_test -e $EXAMPLE_PATH/ta_image/vtrust_cp_ta -o $EXAMPLE_PATH/ta_json -d`

  provision_create_ha_by_config. shrelated parameters introduction:
    * `provision_create_ha_by_config.sh` Generate provision HA data through configuration files. And you can choose whether to run.
    * `-c` Provision config folder path.
    * `-e` Ha/TA image path or encrypted HA json data.
    * `-o` Output path.
    * `-q` \<ssh ip\>(Optional) Run on qemu. The parameter value is ssh_ip. When the parameter is set to "" ssh ip is ${SSH_IP}.
    * `-d` (Optional) Run on device by adb. Only one of parameter -q and -d can be selected.
    * `-h` show help.
  If you are using a custom secret key to encrypt and run TA, you can refer to [Nebula SDK install APP with OTrP](@ref sdk_otrp_en)。

#### 5.Run CA
  Compile with the `make` command in the CA directory `$SDK_HOME/Examples/vtrust_cp/vtrust_cp_ca`
  Run CA on the qemu simulator.Use the `make run` command in the `$SDK_HOME/Examples/vtrust_cp/vtrust_cp_ca` directory.
  Run CA on real device. Use the `make run-ndk` command in the `$SDK_HOME/Examples/vtrust_cp/vtrust_cp_ca` directory.
  After CA runs, TA will be called, and the micropython script will run in TA. A successful example will display "Hello World" in the TEE log.
