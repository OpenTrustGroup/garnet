#include <zircon/assert.h>
#include <threads.h>
#include "retry.h"

static bool should_retry(retry_ctx_t *ctx, block_op_t *bop,
                         zx_status_t status) {
  if (status == ZX_OK) {
    return false;
  }

  if (!RETRYABLE_ERRORS(status)) {
    return false;
  }

  switch (bop->command) {
  case BLOCK_OP_WRITE:
  case BLOCK_OP_READ:
  case BLOCK_OP_FLUSH:
    break;
  default:
    return false;
  }

  if (ctx) {
    if (ctx->rcount >= MAX_RETRY_COUNT) {
      return false;
    }
  }

  return true;
}

static zx_duration_t calculate_retry_delay(uint32_t retry_count,
                                           retry_strategy_t strategy) {
  switch (strategy) {
  case RETRY_IMMEDIATE:
    return 0;

  case RETRY_BACKOFF: {
    uint64_t delay_us = RETRY_BACKOFF_US * (1 << retry_count);
    if (delay_us > RETRY_BACKOFF_MAX_US) {
      delay_us = RETRY_BACKOFF_MAX_US;
    }
    return ZX_USEC(delay_us);
  }

  case RETRY_FIXED_DELAY:
    return ZX_USEC(RETRY_BACKOFF_US);

  default:
    return 0;
  }
}

static int retry_worker(void *arg) {
  retry_ctx_t *ctx = (retry_ctx_t *)arg;

  zx_nanosleep(zx_deadline_after(ctx->delay));

  if (ctx->bop) {
    ctx->rcount++;
    ctx->phys_ops->queue(ctx->phys_ctx, ctx->bop);
    return 0;
  }

  return -1;
}

static bool schedule_retry_after_delay(retry_ctx_t *ctx) {
  thrd_t thread;
  if (thrd_create(&thread, retry_worker, ctx) == thrd_success) {
    thrd_detach(thread);
    return true;
  } else {
    return false;
  }
}

bool perform_retry_diagnosis(retry_ctx_t **ctx, block_op_t *bop,
                             zx_status_t status, block_protocol_ops_t *phys_ops,
                             void *phys_ctx, int strategy) {
  if (unlikely(should_retry(*ctx, bop, status))) {
    if (!(*ctx)) {
      *ctx = malloc(sizeof(retry_ctx_t));
      if (!(*ctx)) {
        zxlogf(ERROR,
               "[Retry][%s]: Failed to allocate memory for retry_ctx_t\n",
               __func__);
        return false;
      }
      (*ctx)->rcount = 0;
      (*ctx)->phys_ops = phys_ops;
      (*ctx)->phys_ctx = phys_ctx;
    }
    retry_ctx_t *_ctx = *ctx;

    zx_duration_t delay =
        calculate_retry_delay(_ctx->rcount, (retry_strategy_t)strategy);

    if (delay > 0) {
      _ctx->delay = delay;
      _ctx->bop = bop;
      if (schedule_retry_after_delay(_ctx)) {
        return true;
      }
    }

    _ctx->rcount++;
    _ctx->phys_ops->queue(_ctx->phys_ctx, bop);
    return true;
  }

  return false;
}
void block_retry_free(retry_ctx_t *ctx) {
  if (ctx) {
    free(ctx);
  }
}
