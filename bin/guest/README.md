# Guest
The `guest` app enables booting a guest operating system using the Zircon
hypervisor.

These instructions will guide you through creating minimal Zircon and Linux
guests. For instructions on building a more comprehensive linux guest system
see the [debian-guest](debian-guest/README.md) package.

These instructions assume a general familiarity with how to netboot the target
device.

## Build host system with the guest package
Configure, build, and boot the guest package as follows:
```
$ fx set x64 --packages garnet/packages/experimental/guest --args guest_display=\"framebuffer\"
$ fx full-build
$ fx boot
```

## Building for QEMU
Configure, build, and boot the guest package as follows:
```
$ fx set arm64 --packages "garnet/packages/experimental/guest"
$ fx full-build
$ fx run
```

## Running guests
After netbooting the target device, to run Zircon:
```
$ guest launch zircon-guest
```

Likewise, to launch a Linux guest:
```
$ guest launch linux-guest
```

## Running from Topaz
To run from Topaz, configure the guest package as follows:
```
$ fx set x64 --packages topaz/packages/topaz,garnet/packages/experimental/guest
```

After netbooting the guest packages can be launched from the system launcher as
`linux-guest` and `zircon-guest`.

## Building for arm64
First flash Zedboot onto your VIM2:
```
$ cd $ZIRCON_DIR
$ scripts/build-zircon-arm64
$ scripts/flash-vim2 -m

```

Then configure, build, and boot the guest package as follows:
```
$ fx set arm64 --packages garnet/packages/experimental/guest --args guest_display=\"framebuffer\" --netboot
$ fx full-build
$ fx boot vim2
```

# Guest Configuration
Guest systems can be configured by including a config file inside the guest
package:
```
{
    "type": "object",
    "properties": {
        "kernel": {
            "type": "string"
        },
        "ramdisk": {
            "type": "string"
        },
        "block": {
            "type": "string"
        },
        "cmdline": {
            "type": "string"
        },
        "balloon-demand-page": {
            "type": "string"
        },
        "balloon-interval": {
            "type": "string"
        },
        "balloon-threshold": {
            "type": "string"
        },
    }
}
```

## Devicetree Feature Switches
The host bootdata can include a `BOOTDATA_DEVICETREE` item. The guest launcher
loads feature state through `GuestFeatures`, which uses the asynchronous
`garnet/lib/features:client` helper and the `features::FeatureProvider`
service. The `features` service owns the direct `/dev/misc/sysinfo` read and
keeps device-tree parsing out of `main.cc`. The supported schema follows the
product feature tree under `/features`.

The parser recognizes the product feature schema:

```dts
/features/vmlog
/features/mmio_bus
/features/mmio_bus/mmio-virtio-block
/features/pci_bus
/features/pci_bus/pci-virtio-block
/features/pci_bus/virtio-cluster
/features/pci_bus/virtio-spi
/features/pci_bus/virtio-i2c
/features/pci_bus/virtio-eint
/features/pci_bus/virtio-console
/features/gzfs
/features/ufs
```

`/features/mmio_bus/mmio-virtio-block` and
`/features/pci_bus/pci-virtio-block` control virtio-block setup, and
`/features/pci_bus/virtio-console` controls virtio-console setup. If a feature
node or any parent feature node has `status = "disabled"`, that feature is
disabled. Missing DTB data, a missing `/features` tree, or a missing individual
feature keeps the historical default of enabling devices.
