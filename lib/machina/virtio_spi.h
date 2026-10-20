// SPDX-License-Identifier: BSD-3-Clause

#ifndef GARNET_LIB_MACHINA_VIRTIO_SPI_H_
#define GARNET_LIB_MACHINA_VIRTIO_SPI_H_
#include <atomic>
#include <deque>
#include <fbl/intrusive_hash_table.h>
#include <fbl/unique_ptr.h>
#include <mutex>
#include <lib/async/cpp/wait.h>
#include <virtio/virtio_ids.h>
#include <zircon/compiler.h>
#include <zircon/types.h>
#include "garnet/lib/machina/virtio_device.h"
#include <lib/async-loop/cpp/loop.h>
namespace machina {
#define SPI_IOC_MAGIC			'k'
/**
 * struct spi_ioc_transfer - describes a single SPI transfer
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
 *	struct spi_ioc_transfer mesg[4];
 *	...
 *	status = ioctl(fd, SPI_IOC_MESSAGE(4), mesg);
 *
 * So for example one transfer might send a nine bit command (right aligned
 * in a 16-bit word), the next could read a block of 8-bit data before
 * terminating that command by temporarily deselecting the chip; the next
 * could send a different nine bit command (re-selecting the chip), and the
 * last transfer might write some register values.
 */
#include <garnet/public/lib/grt-spi.h>
struct spi_ioc_transfer {
	uint64_t		tx_buf;
	uint64_t		rx_buf;
	uint32_t		len;
  uint32_t		bus;
	uint32_t		speed_hz;
	uint16_t		delay_usecs;
	uint8_t		bits_per_word;
	uint8_t		cs_change;
	uint8_t		tx_nbits;
	uint8_t		rx_nbits;
	uint16_t		pad;
	/* If the contents of 'struct spi_ioc_transfer' ever change
	 * incompatibly, then the ioctl number (currently 0) must change;
	 * ioctls with constant size fields get a bit more in the way of
	 * error checking than ones (like this) where that field varies.
	 *
	 * NOTE: struct layout is the same in 64bit and 32bit userspace.
	 */
};
/* not all platforms use <asm-generic/ioctl.h> or _IOC_TYPECHECK() ... */
#define SPI_MSGSIZE(N) \
	((((N)*(sizeof (struct spi_ioc_transfer))) < (1 << _IOC_SIZEBITS)) \
		? ((N)*(sizeof (struct spi_ioc_transfer))) : 0)
#define SPI_IOC_MESSAGE(N) _IOW(SPI_IOC_MAGIC, 0, char[SPI_MSGSIZE(N)])
/* Read / Write of SPI mode (SPI_MODE_0..SPI_MODE_3) (limited to 8 bits) */
#define SPI_IOC_RD_MODE			_IOR(SPI_IOC_MAGIC, 1, __u8)
#define SPI_IOC_WR_MODE			_IOW(SPI_IOC_MAGIC, 1, __u8)
/* Read / Write SPI bit justification */
#define SPI_IOC_RD_LSB_FIRST		_IOR(SPI_IOC_MAGIC, 2, __u8)
#define SPI_IOC_WR_LSB_FIRST		_IOW(SPI_IOC_MAGIC, 2, __u8)
/* Read / Write SPI device word length (1..N) */
#define SPI_IOC_RD_BITS_PER_WORD	_IOR(SPI_IOC_MAGIC, 3, __u8)
#define SPI_IOC_WR_BITS_PER_WORD	_IOW(SPI_IOC_MAGIC, 3, __u8)
/* Read / Write SPI device default max speed hz */
#define SPI_IOC_RD_MAX_SPEED_HZ		_IOR(SPI_IOC_MAGIC, 4, __u32)
#define SPI_IOC_WR_MAX_SPEED_HZ		_IOW(SPI_IOC_MAGIC, 4, __u32)
/* Read / Write of the SPI mode field */
#define SPI_IOC_RD_MODE32		_IOR(SPI_IOC_MAGIC, 5, __u32)
#define SPI_IOC_WR_MODE32		_IOW(SPI_IOC_MAGIC, 5, __u32)
class VirtioSPI;
#define VIRTIO_SPI_Q_COUNT 3
#define MAX_SPI_DEVICE_NUM		10
#define MAX_SPI_BUF_SIZE 1024
#define VIRTIO_ID_SPI 45
typedef struct virtio_spi_config {
  uint8_t spi_num;
  uint8_t bus[MAX_SPI_DEVICE_NUM];
  uint8_t chip_select[MAX_SPI_DEVICE_NUM];
} __PACKED virtio_spi_config_t;
// Virtio SPI device.
class VirtioSPI : public VirtioDeviceBase<VIRTIO_ID_SPI, VIRTIO_SPI_Q_COUNT,
    virtio_spi_config_t> {
 public:
  VirtioSPI(const PhysMem& phys_mem);
  ~VirtioSPI() override;
  virtual zx_status_t HandleSPICommand(VirtioQueue* queue,
                               uint16_t head,
                               uint32_t* used);
 protected:
  static zx_status_t QueueHandler(VirtioQueue* queue,
                                  uint16_t head,
                                  uint32_t* used,
                                  void* ctx);
private:
  zx_status_t ReleaseSpiTrans(void);
  uint8_t native_spi_proc(struct virtio_spi *vspi, struct virtio_spi_req *req, VirtioQueue* queue, uint16_t head);
  static int CsQueueThread(void* ctx);
  zx_status_t HandleSPICommandCsDeferred(VirtioQueue* queue, uint16_t head);
  void TryCompletePendingCs(uint32_t bus);
  // grt_spi_transfer_t trans[MAX_SPI_DEVICE_NUM];
  grt_spi_dma_transfer_t trans[MAX_SPI_DEVICE_NUM];
  struct PendingCsRequest {
    VirtioQueue* queue;
    uint16_t head;
    grt_spi_cs_t cs;
  };
  std::mutex pending_cs_lock_;
  std::deque<PendingCsRequest> pending_cs_[MAX_SPI_DEVICE_NUM];
  thrd_t cs_thread_;
  /*
   * cs_thread_started_ : true once thrd_create succeeded; ~VirtioSPI uses
   *                      this to know whether it must join.
   * cs_thread_run_     : the cs worker keeps looping while this is true.
   *                      ~VirtioSPI clears it, signals cs_wake_event_,
   *                      then joins.
   * cs_wake_event_     : zx_event_t used to wake the cs worker out of its
   *                      idle wait whenever new work shows up (queue notify
   *                      from guest, or another caller wants the loop to
   *                      re-poll TryCompletePendingCs after releasing CS).
   *                      It is also used to break the worker out of its
   *                      idle wait at shutdown.
   */
  bool cs_thread_started_ = false;
  std::atomic<bool> cs_thread_run_{false};
  zx_handle_t cs_wake_event_ = ZX_HANDLE_INVALID;
  // zx_vaddr_t tx_memory[MAX_SPI_DEVICE_NUM];
  // zx_vaddr_t rx_memory[MAX_SPI_DEVICE_NUM];
  // bool fail_next = false;
  struct virtio_spi *vspi;
  int fd;
};
}  // namespace machina
#endif  // GARNET_LIB_MACHINA_VIRTIO_SPI_H_
