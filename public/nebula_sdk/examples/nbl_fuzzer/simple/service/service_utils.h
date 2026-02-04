#include <trusty_std.h>
#include <zircon/types.h>

#define ARG_MAX_LEN 64

typedef struct uuid_info {
  uuid_t uuid;
  uint32_t use_ion_as_heap;
  uint64_t heap_base;
  uint32_t shm_handle;
  uint64_t heap_size;
  char arg[ARG_MAX_LEN];
} uuid_info_t;

typedef struct loader_cmd {
  uint32_t cmd;
  uint32_t param_size;
  uuid_info_t param;
} loader_cmt_t;

int sync_connect(const char* port_name, uint64_t timeout);
int send_msg_to_service(const char* port_name,
                         void* buf,
                         size_t size,
                         bool wait_reply,
                         uint32_t read_len);
void generate_random_msg(void** addr, uint32_t* size);
void send_random_msg(const char* port_name);
