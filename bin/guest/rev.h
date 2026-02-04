#ifndef GARNET_BIN_GUEST_REV_H_
#define GARNET_BIN_GUEST_REV_H_
#include "garnet/lib/machina/utrace.h"

typedef struct rev_task_args{
  machina::Utrace* trace;
} rev_task_args_t;


#define REV_CHECK_TIMEOUT       (12 * 30)           //30min
#define REV_CHECK_INTERVAL      (5000)              //5s times


int nbl_update_rev_task(void* ctx);

#endif  // GARNET_BIN_GUEST_TOP_H_
