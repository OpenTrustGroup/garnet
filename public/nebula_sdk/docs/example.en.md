# Nebula SDK sample code analysis                {#sdk_example_en}

## Explain makefile format of CA(Linux)

See `$SDK_HOME/examples/ree/trusty/Makefile`.

```
# The path of SDK_HOME(absolute path or relative path).
SDK_HOME ?= ../../..

# Common settings of CA.
include ${SDK_HOME}/make/ree-common.mk

# Source files and compile flags for building.
# - SRC_C can contain *.c/*.cc/*.cpp files, and the script will use correct compiler by suffix.
SRC_C = tipc_test.c
# - Flags to compiler, such as "-Ixxx", "--std=c11" and etc. Must use "+=".
CFLAGS +=
# - Flags to linker. Notice: linker is NOT ld, but use gcc/clang++, so use "-Wl,xxx" to transfer flags into ld, for example: -Wl,threads.
# - Must use "+=".
LDFLAGS += -lpthread

# CA binary file name.
TARGET = test-tipc
# Set RUN_ARG to run with arguments. In this example, command "test-tipc -t ta2ta-ipc" will be executed.
RUN_ARG = -t ta2ta-ipc

# [Optional]Set privilege file about security domain, content and format of file refers to "Security Domain Privilege".
# If SD_PRIVILEGE_FILE is not set, ${SDK_HOME}/examples/sd.privilege is used as the security domain privilege (maximum privilege) configuration by default.
#SD_PRIVILEGE_FILE = sd.privilege

# At the end, include the file corresponding to the target type user want to create.
# - In this example, final file is a executable program.
include ${SDK_HOME}/make/ree-program.mk
```

The targets of Makefile：
* **all**: build this project.
* **clean**: remove the "out" folder which contains all intermediate files, programs and libs.
* **run**: run the program in correct platform.

The compiler of CA is clang, and the included headers and linked libraries are in linaro-gcc.

### Defect and notice:
* The CA is running in Linux, not Android.

## Example of CA(Android)

See `$SDK_HOME/examples/android/trusty`.
In fact, Makefile in example is just for uniform usage, real configuration file is Android.mk and Application.mk.

```
# The path of SDK_HOME(absolute path or relative path).
SDK_HOME ?= ../../..

# This SDK contains NDK. If other NDK is provided, please set the path of that NDK.
# ANDROID_NDK_HOME := ?

# This script will set ANDROID_NDK_HOME if it is not set,
include ${SDK_HOME}/make/ndk-common.mk

REE_TRUSTY_PATH="./"
MY_NDK_OPTIONS = NDK_PROJECT_PATH=${REE_TRUSTY_PATH} \
    APP_BUILD_SCRIPT=${REE_TRUSTY_PATH}/Android.mk \
    NDK_APPLICATION_MK=${REE_TRUSTY_PATH}/Application.mk \
    NDK_OUT=./out \
    SDK_ANDROID_HOME=${SDK_ANDROID_HOME} \

# use ndk to build.
all:
    @${ANDROID_NDK_HOME}/ndk-build ${MY_NDK_OPTIONS}

# Other targets configured by users.
```

## Example of HA

See `$SDK_HOME/examples/hee/libc/Makefile`.

```
# The path of SDK_HOME(absolute path or relative path).
SDK_HOME ?= ../../..

# Common settings of HA.
include ${SDK_HOME}/make/hee-common.mk

# Source files and compile flags for building.
# - Notice: HA must contains a manifest.c for setting HA uuid and options.
SRC_C  = manifest.c main.c lib/lib.c

# Flags to compiler.
# - must use "+=".
CFLAGS += -I${SDK_HEE_HOME}/trusty/include -I./lib

#  Flags to linker. Notice: linker is NOT ld, but use gcc/clang++, so use "-Wl,xxx" to transfer flags into ld, for example: -Wl,threads.
# - Should use "+=".
LDFLAGS += -L${SDK_HEE_HOME}/lib

# HA binary file name.
# "$TARGET.untrip" file is unstripped image file, and "$TARGET" file is stripped.
TARGET = test-libc

# NOTICE: the arguments for running HA are NOT supported by now.

# At the end, include file corresponding to target type user want to create.
include ${SDK_HOME}/make/hee-program.mk
```

The compiler of HA is clang.

### Defect and notice:
* The ".so" is NOT supported by this SDK, because it can not be loaded dynamically by HEE.
  So, ".so" files should be put into the HEE images before releasing.

## Example of FAR

Far file include a ELF file and many resource files.

See "/garnet/public/nebula_sdk/examples/hee/far/Makefile".

```
# The path of SDK_HOME(absolute path or relative path).
SDK_HOME ?= ../../..

# Common settings of HA.
include ${SDK_HOME}/make/hee-common.mk

CFLAGS +=
LDFLAGS += -lm

# Prebuilt static libraries can be specified here.(e.g., "lib/libtrusty.a")
EXT_LIB +=

SRC_C = main.c

# Resource files that will be exported to far file.
# Format is "item1,item2", one item is "[path in far]=[path in HOST system]".
# Space(TAB/BLANK) must not be in the path.
FAR_FILES += \
    data/test1.dat=data/test1.dat, \
    data/test2.dat=data/test2.dat

TARGET = test-far

# At the end, include "hee-far.mk".
# "out/${TARGET}.far" file will be created.
include ${SDK_HOME}/make/hee-far.mk
```

```
# The path of external file opened by program is "/pkg/[path in far]".
int fd = open("/pkg/data/test1.dat", O_RDONLY);
```

* Far file not use "manifest.c". But a "resources/manifest.json" containing UUID.
* Far file must include a file "meta/sandbox" which is used to configure the services the far app will use.