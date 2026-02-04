#ifndef VTRUST_CP_REE_LIB_H
#define VTRUST_CP_REE_LIB_H

#define NBL_CP_REE_SUCCESS                            0
#define NBL_CP_REE_OTHER_FAILED                      -1
#define NBL_CP_REE_PARAM_NULL                        -2
#define NBL_CP_REE_BUFFER_OUT_OF_SIZE                -3
#define NBL_CP_REE_ADD_DATA_FAILED                   -5
#define NBL_CP_REE_CONNECT_TA_FAILED                 -6
#define NBL_CP_REE_SEND_DATA_FAILED                  -7
#define NBL_CP_REE_MALLOC_FAILED                     -8
#define NBL_CP_REE_RES_DATA_OUT_OF_BUFFER            -9

#define NBL_CP_TEE_SUCCESS                            0
#define NBL_CP_TEE_OTHER_FAILED                      -1
#define NBL_CP_TEE_PARAM_NULL                        -2
#define NBL_CP_TEE_TA_GET_DATA_FAILED                -3
#define NBL_CP_TEE_VERIFY_PUBLIC_KEY_FAILED          -4
#define NBL_CP_TEE_VERIFY_DATA_SIGNATURE_FAILED      -5
#define NBL_CP_TEE_INIT_MP_FAILED                    -6
#define NBL_CP_TEE_DO_MP_FAILED                      -7
#define NBL_CP_TEE_FINISH_MP_FAILED                  -8
#define NBL_CP_TEE_SET_RES_DATA_FAILED               -9

typedef struct nbl_cp_req_s {
	char *mp_script;
	void* mp_param;
	size_t mp_param_size;
	void *mp_script_sign;
	size_t mp_script_sign_size;
	void *sign_publickey;
	size_t sign_publickey_size;

}__attribute__ ((packed)) nbl_cp_req_t;

typedef struct nbl_cp_res_s {
	int32_t status;
	void* data;
	size_t data_size;
}__attribute__ ((packed)) nbl_cp_res_t;

int nbl_run_ta(nbl_cp_req_t *req_data, nbl_cp_res_t *res_data);

#endif // MICROPY_INCLUDED_UNIX_INPUT_H
