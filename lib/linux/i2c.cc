/* SPDX-License-Identifier: GPL-2.0 */

#include <array>
#include <cstddef>

#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>

#include <ddk/protocol/i2c.h>
#include <ddk/protocol/platform-defs.h>

#include "garnet/lib/linux/i2c_device.h"
#include "garnet/lib/linux/platform_device.h"
#include "garnet/lib/linux/singleton.h"

constexpr size_t kMaxI2cAdapters = 4;
constexpr size_t kMaxI2cTransferSize = PAGE_SIZE;

// debate: should we use protobuf here?
static void serialize_to_req_buf(struct i2c_msg* msg,
                                 int num,
                                 uint8_t* req_buf) {
  size_t hdr_size = sizeof(i2c_msg) * num;
  size_t bytes_written = 0;

  memcpy(req_buf, &num, sizeof(uint32_t));
  bytes_written += sizeof(uint32_t);

  memcpy(req_buf + bytes_written, msg, hdr_size);
  bytes_written += hdr_size;

  for (int i = 0; i < num; i++) {
    if (!(msg[i].flags & I2C_M_RD)) {
      memcpy(req_buf + bytes_written, msg[i].buf, msg[i].len);
      bytes_written += msg[i].len;
    }
  }
}

static void deserialize_from_req_buf(struct i2c_msg** out_msg,
                                     int* out_num,
                                     const uint8_t* req_buf,
                                     uint8_t* resp_buf) {
  size_t req_offset = 0, resp_offset = 0;
  uint32_t num;

  memcpy(&num, req_buf, sizeof(uint32_t));
  req_offset += sizeof(uint32_t);

  size_t hdr_size = sizeof(i2c_msg) * num;
  auto msg = (struct i2c_msg*)(req_buf + req_offset);
  req_offset += hdr_size;

  // reserve space for return value
  resp_offset += sizeof(uint32_t);

  for (int i = 0; i < num; i++) {
    if (!(msg[i].flags & I2C_M_RD)) {
      msg[i].buf = (uint8_t*)req_buf + req_offset;
      req_offset += msg[i].len;
    } else {
      msg[i].buf = resp_buf + resp_offset;
      resp_offset += msg[i].len;
    }
  }

  *out_msg = msg;
  *out_num = num;
}

static void serialize_to_resp_buf(struct i2c_msg* msg,
                                  int num,
                                  int ret,
                                  uint8_t* resp_buf) {
  memcpy(resp_buf, &ret, sizeof(uint32_t));
}

static void deserialize_from_resp_buf(struct i2c_msg* msg,
                                      int* out_ret,
                                      int num,
                                      const uint8_t* resp_buf) {
  size_t bytes_read = 0;

  memcpy(out_ret, resp_buf, sizeof(uint32_t));
  bytes_read += sizeof(uint32_t);

  for (int i = 0; i < num; i++) {
    if (msg[i].flags & I2C_M_RD) {
      memcpy(msg[i].buf, resp_buf + bytes_read, msg[i].len);
      bytes_read += msg[i].len;
    }
  }
}

class I2cAdapterContext : public Singleton<I2cAdapterContext> {
  friend class Singleton<I2cAdapterContext>;

 public:
  static I2cAdapterContext* Get() {
    auto ctx = I2cAdapterContext::Instance();

    auto status = ctx->MaybeInit();
    CHECK(status == ZX_OK);

    return ctx;
  }

  zx_status_t AddAdapter(i2c_adapter* adapter) {
    if (adapter->nr >= kMaxI2cAdapters)
      return ZX_ERR_INVALID_ARGS;

    i2c_adapters_[adapter->nr] = adapter;
    return ZX_OK;
  }

  zx_status_t RemoveAdapter(i2c_adapter* adapter) {
    if (adapter->nr >= kMaxI2cAdapters)
      return ZX_ERR_INVALID_ARGS;

    i2c_adapters_[adapter->nr] = nullptr;
    return ZX_OK;
  }

 private:
  I2cAdapterContext() {}

  zx_status_t MaybeInit() {
    if (unlikely(!initialized_)) {
      auto pdev_ctx = PlatformDeviceContext::Instance();
      i2c_impl_protocol_t i2c;
      i2c.ops = &i2c_ops_;
      i2c.ctx = this;
      auto pbus = pdev_ctx->pbus_proto();
      auto status = pbus_set_protocol(pbus, ZX_PROTOCOL_I2C_IMPL, (void*)&i2c);
      if (status != ZX_OK)
        return status;

      initialized_ = true;
    }

    return ZX_OK;
  }

  static size_t i2c_get_bus_count(void* ctx) { return kMaxI2cAdapters; }

  static zx_status_t i2c_get_max_transfer_size(void* ctx,
                                               uint32_t bus_id,
                                               size_t* out_size) {
    *out_size = kMaxI2cTransferSize;
    return ZX_OK;
  }

  static zx_status_t i2c_set_bitrate(void* ctx,
                                     uint32_t bus_id,
                                     uint32_t bitrate) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  static zx_status_t i2c_transact(void* ctx,
                                  uint32_t bus_id,
                                  uint16_t address,
                                  const void* write_buf,
                                  size_t write_length,
                                  void* read_buf,
                                  size_t read_length) {
    if (!read_length && !write_length) {
      return ZX_ERR_INVALID_ARGS;
    }

    if (bus_id >= kMaxI2cAdapters) {
      return ZX_ERR_INVALID_ARGS;
    }

    auto i2c_ctx = reinterpret_cast<I2cAdapterContext*>(ctx);
    auto adap = i2c_ctx->i2c_adapters_[bus_id];
    if (adap == nullptr)
      return ZX_ERR_NOT_FOUND;

    i2c_msg* msg;
    int num;
    deserialize_from_req_buf(&msg, &num, (const uint8_t*)write_buf,
                             (uint8_t*)read_buf);
    int ret = adap->algo->master_xfer(adap, msg, num);
    serialize_to_resp_buf(msg, num, ret, (uint8_t*)read_buf);
    return ZX_OK;
  }

  i2c_impl_ops_t i2c_ops_ = {
      .get_bus_count = i2c_get_bus_count,
      .get_max_transfer_size = i2c_get_max_transfer_size,
      .set_bitrate = i2c_set_bitrate,
      .transact = i2c_transact,
  };

  std::array<i2c_adapter*, kMaxI2cAdapters> i2c_adapters_;
  bool initialized_ = false;
};

class I2cClientContext : public Singleton<I2cClientContext> {
  friend class Singleton<I2cClientContext>;

 public:
  static I2cClientContext* Get() {
    auto ctx = I2cClientContext::Instance();
    auto status = ctx->MaybeInit();
    CHECK(status == ZX_OK);

    return ctx;
  }

  i2c_protocol_t* i2c_proto() { return &i2c_; }

 private:
  zx_status_t MaybeInit() {
    if (unlikely(!initialized_)) {
      auto i2c_dev_ctx = I2cDeviceContext::Instance();
      auto parent = i2c_dev_ctx->parent_device();
      auto status = device_get_protocol(parent, ZX_PROTOCOL_I2C, (void*)&i2c_);
      if (status != ZX_OK)
        return status;

      initialized_ = true;
    }

    return ZX_OK;
  }

  i2c_protocol_t i2c_;
  bool initialized_ = false;
};

void* i2c_get_adapdata(struct i2c_adapter* adap) {
  return dev_get_drvdata(&adap->dev);
}

void i2c_set_adapdata(struct i2c_adapter* adap, void* data) {
  dev_set_drvdata(&adap->dev, data);
}

int i2c_del_adapter(struct i2c_adapter* adap) {
  auto ctx = I2cAdapterContext::Get();

  auto status = ctx->RemoveAdapter(adap);
  if (status != ZX_OK)
    return -EINVAL;

  return 0;
}

int i2c_add_numbered_adapter(struct i2c_adapter* adap) {
  auto ctx = I2cAdapterContext::Get();

  auto status = ctx->AddAdapter(adap);
  if (status != ZX_OK)
    return -EINVAL;

  return 0;
}

int i2c_recover_bus(struct i2c_adapter* adap) {
  return -ENOTSUP;
}

int i2c_generic_scl_recovery(struct i2c_adapter* adap) {
  return -ENOTSUP;
}

static void get_i2c_msg_size(struct i2c_msg* msg,
                             int num,
                             size_t* out_tx_size,
                             size_t* out_rx_size) {
  size_t tx_size = 0, rx_size = 0;

  for (int i = 0; i < num; i++) {
    if (msg[i].flags & I2C_M_RD)
      rx_size += msg[i].len;
    else
      tx_size += msg[i].len;
  }

  *out_rx_size = rx_size;
  *out_tx_size = tx_size;
}

struct i2c_complete_cookie {
  i2c_msg* msg;
  int num;
  int ret;
};

static void i2c_complete(zx_status_t status,
                         const uint8_t* data,
                         void* cookie) {
  if (status != ZX_OK) {
    LOG(ERROR) << "i2c_complete error: " << status;
    return;
  }

  auto c = (i2c_complete_cookie*)cookie;
  deserialize_from_resp_buf(c->msg, &c->ret, c->num, data);
}

int i2c_transfer(struct i2c_adapter* adap, struct i2c_msg* msg, int num) {
  size_t hdr_size = sizeof(i2c_msg) * num;
  size_t tx_size, rx_size;

  auto ctx = I2cClientContext::Get();
  get_i2c_msg_size(msg, num, &tx_size, &rx_size);
  auto req_size = sizeof(uint32_t) + hdr_size + tx_size;
  auto resp_size = sizeof(uint32_t) + rx_size;
  if (req_size > kMaxI2cTransferSize || resp_size > kMaxI2cTransferSize)
    return -ENOSPC;

  uint8_t req_buf[kMaxI2cTransferSize];
  serialize_to_req_buf(msg, num, req_buf);

  i2c_complete_cookie cookie = {
      .msg = msg,
      .num = num,
  };

  auto status = i2c_transact(ctx->i2c_proto(), adap->nr, req_buf, req_size,
                             resp_size, i2c_complete, &cookie);
  if (status != ZX_OK) {
    return -EIO;
  }

  return cookie.ret;
}
