#include <pretty/sizes.h>
#include <task-utils/walker.h>
#include <zircon/listnode.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/object.h>

#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <cstdlib>
#include "rev.h"
#include <nbl_fwk/nmt_tee.h>
#include "linux/drivers/nebula/nebula_trace/nbl_rev.h"

int nbl_update_rev_task(void* ctx) {
  int i = 0;
  fbl::unique_ptr<rev_task_args_t> args(static_cast<rev_task_args_t*>(ctx));
  machina::Utrace* trace = args->trace;

  nbl_rev_buf_t* rev_pbuffer =
      reinterpret_cast<nbl_rev_buf_t*>(trace->GetInstance()->GetTraceBuf(GRP_NBL_REV));
  if (!rev_pbuffer)
    return ZX_ERR_NO_MEMORY;

  nbl_rev_buf_t orig_rev_buf;
  memcpy(&orig_rev_buf, rev_pbuffer, sizeof(nbl_rev_buf_t));

  size_t len = strlen((const char* )orig_rev_buf.nebula_rev);
  if (len > 0 && orig_rev_buf.nebula_rev[len - 1] == '\n') {
      orig_rev_buf.nebula_rev[len - 1] = '\0';
  }

  len = strlen((const char* )orig_rev_buf.nebula_rev);
  if (len > 0 && orig_rev_buf.nebula_rev[len - 1] == '\r') {
      orig_rev_buf.nebula_rev[len - 1] = '\0';
  }

  for (;;) {
    i ++;
    if (i > REV_CHECK_TIMEOUT)
    {
      FXL_LOG(INFO) << "Update_rev_task timeout quit, deactivated!";
      return 0;
    }
    // reset nebula rev buffer
    memcpy(rev_pbuffer, &orig_rev_buf, sizeof(nbl_rev_buf_t));

    int rc = nbl_act_status(NMT_ACT_PRODUCT_BASIC);
    if (rc != 0) {
      // License not activated
      strcat((char*)rev_pbuffer->nebula_rev, "-deactivated\n");
    } else {
      strcat((char*)rev_pbuffer->nebula_rev, "-activated\n");
      break;
    }

    zx_nanosleep(zx_deadline_after(ZX_MSEC(REV_CHECK_INTERVAL)));
  }

  FXL_LOG(INFO) << "Update_rev_task quit, activated!";
  return 0;
}