// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_GAP_REMOTE_DEVICE_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_GAP_REMOTE_DEVICE_H_

#include <string>
#include <type_traits>
#include <unordered_map>

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "garnet/drivers/bluetooth/lib/common/device_address.h"
#include "garnet/drivers/bluetooth/lib/common/optional.h"
#include "garnet/drivers/bluetooth/lib/gap/gap.h"
#include "garnet/drivers/bluetooth/lib/hci/connection.h"
#include "garnet/drivers/bluetooth/lib/hci/lmp_feature_set.h"
#include "garnet/drivers/bluetooth/lib/sm/pairing_state.h"
#include "lib/fxl/macros.h"

namespace btlib {
namespace gap {

class RemoteDeviceCache;

// Represents a remote Bluetooth device that is known to the current system due
// to discovery and/or connection and bonding procedures. These devices can be
// LE-only, Classic-only, or dual-mode.
//
// Instances should not be created directly and must be obtained via a
// RemoteDeviceCache.
class RemoteDevice final {
 public:
  // Device connection state.
  enum class ConnectionState {
    // No link exists between the local adapter and this device.
    kNotConnected,

    // The device is currently establishing a link or performing service
    // discovery or encryption setup. In this state, a link may have been
    // established but it is not ready to use yet.
    kInitializing,

    // Link setup, service discovery, and any encryption setup has completed
    kConnected
  };

  // Contains RemoteDevice data that apply only to the LE transport.
  class LowEnergyData final {
   public:
    // TODO(NET-1436): Remove default constructor once common::Optional no
    // longer requires it.
    LowEnergyData() = default;
    explicit LowEnergyData(RemoteDevice* owner);

    // Current connection state.
    ConnectionState connection_state() const { return conn_state_; }
    bool connected() const {
      return connection_state() == ConnectionState::kConnected;
    }
    bool bonded() const { return bond_data_.HasValue(); }

    // Advertising (and optionally scan response) data obtained during
    // discovery.
    const common::BufferView advertising_data() const {
      return adv_data_buffer_.view(0, adv_data_len_);
    }

    // Most recently used LE connection parameters. Has no value if this device
    // has never been connected.
    const common::Optional<hci::LEConnectionParameters>& connection_parameters()
        const {
      return conn_params_;
    }

    // Preferred LE connection parameters as reported by this device.
    const common::Optional<hci::LEPreferredConnectionParameters>&
    preferred_connection_parameters() const {
      return preferred_conn_params_;
    }

    // This device's LE bond data, if bonded.
    const common::Optional<sm::PairingData>& bond_data() const {
      return bond_data_;
    }

    // Setters:

    // Updates the LE advertising and scan response data for this device.
    // |rssi| corresponds to the most recent advertisement RSSI.
    // |advertising_data| should include any scan response data if obtained
    // during an active scan.
    void SetAdvertisingData(int8_t rssi, const common::ByteBuffer& data);

    // Updates the connection state and notifies listeners if necessary.
    void SetConnectionState(ConnectionState state);

    // Modify the current or preferred connection parameters for this device.
    // The device must be connectable.
    void SetConnectionParameters(const hci::LEConnectionParameters& value);
    void SetPreferredConnectionParameters(
        const hci::LEPreferredConnectionParameters& value);

    // Stores LE bonding data and makes this device "boded". Marks the device as
    // non-temporary if necessary.
    void SetBondData(const sm::PairingData& bond_data);

    // TODO(armansito): Store most recently seen random address and identity
    // address separately, once RemoteDeviceCache can index devices by multiple
    // addresses.

   private:
    RemoteDevice* dev_;  // weak

    ConnectionState conn_state_;
    size_t adv_data_len_;
    common::DynamicByteBuffer adv_data_buffer_;
    common::Optional<hci::LEConnectionParameters> conn_params_;
    common::Optional<hci::LEPreferredConnectionParameters>
        preferred_conn_params_;

    common::Optional<sm::PairingData> bond_data_;

    // TODO(armansito): Store all keys
    // TODO(armansito): Store GATT service UUIDs.
  };

  // Contains RemoteDevice data that apply only to the BR/EDR transport.
  class BrEdrData final {
   public:
    // TODO(NET-1436): Remove default constructor once common::Optional no
    // longer requires it.
    BrEdrData() = default;
    explicit BrEdrData(RemoteDevice* owner);

    // Current connection state.
    ConnectionState connection_state() const { return conn_state_; }
    bool connected() const {
      return connection_state() == ConnectionState::kConnected;
    }

    // Returns the device class of this device, if it is known.
    const common::Optional<common::DeviceClass>& device_class() const {
      return device_class_;
    }

    // Returns the page scan repetition mode of this device, if known.
    const common::Optional<hci::PageScanRepetitionMode>&
    page_scan_repetition_mode() const {
      return page_scan_rep_mode_;
    }

    // Returns the clock offset reported by the device, if known and valid. The
    // clock offset will have the highest-order bit set and the rest represent
    // bits 16-2 of CLKNslave-CLK (see hci::kClockOffsetFlagBit in
    // hci/hci_constants.h).
    const common::Optional<uint16_t>& clock_offset() const {
      return clock_offset_;
    }
    const common::BufferView extended_inquiry_response() const {
      return eir_buffer_.view(0, eir_len_);
    }

    // Setters:

    // Updates the inquiry data for this device and notifies listeners. These
    // methods expect HCI inquiry result structures as they are obtained from
    // the Bluetooth controller. Each field should be encoded in little-endian
    // byte order.
    void SetInquiryData(const hci::InquiryResult& value);
    void SetInquiryData(const hci::InquiryResultRSSI& value);
    void SetInquiryData(const hci::ExtendedInquiryResultEventParams& value);

    // Updates the connection state and notifies listeners if necessary.
    void SetConnectionState(ConnectionState state);

    // TODO(armansito): Store BD_ADDR here, once RemoteDeviceCache can index
    // devices by multiple addresses.

   private:
    // All multi-byte fields must be in little-endian byte order as they were
    // received from the controller.
    void SetInquiryData(
        common::DeviceClass device_class, uint16_t clock_offset,
        hci::PageScanRepetitionMode page_scan_rep_mode,
        int8_t rssi = hci::kRSSIInvalid,
        const common::BufferView& eir_data = common::BufferView());

    // Updates the EIR data field and returns true if any properties changed.
    bool SetEirData(const common::ByteBuffer& data);

    RemoteDevice* dev_;  // weak
    ConnectionState conn_state_;
    common::Optional<common::DeviceClass> device_class_;
    common::Optional<hci::PageScanRepetitionMode> page_scan_rep_mode_;
    common::Optional<uint16_t> clock_offset_;
    // TODO(jamuraa): Parse more of the Extended Inquiry Response fields
    size_t eir_len_;
    common::DynamicByteBuffer eir_buffer_;

    // TODO(armansito): Store link key.
    // TODO(armansito): Store traditional service UUIDs.
  };

  // 128-bit UUID that uniquely identifies this device on this system.
  const std::string& identifier() const { return identifier_; }

  // The Bluetooth technologies that are supported by this device.
  TechnologyType technology() const { return technology_; }

  // The known device address of this device. Depending on the technologies
  // supported by this device this has the following meaning:
  //
  //   * For BR/EDR devices this is the BD_ADDR.
  //
  //   * For LE devices this is identity address IF identity_known() returns
  //     true. This is always the case if the address type is LE Public.
  //
  //     For LE devices that use privacy, identity_known() will be set to false
  //     upon discovery. The address will be updated only once the identity
  //     address has been obtained during the pairing procedure.
  //
  //   * For BR/EDR/LE devices this is the BD_ADDR and the LE identity address.
  //     If a BR/EDR/LE device uses an identity address that is different from
  //     its BD_ADDR, then there will be two separate RemoteDevice entries for
  //     it.
  const common::DeviceAddress& address() const { return address_; }
  bool identity_known() const { return identity_known_; }

  // The LMP version of this device obtained doing discovery.
  const common::Optional<hci::HCIVersion>& version() const {
    return lmp_version_;
  }

  // Returns true if this is a connectable device.
  bool connectable() const { return connectable_; }

  // Returns true if this device is connected over BR/EDR or LE transports.
  bool connected() const {
    return (le() && le()->connected()) || (bredr() && bredr()->connected());
  }

  // Returns true if this device has been bonded over BR/EDR or LE transports.
  bool bonded() const {
    // TODO(armansito): Check BR/EDR state here.
    return (le() && le()->bonded());
  }

  // Returns the most recently observed RSSI for this remote device. Returns
  // hci::kRSSIInvalid if the value is unknown.
  int8_t rssi() const { return rssi_; }

  // Gets the user-friendly name of the device, if it's known. This can be
  // assigned based on LE advertising data, BR/EDR inquiry data, or by directly
  // calling the SetName() method.
  const common::Optional<std::string>& name() const { return name_; }

  // Returns the set of features of this device.
  const hci::LMPFeatureSet& features() const { return lmp_features_; }

  // A temporary device gets removed from the RemoteDeviceCache after a period
  // of inactivity (see the |update_expiry_callback| argument to the
  // constructor). The following rules determine the temporary state of a
  // device:
  //   a. A device is temporary by default.
  //   b. A device becomes non-temporary when it gets connected.
  //   c. A device becomes temporary again when disconnected only if its
  //      identity is not known (i.e. identity_known() returns false). This only
  //      applies to LE devices that use the privacy feature.
  //
  // Temporary devices are never bonded.
  bool temporary() const { return temporary_; }

  // Returns the LE transport specific data of this device, if any. This will be
  // present if information about this device is obtained using the LE discovery
  // and connection procedures.
  const common::Optional<LowEnergyData>& le() const { return le_data_; }

  // Returns the BR/EDR transport specific data of this device, if any. This
  // will be present if information about this device is obtained using the
  // BR/EDR discovery and connection procedures.
  const common::Optional<BrEdrData>& bredr() const { return bredr_data_; }

  // Returns a mutable reference to each transport-specific data structure,
  // initializing the structure if it is unitialized. Use these to mutate
  // members of the transport-specific structs. The caller must make sure to
  // invoke these only if the device is known to support said technology.
  LowEnergyData& MutLe();
  BrEdrData& MutBrEdr();

  // Returns a string representation of this device.
  std::string ToString() const;

  // The following methods mutate RemoteDevice properties:

  // Updates the name of this device. This will override the existing name (if
  // present) and notify listeners of the change.
  void SetName(const std::string& name);

  // Sets the value of the LMP |features| for the given |page| number.
  void SetFeaturePage(size_t page, uint64_t features) {
    lmp_features_.SetPage(page, features);
  }

  // Sets the last available LMP feature |page| number for this device.
  void set_last_page_number(uint8_t page) {
    lmp_features_.set_last_page_number(page);
  }

  void set_version(hci::HCIVersion version, uint16_t manufacturer,
                   uint16_t subversion) {
    lmp_version_ = version;
    lmp_manufacturer_ = manufacturer;
    lmp_subversion_ = subversion;
  }

 private:
  friend class RemoteDeviceCache;
  using DeviceCallback = fit::function<void(const RemoteDevice&)>;

  // Caller must ensure that both callbacks are non-empty.
  // Note that the ctor is only intended for use by RemoteDeviceCache.
  // Expanding access would a) violate the constraint that all RemoteDevices
  // are created through a RemoteDeviceCache, and b) introduce lifetime issues
  // (do the callbacks outlive |this|?).
  RemoteDevice(DeviceCallback notify_listeners_callback,
               DeviceCallback update_expiry_callback,
               const std::string& identifier,
               const common::DeviceAddress& address, bool connectable);

  // Marks this device's identity as known. Called by RemoteDeviceCache when
  // initializing a bonded device and by LowEnergyData when setting bond data
  // with an identity address.
  void set_identity_known(bool value) { identity_known_ = value; }

  // Assigns a new value for the address of this device. Called by LowEnergyData
  // when a new identity address is assigned.
  void set_address(const common::DeviceAddress& address) { address_ = address; }

  // Updates the RSSI and returns true if it changed.
  bool SetRssiInternal(int8_t rssi);

  // Updates the name and returns true if there was a change without notifying
  // listeners.
  // TODO(armansito): Add similarly styled internal setters so that we can batch
  // more updates.
  bool SetNameInternal(const std::string& name);

  // Marks this device as non-temporary. This operation may fail due to one of
  // the conditions described above the |temporary()| method.
  //
  // TODO(armansito): Replace this with something more sophisticated when we
  // implement bonding procedures. This method is here to remind us that these
  // conditions are subtle and not fully supported yet.
  bool TryMakeNonTemporary();

  // Tells the owning RemoteDeviceCache to update the expiry state of this
  // device.
  void UpdateExpiry();

  // Signal to the cache to notify listeners.
  void NotifyListeners();

  // Callbacks used to notify state changes.
  DeviceCallback notify_listeners_callback_;
  DeviceCallback update_expiry_callback_;

  const std::string identifier_;
  TechnologyType technology_;

  common::DeviceAddress address_;
  bool identity_known_;

  common::Optional<std::string> name_;
  common::Optional<hci::HCIVersion> lmp_version_;
  common::Optional<uint16_t> lmp_manufacturer_;
  common::Optional<uint16_t> lmp_subversion_;
  hci::LMPFeatureSet lmp_features_;
  bool connectable_;
  bool temporary_;
  int8_t rssi_;

  // Data that only applies to the LE transport. This is present if this device
  // is known to support LE.
  common::Optional<LowEnergyData> le_data_;

  // Data that only applies to the BR/EDR transport. This is present if this
  // device is known to support BR/EDR.
  common::Optional<BrEdrData> bredr_data_;

  FXL_DISALLOW_COPY_AND_ASSIGN(RemoteDevice);
};

}  // namespace gap
}  // namespace btlib

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_GAP_REMOTE_DEVICE_H_
