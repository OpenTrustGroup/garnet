# Nebula SDK install APP with OTrP                {#sdk_otrp_en}
This document introduces how to install HA through OTrP protocol.
## Debug HA with OTrP
Use debug key to quickly debug HA, please refer to "Run HA with OTrP" part of [Run Examples document](@ref sdk_run_example_en)
## Provision HA with OTrP

  Nebula supports provision HA into HEE. Provision HA requires a pair of fixed asymmetric key pairs: "spaik" for encryption.

  ### There are several steps in privision HA:
    * Generate data such as creating a security domain and installing HA.
    * Push the generated data into the device.
    * Use the `nbl_daemon -p <provision_data_folder>` command to load and install these data.
    * After the installation is complete, HA can be started by the ID of the HA.

  ### Debug provision HA
  * If you want to use the provision mode to debug HA, you can follow the steps:
      * Compile HA. Here is an example of `$SDK_HOME/examples/hee/libc`. Go to the `$SDK_HOME/sdk/examples/hee/libc` directory. Run the `make` command. The folder `out` will be created and contains intermediate files(*.o) and target program.
      * Executing the `make provision-run-qemu` command will run the HA on qemu (qemu needs to be turned on).If you want to run HA on a real Android device.  You must make sure adb is available and then use the `make provision-run-device` command to run the HA.

  ### Use debug key to provision HA
  * Compile HA. Here is an example of `$SDK_HOME/examples/hee/libc`. Go to the `$SDK_HOME/sdk/examples/hee/libc` directory. Run the `make` command. The folder `out` will be created and contains intermediate files(*.o) and target program.
  * Generate provision data. Run the `make provision` command. In the `out/provision` directory under the example, the JSON data for the provision HA is generated. Data generated for the debug key in the "key_store_test" directory.
  * Push the generated HA json data to the device.
    * Push data to android device. Please use adb or `$SDK_HOME/run/otrp/provision_tool/push_android.sh` script. The first parameter is the source file path to be pushed.The second parameter is the target path.
    * The QEMU emulator can use the `$SDK_HOME/run/otrp/provision_tool/push_qemu.sh` script. The first parameter is the source file path to be pushed.The second parameter is the target path.
  * Install HA. Use the `nbl_daemon -p <provision_data_folder>` command on the REE command line to load and install the json data.
  * Run HA. Enter the real machine command line or QEMU's REE command line. Execute the command `nbl_daemon -l <haid>` to run the installed HA.

  ### Provision HA with custom release key
  * Generate a custom release key. You can refer to the content of [OTrP generate certificates and keys] (#OTrP generate certificates and keys) in this document.
  * Generate a release configuration file, please execute `SDK_HOME/run/otrp/provision_tool/provision_create_config.sh -o <key_folder> `. The "-o" parameter sets the folder path where the key certificate generated in the previous step. Follow the prompts to enter the corresponding content.The configuration file will be generated in the same folder as the key certificate.
  * Compile HA. Here is an example of `$SDK_HOME/examples/hee/libc`. Go to the `$SDK_HOME/sdk/examples/hee/libc` directory. Run the `make` command. The folder `out` will be created and contains intermediate files(*.o) and target program.
  * Generate provision data. Run command `make provision CONFIG_FOLDER_PATH=<key_folder_path1>,<key_folder_path2>` `key_folder_path` is the key folder path generated in the first step. Compile the data of multiple key versions at the same time, the different key directories are separated by commas. It will generate a folder corresponding to the key_folder name in the out/provision directory. Deleting the security domain and its HA's JSON data will be generated in `the out/provision/delete_sd/<key_folder_name>` folder.
  * Push the generated HA json data to the device.
    * Push data to android device. Please use adb or `$SDK_HOME/run/otrp/provision_tool/push_android.sh` script. The first parameter is the source file path to be pushed.The second parameter is the target path.
    * The QEMU emulator can use the `$SDK_HOME/run/otrp/provision_tool/push_qemu.sh` script. The first parameter is the source file path to be pushed.The second parameter is the target path.
  * Install HA. Use the `nbl_daemon -p <provision_data_folder>` command on the REE command line to load and install the json data.
  * Run HA. Enter the real machine command line or QEMU's REE command line. Execute the command `nbl_daemon -l <haid>` to run the installed HA.

  ### Introduction to provision script parameters
  The above tools will use the script tool provided below. Here is a brief description of the script tool.
  * `provision_get_device_state.sh` Get device status, send certificates and other related data.
    * `--tsm_private_key_path` tsm private key pem path, it is used to sign the message data.
    * `--tsm_cert_path`        tsm cert path.This certificate will be sent to HEE. HEE will use this certificate to encrypt the returned data.
    * `--tsm_ocsp_path`        TSM OCSP data, used to validate the expiration of the CA certificate.
    * `--out_file_path`        The full path to the output file needs to include the file name. The file name must be "provision_get_device_state.json"

  * `provision_create_sd.sh` Generate a data file that create a secure domain.
    * `--tsm_id` TSM/TAM identifier.
    * `--sp_id` Service provider identifier.
    * `--hee_name` HEE name.
    * `--sd_name` Security domain name.
    * `--sp_cert_path` SP cert path. It is used to verify the signature of the HA.
    * `--hee_cert_path` HEE cert path, it is used to encrypt the message data.
    * `--tsm_private_key_path` tsm private key pem path, it is used to sign the message data.
    * `--out_file_path`       The full path to the output file needs to include the file name. The file name must be "provision_create_sd.json".
    * `-h`                    Show help.

  * `provision_delete_sd.sh` Generate a data file that delete a secure domain.
    * `--tsm_id` TSM/TAM identifier.
    * `--sp_id` Service provider identifier.
    * `--hee_name` HEE name.
    * `--sd_name` Security domain name.
    * `--sp_cert_path` SP cert path. It is used to verify the signature of the HA.
    * `--hee_cert_path` HEE cert path, it is used to encrypt the message data.
    * `--tsm_private_key_path` tsm private key pem path, it is used to sign the message data.
    * `--delete_ha` true: HA will be deleted in the security domain. false: Security domain can be deleted when there is no HA in the security domain.
    * `--out_file_path`       The full path to the output file needs to include the file name. The file name must be "provision_delete_sd.json"
    * `-h`                    Show help.

  * `provision_install_ha.sh` Generate a data file that install ha.
    * `--tsm_id` TSM/TAM identifier.
    * `--sp_id` Service provider identifier.
    * `--hee_name` HEE name.
    * `--sd_name` Security domain name.
    * `--sp_cert_path` SP cert path. It is used to verify the signature of the HA.
    * `--hee_cert_path` HEE cert path, it is used to encrypt the message data.
    * `--tsm_private_key_path` tsm private key pem path, it is used to sign the message data.
    * `--ha_id` HA identifier (optional).If this parameter is empty, the id will be obtained from the manifest of the HA image. If there is no such option in the manifest, an error will be reported.
    * `--ha_path` Path of the enc HA json data.Generate this data using the signature tool.
    * `--out_file_path`       The full path to the output file needs to include the file name.

  * `provision_create_config.sh` Create a provision configuration file.
    * `-o` The path to the certificate and key. It is also the output path of the configuration file.
    * `-h` Show help.

  * `provision_create_ha_by_config.sh` Generate provision data through a configuration file.
    * `-c` Provision config folder path.
    * `-e` Ha image path or encrypted HA json data.
    * `-o` Output path.
    * `-h` show help.

  * `provision_create_sd_by_config.sh` Generate provision create and update security domain data from the configuration file.
    * `-c` Provision config folder path.
    * `--sd_privilege_file` Configuration file path of security domain privilege.
    * `-o` Output path.
    * `-h` show help.

  * `provision_get_ha.sh` Extract HA from the provision json data of the installation HA.
    * `--in_file_path` Provision install HA json file path.
    * `--spaik_private_key_pem_path` Spaik private pem pkcs8 key path.
    * `--out_file_path` Out ha file path. Need to include the file name in the path.
    * `-h`              Show help.

  * `provision_convert_debug.sh` Generate the HA image and debug provision data according to the release json data of the installation HA.
    * `--in_file_path` rovision install HA json file path.
    * `--spaik_private_key_pem_path` Spaik private pem pkcs8 key path.
    * `--out_file_folder` Out folder path.
    * `-h`              Show help.

  * `push_android.sh` Push data files into real devices.
    * The first parameter is the source file path to be pushed.
    * The second parameter is the target path.

  * `push_qemu` Push data files into QEMU.
    * The first parameter is the source file path to be pushed.
    * The second parameter is the target path.

  * `provision_install_data.sh` Install the data from the already pushed device. The program will first install the provision_get_device_state.json, provision_delete_sd.json, provision_create_sd.json. and then the json data of the other install ha.
    * `--data_path` Provision data folder
    * `-d` (Optional)Run in the device when the "-d" parameter is set, otherwise run in QEMU.
    * `-h` Show help.

## OTrP generate certificates and keys
  * Tools in the`$SDK_HOME/run/otrp/key_store_tool`. Call the following script to generate the corresponding key and certificate file. The exception is the `export_key.sh` script.
  * `genarate_hee.sh` Generates the key and certificate required for HEE. Please refer to the `readme_hee` file for usage.
  * `genarate_sp.sh` Generates the key and certificate required for SP. Please refer to the `readme_sp` file for usage.
  * `genarate_tsm.sh` Generates the key and certificate required for TSM. Please refer to the `readme_tsm`file for usage.
  * `genarate_tfw.sh` Generates the key and certificate required for TFW. Please refer to the `readme_tfw`file for usage.
  * `genarate_spaik.sh` Generate SPAIK data required for provision HA. Please refer to the `readme_spaik` file for usage.

  * `export_key.sh` Used to extract the keys and certificates that need to be injected into the device from the generated keys and certificate folders.

## Detailed introduction of OTrP debugging tool
* Use debug key to quickly debug HA, please refer to "Run HA with OTrP" part of [Run Examples document](@ref sdk_run_example_en). The content described below is suitable for advanced operations such as using the debug key to create a custom security domain to install HA.
* The debugging tools are in `$SDK_HOME/run/otrp/debug_tool`.The debugging tools have following functions:
  * Create or delete security domains and HA.
  * Run the HA that has been installed into the security domain.
  * Query the created security domain
  * Install HA and so on.
* The detailed parameters of the debugging tool are introduced as follows: (You can refer to the content of the secmgr_test.sh test script in the current directory)

  * `secmgr_init.sh` Initialize the environment required for debugging. Only need to be executed once. (QEMU platform needs to be executed once after each boot) Script will create the necessary folders, push the loader and agent to the device/qemu.
    * `-d` Run in the device when the "-d" parameter is set, otherwise run in QEMU.
    * `-v` Full log output.
    * `-h` Show help.

  * `secmgr_create_sd.sh` Create a security domain.
    * `-d` Run in the device when the "-d" parameter is set, otherwise run in QEMU.
    * `-v` Full log output.
    * `-h` Show help.
    * `-l` If the init command(secmgr_init.sh) has been executed. Use "-l" to omit the initialization step. The script will run faster.
    * `-f` If the security domain exists, it will not be created otherwise the security domain will be created.
    * `--sd_name` Security domain name.

  * `secmgr_install_ha.sh` Install ha to the security domain.
    * `-d` Run in the device when the "-d" parameter is set, otherwise run in QEMU.
    * `-v` Full log output.
    * `-h` Show help.
    * `-l` If the init command(secmgr_init.sh) has been executed. Use "-l" to omit the initialization step. The script will run faster.
    * `-f` Install HA if HA does not exist or update HA.
    * `--sd_name` The name of the security domain that ha will be installed in.
    * `--ha_id` (Optional)HA identifier.If this parameter is not set, it will be parsed from the image of HA..
    * `--ha_path` HA image path.

  * `secmgr_ls.sh` Query the security domain and ha info that has been created. Message information：
    ```
    sdCnt:2
    SD(debug_sd)
        haid:905bdb85-2dc5-429d-c9-52-72-35-42-11-11-01, version:1, hash:132...ddd
        haid:905bdb85-2dc5-429d-c9-52-72-35-42-11-11-02, version:1, hash:154...13c
    SD(debug_sd)
        haid:905bdb85-2dc5-429d-c9-52-72-35-42-11-11-03, version:1, hash:a87...9cd
    ```
    * `-d` Run in the device when the "-d" parameter is set, otherwise run in QEMU.
    * `-v` Full log output.
    * `-h` Show help.
    * `-l` If the init command(secmgr_init.sh) has been executed. Use "-l" to omit the initialization step. The script will run faster.

  * `secmgr_run_ha.sh` Run ha by ha id.
    * `-d` Run in the device when the "-d" parameter is set, otherwise run in QEMU.
    * `-v` Full log output.
    * `-h` Show help.
    * `--ha_id` HA identifier.

  * `secmgr_delete_ha.sh` Delete HA from the security domain
    * `-d` Run in the device when the "-d" parameter is set, otherwise run in QEMU.
    * `-v` Full log output.
    * `-h` Show help.
    * `-l` If the init command(secmgr_init.sh) has been executed. Use "-l" to omit the initialization step. The script will run faster.
    * `--sd_name` The name of the security domain
    * `--ha_id` HA identifier.

  * `secmgr_delete_sd.sh`  Delete security domain.
    * `-d` Run in the device when the "-d" parameter is set, otherwise run in QEMU.
    * `-v` Full log output.
    * `-h` Show help.
    * `-l` If the init command(secmgr_init.sh) has been executed. Use "-l" to omit the initialization step. The script will run faster.
    * `--sd_name` The name of the security domain

  * `secmgr_clean.sh` Delete all security domain and HA. HA in the secure domain will also be deleted together.
    * `-d` Run in the device when the "-d" parameter is set, otherwise run in QEMU.
    * `-v` Full log output.
    * `-h` Show help.
    * `-l` If the init command(secmgr_init.sh) has been executed. Use "-l" to omit the initialization step. The script will run faster.

  * `secmgr_debug.sh` It is used to quickly install ha into the device/qemu for debugging. Create a security domain and ha will be installed in it. HA will overwrite if it already exists. It will be started after the installation is complete.
    * `--ha_path` Ha image path.
    * `--ha_id` (optional)Ha identifier. An HA ID will be automatically generated if this parameter is empty.
    * `--sd_name` (optional)The name of the security domain.Use the default security domain if this parameter is empty.
    * `-d` Run in the device when the "-d" parameter is set, otherwise run in QEMU.
    * `-h` Show help.
    * `-v` Full log output.

  * `secmgr_test.sh` Test script.Used to test other secmgr scripts.
    * `-d` Run in the device when the "-d" parameter is set, otherwise run in QEMU.
    * `-v` Full log output.
    * `-h` Show help.

## SP(service provider)Encrypt and sign the HA
  * The HA provided by the SP requires to sign with private key. Encrypt HA with spaik's public key. The signed HA json data is provided to TSM (Trusted Service Manager). It can be generated using the `$SDK_HOME/run/otrp/sign_tool/secmgr_sign_tool.sh` script in the SDK.
    * `--ha_path`	                  Ha image path.
    * `--sp_private_key_pem_path`   SP private key pem file path.
    * `--spaik_public_key_path`     SPAIK public key file path.
    * `--out_file`                  The full path and name of the output ha file.
    * `-h`	                      Show help.

## Parsing configuration information from the HA‘s json signature data
  * Use the `$SDK_HOME/run/otrp/sign_tool/secmgr_get_config.sh` script under the SDK to parse the configuration information in the HA signed json data. HA can add configuration information to the code. The configuration information includes the ID of the HA, the flag of the automatic start, and the like. The configuration information is used when the HA is installed.
    * `--enc_ha_path`      Signed and encrypted HA json data.
    * `--out_file`         The full path and name of the output manifest config json file
    * `-h`                 Show help.

## Security Domain Privilege

"Security Domain Privilege" can influence privilege of TA/HA belonging to this SD.

For example: If privilege NEW_PROCESS of one SD is false, the privilege NEW_PROCESS of all TA/HA belonging to this SD will be false,
even if privilege of TA/HA is set to true.

Users can NOT set one option of SD privilege or the whole privilege file, and then SD will use default value.

Options and default value are below:
  * T/F: true or false, "=ta pri": value eqauls to "ta pri".
  * def=?: default value if not set.

| item                 | sd pri | ta pri |  result |
| --                   | --     | --     |  --     |
| NBL_APP_AUTO_START   | def=T  | def=F  |         |
|                      | T      | T/F    |  =ta pri|
|                      | F      | T/F    |  F      |
| NBL_APP_NEW_PROCESS  | def=F  | def=F  |         |
|                      | T      | T/F    |  =ta pri|
|                      | F      | T/F    |  F      |
| NBL_APP_CALL_TRUSTY_API| def=T| def=F  |         |
|                      | T      | T/F    |  =ta pri|
|                      | F      | T/F    |  F      |
| NBL_APP_TUI          | def=F  | def=F  |         |
|                      | T      | T/F    |  =ta pri|
|                      | F      | T/F    |  F      |

Format of sd privilege is JSON, refers to "examples/security_domain/auto-start_allow/sd.privilege.allow".

SDK before January 14, 2020. If change SD privilege after installed TA, cannot update SD privilege by installing TA again.
Users can "make delete-qemu-sd" or "make-device-sd" to delete installed SD privilege,
and then install TA again, the new SD privilege will be updated in Qemu or Device.