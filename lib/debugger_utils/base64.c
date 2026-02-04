/*
 * Copyrights
 *
 * Portions created or assigned to Cisco Systems, Inc. are
 * Copyright (c) 2014-2016 Cisco Systems, Inc.  All Rights Reserved.
 *
 * This is from "//third_party/hee-trusty/nbl_secmgr_srv/cjose/base64.c"
 */

#include "base64.h"
//#include <cjose/util.h>

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

typedef unsigned char uint8_t;

/**
 * Enumeration of defined error codes.
 */
typedef enum {
    /** No error */
    CJOSE_ERR_NONE = 0,

    /** argument was invalid (beyond invariants) */
    CJOSE_ERR_INVALID_ARG,

    /** context is not in a valid state */
    CJOSE_ERR_INVALID_STATE,

    /** out of memory */
    CJOSE_ERR_NO_MEMORY,

    /** an error returned from the crypto libraries */
    CJOSE_ERR_CRYPTO,

} cjose_errcode;

/**
 * An instance of an error context. Unlike other structures, it
 * is the API user's responsibility to allocate the structure; however
 * the values provided are considered constants, and MUST NOT be
 * deallocated.
 */
typedef struct
{
    /** The error code */
    cjose_errcode code;

    /** The human readable message for the error code */
    const char *message;

    /** The function where the error occured, or "<unknown>"
        if it cannot be determined */
    const char *function;

    /** The file where the error occured */
    const char *file;

    /** The line number in the file where the error occured */
    unsigned long line;

} cjose_err;

// defines
#define B64_BYTE1(ptr) (((*ptr) & 0xfc) >> 2)
#define B64_BYTE2(ptr) ((((*ptr) & 0x03) << 4) | ((*(ptr + 1) & 0xf0) >> 4))
#define B64_BYTE3(ptr) (((*(ptr + 1) & 0x0f) << 2) | ((*(ptr + 2) & 0xc0) >> 6))
#define B64_BYTE4(ptr) (*(ptr + 2) & 0x3f)

/**
 * Temporarily disable compiler warnings, if possible (>=gcc-4.6).
 *
 * In some cases (particularly within macros), certain compiler warnings are
 * unavoidable.  In order to allow these warnings to be treated as errors in
 * most cases, these macros will disable particular warnings only during
 * specific points in the compilation.
 */
#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6)
#define GCC_END_IGNORED_WARNING _Pragma("GCC diagnostic pop")

#define GCC_BEGIN_IGNORED_WARNING_ADDRESS \
    _Pragma("GCC diagnostic push");       \
    _Pragma("GCC diagnostic ignored \"-Waddress\"")
#define GCC_END_IGNORED_WARNING_ADDRESS GCC_END_IGNORED_WARNING
#else
#define GCC_BEGIN_IGNORED_WARNING_ADDRESS
#define GCC_END_IGNORED_WARNING_ADDRESS
#endif /* defined(__GNUC__) && (__GNUC__ > 3) && (__GNUC_MINOR__ > 5) */

const char *cjose_err_message(cjose_errcode code)
{return NULL;}

/**
 * \def CJOSE_ERROR(err, code)
 *
 * Macro to initialize an error context.
 *
 * \param err The pointer to the error context, or NULL if none
 * \param errcode The error code
 */
#define CJOSE_ERROR(err, errcode)                      \
    GCC_BEGIN_IGNORED_WARNING_ADDRESS                  \
    if ((err) != NULL && (errcode) != CJOSE_ERR_NONE)  \
    {                                                  \
        (err)->code = (errcode);                       \
        (err)->message = cjose_err_message((errcode)); \
        (err)->function = __func__;                    \
        (err)->file = __FILE__;                        \
        (err)->line = __LINE__;                        \
    }                                                  \
    GCC_END_IGNORED_WARNING_ADDRESS

// internal data

static const char *ALPHABET_B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// internal functions

static inline bool _encode(const uint8_t *input, size_t inlen, char **output, size_t *outlen, const char *alphabet, cjose_err *err)
{
    if ((inlen > 0 && NULL == input) || (NULL == output) || (NULL == outlen))
    {
        CJOSE_ERROR(err, CJOSE_ERR_INVALID_ARG);
        return false;
    }

    // return empty string on 0 length input
    if (!inlen)
    {
        char *retVal = (char *)malloc(sizeof(char));
        if (!retVal)
        {
            CJOSE_ERROR(err, CJOSE_ERR_NO_MEMORY);
            return false;
        }
        retVal[0] = '\0';
        *output = retVal;
        *outlen = 0;
        return true;
    }

    const bool padit = (ALPHABET_B64 == alphabet);
    size_t rlen = (((inlen + 2) / 3) << 2);
    char *base;

    base = (char *)malloc(sizeof(char) * (rlen + 1));
    if (NULL == base)
    {
        CJOSE_ERROR(err, CJOSE_ERR_NO_MEMORY);
        return false;
    }

    size_t pos = 0, idx = 0;
    while ((idx + 2) < inlen)
    {
        base[pos++] = alphabet[0x3f & (input[idx] >> 2)];
        base[pos++] = alphabet[(0x3f & (input[idx] << 4)) | (0x3f & (input[idx + 1] >> 4))];
        base[pos++] = alphabet[(0x3f & (input[idx + 1] << 2)) | (0x3f & (input[idx + 2] >> 6))];
        base[pos++] = alphabet[0x3f & input[idx + 2]];
        idx += 3;
    }

    if (idx < inlen)
    {
        if ((inlen - 1) == idx)
        {
            base[pos++] = alphabet[0x3f & (input[idx] >> 2)];
            base[pos++] = alphabet[0x3f & (input[idx] << 4)];
            if (padit)
            {
                base[pos++] = '=';
                base[pos++] = '=';
            }
        }
        else
        {
            base[pos++] = alphabet[0x3f & (input[idx] >> 2)];
            base[pos++] = alphabet[(0x3f & (input[idx] << 4)) | (0x3f & (input[idx + 1] >> 4))];
            base[pos++] = alphabet[0x3f & (input[idx + 1] << 2)];
            if (padit)
            {
                base[pos++] = '=';
            }
        }
        rlen = pos;
    }
    base[rlen] = '\0';
    *output = base;
    *outlen = rlen;
    return true;
}

// interface functions

static bool cjose_base64_encode(const uint8_t *input, size_t inlen, char **output, size_t *outlen, cjose_err *err)
{
	bool a = _encode(input, inlen, output, outlen, ALPHABET_B64, err);
    return a;
}

char *base64_encode(const unsigned char *data, size_t input_length, size_t *output_length){
	cjose_err err;
	char *output;
	if (cjose_base64_encode(data, input_length, &output, output_length, &err)) {
		return output;
	} else {
		return NULL;
	}
}

#define MAX_NAME_LEN    1024
#define BUFSIZE         (128 * 1024)
#define B64_SUFFIX      ".base64"
void base64_file_encode(char* file) {
    char outfile[MAX_NAME_LEN];
    FILE  *in, *out;
    int bufsz = BUFSIZE, len;
    size_t outlen;
    unsigned char *buf;
    char *outbuf;

    if (strlen(file) + strlen(B64_SUFFIX) >= sizeof(outfile)) {
        fprintf(stderr, "filename too long\n");
        exit(1);
    }

    do {
        buf = (unsigned char*)malloc(bufsz);
        if (buf == NULL)
            bufsz <<= 1;
    } while (bufsz > 0 && buf == NULL);

    if (buf == NULL) {
        fprintf(stderr, "Allocate memory failed!\n");
        exit(1);
    }

    snprintf(outfile, sizeof(outfile), "%s%s", file, B64_SUFFIX);

    in = fopen(file, "rb");
    if (in == NULL) {
        perror(file);
        exit(1);
    }

    out = fopen(outfile, "w");
    if (out == NULL) {
        fprintf(stderr, "can't open %s\n", outfile);
        exit(1);
    }

    for (;;) {
        len = (int)fread(buf, 1, bufsz, in);
        if (ferror(in)) {
            perror("fread");
            exit(1);
        }
        outbuf = base64_encode(buf, len, &outlen);
        if (outbuf == NULL) {
            fprintf(stderr, "encode base64 failed!\n");
            exit(1);
        }
        if (fwrite(outbuf, 1, outlen, out) != outlen) {
            fprintf(stderr, "failed fwrite!\n");
            exit(1);
        }
        free(outbuf);
        if (len < bufsz)
            break;
    }

    free(buf);

    fclose(in);
    fclose(out);
}
