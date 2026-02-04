# Makefile common part.

SDK_HOME ?= ../../..

# include common setting of makefile.
include ${SDK_HOME}/make/hee-common.mk

CFLAGS += -Wall -Wextra \
		-I./ -I${SDK_HEE_HOME}/trusty/include -I${SDK_HEE_HOME}/nbl_fwk_hee/include -I../common/include -I./nsc/include \
		-I${SDK_HEE_HOME}/boringssl/include -I./service -I./service/secure_storage -I${SDK_HEE_HOME}/nbl_attest/include \
		-I${SDK_HEE_HOME}/nbl_gui/include -Itui -I./syscalls/zx

LDFLAGS += -lpthread \
		-L${SDK_HEE_HOME}/boringssl/lib -lcrypto -L${SDK_HEE_HOME}/nbl_attest/lib -lnbl_attest \
		-L${SDK_HEE_HOME}/nbl_fwk_hee/lib -lnbl_fwk_hee \

# Trusty API libraries must be static, and linked into EXE.
EXT_LIB += ${SDK_HEE_HOME}/trusty/lib/liblibc-trusty.a ${SDK_HEE_HOME}/trusty/lib/librng.a \
			${SDK_HEE_HOME}/trusty/lib/libmtee_api.a \
			${SDK_HEE_HOME}/nbl_nsc/lib/libnbl_nsc.a \
			${SDK_HEE_HOME}/nbl_gui/lib/libnbl_gui.so

SRC_C = fuz_common.c \
	fuz_trusty_param_util.c \
	fuz_random.c \
        syscalls/zx/zx_syscalls.c \
        syscalls/zx/fuz_zx_syscall_param_utils.c \
        syscalls/zx/zx_syscall_test.c \
	intresting_numbers.c \
	syscalls/random.c \
	syscalls/zx_cprng_draw.c \
	syscalls/trusty_syscalls.c \
	service/loader/loader.c \
	service/hwcrypto/hwcrypto.c \
	service/service_utils.c \
	service/secure_storage/secure_storage.c \
	service/secure_storage/storage_utils.c \
	service/secure_storage/fuz_storage_param_utils.c \
	service/attest/attest.c \
	service/license/license.c \
	service/sysinfo/sysinfo.c \
	service/nsc/nsc_utils.c \
	service/nsc/nsc.c \
	service/fwk/fwk.c \
	tui/tui_func.c \
	tui/tui_test.cc \
	time/time.c \
	time/time_test.c \

enable_deprecated_syscalls = true
