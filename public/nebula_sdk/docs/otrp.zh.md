# Nebula SDK OTrP                {#sdk_otrp_zh}
本文主要介绍通过otrp协议安全安装HA。
## 通过OTrP调试HA
使用debug秘钥快速调试运行HA请参考[在各平台上运行Examples](@ref sdk_run_example_zh)文档中“使用OTrP运行HA”中的内容。
## 通过OTrP预置HA

    Nebula支持预先安装HA到HEE中。预制HA加密需要使用一对固定的非对称密钥对：“spaik”。私钥预置在HEE系统中。

  ### 预置HA主要步骤概述：
  * 生成创建安全域和安装HA等json数据文件。
  * 将生成的数据文件放置到设备中。
  * 使用`nbl_daemon -p <provision_data_folder>`命令来加载安装这些数据。
  * 安装完成后可以通过HA的ID来启动HA。

  ### 快速调试运行预置HA
  * 如果仅想利用预置方式调试运行HA可以按照如下步骤进行:
    * 编译HA，以example为例，进入 `examples/hee/libc` 目录后，运行 `make` 命令。在例子下会建立 `out` 文件夹，里面放置生成的 `.o` 和程序。
    * 执行 `make provision-run-qemu` 命令，会直接在qemu（需要提前开启qemu）上运行该HA，方便调试。在真机安卓设备上运行,确保adb可用后，使用`make provision-run-device`命令运行该HA。


  ### 使用debug秘钥预置HA
  * 编译HA，以example为例，进入 `examples/hee/libc` 目录后，运行 `make` 命令。在例子下会建立 `out` 文件夹，里面放置生成的 `.o` 和程序。
  * 执行 `make provision` 命令。在例子下的`out/provision`目录下里生成对应的预置HA所需的JSON数据。`out/provision/key_store_test` 目录下为调试秘钥生成的数据。
  * 将生成的HAjson数据推送到设备。
    * Android设备自行使用adb或者`$SDK_HOME/run/otrp/provision_tool/push_android.sh`脚本推送数据到设备。第一个参数是要推送的源文件路径。第二个参数是推送的目标路径。
    * Qemu启动可以参考[在各平台上运行Examples](@ref sdk_run_example_zh)文档中"在REE(Linux)上运行Examples"部分的内容。Qemu模拟器可使用`$SDK_HOME/run/otrp/provision_tool/push_qemu.sh`脚本推送数据到QEMU。第一个参数是要推送的源文件路径。第二个参数是推送的目标路径。
  * 安装HAjson数据。在REE的命令行中使用`nbl_daemon -p <provision_data_folder>`命令传递json文件夹路径来加载安装这些json数据。
  * 通过HAID启动已经安装的预置的HA。进入真机命令行或者QEMU的REE命令行中。执行`nbl_daemon -l <haid>` 运行已经安装的预置的HA。

  ### 使用自定义正式秘钥预置HA
  * 生成自定义正式秘钥。可以参考本文[OTrP证书和秘钥的生成](#OTrP证书和秘钥的生成)部分的内容。
  * 生成配置文件。执行`SDK_HOME/run/otrp/provision_tool/provision_create_config.sh -o <key_folder> ` -o参数设置上一步生成的秘钥证书所在的文件夹路径。根据提示输入相应的内容。配置文件会生成到秘钥证书所在的文件夹中。
  * 编译HA。这里以`$SDK_HOME/examples/hee/libc`为例。进入`$SDK_HOME/sdk/examples/hee/libc` 目录后, 运行 `make` 命令。在例子下会建立 `out` 文件夹，里面放置生成的 `.o` 和程序。
  * 生成预置json数据。执行`make provision CONFIG_FOLDER_PATH=<key_folder_path1>,<key_folder_path2>` key_folder_path为第一步生成的秘钥文件夹路径。同时编译多秘钥版本的数据，不同的秘钥目录用逗号隔开。执行后会在out/provision生成对应key_folder名字的文件夹。删除安全域及其HA的数据会生成`out/provision/delete_sd/<key_folder_name>`的文件夹中。debug数据会默认生成。
  * 将生成的HAjson数据推送到设备。
    * Android设备自行使用adb或者`$SDK_HOME/run/otrp/provision_tool/push_android.sh`脚本推送数据到设备。第一个参数是要推送的源文件路径。第二个参数是推送的目标路径。
    * Qemu启动可以参考[在各平台上运行Examples](@ref sdk_run_example_zh)文档中"在REE(Linux)上运行Examples"部分的内容。Qemu模拟器可使用`$SDK_HOME/run/otrp/provision_tool/push_qemu.sh`脚本推送数据到QEMU。第一个参数是要推送的源文件路径。第二个参数是推送的目标路径。
  * 安装HAjson数据。在REE的命令行中使用`nbl_daemon -p <provision_data_folder>`命令传递json文件夹路径来加载安装这些json数据。
  * 通过HAID启动已经安装的预置的HA。进入真机命令行或者QEMU的REE命令行中。执行`nbl_daemon -l <haid>` 运行已经安装的预置的HA。

  ### 预置工具相关脚本参数介绍：
  以上工具最终都会使用到下面提供的脚本工具。这里对脚本工具进行简要说明。
  * `provision_get_device_state.sh` 获得设备状态，协调发送证书等相关数据。
    * `--tsm_private_key_path` TSM私钥PEM格式文件路径, 用来签名数据体。
    * `--tsm_cert_path`        TSM的证书，用来验签消息。
    * `--tsm_ocsp_path`        TSM的OCSP数据，用来CA证书是否有效过期。
    * `--out_file_path`        输出文件的完整路径需要包括文件名。文件名必须是“provision_get_device_state.json”

  * `provision_create_sd.sh` 生成预置HA所需的安全域数据。
    * `--tsm_id` tsm的唯一标识符。
    * `--sp_id` SP的的唯一标识符。
    * `--hee_name` HEE的名称。
    * `--sd_name` 安全域的名称。
    * `--sp_cert_path` SP证书路径，用于校验HA的签名。
    * `--hee_cert_path` HEE证书路径,用于加密消息体。
    * `--tsm_private_key_path` TSM私钥PEM格式文件路径, 用来签名数据体。
    * `--out_file_path`       输出文件的完整路径需要包括文件名。文件名必须是“provision_create_sd.json”
    * `-h`                    显示帮助信息。

  * `provision_delete_sd.sh` 生成删除安全域数据。
    * `--tsm_id` tsm的唯一标识符。
    * `--sp_id` SP的的唯一标识符。
    * `--hee_name` HEE的名称。
    * `--sd_name` 安全域的名称。
    * `--sp_cert_path` SP证书路径，用于校验HA的签名。
    * `--hee_cert_path` HEE证书路径,用于加密消息体。
    * `--tsm_private_key_path` TSM私钥PEM格式文件路径, 用来签名数据体。
    * `--delete_ha` true:代表一同删除安全域中的HA。false:安全域中存在HA则无法删除。
    * `--out_file_path`       输出文件的完整路径需要包括文件名。文件名必须是“provision_delete_sd.json”
    * `-h`                    显示帮助信息。

  * `provision_install_ha.sh` 生成安装HA所需的数据。
    * `--tsm_id` tsm的唯一标识符。
    * `--sp_id` SP的的唯一标识符。
    * `--hee_name` HEE的名称。
    * `--sd_name` 安全域的名称。
    * `--sp_cert_path` SP证书路径，用于校验HA的签名。
    * `--hee_cert_path` HEE证书路径,用于加密消息体。
    * `--tsm_private_key_path` TSM私钥PEM格式文件路径, 用来签名数据体。
    * `--ha_id` HA的唯一识别符，如果不设置则从镜像的manifest中解析。如果两证方式都无法获得HAID则会出错。
    * `--ha_path` 使用签名工具加密签名后的HA的json数据的文件路径。
    * `--out_file_path`       输出文件的完整路径需要包括文件名。

  * `provision_create_config.sh` 创建预置配置文件.
    * `-o` 证书秘钥所在的文件夹路径。它也是配置文件的输出路径。
    * `-h` 显示帮助信息。

  * `provision_create_ha_by_config.sh` 通过配置文件生成预置HA数据.
    * `-c` 使用provision_create_config.sh脚本生成的预置HA配置文件所在的文件夹路径。
    * `-e` HA镜像或者使用签名工具加密签名后的HA的json数据的文件路径。
    * `-o` 输出文件夹路径。
    * `-h` 显示帮助信息。

  * `provision_create_sd_by_config.sh` 通过配置文件生成预置创建和更新安全域数据.
    * `-c` 使用provision_create_config.sh脚本生成的预置HA配置文件所在的文件夹路径。
    * `--sd_privilege_file` 安全域权限配置文件的路径
    * `-o` 输出文件夹路径。
    * `-h` 显示帮助信息。

  * `provision_get_ha.sh` 从预置安装HA的json数据中提取HA。
    * `--in_file_path` 预置安装HA的json文件。
    * `--spaik_private_key_pem_path` SPAIK的私钥pkcs8 pem数据文件路径。
    * `--out_file_path` 输出HA的文件路径，包含文件名。
    * `-h`              显示帮助信息。

  * `provision_convert_debug.sh` 根据release版预置安装HA的json数据，生成原始的HA镜像和debug版预置数据。
    * `--in_file_path` 预置安装HA的json文件。
    * `--spaik_private_key_pem_path` SPAIK的私钥pkcs8 pem数据文件路径。
    * `--out_file_folder` 生成文件所在的文件夹路径。
    * `-h`              显示帮助信息。

  * `push_android.sh` 推送指定文件到真机设备中。
    * 第一个参数是要推送的源文件路径。
    * 第二个参数是推送的目标路径。

  * `push_qemu` 推送指定文件到QEMU中。
    * 第一个参数是要推送的源文件路径。
    * 第二个参数是推送的目标路径。

  * `provision_install_data.sh` 将已经推送的设备中的数据进行安装。程序会按照顺序先安装provision_get_device_state.json,provision_delete_sd.json,provision_create_sd.json.之后是其他的install ha的json数据。
    * `--data_path` 数据所在的文件夹路径。
    * `-d` (可选)如果设置此选项则代表在真机上运行。否则代表在QEMU上执行。
    * `-h` 显示帮助信息。

## OTrP证书和秘钥的生成
  * 证书和秘钥的生成工具在`$SDK_HOME/run/otrp/key_store_tool`目录下。依次调用下面的脚本生成对应实体的秘钥和证书文件。`export_key.sh`脚本除外。
  * `genarate_hee.sh`生成HEE所需的秘钥和证书。使用方式请参考`readme_hee`文件。
  * `genarate_sp.sh` 生成SP所需的秘钥和证书。使用方式请参考`readme_sp`文件。
  * `genarate_tsm.sh`生成TSM所需的秘钥和证书。使用方式请参考`readme_tsm`文件。
  * `genarate_tfw.sh`生成TFW所需的秘钥和证书。使用方式请参考`readme_tfw`文件。
  * `genarate_spaik.sh`生成预置HA所需的SPAIK数据。使用方式请参考`readme_spaik`文件。

  * `export_key.sh` 用于从已经生成好的秘钥和证书文件夹提取需要注入到设备中的秘钥和证书。

## OTrP调试工具的详细介绍
* 使用debug秘钥快速调试运行HA请参考[在各平台上运行Examples](@ref sdk_run_example_zh)文档中“使用OTrP运行HA”中的内容。下面介绍的内容适用于使用debug秘钥逐步创建自定义的安全域安装HA等高级操作。
* 调试工具在 `$SDK_HOME/run/otrp/debug_tool`目录下。使用调试工具可以创建或者删除调试用的安全域和HA。运行已经安装到安全域中的HA。查询创建过的安全域和安装过的HA等功能。
* 调试工具详细参数介绍如下：（可以参考当前目录下的secmgr_test.sh测试脚本中的内容）

  * secmgr_init.sh 首先执行，初始化调试所需环境（只需初始化一次,QEMU每次启动都需要执行一次）。此功能会在真机或者qemu上创建所需的文件目录。推送nbl_daemont到设备中。如果执行过本操作，后续其他脚本可以通过使用“-l”参数，防止上述初始化重复执行。以加快脚本的运行速度。参数：
    * -d 表示在真机上运行，否则在qemu上执行命令。
    * -v 显示完整日志。
    * -h 显示帮助信息。

  * secmgr_create_sd.sh 创建一个测试安全域。参数：
    * -d 表示在真机上运行，否则在qemu上执行命令。
    * -v 显示完整日志。
    * -h 显示帮助信息。
    * -l 不再重复执行调试工具初始化所需工作。
    * -f 安全域不存在就创建安全员域，存在则不创建。
    * --sd_name 创建的安全域的名称。

  * secmgr_install_ha.sh 在创建的安全域中安装HA。之前一定要先创建好HA所需的安全域。
    * -d 表示在真机上运行，否则在qemu上执行命令。
    * -v 显示完整日志。
    * -h 显示帮助信息。
    * -l 不再重复执行调试工具初始化所需工作。
    * -f HA不存在就安装，否则就更新HA。
    * --sd_name HA要安装到的安域的名字。
    * --ha_id (可选)HA的ID。HA的唯一识别符。安装，删除和启动HA时所需。如果不设置，将从HA镜像中解析获得。
    * --ha_path HA镜像路径。

  * secmgr_ls.sh 查询已经创建过的安全域和安装过的HA。其格式参考如下：
    ```
    sdCnt:2
    SD(debug_sd)
        haid:905bdb85-2dc5-429d-c9-52-72-35-42-11-11-01, version:1, hash:132...ddd
        haid:905bdb85-2dc5-429d-c9-52-72-35-42-11-11-02, version:1, hash:154...13c
    SD(debug_sd)
        haid:905bdb85-2dc5-429d-c9-52-72-35-42-11-11-03, version:1, hash:a87...9cd
    ```
    参数：
    * -d 表示在真机上运行，否则在qemu上执行命令。
    * -v 显示完整日志。
    * -h 显示帮助信息。
    * -l 不再重复执行调试工具初始化所需工作。

  * secmgr_run_ha.sh 通过HAID运行安装过的一个HA。qemu平台LOG会显示在HEE 终端中。真机平台会显示在设备的串口LOG中。参数：
    * -d 表示在真机上运行，否则在qemu上执行命令。
    * -v 显示完整日志。
    * -h 显示帮助信息。
    * --ha_id HA的ID。HA的唯一识别符。要启动的HA的ID。

  * secmgr_delete_ha.sh 从指定调试安全域中删除已经安装过的HA。参数：
    * -d 表示在真机上运行，否则在qemu上执行命令。
    * -v 显示完整日志。
    * -h 显示帮助信息。
    * -l 不再重复执行调试工具初始化所需工作。
    * --sd_name 要删除的HA所在安全员域的名称。
    * --ha_id HA的ID。HA的唯一识别符。要删除的HA的ID。

  * secmgr_delete_sd.sh  删除指定调试安全域包括安全域中的HA。参数：
    * -d 表示在真机上运行，否则在qemu上执行命令。
    * -v 显示完整日志。
    * -h 显示帮助信息。
    * -l 不再重复执行调试工具初始化所需工作。
    * --sd_name 要删除的安全员域的名称。

  * secmgr_clean.sh  删除所有调试安全域以及安全域中的HA。参数：
    * -d 表示在真机上运行，否则在qemu上执行命令。
    * -v 显示完整日志。
    * -h 显示帮助信息。
    * -l 不再重复执行调试工具初始化所需工作。

  * secmgr_debug.sh  创建安全域然后安装运行HA。安全域如果存在则不会重新创建。HA如果已经存在则会覆盖。参数：
    * --ha_path HA镜像路径。
    * --ha_id (可选)HA的ID。HA的唯一识别符。如果不设定此参数，将自动生成一个HA的ID。
    * --sd_name (可选)安全域的名称。如果不设定则使用默认安全域。
    * -d 表示在真机上运行，否则在qemu上执行命令。
    * -h 显示帮助信息。
    * -v 显示完整日志。

  * secmgr_test.sh 测试脚本。用于测试以上功能脚本是否正常。也可作为一个调试脚本使用示例。在`$SDK_HOME/examples/hee/libc`目录下执行make。 然后执行本脚本。脚本会将libc工程作为测试HA镜像进行测试。如果功能正常则不会出现错误日志。
    * -d 表示在真机上运行，否则在qemu上执行命令。
    * -v 显示完整日志。
    * -h 显示帮助信息。

## SP(service provider)对HA进行加密签名
  * SP提供的HA需要SP使用自己的秘钥进行签名。使用spaik的公钥加密。生成的json数据提供给TSM（Trusted Service Manager）管理。使用SDK下的`$SDK_HOME/run/otrp/sign_tool/secmgr_sign_tool.sh`脚本可以生成HA签名加密后的数据。参数：
    * `--ha_path`	                需要签名的HA文件路径。
    * `--sp_private_key_pem_path`   SP的PEM格式的私钥文件路径。
    * `--spaik_public_key_path`     SPAIK的公钥文件路径。
    * `--out_file`                  签名后的输出文件的完整路径需要包括文件名。
    * `-h`	                        显示帮助信息。

## 从HA的json签名加密数据中解析配置信息
  * 使用SDK下的`$SDK_HOME/run/otrp/sign_tool/secmgr_get_config.sh`脚本可以解析HA签名加密过的数据中的配置信息。HA可以在代码中增加配置信息。配置信息包括HA的ID，自动启动的标志位等。在HA进行安装的时会使用其配置信息。
    * `--enc_ha_path` 加密后的HA的数据路径。
    * `--out_file` 输出的解析后的manifest配置信息的json文件路径。
    * `-h` 显示帮助。

## 安全域权限

Security Domain Privilege 是”安全域权限“，它允许设定安全域(Security Domain)的权限，这个权限会影响到SD内所有的TA/HA的权限。

比如设定SD的NEW_PROCESS是false，那么SD内的HA/TA的manifest.c中即使设置了NEW_PROCESS为true也无法创建新进程。

如果没有设置SD privilege也是允许的，那么SD中将使用缺省值。

设置项目，以及缺省值见下表：
  * T/F: true or false, "=ta pri": 等于ta pri的值。
  * def=?: 当没有设置权限时的缺省值是什么。

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

SD的privilege文件的格式是JSON，参考"examples/security_domain/auto-start_allow/sd.privilege.allow"。

在2020年1月14日之前的旧版本的SDK中。如果修改了SD privilege，重新安装TA是无法更新SD的，可以用 make delete-qemu-sd 或者 make-device-sd 来删除已经安装的SD，
然后重新安装TA就可以了。