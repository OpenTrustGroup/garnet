Bluetooth
=========

The Fuchsia Bluetooth system aims to provide a dual-mode implementation of the
Bluetooth Host Subsystem (5.0+) supporting a framework for developing Low Energy
and Traditional profiles.

- [Public API](../../public/lib/bluetooth/fidl)
- [Tools](../bluetooth_tools)
- [System Architecture](../../docs/bluetooth_architecture.md)
- [Host Library](../../drivers/bluetooth/lib)
- [Host Bus Driver](../../drivers/bluetooth/host)
- [HCI Drivers](../../drivers/bluetooth/hci)
- [HCI Transport Drivers](https://fuchsia.googlesource.com/zircon/+/master/system/dev/bluetooth?autodive=0)

## Getting Started
### API Examples

Examples using Fuchsia's Bluetooth Low Energy APIs for all four LE roles can be
found in Garnet and Topaz. All of these are currently compiled into Fuchsia by
default.

- __LE scanner__: see [`eddystone_agent`](https://fuchsia.googlesource.com/topaz/+/master/examples/bluetooth/eddystone_agent/).
This is a suggestion agent that proposes URL links that are obtained from
Eddystone beacons. This is built in topaz by default.
- __LE broadcaster__: see [`eddystone_advertiser`](https://fuchsia.googlesource.com/topaz/+/master/examples/bluetooth/eddystone_advertiser/).
This is a Flutter module that can advertise any entered URL as an Eddystone
beacon.
- __LE peripheral__: see the [`ble_rect`](https://fuchsia.googlesource.com/topaz/+/master/examples/bluetooth/ble_rect/)
and [`ble_battery_service`](../../examples/bluetooth/ble_battery_service) examples.
- __LE central__: see [`ble_scanner`](https://fuchsia.googlesource.com/topaz/+/master/examples/bluetooth/ble_scanner/).

### Control API

Dual-mode (LE + Classic) GAP operations that are typically exposed to privileged
clients are performed using the [control.fidl](../../public/lib/bluetooth/fidl/control.fidl)
API. This API is intended for managing local adapters, device discovery & discoverability,
pairing/bonding, and other settings.

[`bluetoothcli`](../bluetooth_tools/bluetoothcli) is a command-line front-end
for this API:

```
$ bluetoothcli
bluetooth> list-adapters
  Adapter 0
    id: bf004a8b-d691-4298-8c79-130b83e047a1
    address: 00:1A:7D:DA:0A
bluetooth>
```

We also have a Flutter [module](https://fuchsia.googlesource.com/docs/+/HEAD/glossary.md#module)
that acts as a Bluetooth system menu based on this API at
[topaz/app/bluetooth_settings](https://fuchsia.googlesource.com/topaz/+/master/app/bluetooth_settings/).

### Tools

See the [bluetooth_tools](../bluetooth_tools) package for more information on
available command line tools for testing/debugging.

### Testing

The `bluetooth_tests` package contains Bluetooth test binaries. This package is
defined in the [top level BUILD file](BUILD.gn).

Host subsystem tests are compiled into a single [GoogleTest](https://github.com/google/googletest) binary,
which gets installed at `/system/test/bluetooth_unittests`.

To run all tests:

```
$ /system/test/bluetooth_unittests
```

Use the `--gtest_filter`
[flag](https://github.com/google/googletest/blob/master/googletest/docs/AdvancedGuide.md#running-a-subset-of-the-tests)
to run a subset of the tests:

```
# This only runs the L2CAP unit tests.
$ /system/test/bluetooth_unittests --gtest_filter=L2CAP_*
```

Use the `--verbose` flag to set log verbosity:

```
# This logs all messages logged using FXL_VLOG (up to level 2)
$ /system/test/bluetooth_unittests --verbose=2
```

TODO(armansito): Describe integration tests

### Log Verbosity

#### bin/bluetooth

The Bluetooth system service is invoked by sysmgr to resolve service requests.
The mapping between environment service names and their handlers is defined in
[bin/sysmgr/services.config](../../bin/sysmgr/services.config). Add the
`--verbose` option to the Bluetooth entries to increase verbosity, for example:

```
...
    "bluetooth::control::AdapterManager": [ "bluetooth", "--verbose=2" ],
    "bluetooth::gatt::Server": [ "bluetooth", "--verbose=2" ],
    "bluetooth::low_energy::Central": [ "bluetooth", "--verbose=2" ],
    "bluetooth::low_energy::Peripheral": [ "bluetooth", "--verbose=2" ],
...

```

#### bthost

The bthost driver currently uses the FXL logging system. To enable maximum log
verbosity, set the `BT_DEBUG` macro to `1` in [drivers/bluetooth/host/driver.cc](../../drivers/bluetooth/host/driver.cc).
