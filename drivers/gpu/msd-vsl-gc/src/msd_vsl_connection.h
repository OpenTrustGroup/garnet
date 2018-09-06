// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_VSL_CONNECTION_H
#define MSD_VSL_CONNECTION_H

#include "address_space.h"
#include "magma_util/macros.h"
#include "msd.h"
#include <memory>

class MsdVslConnection : AddressSpace::Owner {
public:
    class Owner : public AddressSpace::Owner {
    public:
        virtual void ConnectionReleased(MsdVslConnection* connection) = 0;
    };

    MsdVslConnection(Owner* owner, uint32_t page_table_array_slot,
                     std::shared_ptr<AddressSpace> address_space, msd_client_id_t client_id)
        : owner_(owner), page_table_array_slot_(page_table_array_slot),
          address_space_(std::move(address_space)), client_id_(client_id)
    {
    }

    virtual ~MsdVslConnection() { owner_->ConnectionReleased(this); }

    msd_client_id_t client_id() { return client_id_; }

    uint32_t page_table_array_slot() { return page_table_array_slot_; }

    std::shared_ptr<AddressSpace> address_space() { return address_space_; }

private:
    // AddressSpace::Owner
    magma::PlatformBusMapper* bus_mapper() override { return owner_->bus_mapper(); }

    Owner* owner_;
    uint32_t page_table_array_slot_;
    std::shared_ptr<AddressSpace> address_space_;
    msd_client_id_t client_id_;
};

class MsdVslAbiConnection : public msd_connection_t {
public:
    MsdVslAbiConnection(std::shared_ptr<MsdVslConnection> ptr) : ptr_(std::move(ptr))
    {
        magic_ = kMagic;
    }

    static MsdVslAbiConnection* cast(msd_connection_t* connection)
    {
        DASSERT(connection);
        DASSERT(connection->magic_ == kMagic);
        return static_cast<MsdVslAbiConnection*>(connection);
    }

    std::shared_ptr<MsdVslConnection> ptr() { return ptr_; }

private:
    std::shared_ptr<MsdVslConnection> ptr_;
    static const uint32_t kMagic = 0x636f6e6e; // "conn" (Connection)
};

#endif // MSD_VSL_CONNECTION_H
