# Nebula SDK run example                {#sdk_run_example_en}

## Run exmaples in REE(Linux)

<a name="run-qemu"></a>

1. Build `$SDK_HOME/Examples` : Change current directory to `$SDK_HOME/Examples`. Run `make` command.The folder `$SDK_HOME/Examples/*/out` will be created and contains intermediate files(*.o) and target program(*.so/*.a/ELF/FAR).
2. Start QEMU: Change current directory to `$SDK_HOME`. Run `run/run-qemu.sh`.
    **[During the first run, SSH will be start and ask users to confirm keys, please input "yes".]**.
3. `run/run-qemu.sh` will start a virtual network for QEMU and will use "sudo" command, so please input current user's password.
    Because sudo will remember this input, so this password will be required to input at a short time.
4. The console show start screen:

```
Bringing up the network interface: qemu
Starting dnsmasq...
Enable IP forwarding...
QEMU 2.10.1 monitor - type 'help' for more information
(qemu)
```
And then Nebula system will boot.

1. As soon as Nebula boot, THREE terminals will pop up:
    * **REE Terminal**, it's REE(Linux), prompt is "root@FVP:/", and users can input linux command and run.
    * **HEE Terminal**, it's HEE, prompt is "$", and users can input inner command and run.
    * **SYS Terminal**, it's a read-only log for ATF start information.
2. Run example: start a new console, and cd into any example, for example "test simple libc"(`$SDK_HOME/examples/hee/libc`),
    and run `make run`。
    * The 'run' target will start a ssh client to connect REE(Linux), and push "test-libc" program from '$SDK_HOME/examples/hee/libc/out' folder to linux FS.
    * If program is CA, the 'run' target will run it in Linux environment. The reuslt is shown in REE(Linux).
    * If program is HA, the 'run' target will use inner tool "nbl_daemon" to push the HA into HEE, and run it. The result is shown in HEE terminal.
3. Quit from QEMU: input `quit` or `q` in command line of QEMU.

### Notice:

* if `make run` encounters the below error：

```
ssh: connect to host 192.168.3.53 port 22: Connection refused
../../../../make/hee-program.mk:19: recipe for target 'run' failed
make[1]: *** [run] Error 255
```

This means two status:

    * The Nebula system has not started completely. Wait for a moment and run command again.
    * Run command "run-qemu.sh" with "-N", so the network is disable.
* Example "examples/hee/shared-lib" is a library and is not support dynamically loading by now.
* If "run-qemu.sh -s" is specified, qemu will start with gdb option. CPU will be frozen before nebula booting. Input "c" command to continue booting nebula.

```
(qemu) c
(qemu)
```

## Run HA with OTrP
### Run HA in QEMU with OTrP
* Build `$SDK_HOME/Examples/hee` : Change directory to `$SDK_HOME/Examples/hee`. Run `make` command.The folder `$SDK_HOME/Examples/hee/*/out` will be created and contains intermediate files(*.o) and target program(*.so/*.a/ELF/FAR).
* Run QEMU. Refer to the "Running Examples on REE (Linux)" section of the document.
* Run hee example: start a new console,and cd into any hee example, for example "test simple libc"(`$SDK_HOME/examples/hee/libc`), and run `make run-qemu`.
    * The 'run-qemu' target will start a ssh client to connect REE(Linux). Use the OTrP debugging tool to package the "test-libc" image with default certificate and key. HAID is randomly generated. HA will be installed into the security domain which is created by default.
    * After the HA installation is completed.Nbl_daemon will load HA with HAID.

### Run HA in Android with OTrP
* Build `$SDK_HOME/Examples/hee` : Change dir to `$SDK_HOME/Examples/hee`. Run `make` command.The folder `$SDK_HOME/Examples/hee/*/out` will be created and contains intermediate files(*.o) and target program(*.so/*.a/ELF/FAR).
* Start the android device and enable adb debugging.
* Run hee example: cd into any hee example, for example "test simple libc"(`$SDK_HOME/examples/hee/libc`), and run `make run-device`.
    * The 'run-qemu' target will start a ssh client to connect REE(Linux). Use the OTrP debugging tool to package the "test-libc" image with default certificate and key. HAID is randomly generated. HA will be installed in the security domain which is created by default.
    * After the HA installation is completed.Nbl_daemon will load HA with HAID.The log will be displayed in the uart port.

### Run HA in other Linux device with OTrP
* The environment of Linux device is similar to the QEMU environment，so you can refer to "Run HA in QEMU with OTrP" to run the HA，the difference is to specify the IP address of the linux device，change `make run-qemu` to `DEVICE_IP=x.x.x.x make run-qemu`.
* Note: make sure the host can connect to the device using the SSH protocol.

## Run examples in REE(Android)

Run examples in `$SDK_HOME/examples/android`.
1. start a emulator or Android device.
2. cd into any foler in `$SDK_HOME/examples/android`, and then build it with `make`.
3. Run with `make run`.
    This script will push the program into "/data" in Android by `adb`(Host program), and then run it.