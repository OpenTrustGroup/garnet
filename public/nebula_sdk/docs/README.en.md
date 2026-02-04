# Nebula SDK                {#sdk_read_me_en}

A comprehensive environment for developing HA and CA on top of Nebula system. It includes tools, examples, libraries and HEE/REE QEMU emulation environment. QEMU emulation environment is used for developing HA quickly.

## Glossary

* **SDK_HOME**: The path to SDK.
* **Host**: The machine deploying this SDK. For now it's Ubuntu System.
* **HEE**: Hypervisor Execution Environment.
* **HA**: Hypervisor Application, HA will run on top of HEE.
* **HAID**: Hypervisor Application Identification. Currently only UUID is supported.
* **SD**: Security Domain. HA will be installed in the security domain when using the OTrP.
* **OTrP**: The Open Trust Protocol，a protocol that is used to install, update, and delete applications in a Trusted Execution Environment and to manage their security configuration.
* **FAR**: The HA archive packaging format contains program(ELF) and data. It can be loaded and run dynamically.
* **REE**: Rich Execution Environment. Linux is a typical REE.
* **CA**: Client Application which will run in REE.
* **FS**: File System.

## SDK Tree

```
├── docs                          documents about readme and changlog, etc.
├── android                       Headers and libraries of Android
│   ├── include                     Headers
│   └── lib                         Libraries
├── ree                           Headers and libraries of Linux
│   ├── include                     Headers
│   └── lib                         Libraries
├── hee                           Headers and libraries of HEE
│   ├── include                     Headers containing libc/zircon etc.
│   ├── debug                       Libraries containing debug information.
│   ├── dist                        Stripped libraries.
│   ├── lib                         Dyanmic libraries only having symbols.[Default link these libraries]
│   ├── boringssl                   Headers and libraries of OpenSSL
│   ├── omp                         Headers and libraries of OpenMP
│   └── trusty                      Headers and libraries of Trusty API and secure-storage,hwkey,rng.
├── examples                      Examples
│   ├── Makefile                    Build all exmaples.
│   ├── nbl_fwk                     About nbl_fwk in REE and HEE
│   ├── nbl_gui                     About Nebula TUI
│   ├── nbl_attest                  About device attestation and NFC
│   ├── hee                         About HEE
│   ├── android                     About REE(Android)
│   ├── ree                         About REE(Linux)
│   ├── sd.privilege                Security domain privilege profile used by default
│   └── vtrust_cp                   REE example of cross-platform application
├── host                          Auxiliary tools running in Host
│   └── x64                         Tools of 64Bits
├── make                          Top makefile for build
│   ├── hee-common.mk               Common settings for HA
│   ├── hee-program.mk              Make HA program in ELF format
│   ├── hee-far.mk                  Make HA program in FAR format
│   ├── hee-static-lib.mk           Make static library
│   ├── ndk-common.mk               Common settings for CA(Android)
│   ├── ree-common.mk               Common settings for CA(Linux)
│   └── ree-program.mk              Make CA program(Linux)
├── run                           Run a QEMU
│   ├── qemu/                       QEMU program and images.
│   ├── run-ca-in-qemu.sh           Run CA in REE(Linux)
│   ├── run-ha-in-qemu.sh           Run HA in HEE
│   ├── run-qemu.sh                 Boot QEMU environment.
│   ├── vtrust_cp/                  Cross-platform tools
│   │   ├── key_tool/                  Cross-platform related keys folder, which contains debugging keys and generation tools
│   │   └── sign_tool/                 Cross-platform related script signing tool
│   └── otrp/                               Tools of OTrP.
│       ├── debug_tool/                       OTrP debugging tools.
│       │   ├── secmgr_debug_tool_v0.1.jar      OTrp debugging jar file.
│       │   ├── debug/                          Temporary folder for debugging tools.
│       │   ├── secmgr_debug.sh                 Create a default SD. Install and run HA.
│       │   ├── secmgr_test.sh                  Test script.
│       │   ├── secmgr_init.sh                  Initialize debugging data.
│       │   ├── secmgr_create_sd.sh             Create a SD for debugging.
│       │   ├── secmgr_delete_sd.sh             Delete a SD and all its HAs.
│       │   ├── secmgr_install_ha.sh            Install HA into a secure domain.
│       │   ├── secmgr_delete_ha.sh             Delete HA from the security domain.
│       │   ├── secmgr_run_ha.sh                Run a HA.
│       │   ├── secmgr_ls.sh                    Show all SDs and HAs.
│       │   ├── secmgr_clean.sh                 Remove all SDs and HAs.
│       │   ├── secmgr_opt_common.sh            Common debugging script.
│       ├── sign_tool/                        Signing HA tools.
│       │   ├── secmgr_sign_ha.sh               Signature encryption HA script.
│       │   ├── secmgr_sign_tool.jar            Signature encryption HA and parsing. configuration toolkit.
│       │   ├── secmgr_get_config.sh            Parsing configuration file script from json data of HA that has been signed and encrypted.
│       │   ├── demo.sh                         Demo script.
│       ├── elf_tool/                         Tool directory for parsing configuration information from the HA image.
│       │   ├── nebula_elf_tool.jar             HA image parsing jar package.
│       │   ├── nebula_elf.sh                   HA image parsing script.
│       │   ├── far                             Parsing far dependent tools.
│       │   └── sample.sh                       Sample script.
│       ├── provision_tool/                   Provision tool folder.
│       │   ├── secmgr_provision_tool.jar       Provision jar package.
│       │   ├── demo.sh                         demo script.
│       │   ├── provision_get_device_state.sh   Generate a script that gets device information data.
│       │   ├── provision_create_sd.sh          A script is used to generate device information data.
│       │   ├── provision_delete_sd.sh          A script is used to generate create Security domain data.
│       │   ├── provision_install_data.sh       A script to let nbl_daemon install provision data
│       │   ├── provision_install_ha.sh         A script is used to generate install ha data.
│       │   ├── provision_create_config.sh      Create a provision configuration file.
│       │   ├── provision_create_ha_by_config.sh Generate provision data through a configuration file.
│       │   ├── provision_create_sd_by_config.sh Generate provision create and update security domain data from the configuration file.
│       │   ├── provision_get_ha.sh             Extract HA from the provision json data of the installation HA.
│       │   ├── provision_convert_debug.sh      A script for generate the HA image and debug provision data according to the release json data of the installation HA.
│       │   ├── provision_secmgr_test_ha.sh     Generate secmgr test data.
│       │   ├── push_android.sh                 Script to push files in the specified directory to the Android device.
│       │   └── push_qemu.sh                    Script to push files in the specified directory to the QEMU.
│       ├── key_store_tool/                   Certificate and key generation tools.
│       │   ├── export_key.sh                   It can export the key and certificate to be injected from the root path of the key certificate folder (for example, the folder key_store_test under the SDK).
│       │   ├── genarate_hee.sh                 Generate data such as related certificates and keys required for HEE.
│       │   ├── readme_hee                      Related instructions for the genarate_hee.sh script.
│       │   ├── genarate_sp.sh                  Generate data such as related certificates and keys required for SP(Service Provider).
│       │   ├── readme_sp                       Related instructions for the genarate_sp.sh script.
│       │   ├── genarate_tsm.sh                 Generate data such as related certificates and keys required for TSM/TAM(Trusted Service Manager/Trusted Application Manager).
│       │   ├── readme_tsm                      Related instructions for the genarate_tsm.sh script.
│       │   ├── genarate_spaik.sh               Generate data such as related certificates and keys required for spaik(Used to encrypt HA).
│       │   ├── readme_spaik                    Related instructions for the genarate_spaik.sh script.
│       │   ├── genarate_tfw.sh                 Generate data such as related certificates and keys required for TFW(Trusted Firmware).
│       │   ├── readme_tfw                      Related instructions for the genarate_tfw.sh script.
│       └── key_store_test/                   Debugging keys and certificates folder.
└── toolchains                    Toolchains
    ├── android-ndk/                Android-NDK for building CA(Android)
    ├── clang/                      Clang toolchain for building CA(Linux) and HA
    └── linaro-gcc/                 Linaro-gcc toolchain in which headers and libraries used by clang.
```

## Requirement

* Host OS： SDK is tested on `Ubuntu 16.04`.
    * Language of user should be "English(United States)".
    * Ubuntu may run in virtual machine such as vmware.
    * When Ubuntu runs in remote server, and user operates it by ssh/telnet, the emulator(qemu) of nebula will fail.
    * Please use SDK by normal users, but root.

* `make`, `python`, `ssh` are installed.

* `adb` is required if run on Android device.
    `sudo apt-get install adb`

* Other tools:
    `sudo apt-get install uml-utilities`

* `ssvnc` should be installed if TUI feature is enabled.

    `sudo apt-get install ssvnc`

## Run Examples
Please refer to [Nebula SDK run example document](@ref sdk_run_example_en) for how the examples in the SDK work on different platforms.

## Example code analysis
Example code analysis, please refer to[Nebula SDK sample code analysis document](@ref sdk_example_en)

## Install APP with OTrP
Install APP with OTrP protocol，please refer to [Nebula SDK install APP with OTrP document](@ref sdk_otrp_en)

## Nebula TUI

We provide 2 TUI samples for your reference, `payment` and `qrcode`.  We use `payment` to demonstrate the development flow of a Nebula TUI application:

1. Launch qemu emulator. Refer to [this](@ref sdk_run_example_en).
2. Launch `ssvncviewer`. Enter `ssvncviewer -scale 0.5 :0 -x11cursor`. VNC window should not be closed during development flow. If VNC window is too small, try to increase the argument along with `-scale` (ex. 0.7) should help.
3. `cd ${SDK_HOME}/examples/nbl_gui/payment`
4. `make clean; make; make run-qemu`. TUI should appear on VNC window, and you can interactive with it.
5. Develop TUI application, adding new features, changing layout...etc.
6. Repeat step 4~5 until the development process is completed. (If application crashes, you can restart it via step 4)
7. Development completed. Quit qemu emulator. (VNC window will be closed automatically)

For detailed information about TUI framework. Please refer to [this document](@ref tui_readme_zh).
