# Vtrust cross platform                {#vtrust_cross_platform_zh}
本文主要介绍跨平台产品的使用
## 运行example
1. 编译CA 进入`$SDK_HOME/Examples/vtrust_cp/vtrust_cp_ca`目录后，运行 make 命令。
   会建立 `out` 文件夹，里面放置生成的 `.o` 和程序。
2. 启动模拟器：在 `$SDK_HOME` 下，输入 `run/run-qemu.sh`，**[第一次连接ssh，会让确认证书，请输入 `yes`]**。
   然后qemu会启动网络，这里需要系统的高级权限，会调用sudo，请用户输入 **当前用户的密码**。
   之后每次启动qemu都需要输入，但是sudo可能会记住之前的输入，而在短时间内不用输入。
   具体模拟器以及相关终端的介绍可以参考[在各平台上运行Examples](@ref sdk_run_example_zh)文档中前面的内容。
3. 运行目录下的`debug.sh`文件会自动运行示例CA以及TA。运行成功会在TEE日志中显示"Hello World"。
   如果是在真机上运行调试可以使用`debug.sh -d`参数。

## 跨平台APP开发具体步骤
  一键运行调试可以使用example下的`debug.sh`脚本，下面是详细步骤介绍。
#### 1.编写可以跨平台的micropython脚本。
  示例脚本在`$SDK_HOME/Examples/vtrust_cp/vtrust_cp_ca/data/script.py`

  跨平台特定API说明：
  mp_obj_t cp_get_data()
  通过micropython脚本的`vtrust.cp.get_data()`可以获得从CA中调用nbl_run_ta时设置的参数。

  mp_obj_t cp_get_data_size()
  同样的`vtrust.cp.get_data_size()`可以获CA中调用nbl_run_ta时设置的数据的长度。

  更多内容请参考micropython官方文档以及示例代码。

#### 2.秘钥的生成
  正式环境中生成一对非对称秘钥主要用于上一步中micropython脚本的签名与验证。
  在调试环境下，SDK已经准备好了一套调试的秘钥。使用debug.sh脚本可以直接使用调试秘钥运行程序，不必生成秘钥。

  1.生成脚本签名秘钥
  目录`$SDK_HOME/run/vtrust_cp/key_tool/genarate_sign_key.sh`用于生成一对非对称秘钥。具体使用方式可以参考目录下的readme文档第二部分“生成签名证书”
  SDK已经准备好了一套调试的秘钥在`$SDK_HOME/run/vtrust_cp/key_tool/debug_key`目录下。

  2.将生成的公钥key_sign_publickey.pem提供给谦川科技，从而获得一个签过名的公钥数据。
  SDK已经准备好了一套调试的签过名的秘钥在`$SDK_HOME/Examples/vtrust_cp/vtrust_cp_ca/data/sign_publickey_data`

#### 3.签名脚本
  对编写好的micropython脚本进行签名。保证运行的脚本是没有被篡改的。
  调试环境下debug.sh脚本可以自动使用debug秘钥签名脚本运行。
  脚本签名工具在`$SDK_HOME/run/vtrust_cp/sign_tool/vtrust_sign_script.sh`具体参数如下：
  `--data_path` 需要签名的脚本文件
  `--private_key_pem_path` 签名的私钥，上一步生成。
  `--out_file` 签名完成文件的输出路径，包括文件名字。
  `-h` 帮助
  在example中签名数据在`$SDK_HOME/Examples/vtrust_cp/vtrust_cp_ca/data/script_sign`

#### 4.编写CA
  编写CA实现加载micropython脚本、签名以及相关秘钥等。调用nbl_run_ta函数传递对应的参数包括micropython脚本、脚本签名、脚本所需参数、验证签名的公钥来启动跨平台TA来运行micropython脚本。具体使用请参考example`$SDK_HOME/Examples/vtrust_cp/vtrust_cp_ca`。

  接口说明：
typedef struct nbl_cp_req_s
{
  char *mp_script;            //将要执行的micropython脚本
  void* mp_param;             //micropython脚本执行中所需的参数
  size_t mp_param_size;       //micropython脚本执行中所需的参数长度
  void *mp_script_sign;       //micropython脚本签名
  size_t mp_script_sign_size; //micropython脚本签名长度
  void *sign_publickey;       //micropython脚本签名公钥。公钥需要根证书签名
  size_t sign_publickey_size; //micropython脚本签名公钥长度

} __attribute__ ((packed)) nbl_cp_req_t;

typedef struct nbl_cp_res_s
{
   int32_t status;            //返回执行结果状态
   void* data;                //返回数据
   size_t data_size;          //返回数据长度
} __attribute__ ((packed)) nbl_cp_res_t;

//启动运行安全APP并执行micropython脚本,req_data为请求数据，res_data为返回数据。
int nbl_run_ta(nbl_cp_req_t *req_data, nbl_cp_res_t *res_data);

#### 5.运行TA
  TA镜像文件在`$SDK_HOME/Examples/vtrust_cp/vtrust_cp_ca/ta_image`目录下。

  模拟器上运行可以使用provision_create_ha_by_config.sh脚本需要-q参数，示例如下
    `$SDK_PATH/run/otrp/provision_tool/provision_create_ha_by_config.sh -c $SDK_PATH/run/otrp/key_store_test -e $EXAMPLE_PATH/ta_image/vtrust_cp_ta -o $EXAMPLE_PATH/ta_json -q ""`

  真机上运行也可以使用provision_create_ha_by_config.sh脚本，需要-d参数，示例参数
    `$SDK_PATH/run/otrp/provision_tool/provision_create_ha_by_config.sh -c $SDK_PATH/run/otrp/key_store_test -e $EXAMPLE_PATH/ta_image/vtrust_cp_ta -o $EXAMPLE_PATH/ta_json -d`

  provision_create_ha_by_config.sh脚本的相关参数介绍：
    * `provision_create_ha_by_config.sh` 通过配置文件生成预置HA数据。并可以选择是否运行。
    * `-c` 使用provision_create_config.sh脚本生成的预置HA配置文件所在的文件夹路径。
    * `-e` HA镜像或者使用签名工具加密签名后的HA的json数据的文件路径。
    * `-o` OTrP协议json数据输出文件夹路径。
    * `-q` \<ssh ip\> (可选)在QEMU模拟器上运行，参数是ssh_ip。参数为""时代表使用默认ssh_ip。`-d`，`-q`参数二选一。都不设置则只生成数据不运行TA。
    * `-d` (可选)在真机上运行。`-d`，`-q`参数二选一。都不设置则只生成数据不运行TA。
    * `-h` 显示帮助信息。
  如果是使用自定义的秘钥加密运行TA，可以参考[通过otrp协议安全安装HA](@ref sdk_otrp_zh)文档中的相关内容。

#### 5.运行CA
  首先用make进行编译。
  qemu模拟器上运行CA。在`$SDK_HOME/Examples/vtrust_cp/vtrust_cp_ca`目录使用make run命令。
  真机上运行CA。在`$SDK_HOME/Examples/vtrust_cp/vtrust_cp_ca`目录使用make run-ndk命令。
  CA运行后会调用TA在TA中运行micropython脚本。example运行成功会在TEE日志中显示"Hello World"。
