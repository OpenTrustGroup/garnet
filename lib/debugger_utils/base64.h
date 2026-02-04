/*
 * base64.h
 *
 *  Created on: Jul 5, 2018
 *      Author: lijch
 */

#ifndef THIRD_PARTY_HEE_TRUSTY_GRT_TRUSTY_LOADER_BASE64_BASE64_H_
#define THIRD_PARTY_HEE_TRUSTY_GRT_TRUSTY_LOADER_BASE64_BASE64_H_

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

char *base64_encode(const unsigned char *data, size_t input_length, size_t *output_length);
void base64_file_encode(char* file);

#ifdef __cplusplus
}
#endif

#endif /* THIRD_PARTY_HEE_TRUSTY_GRT_TRUSTY_LOADER_BASE64_BASE64_H_ */
