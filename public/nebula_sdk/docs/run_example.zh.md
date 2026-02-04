# Nebula SDK example 的运行                {#sdk_run_example_zh}

## 在REE(Linux)上运行Examples

<a name="run-qemu"></a>

1. 编译 `$SDK_HOME/Examples`：进入 examples 目录后，运行 make 命令。
    在每个例子下会建立 `out` 文件夹，里面放置生成的 `.o` 和程序。
2. 启动模拟器：在 `$SDK_HOME` 下，输入 `run/run-qemu.sh`，**[第一次连接ssh，会让确认证书，请输入 `yes`]**。
3. 然后qemu会启动网络，这里需要系统的高级权限，会调用sudo，请用户输入 **当前用户的密码**。
   之后每次启动qemu都需要输入，但是sudo可能会记住之前的输入，而在短时间内不用输入。
4. 显示启动画面：

```
Bringing up the network interface: qemu
Starting dnsmasq...
Enable IP forwarding...
QEMU 2.10.1 monitor - type 'help' for more information
(qemu)
```
之后，Nebula系统将启动。

1. 此时会启动三个控制台，分别是
    * **REE 终端**，运行在Qemu上的linux。命令提示符号是"root@FVP:/"，可以在此环境运行linux的命令。
    * **HEE 终端**，运行在Qemu上的HEE系统，命令提示符号是"$"，可以在此环境运行HEE的命令，类似于Linux系统。
    * **系统终端**，显示ATF启动相关的日志，不可输入。
2. 如果想运行example中的例子，启动另外一个控制台，然后进入任一个例子中，
    比如测试简单libc函数的目录（`$SDK_HOME/examples/hee/libc`），然后运行 `make run`。
    * Makefile中会运行ssh客户端，连接Qemu中运行的linux，然后将本地out文件夹下面的"test-libc"复制到linux中，
    * 如果是一个CA，就直接在linux中运行，日志在REE和HEE的终端显示。
    * 如果是HA，就会用nbl_daemon的辅助程序将此程序传递到HEE去运行，日志会在HEE的终端中显示。
3. 想退出模拟器运行，在Qemu的提示命令行中输入`quit`或者`q`。

### 注意事项

* 如果 `make run`，出现下面错误：

```
ssh: connect to host 192.168.3.53 port 22: Connection refused
../../../../make/hee-program.mk:19: recipe for target 'run' failed
make[1]: *** [run] Error 255
```

有两种情况：

    * 表示目前QEMU中的系统还没有完全启动，所以需要等待几秒种，然后再运行命令。
    * 或者启动参数中加入了"-N"，关闭了网络功能。
* 例子"examples/hee/shared-lib"为了安装需要重新启动QEMU，所以它的run目标什么都不做，
    只提供如何测试的提示信息。如果想测试这个项目，请按照提示操作。
* 如果想调试内核，请输入"run-qemu.sh -s"，此选项会启动qemu的gdb功能，然后进入QEMU的命令行后，输入"c"(接着运行模拟器)。

```
(qemu) c
(qemu)
```

## 使用OTrP调试HA
### 使用OTrP在QEMU上快速运行HA
* 编译 `$SDK_HOME/Examples/hee`，进入 examples/hee 目录后，运行 make 命令。
  在每个例子下会建立 `out` 文件夹，里面放置生成的 `.o` 和程序。
* 启动QEMU,参考上面“在REE(Linux)上运行Examples”部分文档中的内容。
* 调试运行example/hee中的例子，启动另外一个控制台，然后可以进入任一个例子中，
  比如测试简单libc函数的目录（`$SDK_HOME/examples/hee/libc`），然后运行 `make run-qemu`。
    * Makefile中会运行ssh客户端，连接Qemu中运行的linux，然后将本地out文件夹下面的"test-libc"镜像通过OTrP调试工具进行打包，秘钥和证书采用默认的调试版。HAID会随机自动生成。然后在QEMU HEE创建默认安全域，将打包的HA数据安装到默认安全域中。
    * HA安装完成后会通过nbl_daemon通过HAID加载运行。日志会在HEE的终端中显示。

### 使用OTrP在Android上快速运行HA
* 编译 `$SDK_HOME/Examples/hee`，进入 examples/hee 目录后，运行 make 命令。
  在每个例子下会建立 `out` 文件夹，里面放置生成的 `.o` 和程序。
* 启动真机并开启adb调试。
* 进入任一个例子中，比如测试简单libc函数的目录（`$SDK_HOME/examples/hee/libc`），
  然后运行 `make run-device`。
    * Makefile会在本地out文件夹下面生成"test-libc"镜像。通过OTrP调试工具进行打包，秘钥和证书采用默认的调试版。HAID会随机自动生成。然后在真机HEE创建默认安全域，将打包的HA数据安装到默认安全域中。
    * HA安装完成后会通过nbl_daemon通过HAID加载运行。日志会在真机串口中显示。

### 使用OTrP在其他Linux设备上快速运行HA
* 运行Linux设备的环境和QEMU环境比较类似，因此可以参考上面“使用OTrP在QEMU上快速运行HA”的相关内容来操作，区别是运行的时候要指定Linux设备的IP地址，即`make run-qemu`修改为`DEVICE_IP=x.x.x.x make run-qemu`。
* 注意: 要确保Host能使用SSH协议连接到Linux设备上。

## 在REE(Android)上运行Examples

在 `$SDK_HOME/examples/android` 下面的例子，需要
1. 启动android模拟器或者真机。
2. 进入 `$SDK_HOME/examples/android` 下某个文件夹，然后运行`make`，编译此项目。
3. 然后运行 `make run`。
    此命令会用adb将编译后的程序放到android的`/data`目录中，然后运行程序。
