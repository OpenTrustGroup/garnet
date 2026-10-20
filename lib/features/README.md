# Platform Features

`lib/features` owns the Garnet representation of platform feature state under
the `/features` devicetree node.

The implementation is split into three layers:

* `//garnet/lib/features` parses `BOOTDATA_DEVICETREE` data from
  `/dev/misc/sysinfo` into the typed C++ `features::FeatureSet`. This target
  has no app or FIDL dependency and is the low-level path for code that cannot
  depend on sysmgr services.
* `//garnet/bin/features` publishes `features::FeatureProvider`. The service
  does not read sysinfo in its constructor. It lazily loads `FeatureSet` on the
  first `GetFeatures()` request, caches only successful loads, and retries on a
  later request after a failed load.
* `//garnet/lib/features:client` is the service client for Garnet components
  outside `drivers/`. It uses asynchronous FIDL and sends one bulk
  `GetFeatures()` request, then converts the returned `FeatureState` into the
  C++ `FeatureSet`.

`features::FeatureProvider` is routed by `bin/sysmgr/config/services.config`
and by the arm64 minimal services override. Product package manifests include
the `features` package where service-backed feature queries are expected.

## Schema

The parser recognizes these feature paths:

```dts
/features/nebulalog
/features/vmlog
/features/mmio_bus
/features/mmio_bus/mmio-virtio-block
/features/pci_bus
/features/pci_bus/pci-virtio-block
/features/pci_bus/virtio-cluster
/features/pci_bus/virtio-spi
/features/pci_bus/virtio-i2c
/features/pci_bus/virtio-eint
/features/pci_bus/virtio-rtc
/features/pci_bus/virtio-console
/features/pci_bus/virtio-vsock
/features/virtio-vsock/gzfs
/features/ufs
/features/smc
/features/pvblk
/features/cvm_lock
/features/wdt-sink
```

Features default to disabled. A feature is enabled when its node is present and
neither that node nor any parent feature node has `status = "disabled"` or a
status beginning with `"fail"`. Missing DTB data, a missing `/features` tree,
or a missing individual feature node keeps that feature disabled.

The `wdt-sink` policy uses the shared `zircon/boot/wdt-profile.h` predicate
in both Garnet and the boot shim. Existing parent/child nodes with absent
status, `okay`, or `ok` enable it. Missing nodes, disabled parents, failed,
unknown, or malformed statuses select no-sink. This stricter policy rule
does not change the other feature predicates above.

The MTK boot shim and sysinfo consume the same embedded product
`BOOTDATA_DEVICETREE`, not the separate firmware `BOOTDATA_FDT`.
Venus QEMU without a product DTB retains software watchdogs after its
product ID is verified. A sysinfo open/read error is returned to callers.
