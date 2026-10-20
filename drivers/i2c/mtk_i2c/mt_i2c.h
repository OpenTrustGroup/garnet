/*
 * Copyright (c) 2020, Mediatek Inc. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/** @ingroup IP_group_i2c_external_function
* @par Description
*     mtk_i2c_read(): Read data from slave device.
* @param[in]
*     bus_num: I2C bus id where the slave device to be accessed is located.
*              Since adsp only uses I2C2, the id needs to be set to 2.
*     device_addr: The slave device 7-bits address.
*     speed_khz: Setting i2c transfer speed,example 100, 400, 1000.
*     buffer: Read data buffer.
*     len: Read data length.
* @return
*     0: i2c transmission successful.
*     Negative value: i2c transmission failed.
* @par Boundary case and Limitation
*     bus_num only support I2C2 due to adsp only user i2c2.
* @par Error case and Error handling
*     none.
* @par Refer to the source code
*/
int mtk_i2c_read(unsigned char bus_num, unsigned char device_addr, unsigned int speed_khz,
		 unsigned char *buffer, unsigned char *vbuffer, unsigned int len);

/** @ingroup IP_group_i2c_external_function
* @par Description
*    mtk_i2c_write(): Write data to slave device.
* @param[in]
*     bus_num: I2C bus id where the slave device to be accessed is located.
*              Since adsp only uses I2C2, the id needs to be set to 2.
*     device_addr: The slave device 7-bits address.
*     speed_khz: Setting i2c transfer speed,example 100, 400, 1000.
*     buffer: write data buffer, Contains the slave device register address and
*             the data to be set.
*     len: Write data length.
* @return
*     0: i2c transmission successful.
*     Negative value: i2c transmission failed.
* @par Boundary case and Limitation
*     bus_num only support I2C2 due to adsp only user i2c2.
* @par Error case and Error handling
*     none.
* @par Refer to the source code
*/
int mtk_i2c_write(unsigned char bus_num, unsigned char device_addr,
	unsigned int speed_khz, unsigned char *buffer, unsigned char *vbuffer, unsigned int len);

int mtk_i2c_write_only(unsigned char bus_num, unsigned char device_addr,
	unsigned int speed_khz, unsigned char *buffer, unsigned int len, void *i2c_addr);

/** @ingroup IP_group_i2c_external_function
* @par Description
*    mtk_i2c_write_read: Read the value of the specified register from the device.
* @param[in]
*     bus_num: I2C bus id where the slave device to be accessed is located.
*     Since adsp only uses I2C2, the id needs to be set to 2.
*     device_addr: The slave device 7-bits address.
*     speed_khz: Setting i2c transfer speed,example 100, 400, 1000.
*     write_buffer: The slave device register address,that is, the register location
*     of the slave device to be read.
*     read_buffer: Read data buffer.
*     write_len: Write data length.
*     read_len: Read data length.
* @return
*     0: i2c transmission successful.
*     Negative value: i2c transmission failed.
* @par Boundary case and Limitation
*     bus_num only support I2C2 due to adsp only user i2c2.
* @par Error case and Error handling
*     none.
* @par Refer to the source code
*/
int mtk_i2c_write_read(unsigned char bus_num, unsigned char device_addr, unsigned int speed_khz,
		unsigned char *write_buffer, unsigned char *write_vbuffer, unsigned char *read_buffer, 
		unsigned char *read_vbuffer, unsigned int write_len, unsigned int read_len);

/** @ingroup IP_group_i2c_external_function
* @par Description
*     i2c_hw_init(): Initialize i2c hardware related setting.
*     It only needs to be called once.usually already called in main.c
* @param[in]
*     none.
* @return
*     0: i2c initialization successful.
*     Negative value: i2c initialization failed.
* @par Boundary case and Limitation
*     none.
* @par Error case and Error handling
*     none.
* @par Refer to the source code
*/
int mtk_i2c_hw_init(void);

/** @ingroup IP_group_i2c_external_function
* @par Description
*     i2c_test(): This API only for test with EEPROM.
* @param[in]
*     bus_num: I2C bus id where the slave device to be accessed is located.
* @return
*     0: i2c transmission successful.
*     Negative value: i2c transmission failed.
* @par Boundary case and Limitation
*     bus_num only support I2C2 due to adsp only user i2c2.
* @par Error case and Error handling
*     none.
* @par Refer to the source code
*/
// int mtk_i2c_test(int bus_id);
