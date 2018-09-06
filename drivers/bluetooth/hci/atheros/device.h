// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_HCI_ATHEROS_DEVICE_H_
#define GARNET_DRIVERS_BLUETOOTH_HCI_ATHEROS_DEVICE_H_

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/bt-hci.h>
#include <ddk/protocol/usb.h>
#include <fbl/mutex.h>
#include <lib/sync/completion.h>
#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "garnet/drivers/bluetooth/lib/hci/control_packets.h"
#include "garnet/drivers/bluetooth/lib/hci/hci.h"

struct qca_version {
  uint32_t rom_version;
  uint32_t patch_version;
  uint32_t ram_version;
  uint32_t ref_clock;
  uint8_t reserved[4];
} __PACKED;

namespace btatheros {

class Device {
 public:
  Device(zx_device_t* device, bt_hci_protocol_t* hci, usb_protocol_t* usb);

  ~Device() = default;

  // Bind the device, invisibly.
  zx_status_t Bind();

  // Load the firmware over usb and complete device initialization.
  // if firmware is loaded, the device will be made visible.
  // otherwise the device will be removed and devhost will
  // unbind.
  zx_status_t LoadFirmware();

  // ddk::Device methods
  void DdkUnbind();
  void DdkRelease();
  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out_proto);
  zx_status_t DdkIoctl(uint32_t op, const void* in_buf, size_t in_len,
                       void* out_buf, size_t out_len, size_t* actual);

 private:
  // Removes the device and leaves an error on the kernel log
  // prepended with |note|.
  // Returns |status|.
  zx_status_t Remove(zx_status_t status, const char* note);

  // Build a synchronous USB Request packet
  // |req| lifetime needs to be as long as the underlying
  // usb_request_t.
  zx_status_t UsbRequest(usb_request_t* req);

  // Load the Qualcomm firmware in RAM
  zx_status_t LoadRAM(const qca_version& ver);

  // Load the Qualcomm firmware in NVM
  zx_status_t LoadNVM(const qca_version& ver);

  // Makes the device visible and leaves |note| on the kernel log.
  // Returns ZX_OK.
  zx_status_t Appear(const char* note);

  // Maps the firmware referenced by |name| into memory.
  // Returns the vmo that the firmware is loaded into or ZX_HANDLE_INVALID if it
  // could not be loaded.
  // Closing this handle will invalidate |fw_addr|, which
  // receives a pointer to the memory.
  // |fw_size| receives the size of the firmware if valid.
  zx_handle_t MapFirmware(const char* name, uintptr_t* fw_addr,
                          size_t* fw_size);

  zx_device_t* parent_;
  zx_device_t* zxdev_;

  fbl::Mutex mutex_;

  // Synchronization for the USB endpoint
  sync_completion_t completion_ __TA_GUARDED(mutex_);

  bt_hci_protocol_t hci_;
  usb_protocol_t usb_;

  bool firmware_loaded_ __TA_GUARDED(mutex_);
};

}  // namespace btatheros

#endif  // GARNET_DRIVERS_BLUETOOTH_HCI_ATHEROS_DEVICE_H_
