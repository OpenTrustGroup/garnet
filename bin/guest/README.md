# Guest
The `vmm` app enables booting a guest operating system using the Zircon
hypervisor.

These instructions will guide you through creating minimal Zircon and Linux
guests. For instructions on building a more comprehensive linux guest system
see the [debian_guest](pkg/debian_guest/README.md) package.

These instructions assume a general familiarity with how to netboot the target
device.

## Build host system with the guest package
Configure, build, and boot the guest package as follows:
```
$ fx set x64 --packages garnet/packages/experimental/linux_guest,garnet/packages/experimental/zircon_guest --args guest_display=\"framebuffer\"
$ fx full-build
$ fx boot
```

## Building for QEMU
Configure, build, and boot the guest package as follows:
```
$ fx set arm64 --packages garnet/packages/experimental/linux_guest,garnet/packages/experimental/zircon_guest
$ fx full-build
$ fx run
```

## Running guests
After netbooting the target device, to run Zircon:
```
$ guest launch zircon_guest
```

Likewise, to launch a Linux guest:
```
$ guest launch linux_guest
```

## Running from Topaz
To run from Topaz, configure the guest package as follows:
```
$ fx set x64 --packages topaz/packages/topaz,garnet/packages/experimental/linux_guest,garnet/packages/experimental/zircon_guest
```

After netbooting the guest packages can be launched from the system launcher as
`linux_guest` and `zircon_guest`.

## Building for arm64
First flash Zedboot onto your VIM2:
```
$ cd $ZIRCON_DIR
$ scripts/build-zircon-arm64
$ scripts/flash-vim2 -m

```

Then configure, build, and boot the guest package as follows:
```
$ fx set arm64 --packages garnet/packages/experimental/linux_guest,garnet/packages/experimental/zircon_guest --args guest_display=\"framebuffer\" --netboot
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
