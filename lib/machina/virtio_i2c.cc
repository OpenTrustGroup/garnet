// SPDX-License-Identifier: BSD-3-Clause

#include "garnet/lib/machina/virtio_i2c.h"
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <fbl/intrusive_hash_table.h>
#include <fbl/unique_ptr.h>
#include <thread>
#include "lib/fxl/logging.h"
#include <zircon/process.h>
#include <zircon/syscalls.h>

namespace machina {

#define VIRTIO_I2C_F_ZERO_LENGTH_REQUEST	0

#define VIRTIO_I2C_MSG_OK     0
#define VIRTIO_I2C_MSG_ERR    1

/*
 * Wire-format structs. MUST byte-match
 *   FE  : src/kernel/modules/virt/spm_v1/virtio_i2c/virtio_i2c.c
 *         (struct virtio_i2c_batch_head / msg_desc / batch_in_hdr)
 *   BE  : src/hypervisor/grt_ddk/garnet/public/lib/grt-i2c.h
 *         (grt_i2c_batch_head_t / grt_i2c_msg_desc_t / grt_i2c_batch_in_hdr_t)
 */
struct virtio_i2c_msg_desc {
	uint16_t addr;
	uint16_t flags;
	uint32_t len;
	uint32_t buf_off;
	uint32_t reserved;
} __attribute__((packed));

struct virtio_i2c_batch_head {
	uint16_t bus;
	uint16_t msg_num;
	uint32_t total_len;
	uint32_t speed;
	uint32_t seq_id;
} __attribute__((packed));

struct virtio_i2c_batch_in_hdr {
	uint8_t  status;
	uint8_t  reserved[3];
	uint32_t completed_msgs;
} __attribute__((packed));

#define VIRTIO_I2C_MSG_FLAG_RD 0x0001u

/*
 * Per-batch transient request state used internally by the bridge.
 * Lives on the stack inside HandleI2CCommand; never touches host-visible
 * memory after the ioctl returns.
 *
 * out_data is OUT direction (host read-only).
 * in_data  is IN  direction (host writes here).
 */
struct virtio_i2c_req {
	struct virtio_i2c_batch_head *head;
	struct virtio_i2c_msg_desc   *descs;
	const uint8_t                *out_data;     /* OUT, read-only on host */
	uint32_t                      out_data_len;
	uint8_t                      *in_data;      /* IN , host writes here  */
	uint32_t                      in_data_len;
	struct virtio_i2c_batch_in_hdr *in_hdr;
};

/*
 * Per-device struct
 */
 struct native_i2c_dev {
    int fd;
    int bus;
};

struct virtio_i2c {
    struct native_i2c_dev *native_i2c[MAX_I2C_DEVICE_NUM];
};

char * native_i2c_configs = ((char*)"0.0,1.0,2.0,3.0,4.0,5.0,6.0,7.0,8.0,9.0,10.0,11.0,12.0");

bool i2c_enable[13] =
    {false, false, true, false, false, true, false, false, false, false, false, false, true};

const char* kI2cNodes[] = {
    "/dev/sys/platform/i2c0/mtk_i2c",
    "/dev/sys/platform/i2c1/mtk_i2c",
    "/dev/sys/platform/i2c2/mtk_i2c",
    "/dev/sys/platform/i2c3/mtk_i2c",
    "/dev/sys/platform/i2c4/mtk_i2c",
    "/dev/sys/platform/i2c5/mtk_i2c",
    "/dev/sys/platform/i2c6/mtk_i2c",
    "/dev/sys/platform/i2c7/mtk_i2c",
    "/dev/sys/platform/i2c8/mtk_i2c",
    "/dev/sys/platform/i2c9/mtk_i2c",
    "/dev/sys/platform/i2c10/mtk_i2c",
    "/dev/sys/platform/i2c11/mtk_i2c",
    "/dev/sys/platform/i2c12/mtk_i2c",
};

const int kI2cNodesCount = sizeof(kI2cNodes) / sizeof(kI2cNodes[0]);

static int dm_strtol(const char *s, char **end, unsigned int base, long *val)
{
	if (!s)
		return -1;

	*val = strtol(s, end, base);
	if ((end && *end == s) || errno == ERANGE)
		return -1;
	return 0;
}

static int dm_strtoi(const char *s, char **end, unsigned int base, int *val)
{
	long l_val;
	int ret;

	l_val = 0;
	ret = dm_strtol(s, end, base, &l_val);
	if (ret == 0)
		*val = (int)l_val;
	return ret;
}

//parse total support i2c bus num
static int virtio_i2c_parse(struct virtio_i2c *vi2c, char *optstr)
{
	int bus = 0;
	int n_i2c = 0;
	char native_path[20];//no used
	char *cstr, *tmp;

	tmp = optstr;

	while ((cstr = strsep(&tmp, ",")) != NULL) {
		if (cstr[0] == '\0')
			continue;
		
		if (dm_strtoi(cstr, NULL, 10, &bus) || (bus < 0) ||
			(bus >= MAX_I2C_DEVICE_NUM))
			continue;

		memset(native_path, 0, 20);
		snprintf(native_path, sizeof(native_path), "/dev/i2cdev%d", bus);
		native_path[sizeof(native_path) - 1] = '\0';

		vi2c->native_i2c[n_i2c] = (struct native_i2c_dev *)calloc(1, sizeof(struct native_i2c_dev));
		vi2c->native_i2c[n_i2c]->bus = bus;
		vi2c->native_i2c[n_i2c]->fd = -1;

		if (n_i2c >= MAX_I2C_DEVICE_NUM) {
			FXL_LOG(ERROR) << "too many adapter, only support " <<MAX_I2C_DEVICE_NUM;
			return 0;
		}
		n_i2c++;
	}
	
	return n_i2c;
}

static struct native_i2c_dev *native_i2c_find(struct virtio_i2c *vi2c, int bus)
{
	int i = 0;
	if (bus >= kI2cNodesCount)
		return NULL;
	for (i =0; i< MAX_I2C_DEVICE_NUM; i++) {
		if (vi2c->native_i2c[i]->bus == bus) {
			return vi2c->native_i2c[i];
		}
	}
	return NULL;
}

/*
 * native_i2c_proc - run one batch transfer through the native driver.
 *
 * The FE has packed @req->out_data with the write-msg payloads (read-msg
 * slots are ignored). After the BE finishes we fill @req->in_data with
 * read results. Both buffers use the per-msg buf_off/len from req->descs.
 *
 * We:
 *   1. memcpy out_data        -> input_memory[bus]  (BE expects writes here)
 *   2. zero  output_memory[bus] for the same range
 *   3. issue one IOCTL_GRT_I2C_BATCH_TRANSFER carrying head+descs
 *   4. memcpy output_memory[bus] -> in_data         (read data lands here)
 *
 * The BE driver loops over descs internally, so this is exactly one
 * ioctl per virtio request regardless of msg_num.
 */
uint8_t VirtioI2C::native_i2c_proc(struct virtio_i2c *vi2c,
                                   struct virtio_i2c_req *req,
                                   VirtioQueue* /*queue*/, uint16_t /*head*/) {
    struct native_i2c_dev *native_i2c;
    uint16_t bus = req->head->bus;
    uint16_t msg_num = req->head->msg_num;
    uint32_t total_len = req->head->total_len;

    if (bus >= MAX_I2C_DEVICE_NUM) {
        FXL_LOG(ERROR) << __func__ << ": invalid bus " << bus;
        return VIRTIO_I2C_MSG_ERR;
    }

    /*
     * Serialise concurrent FE requests targeting the same bus.
     */
    std::lock_guard<std::mutex> bus_lk(bus_mtx_[bus]);

    if (msg_num == 0 || msg_num > GRT_I2C_BATCH_MAX_MSGS) {
        FXL_LOG(ERROR) << __func__ << ": invalid msg_num " << msg_num;
        return VIRTIO_I2C_MSG_ERR;
    }

    if (total_len > GRT_I2C_BATCH_MAX_TOTAL_LEN ||
        total_len > MAX_I2C_BUF_SIZE ||
        total_len > req->out_data_len ||
        total_len > req->in_data_len) {
        FXL_LOG(ERROR) << __func__ << ": total_len " << total_len
                       << " out of range (out_len="
                       << req->out_data_len
                       << ", in_len="
                       << req->in_data_len << ")";
        return VIRTIO_I2C_MSG_ERR;
    }

    native_i2c = native_i2c_find(vi2c, bus);
    if (!native_i2c) {
        FXL_LOG(ERROR) << __func__ << ": could not find device for bus " << bus;
        return VIRTIO_I2C_MSG_ERR;
    }

    if (input_memory[bus] == 0 || output_memory[bus] == 0) {
        FXL_LOG(ERROR) << __func__
                       << ": shared memory not mapped, bus=" << bus
                       << " in=" << input_memory[bus]
                       << " out=" << output_memory[bus];
        return VIRTIO_I2C_MSG_ERR;
    }

    /*
     * NOTE: avoid FXL_LOG(INFO) on the hot path. Each LogMessage creates
     * an ostringstream, allocates buffers and triggers writes to fdio's
     * stderr VMO. Under multi-OS concurrency that produced enough memory
     * pressure to fault inside libc memcpy when host kernel could not
     * commit a new page on first write. Use VLOG(2)/dlog if needed.
     */

    /*
     * Stage 1: pack write data into BE input area.
     * out_data is guest-OUT memory (host read-only) -> safe to read.
     * input_memory is host-anonymous VMO mapping     -> safe to write.
     */
    if (total_len > 0) {
        memcpy((char *)input_memory[bus], req->out_data, total_len);
        memset((char *)output_memory[bus], 0, total_len);
    }

    /*
     * Build the batch ioctl payload: head + descs[msg_num].
     * Allocated on stack (max ~16*16 + 16 = 272 bytes), no heap churn.
     */
    uint8_t  payload[sizeof(grt_i2c_batch_head_t) +
                     GRT_I2C_BATCH_MAX_MSGS * sizeof(grt_i2c_msg_desc_t)];
    size_t   payload_len = sizeof(grt_i2c_batch_head_t) +
                           msg_num * sizeof(grt_i2c_msg_desc_t);

    grt_i2c_batch_head_t *bh = reinterpret_cast<grt_i2c_batch_head_t *>(payload);
    bh->bus       = bus;
    bh->msg_num   = msg_num;
    bh->total_len = total_len;
    bh->speed     = req->head->speed;
    bh->seq_id    = req->head->seq_id;

    grt_i2c_msg_desc_t *out_descs = reinterpret_cast<grt_i2c_msg_desc_t *>(
        payload + sizeof(grt_i2c_batch_head_t));
    for (uint16_t i = 0; i < msg_num; i++) {
        out_descs[i].addr     = req->descs[i].addr;
        out_descs[i].flags    = req->descs[i].flags;
        out_descs[i].len      = req->descs[i].len;
        out_descs[i].buf_off  = req->descs[i].buf_off;
        out_descs[i].reserved = 0;
    }

    grt_i2c_batch_in_hdr_t resp = {};

    /*
     * Issue the batch ioctl via the fdio wrapper. Variable-length input
     * (head + N msg_desc) -> fixed-size out (grt_i2c_batch_in_hdr_t).
     *
     * fdio_ioctl semantics: return >= 0 on success (= bytes written to
     * out_buf), or a negative zx_status_t on failure.
     *
     * No retry loop: the BE driver's batch path takes the per-bus mutex
     * with a blocking mtx_lock(), so it will NEVER return ZX_ERR_SHOULD_WAIT.
     * Any non-OK return is therefore a real error and is surfaced to the
     * FE immediately. This avoids the old busy-wait cost (up to 50s of
     * vCPU spin) and the WARNING log storm under contention.
     */
    ssize_t rc = ioctl_grt_i2c_batch_transfer(native_i2c->fd,
                                              (const grt_i2c_batch_head_t *)payload,
                                              payload_len,
                                              &resp);
    if (rc < 0) {
        FXL_LOG(ERROR) << "I2C BE batch ioctl failed, seq="
                       << bh->seq_id << ", bus=" << bus
                       << ", msg_num=" << msg_num
                       << ", rc=" << rc;
        return VIRTIO_I2C_MSG_ERR;
    }

    if ((size_t)rc < sizeof(resp)) {
        FXL_LOG(ERROR) << "I2C BE batch short response: " << rc
                       << " < " << sizeof(resp) << ", seq=" << bh->seq_id;
        return VIRTIO_I2C_MSG_ERR;
    }

    /*
     * Stage 4: copy read-data back into the FE-visible IN buffer.
     * in_data is mapped IN on the wire so the host is allowed to write.
     */
    if (total_len > 0) {
        memcpy(req->in_data, (char *)output_memory[bus], total_len);
    }

    /* Surface BE-reported counters to FE via in_hdr. */
    req->in_hdr->status          = resp.status;
    req->in_hdr->reserved[0]     = 0;
    req->in_hdr->reserved[1]     = 0;
    req->in_hdr->reserved[2]     = 0;
    req->in_hdr->completed_msgs  = resp.completed_msgs;

    return resp.status;
}

VirtioI2C::VirtioI2C(const PhysMem& phys_mem)
    : VirtioDeviceBase(phys_mem) {

    int i = 0, i2c_cnt = 0;

    zx_status_t loop_status = m_i2c_loop.StartThread("virtio-i2c");
    if (loop_status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to start virtio-i2c loop: " << loop_status;
      return;
    }
    loop_started_ = true;
    m_async = m_i2c_loop.async();

  	zx_status_t status = queue(0)->PollAsync(
      m_async, &single_queue, &VirtioI2C::QueueHandler, this);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to PollAsync: " << status;
      return;
    }

    char * opts = strdup(native_i2c_configs);

    vi2c = (struct virtio_i2c *)calloc(1, sizeof(struct virtio_i2c));
    if (!vi2c) {
      FXL_LOG(ERROR) << ("calloc returns NULL\n");
      free(opts);
      return;
    }
    memset(vi2c, 0, sizeof(struct virtio_i2c));

    i2c_cnt = virtio_i2c_parse(vi2c, opts);

    if (i2c_cnt <= 0) {
      FXL_LOG(ERROR) << "failed to parse i2cdev\n";
      free(opts);
      return;
    }

    config_.i2c_num = i2c_cnt;
    for (i = 0; i < i2c_cnt; i++) {
      config_.bus[i] = vi2c->native_i2c[i]->bus;
    }

    free(opts);

    add_device_features(1 << VIRTIO_I2C_F_ZERO_LENGTH_REQUEST);//not understand

    for (i = 0; i < kI2cNodesCount; i++) {
      if (!i2c_enable[i])
        continue;

		  if ((fd = open(kI2cNodes[i], O_RDWR)) < 0) {
			  FXL_LOG(ERROR) << "failed to open i2c dev " << i;
        continue;
		  }
		  vi2c->native_i2c[i]->fd = fd;

		  status = InitI2cTrans(i, vi2c->native_i2c[i]->fd);
		  if (status != ZX_OK) {
		  FXL_LOG(ERROR) << "Failed to InitI2cTrans: " << status;
		}
  }
  return;
}

VirtioI2C::~VirtioI2C() {
    if (loop_started_) {
        single_queue.Cancel(m_async);
        m_i2c_loop.Quit();
        m_i2c_loop.JoinThreads();
        loop_started_ = false;
    }

    ReleaseI2cTrans();

    if (vi2c) {
        for (int i = 0; i < MAX_I2C_DEVICE_NUM; ++i) {
            free(vi2c->native_i2c[i]);
            vi2c->native_i2c[i] = nullptr;
        }
    }
    free(vi2c);
    vi2c = nullptr;
}

zx_status_t VirtioI2C::InitI2cTrans(int bus_id, int fd) {
	//in buf
    zx_status_t status = zx_vmo_create(MAX_I2C_BUF_SIZE, 0, &trans[bus_id].input_vmo);
    if (status != ZX_OK) {
        FXL_LOG(ERROR) << "create VMO failed:" << status;
        // goto fail;
    }

    /*
     * Commit all pages eagerly so the first write does not trigger a
     * demand-fault in the hot path. We observed VMM data aborts with
     * "failed to fault in or grab existing page" when the very first
     * memcpy after VMM start touched an uncommitted VMO page under
     * memory pressure.
     */
    status = zx_vmo_op_range(trans[bus_id].input_vmo, ZX_VMO_OP_COMMIT,
                             0, MAX_I2C_BUF_SIZE, NULL, 0);
    if (status != ZX_OK) {
        FXL_LOG(WARNING) << "input_vmo commit failed bus=" << bus_id
                         << " status=" << status;
    }

    if ((status = zx_vmar_map(zx_vmar_root_self(),
                ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, 0, trans[bus_id].input_vmo, 0, MAX_I2C_BUF_SIZE,
                (uintptr_t*)&input_memory[bus_id])) < 0) {
        // goto fail;
        FXL_LOG(ERROR) << "zx_vmar_map failed:" << status;
    }

    /* Touch every page so any lingering CoW / lazy commit completes
     * before the device starts taking traffic. */
    if (input_memory[bus_id]) {
        memset((void *)input_memory[bus_id], 0, MAX_I2C_BUF_SIZE);
    }

	//out buf
    status = zx_vmo_create(MAX_I2C_BUF_SIZE, 0, &trans[bus_id].output_vmo);
    if (status != ZX_OK) {
        FXL_LOG(ERROR) << "create VMO failed:" << status;
        // goto fail;
    }

    status = zx_vmo_op_range(trans[bus_id].output_vmo, ZX_VMO_OP_COMMIT,
                             0, MAX_I2C_BUF_SIZE, NULL, 0);
    if (status != ZX_OK) {
        FXL_LOG(WARNING) << "output_vmo commit failed bus=" << bus_id
                         << " status=" << status;
    }

    if ((status = zx_vmar_map(zx_vmar_root_self(),
                ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, 0, trans[bus_id].output_vmo, 0, MAX_I2C_BUF_SIZE,
                (uintptr_t*)&output_memory[bus_id])) < 0) {
        // goto fail;
        FXL_LOG(ERROR) << "zx_vmar_map failed:" << status;
    }

    if (output_memory[bus_id]) {
        memset((void *)output_memory[bus_id], 0, MAX_I2C_BUF_SIZE);
    }

    trans[bus_id].size = MAX_I2C_BUF_SIZE;
    grt_i2c_transfer_t driver_trans = trans[bus_id];
    driver_trans.input_vmo = ZX_HANDLE_INVALID;
    driver_trans.output_vmo = ZX_HANDLE_INVALID;

    status = zx_handle_duplicate(trans[bus_id].input_vmo,
                                 ZX_RIGHT_SAME_RIGHTS,
                                 &driver_trans.input_vmo);
    if (status != ZX_OK) {
        FXL_LOG(ERROR) << "i2c duplicate input vmo failed:" << status;
        return status;
    }

    status = zx_handle_duplicate(trans[bus_id].output_vmo,
                                 ZX_RIGHT_SAME_RIGHTS,
                                 &driver_trans.output_vmo);
    if (status != ZX_OK) {
        FXL_LOG(ERROR) << "i2c duplicate output vmo failed:" << status;
        zx_handle_close(driver_trans.input_vmo);
        return status;
    }

    status = ioctl_grt_i2c_create_transfer_handles(fd, &driver_trans);
	
    if (status != ZX_OK) {
        FXL_LOG(ERROR) << "virtio be create i2c handles failed:" << status;
    }

    return status;
}

zx_status_t VirtioI2C::ReleaseI2cTrans(void) {
    for (int i = 0; i < MAX_I2C_DEVICE_NUM; ++i) {
        if (vi2c && vi2c->native_i2c[i] && vi2c->native_i2c[i]->fd >= 0) {
            ioctl_grt_i2c_close_transfer_handles(vi2c->native_i2c[i]->fd);
            close(vi2c->native_i2c[i]->fd);
            vi2c->native_i2c[i]->fd = -1;
        }

        if (input_memory[i] != 0) {
            zx_vmar_unmap(zx_vmar_root_self(), input_memory[i],
                          MAX_I2C_BUF_SIZE);
            input_memory[i] = 0;
        }

        if (output_memory[i] != 0) {
            zx_vmar_unmap(zx_vmar_root_self(), output_memory[i],
                          MAX_I2C_BUF_SIZE);
            output_memory[i] = 0;
        }

        if (trans[i].input_vmo != ZX_HANDLE_INVALID) {
            zx_handle_close(trans[i].input_vmo);
            trans[i].input_vmo = ZX_HANDLE_INVALID;
        }

        if (trans[i].output_vmo != ZX_HANDLE_INVALID) {
            zx_handle_close(trans[i].output_vmo);
            trans[i].output_vmo = ZX_HANDLE_INVALID;
        }
    }

    return ZX_OK;
}

zx_status_t VirtioI2C::QueueHandler(VirtioQueue* queue,
                                    uint16_t head,
                                    uint32_t* used,
                                    void* ctx) {
	VirtioI2C* i2c = reinterpret_cast<VirtioI2C*>(ctx);
  return i2c->HandleI2CCommand(queue, head, used);
}

/*
 * Batch protocol descriptor chain (5 segments):
 *   desc[0] OUT : virtio_i2c_batch_head
 *   desc[1] OUT : virtio_i2c_msg_desc[msg_num]
 *   desc[2] OUT : out_data    (write-msg payloads, host read-only)
 *   desc[3] IN  : in_data     (read-msg payloads, host writes here)
 *   desc[4] IN  : virtio_i2c_batch_in_hdr
 *
 * IMPORTANT: data_buf was previously single-shared OUT, but virtio
 * forbids the host from writing OUT pages and that caused a data abort
 * in the VMM's libc memcpy. The new layout keeps writes/reads on
 * separate descriptors so each side touches only memory it is allowed
 * to touch.
 */
zx_status_t VirtioI2C::HandleI2CCommand(VirtioQueue* queue,
                                        uint16_t head,
                                        uint32_t* used) {
	virtio_desc_t d_head, d_descs, d_out, d_in_data, d_in_hdr;
	struct virtio_i2c_req req;

	queue->ReadDesc(head, &d_head);
	if (!d_head.has_next) {
		FXL_LOG(ERROR) << "virtio_i2c: chain missing desc[1]";
		return ZX_OK;
	}
	queue->ReadDesc(d_head.next, &d_descs);
	if (!d_descs.has_next) {
		FXL_LOG(ERROR) << "virtio_i2c: chain missing desc[2]";
		return ZX_OK;
	}
	queue->ReadDesc(d_descs.next, &d_out);
	if (!d_out.has_next) {
		FXL_LOG(ERROR) << "virtio_i2c: chain missing desc[3]";
		return ZX_OK;
	}
	queue->ReadDesc(d_out.next, &d_in_data);
	if (!d_in_data.has_next) {
		FXL_LOG(ERROR) << "virtio_i2c: chain missing desc[4]";
		return ZX_OK;
	}
	queue->ReadDesc(d_in_data.next, &d_in_hdr);

	if (d_head.len < sizeof(struct virtio_i2c_batch_head)) {
		FXL_LOG(ERROR) << "virtio_i2c: head desc too small " << d_head.len;
		return ZX_OK;
	}
	if (d_in_hdr.len < sizeof(struct virtio_i2c_batch_in_hdr)) {
		FXL_LOG(ERROR) << "virtio_i2c: in_hdr desc too small " << d_in_hdr.len;
		return ZX_OK;
	}

	/*
	 * Strict virtio direction check. The FE MUST submit:
	 *   desc[0..2] writable=false (OUT, host read-only)
	 *   desc[3..4] writable=true  (IN , host write-allowed)
	 *
	 * If the FE accidentally submits the wrong direction, our memcpy
	 * into d_in_data.addr would touch a host-read-only page and abort
	 * inside libc. Fail fast here rather than crashing the VMM.
	 */
	if (d_head.writable    || d_descs.writable || d_out.writable ||
	    !d_in_data.writable || !d_in_hdr.writable) {
		FXL_LOG(ERROR) << "virtio_i2c: bad desc direction "
		               << "head.w=" << d_head.writable
		               << " descs.w=" << d_descs.writable
		               << " out.w=" << d_out.writable
		               << " in_data.w=" << d_in_data.writable
		               << " in_hdr.w=" << d_in_hdr.writable
		               << " (head=" << d_head.addr
		               << " len=" << d_head.len
		               << ", descs len=" << d_descs.len
		               << ", out len=" << d_out.len
		               << ", in_data len=" << d_in_data.len
		               << ", in_hdr len=" << d_in_hdr.len << ")";
		if (d_in_hdr.writable && d_in_hdr.len >= sizeof(struct virtio_i2c_batch_in_hdr)) {
			struct virtio_i2c_batch_in_hdr *ih =
				(struct virtio_i2c_batch_in_hdr *)d_in_hdr.addr;
			ih->status = VIRTIO_I2C_MSG_ERR;
			ih->completed_msgs = 0;
			*used += sizeof(struct virtio_i2c_batch_in_hdr);
		}
		return ZX_OK;
	}

	req.head         = (struct virtio_i2c_batch_head *)d_head.addr;
	req.descs        = (struct virtio_i2c_msg_desc *)d_descs.addr;
	req.out_data     = (const uint8_t *)d_out.addr;
	req.out_data_len = d_out.len;
	req.in_data      = (uint8_t *)d_in_data.addr;
	req.in_data_len  = d_in_data.len;
	req.in_hdr       = (struct virtio_i2c_batch_in_hdr *)d_in_hdr.addr;

	uint16_t msg_num = req.head->msg_num;
	if (d_descs.len < (size_t)msg_num * sizeof(struct virtio_i2c_msg_desc)) {
		FXL_LOG(ERROR) << "virtio_i2c: desc array too small ("
		               << d_descs.len << " < "
		               << msg_num * sizeof(struct virtio_i2c_msg_desc)
		               << ")";
		req.in_hdr->status = VIRTIO_I2C_MSG_ERR;
		req.in_hdr->completed_msgs = 0;
		*used += sizeof(struct virtio_i2c_batch_in_hdr);
		return ZX_OK;
	}

	/* Hot path: no INFO log here. Strict desc-direction check
	 * already guarantees memcpy targets are valid before we run. */

	req.in_hdr->status         = native_i2c_proc(vi2c, &req, queue, head);
	if (req.in_hdr->status != VIRTIO_I2C_MSG_OK)
		req.in_hdr->completed_msgs = 0;

	/*
	 * Report only IN-direction bytes to the FE (in_data + in_hdr).
	 * OUT segments (head, descs, out_data) are read-only on the host
	 * and must NOT be counted in *used.
	 */
	*used += req.in_data_len + sizeof(struct virtio_i2c_batch_in_hdr);
	return ZX_OK;
}

}  // namespace machina
