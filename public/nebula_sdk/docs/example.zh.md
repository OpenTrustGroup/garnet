# Nebula SDK 示例代码解析                {#sdk_example_zh}

## CA的例子(Linux)

参考 `$SDK_HOME/examples/ree/trusty/Makefile`。

```
# 这里需要定义SDK在哪里，可以用绝对路径也可以是相对路径。
SDK_HOME ?= ../../..

# 然后包含程序运行的环境设定，这里是CA，所以包含ree-common.mk。
include ${SDK_HOME}/make/ree-common.mk

# 下面定义编译用的文件和设定。
# - SRC_C可以包含 *.c/*.cc/*.cpp 文件，编译器会根据不同的后缀区分不同的语言(只限C/C++)。
SRC_C = tipc_test.c
# - 编译用的选项，比如 -Ixxx，或者 --std=c11，必须用 "+="。
CFLAGS +=
# - 链接用的选项，注意不是直接用ld命令，而是用clang++，所以传递给连接器的选项应该是 -Wl,threads 。
# - 必须用 "+="。
LDFLAGS += -lpthread

# 生成的程序的名字，放在 out 文件夹中。
TARGET = test-tipc
# 如果运行，需要的参数。此例子是make run时，会运行 test-tipc -t ta2ta-ipc。
RUN_ARG = -t ta2ta-ipc

# 结尾必须包含想要编译成什么程序，这里是生成一个可执行程序。
include ${SDK_HOME}/make/ree-program.mk
```

Makefile的目标包含：
* **all**: 编译代码，是缺省目标。
* **clean**: 删除 out 文件夹，里面包含所有中间和结果文件。
* **run**: 将目标文件运行在目标平台中。

编译CA用Clang，但是链接的库和头文件都是 linaro-gcc 提供的。

### 缺陷和注意事项：
* 此处CA是在Linux中运行，和 Android 并不完全相同。

## CA例子(Android)

参考 `$SDK_HOME/examples/android/trusty`。
里面的 Makefile 这是为了实现统一的make目标而实现的，真正使用的是 ndk-build 去编译和清理，
而用 adb 去运行此程序。

```
# 必须包含
SDK_HOME ?= ../../..

# 如果自己的NDK，可以在此设定路径。
# ANDROID_NDK_HOME := ?

# 必须包含，它会寻找ndk的位置，以及设定 “SDK_ANDROID_HOME”，里面包含CA需要的头文件和库。
include ${SDK_HOME}/make/ndk-common.mk

REE_TRUSTY_PATH="./"
MY_NDK_OPTIONS = NDK_PROJECT_PATH=${REE_TRUSTY_PATH} \
    APP_BUILD_SCRIPT=${REE_TRUSTY_PATH}/Android.mk \
    NDK_APPLICATION_MK=${REE_TRUSTY_PATH}/Application.mk \
    NDK_OUT=./out \
    SDK_ANDROID_HOME=${SDK_ANDROID_HOME} \

# 这里调用 ndk-build 去编译。
all:
    @${ANDROID_NDK_HOME}/ndk-build ${MY_NDK_OPTIONS}

# 其他目标，用户可以自己实现。

```

例子用 Android.mk 和 Application.mk，具体语法就不详细说明了，请参考Android的NDK的指导。
另外，开发者不必拘泥于这种方案去编译，可以按照自己的想法编写 Android.mk 和 Application.mk。

## HA的例子

参考 `$SDK_HOME/examples/hee/libc/Makefile`。

```
# 必须设定SDK_HOME，可以是相对路径或者绝对路径。
SDK_HOME ?= ../../..

# 然后包含程序运行的环境设定，这里是HA，所以包含 hee-common.mk。
include ${SDK_HOME}/make/hee-common.mk

# 包含编译的代码。
# - 注意，每个HA都需要一个manifest.c，设置相关的uuid和其他选项。
#   manifest.c文件的具体格式和内容参考例子中文件。
SRC_C  = manifest.c main.c lib/lib.c

# 编译用的选项，比如 -Ixxx，或者 --std=c11，对于C/C++都有作用。
# - 必须用 "+="。
CFLAGS += -I${SDK_HEE_HOME}/trusty/include -I./lib

# 链接用的选项，注意不是直接用ld命令，而是用clang++，所以传递给链接器的选项应该是 -Wl,--threads 。
# - 必须用 "+="。
LDFLAGS += -L${SDK_HEE_HOME}/lib

# 生成的程序的名字，放在 out 文件夹中。
# HA会生成一个$TARGET.untrip文件，这个是strip前的程序，$TARGET是strip后的程序。
TARGET = test-libc

# 注意，HA不支持 RUN_ARG，也就是不能传递参数。

# [可选项]设置SD的权限文件，文件内容和规定参考 “Security Domain Privilege” 章节。
# 如不设置则默认使用${SDK_HOME}/examples/sd.privilege作为安全域权限（最大权限）的配置。
#SD_PRIVILEGE_FILE = sd.privilege

# 结尾必须包含想要编译成成什么程序。
include ${SDK_HOME}/make/hee-program.mk
```

编译HA用clang。

### 缺陷和注意事项：
* 目前HA不支持使用第三方动态库，所有动态库必须是预先放置在Qem或者Android镜像中才可以使用。

## FAR的例子

普通的HA是ELF格式，此SDK还提供FAR格式，此格式允许加入配置文件和其他数据文件等。

参考 "/garnet/public/nebula_sdk/examples/hee/far/Makefile"。

```
SDK_HOME ?= ../../..

# 包含HA的一般设定
include ${SDK_HOME}/make/hee-common.mk

CFLAGS +=
LDFLAGS += -lm

# 如果想使用第三方的静态库，那么可以在这里把静态库的路径写下来，比如 "lib/libtrusty.a"，
# 或者在 LDFLAGS 中加入此库。
EXT_LIB +=

SRC_C = main.c

# 想添加的额外数据文件。每项用","分割，每项的格式是"文件在镜像中的路径=HOST机器中的文件路径"。
# 文件名字和路径名字都不能包含空格。
FAR_FILES += \
    data/test1.dat=data/test1.dat, \
    data/test2.dat=data/test2.dat

TARGET = test-far

# 在结尾，包含"hee-far.mk"文件，就会生成"out/${TARGET}.far"文件。
include ${SDK_HOME}/make/hee-far.mk
```

```
# 代码中打开文件的路径是"/pkg/文件在镜像中的路径"。
int fd = open("/pkg/data/test1.dat", O_RDONLY);
```

* 这个项目不再包含 "manifest.c"，而是包含"resources/manifest.json"，在这个文件中包含UUID。
* 这个项目还要包含"meta/sandbox"文件，此文件会影响运行时程序可以看到的文件系统。
  缺省情况下，开发者最好复制例子中的这个文件。