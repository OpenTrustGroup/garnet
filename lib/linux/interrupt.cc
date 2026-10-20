/* SPDX-License-Identifier: GPL-2.0 */

#include <ddk/debug.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <threads.h>
#include <zircon/threads.h>
#include <zircon/types.h>

#include "garnet/lib/linux/platform_device.h"

struct irq_wrapper_tls_data {
  zx_handle_t irq_handle;
  irq_handler_t irq_handler_func;
  void* dev_id;
};

static pthread_key_t irq_wrapper_tls_key;
static struct irq_wrapper_tls_data* tls_data;

static void tls_key_destructor(void* value) {
  free(value);
}

static int irq_wrapper(void* ctx) {
  uint64_t slots;
  zx_status_t status;
  struct irq_wrapper_tls_data* tls_data;
  pthread_setspecific(irq_wrapper_tls_key, ctx);

  tls_data =
      (struct irq_wrapper_tls_data*)pthread_getspecific(irq_wrapper_tls_key);

  while (1) {
    status = zx_interrupt_wait(tls_data->irq_handle, &slots);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: irq wait failed, retcode = %d\n", __FUNCTION__,
             status);
      continue;
    }
    tls_data->irq_handler_func(tls_data->irq_handle, tls_data->dev_id);
  }

  return 0;
}

int devm_request_irq(struct device* dev,
                     unsigned int irq,
                     irq_handler_t handler,
                     unsigned long flags,
                     const char* name,
                     void* dev_id) {
  int thrd_rc = 0;
  char irq_wrapper_name[16] = {};
  thrd_t irq_wrapper_thread;
  zx_status_t status;

  thrd_rc = pthread_key_create(&irq_wrapper_tls_key, tls_key_destructor);

  tls_data = (struct irq_wrapper_tls_data*)malloc(sizeof(*tls_data));
  tls_data->irq_handle = irq;
  tls_data->irq_handler_func = handler;
  tls_data->dev_id = dev_id;

  snprintf(irq_wrapper_name, sizeof(irq_wrapper_name), "irq-wrapper-%u", irq);
  thrd_rc = thrd_create_with_name(&irq_wrapper_thread, irq_wrapper, tls_data,
                                  irq_wrapper_name);
  if (thrd_rc != thrd_success) {
    status = thrd_status_to_zx_status(thrd_rc);
    goto fail;
  }
  thrd_detach(irq_wrapper_thread);

  return thrd_rc;

fail:
  free(tls_data);
  pthread_key_delete(irq_wrapper_tls_key);
  zxlogf(ERROR, "thrd_create_with_name, irq_wrapper err=%d\n", thrd_rc);
  return status;
}