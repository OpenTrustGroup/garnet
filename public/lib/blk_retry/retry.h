#ifndef RETRY_H
#define RETRY_H

#include <zircon/types.h>
#include <zircon/process.h>
#include <zircon/device/block.h>
#include <ddk/protocol/block.h>
#include <ddk/device.h>
#include <ddk/debug.h>
#include <threads.h>

#define MAX_RETRY_COUNT 3
#define RETRY_BACKOFF_US 100
#define RETRY_BACKOFF_MAX_US 500

typedef enum {
  RETRY_IMMEDIATE,   // immediate
  RETRY_BACKOFF,     // backoff
  RETRY_FIXED_DELAY, // fixed delay
} retry_strategy_t;

#define RETRYABLE_ERRORS(err)                                                  \
  ((err) == ZX_ERR_IO || (err) == ZX_ERR_PEER_CLOSED ||                        \
   (err) == ZX_ERR_NO_RESOURCES || (err) == ZX_ERR_SHOULD_WAIT ||              \
   (err) == ZX_ERR_INTERNAL || (err) == ZX_ERR_NO_MEMORY)

typedef struct retry_ctx {
  block_protocol_ops_t *phys_ops;
  void *phys_ctx;
  block_op_t *bop;
  zx_duration_t delay;
  uint32_t rcount;
} retry_ctx_t;

bool perform_retry_diagnosis(retry_ctx_t **ctx, block_op_t *bop,
                             zx_status_t status, block_protocol_ops_t *phys_ops,
                             void *phys_ctx, int strategy);
void block_retry_free(retry_ctx_t *ctx);
#endif // RETRY_H