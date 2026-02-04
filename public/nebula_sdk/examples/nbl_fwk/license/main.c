// Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <nbl_fwk_ree/err.h>
#include <nbl_fwk_ree/nmt_ree.h>

static char *get_activation_code(const char *machine_code) {
	char *activation_code = NULL;

	printf("The device machine code is %s\n", machine_code);

	/*
	 * TODO Transfer the machine code to Nebula Manufacturing Tools(NMT) and
	 * generate a activation code.
	 */

	/*
	 * The qemu's activation code is "4192846".
	 */
	activation_code = (char *)malloc(sizeof("4192846"));
	strcpy(activation_code, "4192846");

	return activation_code;
}

int main(int argc, char* argv[]) {
	(void)argc; (void)argv;
	int rc = -1;
	uint8_t *machine_code = NULL;
	uint8_t *activation_code = NULL;
	uint32_t size = 0;

	/*
	 * Firstly check activation result. If the device is not activated,
	 * NMT_ERR_LICENSE_CODE_INCORRECT will be returned.
	 */
	do {
		rc = nbl_act_status(NMT_ACT_PRODUCT_BASIC, NULL, NULL);
		if (rc == NBL_ERR_NOT_READY) {
			printf("License service is not ready. Retry again!\n");
			sleep(1);
		}
	} while(rc == NBL_ERR_NOT_READY);

	if (rc == NBL_ERR_NOT_VALID) {
		/*
		 * If the device is not activated, get device machine code.
		 */
		rc = nbl_act_machine_code(&machine_code, &size);
		if (rc != NBL_NO_ERROR) {
			printf("Get device machine code failed (%d).\n", rc);
			goto failed;
		}

		/*
		 * Get the activation code related to the machine_code.
		 */
		activation_code = (uint8_t *)get_activation_code((char *)machine_code);
		if (activation_code) {
			rc = nbl_act_license_code(activation_code,
					strlen((char *) activation_code) + 1,
					NMT_ACT_PRODUCT_BASIC, NULL, NULL);
			if (rc != NBL_NO_ERROR) {
				printf("Device activation failed (%d).\n", rc);
			} else {
				printf("Device activation success.\n");
			}
		} else {
			printf("Get activation code failed.\n");
		}
	} else if (rc == NBL_NO_ERROR) {
		printf("The device is already activated.\n");
	} else {
		printf("Other errors (%d)\n", rc);
	}

	// Developer shall disable device factory mode.
	// But here comment the source for debug, because if disable factory mode, this mode will NEVER be restored.
	// rc = nbl_plt_is_device_close();

failed:
	if (machine_code) {
		free(machine_code);
		machine_code = NULL;
	}

	if (activation_code) {
		free(activation_code);
		activation_code = NULL;
	}

	return rc;
}
