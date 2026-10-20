// SPDX-License-Identifier: BSD-3-Clause

#ifndef GARNET_LIB_MACHINA_VIRTIO_I2C_H_
#define GARNET_LIB_MACHINA_VIRTIO_I2C_H_

#include <fbl/intrusive_hash_table.h>
#include <fbl/unique_ptr.h>
#include <lib/async/cpp/wait.h>
#include <virtio/virtio_ids.h>
#include <zircon/compiler.h>
#include <zircon/types.h>
#include "garnet/lib/machina/virtio_device.h"
#include <lib/async-loop/cpp/loop.h>
#include <mutex>

namespace machina {

#define SPI_IOC_MAGIC			'k'

/**
 * struct i2c_ioc_transfer - describes a single SPI transfer
 * @tx_buf: Holds pointer to userspace buffer with transmit data, or null.
 *	If no data is provided, zeroes are shifted out.
 * @rx_buf: Holds pointer to userspace buffer for receive data, or null.
 * @len: Length of tx and rx buffers, in bytes.
 * @speed_hz: Temporary override of the device's bitrate.
 * @bits_per_word: Temporary override of the device's wordsize.
 * @delay_usecs: If nonzero, how long to delay after the last bit transfer
 *	before optionally deselecting the device before the next transfer.
 * @cs_change: True to deselect device before starting the next transfer.
 *
 * This structure is mapped directly to the kernel spi_transfer structure;
 * the fields have the same meanings, except of course that the pointers
 * are in a different address space (and may be of different sizes in some
 * cases, such as 32-bit i386 userspace over a 64-bit x86_64 kernel).
 * Zero-initialize the structure, including currently unused fields, to
 * accommodate potential future updates.
 *
 * SPI_IOC_MESSAGE gives userspace the equivalent of kernel spi_sync().
 * Pass it an array of related transfers, they'll execute together.
 * Each transfer may be half duplex (either direction) or full duplex.
 *
 *	struct i2c_ioc_transfer mesg[4];
 *	...
 *	status = ioctl(fd, SPI_IOC_MESSAGE(4), mesg);
 *
 * So for example one transfer might send a nine bit command (right aligned
 * in a 16-bit word), the next could read a block of 8-bit data before
 * terminating that command by temporarily deselecting the chip; the next
 * could send a different nine bit command (re-selecting the chip), and the
 * last transfer might write some register values.
 */
#include <garnet/public/lib/grt-i2c.h>
struct i2c_ioc_transfer {
	uint64_t buf;
	uint32_t len;
	uint32_t speed_hz;
    uint16_t addr;
	uint16_t flags;
	uint32_t mode;

	/* If the contents of 'struct i2c_ioc_transfer' ever change
	 * incompatibly, then the ioctl number (currently 0) must change;
	 * ioctls with constant size fields get a bit more in the way of
	 * error checking than ones (like this) where that field varies.
	 *
	 * NOTE: struct layout is the same in 64bit and 32bit userspace.
	 */
};

/* not all platforms use <asm-generic/ioctl.h> or _IOC_TYPECHECK() ... */

class VirtioI2C;

#define VIRTIO_I2C_Q_COUNT 3
#define MAX_I2C_DEVICE_NUM		13
/*
 * Shared input/output buffer per i2c bus. Sized to hold the largest
 * supported batch transfer (see GRT_I2C_BATCH_MAX_TOTAL_LEN in grt-i2c.h).
 * Must be >= GRT_I2C_BATCH_MAX_TOTAL_LEN.
 */
#define MAX_I2C_BUF_SIZE 4096
#define VIRTIO_ID_I2C_ADAPTER 34

typedef struct virtio_i2c_config {
  uint8_t i2c_num;
  uint8_t bus[MAX_I2C_DEVICE_NUM];
  uint8_t chip_select[MAX_I2C_DEVICE_NUM];
} __PACKED virtio_i2c_config_t;

// Virtio I2C device.
class VirtioI2C : public VirtioDeviceBase<VIRTIO_ID_I2C_ADAPTER, VIRTIO_I2C_Q_COUNT,
    virtio_i2c_config_t> {
 public:
  VirtioI2C(const PhysMem& phys_mem);
  ~VirtioI2C() override;

  virtual zx_status_t HandleI2CCommand(VirtioQueue* queue,
                               uint16_t head,
                               uint32_t* used);

 protected:
  static zx_status_t QueueHandler(VirtioQueue* queue,
                                  uint16_t head,
                                  uint32_t* used,
                                  void* ctx);
private:
  zx_status_t InitI2cTrans(int bus_id, int fd);
  zx_status_t ReleaseI2cTrans(void);
  uint8_t native_i2c_proc(struct virtio_i2c *vi2c, struct virtio_i2c_req *req, VirtioQueue* queue, uint16_t head);
  async::Wait single_queue;
  grt_i2c_transfer_t trans[MAX_I2C_DEVICE_NUM] = {};
  zx_vaddr_t input_memory[MAX_I2C_DEVICE_NUM] = {};
  zx_vaddr_t output_memory[MAX_I2C_DEVICE_NUM] = {};
  /*
   * Per-bus mutex: serialise concurrent vCPU requests that target the
   * same i2c bus. Each bus has its own pair of shared input/output
   * buffers (input_memory / output_memory), and the BE driver holds
   * a per-bus mutex too. Without this lock, two concurrent FE requests
   * on the same bus race on the shared buffer (memcpy clobber) AND on
   * the desc chain (the second request's data_buf may target memory
   * already returned to the guest by vhost_add_used). That's the
   * symptom we observed as a libc memcpy data abort inside the VMM
   * process.
   */
  std::mutex bus_mtx_[MAX_I2C_DEVICE_NUM];
  // bool fail_next = false;
  struct virtio_i2c *vi2c = nullptr;
  async::Loop m_i2c_loop;
  async_t* m_async = nullptr;
  bool loop_started_ = false;
  int fd = -1;
};

}  // namespace machina



#endif  // GARNET_LIB_MACHINA_VIRTIO_I2C_H_
