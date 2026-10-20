/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef NEBULA_SCHED_DUMP_H_
#define NEBULA_SCHED_DUMP_H_
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

#define CROSS_VM_DUMP_VERSION 0x0000000002000003ull

#define MAX_CRITICAL_PATH_COUNT 63

#define CROSS_VM_DUMP_CTRL_SIZE     (0x1000)
#define CROSS_VM_DUMP_SHARE_OFFSET  (CROSS_VM_DUMP_CTRL_SIZE)
#define CROSS_VM_DUMP_SHARE_SIZE (0x1000)

#define CROSS_VM_TOTAL_SIZE (CROSS_VM_DUMP_SHARE_SIZE)

enum {
  CVMD_CP_STATE_MONITORING = 0,  // monitoring timeout.
  CVMD_CP_STATE_PENDING = 1,     // timeout detected and waiting for dump.
  CVMD_CP_STATE_DUMPING = 2,     // critical path data is dumping.
};

enum {
  CVMD_CMD_MP_BUFFER_CONFIG = 0,
  CVMD_CMD_TP_BUFFER_CONFIG = 1,
  CVMD_CMD_EXCEPTION_DUMP = 2,
};

enum {
  CROSS_VM_DUMP_VDEV_NOTIFY_G2H = 2,
  CROSS_VM_DUMP_VDEV_NOTIFY_H2G = 3,
};

enum {
  // Existing UOS exception dump completion wait window. This bounds exception
  // resume if the dump completion signal never arrives.
  CVMD_EXCEPTION_DUMP_COMPLETION_TIMEOUT_SEC = 5,
};

struct cvmd_cp {
  /* Output fields */
  uint32_t state;
  uint32_t mp_shm_off;
  uint32_t mp_shm_size;
  uint32_t tp_shm_off;
  uint32_t tp_shm_size;
  atomic_uint tp_unused;
  uint64_t last_ticks;
  uint64_t last_begin_count;

  /* Input fields */
  uint64_t timeout_ticks;

  /* Input&Output fields */
  atomic_ullong begin_count;
  atomic_ullong end_count;
};

struct cvmd_ctl {
  /* Output fields */
  uint64_t version;

  /* Input&Output fields */
  uint8_t dump_irq_done;
  uint8_t dumper_done;
  uint8_t reserved1[6];

  /* Input fields */
  uint64_t cp_bitmap;

  uint8_t reserved2[40];

  /* critical paths */
  struct cvmd_cp cps[];
};

struct cvmd_mp {
  uint64_t value;
};

struct cvmd_tp {
  uint64_t tpid;
  uint64_t key;
  uint64_t value;
};

extern struct cvmd_ctl* g_cvmd_ctl;
extern uint8_t* g_cvmd_share;
extern unsigned int g_cvmd_share_size;

void cvmd_init(uint64_t cvmd_shm_base, uint64_t cvmd_shm_size);
void cvmd_deinit();
static inline void cvmd_cp_begin(unsigned cpid) {
  struct cvmd_ctl* ctl = g_cvmd_ctl;
  struct cvmd_cp* cp;

  if (!ctl || cpid >= MAX_CRITICAL_PATH_COUNT) {
    return;
  }

  if (!(ctl->cp_bitmap & (1ULL << MAX_CRITICAL_PATH_COUNT))) {
    return;
  }

  if (!(ctl->cp_bitmap & (1ULL << cpid))) {
    return;
  }

  cp = &ctl->cps[cpid];
  if (cp->state != CVMD_CP_STATE_MONITORING) {
    return;
  }

  atomic_fetch_add(&cp->begin_count, 1);
}

static inline void cvmd_cp_end(unsigned cpid) {
  struct cvmd_ctl* ctl = g_cvmd_ctl;
  struct cvmd_cp* cp;

  if (!ctl || cpid >= MAX_CRITICAL_PATH_COUNT) {
    return;
  }

  if (!(ctl->cp_bitmap & (1ULL << MAX_CRITICAL_PATH_COUNT))) {
    return;
  }

  if (!(ctl->cp_bitmap & (1ULL << cpid))) {
    return;
  }

  cp = &ctl->cps[cpid];
  if (cp->state != CVMD_CP_STATE_MONITORING) {
    return;
  }

  atomic_fetch_add(&cp->end_count, 1);
}

static inline int cvmd_mp_set(unsigned cpid, unsigned mpid, uint64_t value) {
  struct cvmd_ctl* ctl = g_cvmd_ctl;
  struct cvmd_cp* cp;
  uint8_t* share;
  unsigned size;
  uint32_t mp_off;
  uint32_t mp_size;
  uint32_t mp_count;
  struct cvmd_mp* mps;

  if (!ctl || cpid >= MAX_CRITICAL_PATH_COUNT) {
    return -1;
  }

  if (!(ctl->cp_bitmap & (1ULL << MAX_CRITICAL_PATH_COUNT))) {
      return -5;
  }

  if (!(ctl->cp_bitmap & (1ULL << cpid))) {
      return -2;
  }

  cp = &ctl->cps[cpid];
  mp_size = cp->mp_shm_size;
  if (!mp_size || cp->state != CVMD_CP_STATE_MONITORING) {
    return -3;
  }

  share = g_cvmd_share;
  size = g_cvmd_share_size;
  mp_off = cp->mp_shm_off;
  mp_count = mp_size / sizeof(struct cvmd_mp);

  if (!share || mp_off >= size || mp_off + mp_size >= size ||
      mpid >= mp_count) {
    return -4;
  }

  mps = (struct cvmd_mp*)(share + mp_off + mpid * sizeof(struct cvmd_mp));
  mps->value = value;

  return 0;
}

static inline int cvmd_mp_set_ticks(unsigned cpid, unsigned mpid) {
  struct cvmd_ctl* ctl = g_cvmd_ctl;
  struct cvmd_cp* cp;
  uint8_t* share;
  unsigned size;
  uint32_t mp_off;
  uint32_t mp_size;
  uint32_t mp_count;
  struct cvmd_mp* mps;
  uint64_t tk;

  if (!ctl || cpid >= MAX_CRITICAL_PATH_COUNT) {
    return -1;
  }

  if (!(ctl->cp_bitmap & (1ULL << MAX_CRITICAL_PATH_COUNT))) {
      return -5;
  }

  if (!(ctl->cp_bitmap & (1ULL << cpid))) {
      return -2;
  }

  cp = &ctl->cps[cpid];
  mp_size = cp->mp_shm_size;
  if (!mp_size || cp->state != CVMD_CP_STATE_MONITORING) {
    return -3;
  }

  share = g_cvmd_share;
  size = g_cvmd_share_size;
  mp_off = cp->mp_shm_off;
  mp_count = mp_size / sizeof(struct cvmd_mp);

  if (!share || mpid >= mp_count || mp_off >= size ||
      mp_off + mp_size >= size) {
    return -4;
  }

  __asm__ volatile("mrs %0, cntpct_el0" : "=r"(tk));

  mps = (struct cvmd_mp*)(share + mp_off + mpid * sizeof(struct cvmd_mp));
  mps->value = tk;

  return 0;
}

static inline int cvmd_tp_add(unsigned cpid,
                              unsigned tpid,
                              uint64_t key,
                              uint64_t value) {
  struct cvmd_ctl* ctl = g_cvmd_ctl;
  struct cvmd_cp* cp;
  uint8_t* share;
  unsigned size;
  uint32_t tp_off;
  uint32_t tp_size;
  uint32_t tp_count;
  struct cvmd_tp* tps;
  uint32_t slot;

  if (!ctl || cpid >= MAX_CRITICAL_PATH_COUNT) {
    return -1;
  }

  if (!(ctl->cp_bitmap & (1ULL << MAX_CRITICAL_PATH_COUNT))) {
      return -5;
  }

  if (!(ctl->cp_bitmap & (1ULL << cpid))) {
      return -2;
  }

  cp = &ctl->cps[cpid];
  tp_size = cp->tp_shm_size;
  if (!tp_size || cp->state != CVMD_CP_STATE_MONITORING) {
    return -3;
  }

  share = g_cvmd_share;
  size = g_cvmd_share_size;
  tp_off = cp->tp_shm_off;

  if (!share || tp_off >= size || tp_off + tp_size >= size) {
    return -4;
  }

  tp_count = tp_size / sizeof(struct cvmd_tp);
  slot = atomic_fetch_add(&cp->tp_unused, 1) % tp_count;
  tps = (struct cvmd_tp*)(share + tp_off + slot * sizeof(struct cvmd_tp));
  tps->tpid = tpid;
  tps->key = key;
  tps->value = value;

  return 0;
}

static inline int cvmd_tp_add_ticks(unsigned cpid,
                                    unsigned tpid,
                                    uint64_t key) {
  struct cvmd_ctl* ctl = g_cvmd_ctl;
  struct cvmd_cp* cp;
  uint8_t* share;
  unsigned size;
  uint32_t tp_off;
  uint32_t tp_size;
  uint32_t tp_count;
  struct cvmd_tp* tps;
  uint32_t slot;
  uint64_t tk;

  if (!ctl || cpid >= MAX_CRITICAL_PATH_COUNT) {
    return -1;
  }

  if (!(ctl->cp_bitmap & (1ULL << MAX_CRITICAL_PATH_COUNT))) {
      return -5;
  }

  if (!(ctl->cp_bitmap & (1ULL << cpid))) {
      return -2;
  }

  cp = &ctl->cps[cpid];
  tp_size = cp->tp_shm_size;
  if (!tp_size || cp->state != CVMD_CP_STATE_MONITORING) {
    return -3;
  }

  share = g_cvmd_share;
  size = g_cvmd_share_size;
  tp_off = cp->tp_shm_off;

  if (!share || tp_off >= size || tp_off + tp_size >= size) {
    return -4;
  }

  tp_count = tp_size / sizeof(struct cvmd_tp);
  slot = atomic_fetch_add(&cp->tp_unused, 1) % tp_count;
  tps = (struct cvmd_tp*)(share + tp_off + slot * sizeof(struct cvmd_tp));

  __asm__ volatile("mrs %0, cntpct_el0" : "=r"(tk));

  tps->tpid = tpid;
  tps->key = key;
  tps->value = tk;

  return 0;
}

#endif /* NEBULA_SCHED_DUMP_H_ */
