# Nebula SDK                {#sdk_read_me_zh}

## 概述
基于Nebula的HA和CA的综合开发环境，包含相关的工具、库、示例以及HEE/REE的QEMU仿真环境。
REE的QEMU仿真环境支持快速开发基于Nebula的HA。

## 名词解释

* **SDK_HOME**: SDK所在目录。
* **Host**: SDK工作的系统平台（目前支持ubuntu）。
* **HEE**: Hypervisor Execution Environment，虚拟化执行环境。
* **HA**: Hypervisor Application，HA是运行在HEE中的应用。
* **HAID**: Hypervisor Application Identification，HA唯一识别符。HAID目前只支持UUID。
* **SD**: Security Domain，安全域。使用OTrP功能，HA将安装在安全域中。
* **OTrP**: The Open Trust Protocol，在可信环境中安装更新删除应用程序并管理其安全配置的协议。
* **FAR**: HA的打包格式，包含程序和数据，FAR可以被动态加载到HEE中运行。
* **REE**: Rich Execution Environment，富应用执行环境，Linux是一种典型的REE运行环境。
* **CA**: Client App，作为HA的客户端应用运行在REE上。
* **FS**: File System，文件系统。

## SDK的目录结构

```
├── docs                          此SDK的文档，包括使用说明和版本信息。
├── android                       Android的相关的头文件和库
│   ├── include                     头文件
│   └── lib                         库
├── ree                           Linux的相关的头文件和库
│   ├── include                     头文件
│   └── lib                         库
├── hee                           HEE相关的头文件和库
│   ├── include                     包含libc等基础库的头文件
│   ├── debug                       包含调试信息的库
│   ├── dist                        strip过的库
│   ├── lib                         只有符号的动态库，【请链接这里面的库】
│   ├── boringssl                   提供openssl的头文件和库
│   ├── omp                         提供openmp的头文件和库
│   └── trusty                      提供trusty(包括secure-storage/hwkey/rng)的头文件和库
├── examples                      示例
│   ├── Makefile                    总的Makefile，用于编译全部例子
│   ├── nbl_fwk                     关于nbl_fwk的例子，包括REE/HEE
│   ├── nbl_gui                     关于Nebula TUI的例子
│   ├── nbl_attest                  关于裝置檢測及NFC的例子
│   ├── hee                         关于HEE的例子
│   ├── android                     关于REE(Android)的例子
│   ├── ree                         关于REE(Linux)的例子
│   ├── sd.privilege                默认使用的安全域privilege配置文件
│   └── vtrust_cp                   关于跨平台应用的REE端例子
├── host                          Host上运行的辅助程序
│   └── x64                         64Bits 系统上使用的工具组
├── make                          用于编写HA和CA的Makefile
│   ├── hee-common.mk               设定HA相关的统一设定
│   ├── hee-program.mk              编译为HA的应用
│   ├── hee-far.mk                  编译为HA(FAR格式)的应用
│   ├── hee-static-lib.mk           编译为静态库，给HA使用。
│   ├── ndk-common.mk               设定CA(Android)相关的统一设定
│   ├── ree-common.mk               设定CA(Linux)相关的统一设定
│   └── ree-program.mk              编译为CA(Linux)的应用
├── run                           模拟器(Qemu)相关的程序和脚本
│   ├── qemu/                       模拟器的程序和对应的镜像
│   ├── run-ca-in-qemu.sh           在REE中运行一个CA
│   ├── run-ha-in-qemu.sh           在HEE中运行一个HA
│   ├── run-qemu.sh                 启动Qemu
│   ├── vtrust_cp/                  跨平台相关工具
│   │  ├── key_tool/                    跨平台相关秘钥，里面包含了调试秘钥和生成工具
│   │  └── sign_tool/                   跨平台相关脚本签名工具
│   └── otrp/                               OTrP相关工具
│       ├── debug_tool/                       OTrP调试相关工具目录
│       │   ├── secmgr_debug_tool.jar           OTrp调试jar包工具
│       │   ├── debug/                          调试生成的数据文件的临时文件夹
│       │   ├── secmgr_debug.sh                 调试脚本，创建默认安全域，安装运行HA
│       │   ├── secmgr_test.sh                  测试OTrP其他调试脚本功能的脚本
│       │   ├── secmgr_init.sh                  初始化调试所需数据
│       │   ├── secmgr_create_sd.sh             创建用于调试的安全域
│       │   ├── secmgr_delete_sd.sh             删除用于调试的安全域,同时删除其中的HA
│       │   ├── secmgr_install_ha.sh            安装HA到指定安全域中
│       │   ├── secmgr_delete_ha.sh             从安全域中删除指定的HA
│       │   ├── secmgr_run_ha.sh                运行一个HA
│       │   ├── secmgr_ls.sh                    显示已创建的调试安全域和安装过的HA
│       │   ├── secmgr_clean.sh                 删除所有调试的安全域和其中的HA
│       │   ├── secmgr_opt_common.sh            共通解析脚本
│       ├── sign_tool                         签名HA的相关工具目录
│       │   ├── secmgr_sign_tool.jar            签名加密HA以及解析配置的工具包
│       │   ├── secmgr_sign_tool.sh             签名加密HA的脚本
│       │   ├── secmgr_get_config.sh            从签名加密的HA的json数据解析配置文件脚本
│       │   ├── demo.sh                         示例脚本
│       ├── elf_tool/                         镜像中解析配置信息的相关工具目录
│       │   ├── nebula_elf_tool.jar             HA镜像解析jar包工具
│       │   ├── nebula_elf.sh                   HA镜像解析jar包脚本
│       │   ├── far                             解析far依赖的工具
│       │   └── demo.sh                         示例脚本
│       ├── provision_tool/                   预置HA相关工具目录
│       │   ├── secmgr_provision_tool.jar       预置HAjar包工具
│       │   ├── demo.sh                         示例脚本
│       │   ├── provision_get_device_state.sh   生成获得设备信息数据的脚本
│       │   ├── provision_create_sd.sh          生成创建安全域数据的脚本
│       │   ├── provision_delete_sd.sh          生成删除安全域数据的脚本
│       │   ├── provision_install_data.sh       让nbl_daemon安装预置数据的脚本
│       │   ├── provision_install_ha.sh         生成安装HA数据的脚本
│       │   ├── provision_create_config.sh       创建预置配置文件
│       │   ├── provision_create_ha_by_config.sh 通过配置文件生成预置HA数据
│       │   ├── provision_create_sd_by_config.sh 根据配置文件生成创建和更新预置安全域数据的脚本
│       │   ├── provision_get_ha.sh             从预置安装HA的json数据中提取HA
│       │   ├── provision_convert_debug.sh      根据release版预置安装HA的json数据，生成原始的HA镜像和debug版预置数据和的脚本
│       │   ├── provision_secmgr_test_ha.sh     生成secmgr测试数据脚本
│       │   ├── push_android.sh                 推送指定目录下的文件到安卓设备的脚本
│       │   └── push_qemu.sh                    推送指定目录下的文件到qemu模拟器的脚本
│       ├── key_store_tool/                   证书和秘钥生成的相关工具
│       │   ├── export_key.sh                   可以从秘钥证书根路径（例如SDK下的测试秘钥文件夹key_store_test）导出需要注入的秘钥和证书。
│       │   ├── genarate_hee.sh                 生成HEE所需的相关证书和秘钥等数据
│       │   ├── readme_hee                      genarate_hee.sh脚本的相关使用说明文档
│       │   ├── genarate_sp.sh                  生成SP（Service Provider）所需的相关证书和秘钥等数据
│       │   ├── readme_sp                       genarate_sp.sh脚本的相关使用说明文档
│       │   ├── genarate_tsm.sh                 生成TSM/TAM(Trusted Service Manager/Trusted Application Manager)所需的相关证书和秘钥等数据
│       │   ├── readme_tsm                      genarate_tsm.sh脚本的相关使用说明文档
│       │   ├── genarate_spaik.sh               生成spaik（用于加密HA）所需的相关证书和秘钥等数据
│       │   ├── readme_spaik                    genarate_spaik.sh脚本的相关使用说明文档
│       │   ├── genarate_tfw.sh                 生成TFW（Trusted Firmware）所需的相关证书和秘钥等数据
│       │   ├── readme_tfw                      genarate_tfw.sh脚本的相关使用说明文档
│       └── key_store_test/                   调试所需的秘钥和证书等文件目录
└── toolchains                    工具链
    ├── android-ndk/                NDK工具链，编译CA(Android)使用
    ├── clang/                      clang工具链，编译CA(Linux)和HA使用
    └── linaro-gcc/                 linaro的gcc工具链，内部的头文件和库提供给clang编译使用
```

## 需要环境

* Host机：SDK在 `Ubuntu 16.04` 上测试通过，推荐使用`Ubuntu 16.04`。
    * 用户语言必须是”English(United States)“。
    * Ubuntu可以运行在vmware等虚拟机上。
    * 当Ubuntu运行在远程的服务器，用户通过ssh或telnet登录时，SDK的模拟环境（qemu)无法正常运行。
    * 请以普通用户运行，不要用root用户。

* 客户系统上已经安装 make/python/ssh。

* 如果需要连接Android，还需要安装 adb，如果采用 Ubuntu 系统的 adb，安装命令:
    `sudo apt-get install adb`

* 需要额外安装的软件：
    `sudo apt-get install uml-utilities`

* 如需要使用TUI，需要安装`ssvncviewer`：

    `sudo apt-get install ssvnc`

##在各平台上运行Examples
SDK中examples在各平台的运行方式请参考[在各平台上运行Examples文档](@ref sdk_run_example_zh)

##Example代码解析
example代码的解析请参考[Example代码解析文档](@ref sdk_example_zh)

##使用OTrP安装APP
通过OTrP协议安装，预置HA等方法请参考[使用OTrP安装APP文档](@ref sdk_otrp_zh)

## Nebula TUI

目前提供两种TUI应用范例，`payment`以及`qrcode`。在此以`payment`来示范TUI应用程序开发流程：

1. 启动qemu模拟器，请参考[在各平台上运行Examples文档](@ref sdk_run_example_zh)。
2. 打开ssvncviewer，键入`ssvncviewer -scale 0.5 :0 -x11cursor`。VNC视窗在开发过程中不需要关闭。如果VNC视窗过小，试着增加-scale的数字（ex. 0.7）应该能改善。
3. `cd ${SDK_HOME}/examples/nbl_gui/payment`
4. `make clean ; make ; make run-qemu`，这时应该可以在VNC视窗上看到画面，并且可以互动。
5. 开发TUI应用程序，增加新功能，修改布局等。
6. 重复步骤4～5，直到应用程序调试完成。（如果开发的过程中应用程序崩溃，可以透过步骤4重新启动应用程序）
7. 调试完成，关闭Qemu模拟器。（VNC视窗会自动关闭）

注意：当TUI应用程序结束后（或是崩溃），VNC视窗上还会有残留画面，这是因为之前渲染的画面还留在显存之中没有被清除。

关于TUI框架的文档，请参考[这里](@ref tui_readme_zh)。
