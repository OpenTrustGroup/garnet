// SPDX-License-Identifier: BSD-3-Clause

/*
 * grt_loader_helper.h
 *
 *  Created on: Jul 4, 2018
 *      Author: lijch
 */

#ifndef GARNET_BIN_APPMGR_GRT_TRUSTY_HELPER_H_
#define GARNET_BIN_APPMGR_GRT_TRUSTY_HELPER_H_

#include <string>

#include <fuchsia/cpp/component.h>
#include <nbl_app_manifest.h>

#define LAUNCHER_RESPONSE 1
#define RESTART_FLG_MSG 2
#define EXIT_MSG 3
#define NBL_APP_DEFAULT_HEAP_SIZE 0x500000

typedef struct trusty_mmio {
  uint32_t id;
  uint32_t base;
  uint32_t size;
} trusty_mmio_t;

void enable_trusty_irq(uint32_t process_handle);

void trusty_add_mmio_check(void);

void get_trusty_app_uuid(component::ApplicationPackage* package, uuid_t* uuid);

bool trusty_mem_map_check(uuid_t *uuid,
                          uint32_t base,
                          uint32_t size);

bool is_trusty_app(const char* path);
bool is_loader(const char* path);
bool is_gnapp_loader(const char* path);
zx_handle_t create_ion_heap_vmo(std::string url,
                                const std::vector<::fidl::StringPtr> &ion_args);
zx_handle_t create_heap_vmo(uint64_t heap_size);
void trusty_app_exit(std::string url);

#endif /* GARNET_BIN_APPMGR_GRT_TRUSTY_HELPER_H_ */
