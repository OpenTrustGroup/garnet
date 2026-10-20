/* SPDX-License-Identifier: GPL-2.0 */


#define MODULE_DEVICE_TABLE(type, name) \
  void *__dev_table_##type = (void *)(name)
