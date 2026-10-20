// SPDX-License-Identifier: BSD-3-Clause

#include "garnet/lib/machina/virtio_spi.h"
#include <fcntl.h>
#include <string.h>
#include <fbl/intrusive_hash_table.h>
#include <fbl/unique_ptr.h>
#include <thread>
#include "lib/fxl/logging.h"
#include <zircon/process.h>
#include <zircon/syscalls.h>
namespace machina {
#define VIRTIO_SPI_F_ZERO_LENGTH_REQUEST	0
#define VIRTIO_SPI_MSG_OK	0
#define VIRTIO_SPI_MSG_ERR	1

constexpr zx_duration_t kSpiCsWorkerIdleDelay = ZX_MSEC(10);

static bool spi_cs_retryable(zx_status_t status) {
	return status == ZX_ERR_ALREADY_BOUND || status == ZX_ERR_SHOULD_WAIT;
}

struct native_spi_dev {
	int fd;
	int bus;
	int chip_select;
};

struct virtio_spi_transfer_head {
	uint32_t spi_clk_hz;
	uint32_t bus;
	uint32_t mode;
	uint32_t speed_hz;
	uint32_t word_delay_usecs;
	uint32_t len;
	uint16_t delay_usecs;
	uint8_t chip_select;
	uint8_t bits_per_word;
	uint8_t cs_change;
	uint8_t tx_en;
	uint8_t rx_en;
};

struct virtio_spi_transfer_end {
	int result;
};

struct virtio_spi_req {
	struct virtio_spi_transfer_head *head;
	uint64_t rx_pa_buf;
	uint64_t tx_pa_buf;
	struct virtio_spi_transfer_end *end;
};

struct virtio_spi_cs {
	bool enable;
	uint32_t bus;
};

static bool spi_cs_is_acquire(const grt_spi_cs_t* spi_cs) {
	return !spi_cs->enable;
}

/*
 * Per-device struct
 */
struct virtio_spi {
	struct native_spi_dev *native_spi[MAX_SPI_DEVICE_NUM];
};
char * native_spi_configs = ((char*)"0.0,1.0,2.0,3.0,4.0,5.0,6.0,7.0");
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
static int virtio_spi_parse(struct virtio_spi *vspi, char *optstr)
{
	int bus = 0, chip = 0;
	int n_spi = 0;
	int fd = 0;
	char native_path[20];
	char *cstr, *lstr, *tmp;
	tmp = optstr;
	while ((cstr = strsep(&tmp, ",")) != NULL) {
		if (cstr[0] == '\0')
			continue;
		lstr = strsep(&cstr, ".");
		if ((lstr[0] == '\0') || (cstr[0] == '\0'))
			continue;

		if (dm_strtoi(lstr, NULL, 10, &bus) || (bus < 0) ||
			(bus >= MAX_SPI_DEVICE_NUM))
			continue;

		if (dm_strtoi(cstr, NULL, 10, &chip) || chip < 0)
			continue;
		if (n_spi >= MAX_SPI_DEVICE_NUM) {
			FXL_LOG(ERROR) << "too many adapter, only support " << MAX_SPI_DEVICE_NUM;
			return 0;
		}
		memset(native_path, 0, 20);
		snprintf(native_path, sizeof(native_path), "/dev/spidev%d.%d", bus, chip);
		native_path[sizeof(native_path) - 1] = '\0';
		printf("spi %s fd %d bus %d chip %d\n", native_path, fd, bus, chip);
		vspi->native_spi[n_spi] = (struct native_spi_dev *)calloc(1, sizeof(struct native_spi_dev));
		if (!vspi->native_spi[n_spi]) {
			FXL_LOG(ERROR) << "failed to allocate native spi config";
			return 0;
		}
		vspi->native_spi[n_spi]->fd = -1;
		vspi->native_spi[n_spi]->bus = bus;
		vspi->native_spi[n_spi]->chip_select = chip;
		n_spi++;
	}

	return n_spi;
}
static struct native_spi_dev *native_spi_find(struct virtio_spi *vspi, int bus)
{
	int i = 0;
	if (!vspi)
		return NULL;
	if (bus >= kSpiNodesCount)
		return NULL;
	for (i =0; i< MAX_SPI_DEVICE_NUM; i++) {
		if (!vspi->native_spi[i]) {
			continue;
		}
		if (vspi->native_spi[i]->bus == bus) {
			return vspi->native_spi[i];
		}
	}
	return NULL;
}
uint8_t VirtioSPI::native_spi_proc(struct virtio_spi *vspi, struct virtio_spi_req *req, VirtioQueue* queue, uint16_t head)
{
	struct native_spi_dev *native_spi;
	uint8_t status;
	native_spi = native_spi_find(vspi, req->head->bus);
	if (!native_spi) {
		FXL_LOG(ERROR) << __func__ << ": could not find device for addr\n";
		return VIRTIO_SPI_MSG_ERR;
	}
	if (native_spi->fd < 0) {
		FXL_LOG(ERROR) << __func__ << ": invalid spi fd for bus " << req->head->bus;
		return VIRTIO_SPI_MSG_ERR;
	}

	trans[req->head->bus].trans_head.spi_clk_hz = req->head->spi_clk_hz;
	trans[req->head->bus].trans_head.bus = req->head->bus;
	trans[req->head->bus].trans_head.mode = req->head->mode;
	trans[req->head->bus].trans_head.speed_hz = req->head->speed_hz;
	trans[req->head->bus].trans_head.word_delay_usecs = req->head->word_delay_usecs;
	trans[req->head->bus].trans_head.len = req->head->len;
	trans[req->head->bus].trans_head.delay_usecs = req->head->delay_usecs;
	trans[req->head->bus].trans_head.chip_select = req->head->chip_select;
	trans[req->head->bus].trans_head.bits_per_word = req->head->bits_per_word;
	trans[req->head->bus].trans_head.cs_change = req->head->cs_change;

	trans[req->head->bus].tx = req->tx_pa_buf;
	trans[req->head->bus].rx = req->rx_pa_buf;
	trans[req->head->bus].size = req->head->len;

	FXL_LOG(ERROR) << "DEBUG_BE_SPI: spi dma transfer size: " << trans[req->head->bus].size << ", tx_pa: " << std::hex << trans[req->head->bus].tx << ", rx_pa: "  << std::hex << trans[req->head->bus].rx;

	status = ioctl_grt_spi_dma_transfer(native_spi->fd, &trans[req->head->bus]);
	if (status != ZX_OK) {
		status = VIRTIO_SPI_MSG_ERR;
	} else {
		status = VIRTIO_SPI_MSG_OK;
	}

	return status;
}

void VirtioSPI::TryCompletePendingCs(uint32_t bus)
{
	struct native_spi_dev *native_spi;
	PendingCsRequest pending;
	zx_status_t status;

	native_spi = native_spi_find(vspi, bus);
	if (!native_spi || native_spi->fd < 0) {
		return;
	}

	while (true) {
		{
			std::lock_guard<std::mutex> lock(pending_cs_lock_);
			if (pending_cs_[bus].empty()) {
				return;
			}
			pending = pending_cs_[bus].front();
		}

		status = ioctl_grt_spi_set_cs(native_spi->fd, &pending.cs);
		if (spi_cs_retryable(status)) {
			return;
		}

		{
			std::lock_guard<std::mutex> lock(pending_cs_lock_);
			if (!pending_cs_[bus].empty() && pending_cs_[bus].front().head == pending.head) {
				pending_cs_[bus].pop_front();
			}
		}

		if (status != ZX_OK) {
			FXL_LOG(ERROR) << "SPI pending CS failed on bus " << bus << ", status " << status;
			pending.queue->Return(pending.head, 0);
			continue;
		}

		pending.queue->Return(pending.head, 0);
		return;
	}
}

zx_status_t VirtioSPI::HandleSPICommandCsDeferred(VirtioQueue* queue, uint16_t head)
{
	virtio_desc_t request_cs_desc;
	struct virtio_spi_cs *spi_cs;
	struct native_spi_dev *native_spi;
	zx_status_t status;
	grt_spi_cs_t cs;

	queue->ReadDesc(head, &request_cs_desc);
	if (request_cs_desc.addr == NULL) {
		FXL_LOG(ERROR) << "request_cs_desc addr is invalid!\n";
		return queue->Return(head, 0);
	}

	spi_cs = (struct virtio_spi_cs *)request_cs_desc.addr;
	native_spi = native_spi_find(vspi, spi_cs->bus);
	if (!native_spi || native_spi->fd < 0) {
		FXL_LOG(ERROR) << "invalid spi device for CS bus " << spi_cs->bus;
		return queue->Return(head, 0);
	}

	cs.bus = spi_cs->bus;
	cs.enable = spi_cs->enable;
	status = ioctl_grt_spi_set_cs(native_spi->fd, &cs);
	if (spi_cs_is_acquire(&cs) && spi_cs_retryable(status)) {
		std::lock_guard<std::mutex> lock(pending_cs_lock_);
		pending_cs_[cs.bus].push_back({ queue, head, cs });
		FXL_LOG(INFO) << "SPI CS acquire queued on bus " << cs.bus << ", status " << status;
		/* Wake the worker so it polls TryCompletePendingCs without
		 * waiting for the kSpiCsWorkerIdleDelay tick. */
		if (cs_wake_event_ != ZX_HANDLE_INVALID) {
			zx_object_signal(cs_wake_event_, 0, ZX_USER_SIGNAL_0);
		}
		return ZX_OK;
	}

	if (status != ZX_OK) {
		FXL_LOG(ERROR) << "SPI CS failed on bus " << cs.bus << ", status " << status;
	}

	status = queue->Return(head, 0);
	if (!spi_cs_is_acquire(&cs)) {
		TryCompletePendingCs(cs.bus);
		/* Releasing CS may unblock another bus's pending acquire; ensure
		 * the worker re-evaluates promptly. */
		if (cs_wake_event_ != ZX_HANDLE_INVALID) {
			zx_object_signal(cs_wake_event_, 0, ZX_USER_SIGNAL_0);
		}
	}
	return status;
}

int VirtioSPI::CsQueueThread(void* ctx)
{
	VirtioSPI* spi = reinterpret_cast<VirtioSPI*>(ctx);
	VirtioQueue* cs_queue = spi->queue(1);

	/*
	 * Worker now exits cleanly when cs_thread_run_ becomes false and the
	 * wake event is signaled by ~VirtioSPI. This eliminates the previous
	 * unbounded `while (true)` loop and the busy 10ms polling.
	 *
	 * Idle wait strategy:
	 *   - Wait on TWO objects via zx_object_wait_many:
	 *       (a) the queue's event (set by guest notifying new descriptors,
	 *           drained by NextAvailLocked when the avail ring empties)
	 *       (b) cs_wake_event_, signaled by:
	 *             * HandleSPICommandCsDeferred when a request was just
	 *               enqueued for retry, or when CS was just released
	 *             * ~VirtioSPI on shutdown
	 *   - When pending_cs_ is non-empty (host CS owned by another OS),
	 *     fall back to a finite timeout so we re-poll periodically.
	 *   - Otherwise block indefinitely until either object signals.
	 */
	while (spi->cs_thread_run_.load()) {
		uint16_t head;
		bool did_work = false;

		while (cs_queue->NextAvail(&head) == ZX_OK) {
			did_work = true;
			zx_status_t status = spi->HandleSPICommandCsDeferred(cs_queue, head);
			if (status != ZX_OK) {
				FXL_LOG(ERROR) << "virtio-spi-cs handler failed: " << status;
			}
			if (!spi->cs_thread_run_.load()) {
				return 0;
			}
		}

		bool has_pending = false;
		for (uint32_t bus = 0; bus < MAX_SPI_DEVICE_NUM; bus++) {
			spi->TryCompletePendingCs(bus);
			{
				std::lock_guard<std::mutex> lock(spi->pending_cs_lock_);
				if (!spi->pending_cs_[bus].empty()) {
					has_pending = true;
				}
			}
		}

		if (!spi->cs_thread_run_.load()) {
			break;
		}
		if (did_work) {
			continue;
		}

		zx_wait_item_t items[2] = {};
		items[0].handle = cs_queue->event();
		items[0].waitfor = VirtioQueue::SIGNAL_QUEUE_AVAIL;
		items[1].handle = spi->cs_wake_event_;
		items[1].waitfor = ZX_USER_SIGNAL_0;

		zx_time_t deadline = has_pending
			? zx_deadline_after(kSpiCsWorkerIdleDelay)
			: ZX_TIME_INFINITE;
		zx_status_t st = zx_object_wait_many(items, 2, deadline);
		(void)st;  /* timeouts and wake signals are both fine; we re-poll */
		if (items[1].pending & ZX_USER_SIGNAL_0) {
			zx_object_signal(spi->cs_wake_event_, ZX_USER_SIGNAL_0, 0);
		}
	}

	return 0;
}

VirtioSPI::VirtioSPI(const PhysMem& phys_mem)
	: VirtioDeviceBase(phys_mem) {
	int i = 0, spi_cnt = 0;
	zx_status_t status;
	char * opts = strdup(native_spi_configs);
	vspi = (struct virtio_spi *)calloc(1, sizeof(struct virtio_spi));
	if (!vspi) {
		FXL_LOG(ERROR) << ("calloc returns NULL\n");
		free(opts);
		return;
	}
	memset(vspi, 0, sizeof(struct virtio_spi));
	spi_cnt = virtio_spi_parse(vspi, opts);
	if (spi_cnt <= 0) {
		FXL_LOG(ERROR) << "failed to parse spidev\n";
		free(opts);
		return;
	}
	config_.spi_num = spi_cnt;
	for (i = 0; i < spi_cnt; i++) {
		config_.bus[i] = vspi->native_spi[i]->bus;
		config_.chip_select[i] = vspi->native_spi[i]->chip_select;
	}
	free(opts);
	add_device_features(1 << VIRTIO_SPI_F_ZERO_LENGTH_REQUEST);
	for (i = 0; i < spi_cnt; i++) {
		int bus = vspi->native_spi[i]->bus;
		if (bus >= kSpiNodesCount) {
			FXL_LOG(ERROR) << "invalid spi bus " << bus;
			continue;
		}
		fd = open(kSpiNodes[bus], O_RDWR);
		if (fd < 0) {
			FXL_LOG(ERROR) << "failed to open spi dev " << bus;
			continue;
		}
		vspi->native_spi[i]->fd = fd;
	}

	status = queue(0)->Poll(&VirtioSPI::QueueHandler, this, "virtio-spi-xfer");
	if (status != ZX_OK) {
		FXL_LOG(ERROR) << "Failed to start transfer queue poll: " << status;
		return;
	}

	/*
	 * Create the wake event before spawning the cs worker. The worker uses
	 * this event to block when there is no work, instead of busy-polling on
	 * a 10ms timer. We also keep ownership of the thread (NOT detached), so
	 * ~VirtioSPI can perform an orderly join. Without the join, when an SOS
	 * process terminates the cs worker continues running against an already-
	 * freed VirtioSPI / vspi, which races with descriptor cleanup and
	 * eventually faults.
	 */
	status = zx_event_create(0, &cs_wake_event_);
	if (status != ZX_OK) {
		FXL_LOG(ERROR) << "Failed to create cs wake event: " << status;
		return;
	}

	cs_thread_run_.store(true);
	int ret = thrd_create_with_name(&cs_thread_, VirtioSPI::CsQueueThread, this,
		"virtio-spi-cs");
	if (ret != thrd_success) {
		FXL_LOG(ERROR) << "Failed to start cs queue thread: " << ret;
		cs_thread_run_.store(false);
		zx_handle_close(cs_wake_event_);
		cs_wake_event_ = ZX_HANDLE_INVALID;
		return;
	}
	cs_thread_started_ = true;
}

VirtioSPI::~VirtioSPI() {
	int i;

	if (queue(0)) {
		queue(0)->Terminate();
		queue(0)->Join();
	}

	/*
	 * Order matters here. The cs worker dereferences members of *this* and
	 * of vspi (via native_spi_find) on every iteration, so we must stop &
	 * join it before freeing those resources. Previously the worker was
	 * detached, so ~VirtioSPI raced with the still-running thread; on SOS
	 * exit this manifested as use-after-free in the worker.
	 */
	if (cs_thread_started_) {
		cs_thread_run_.store(false);
		if (cs_wake_event_ != ZX_HANDLE_INVALID) {
			zx_object_signal(cs_wake_event_, 0, ZX_USER_SIGNAL_0);
		}
		thrd_join(cs_thread_, nullptr);
		cs_thread_started_ = false;
	}
	if (cs_wake_event_ != ZX_HANDLE_INVALID) {
		zx_handle_close(cs_wake_event_);
		cs_wake_event_ = ZX_HANDLE_INVALID;
	}

	/*
	 * Drain any pending CS retry requests. These hold guest virtio queue
	 * heads; if we do not return them, the guest's queue would leak entries
	 * that may be reused by a fresh boot of the same guest after a reset
	 * (which previously caused the worker to "complete" stale heads against
	 * the new ring). Returning them here is harmless because the guest is
	 * already detaching; if the guest is rebooting, it will re-issue any
	 * needed CS acquires after restart.
	 */
	{
		std::lock_guard<std::mutex> lock(pending_cs_lock_);
		for (uint32_t bus = 0; bus < MAX_SPI_DEVICE_NUM; bus++) {
			while (!pending_cs_[bus].empty()) {
				PendingCsRequest pending = pending_cs_[bus].front();
				pending_cs_[bus].pop_front();
				if (pending.queue) {
					pending.queue->Return(pending.head, 0);
				}
			}
		}
	}

	ReleaseSpiTrans();
	if (vspi) {
		for (i = 0; i < MAX_SPI_DEVICE_NUM; i++) {
			free(vspi->native_spi[i]);
		}
	}
	free(vspi);
}

zx_status_t machina::VirtioSPI::ReleaseSpiTrans(void) {
	int i;

	if (!vspi) {
		return ZX_OK;
	}

	for (i = 0; i < MAX_SPI_DEVICE_NUM; i++) {
		if (!vspi->native_spi[i]) {
			continue;
		}
		if (vspi->native_spi[i]->fd >= 0) {
			close(vspi->native_spi[i]->fd);
			vspi->native_spi[i]->fd = -1;
		}
	}

	fd = -1;
	return ZX_OK;
}

zx_status_t VirtioSPI::QueueHandler(VirtioQueue* queue,
									uint16_t head,
									uint32_t* used,
									void* ctx) {
	VirtioSPI* spi = reinterpret_cast<VirtioSPI*>(ctx);
	return spi->HandleSPICommand(queue, head, used);
}

zx_status_t VirtioSPI::HandleSPICommand(VirtioQueue* queue,
                                        uint16_t head,
                                        uint32_t* used) {
	virtio_desc_t request_head_desc;
    virtio_desc_t request_tx_pa_desc;
	virtio_desc_t request_rx_pa_desc;
	virtio_desc_t request_end_desc;

	struct virtio_spi_req req;

	queue->ReadDesc(head, &request_head_desc);

	if (!request_head_desc.has_next) {
		FXL_LOG(ERROR) << " does not contain a tx_pa descriptor";
		return ZX_OK;
	}
	queue->ReadDesc(request_head_desc.next, &request_tx_pa_desc);

    if (!request_tx_pa_desc.has_next) {
        FXL_LOG(ERROR) << "does not contain a rx_pa descriptor";
        return ZX_OK;
    }
    queue->ReadDesc(request_tx_pa_desc.next, &request_rx_pa_desc);

	if (!request_rx_pa_desc.has_next) {
		FXL_LOG(ERROR) << "does not contain a end descriptor";
		return ZX_OK;
	}
    queue->ReadDesc(request_rx_pa_desc.next, &request_end_desc);

	req.head = (struct virtio_spi_transfer_head *)request_head_desc.addr;
	req.end = (struct virtio_spi_transfer_end *)request_end_desc.addr;

    if (request_tx_pa_desc.len != sizeof(uint64_t)) {
        FXL_LOG(ERROR) << "tx_pa descriptor wrong size: " << request_tx_pa_desc.len;
        return ZX_ERR_INVALID_ARGS;
    }
    req.tx_pa_buf = *(uint64_t *)request_tx_pa_desc.addr;

    if (request_rx_pa_desc.len != sizeof(uint64_t)) {
        FXL_LOG(ERROR) << "rx_pa descriptor wrong size: " << request_rx_pa_desc.len;
        return ZX_ERR_INVALID_ARGS;
    }
    req.rx_pa_buf = *(uint64_t *)request_rx_pa_desc.addr;

    req.end->result = native_spi_proc(vspi, &req, queue, head);

	*used += sizeof(struct virtio_spi_transfer_end) + req.head->len;

	return ZX_OK;
}

}
