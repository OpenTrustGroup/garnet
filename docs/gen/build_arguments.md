# GN Build Arguments

## All builds

### current_os

**Current value (from the default):** `""`

### kernel_cmdline_files
Files containing additional kernel command line arguments to bake into
the boot image.  The contents of these files (in order) come after any
arguments directly in [`kernel_cmdline_args`](#kernel_cmdline_args).
These can be GN `//` source pathnames or absolute system pathnames.

**Current value (from the default):** `[]`

From [//build/images/BUILD.gn:306](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/images/BUILD.gn#306)

### universal_variants

**Current value (from the default):**
```
[{
  configs = []
  name = "release"
  toolchain_args = {
  is_debug = false
}
}]
```

From [//build/config/BUILDCONFIG.gn:389](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/config/BUILDCONFIG.gn#389)

### bootfs_extra
List of extra manifest entries for files to add to the BOOTFS.
Each entry can be a "TARGET=SOURCE" string, or it can be a scope
with `sources` and `outputs` in the style of a copy() target:
`outputs[0]` is used as `TARGET` (see `gn help source_expansion`).

**Current value (from the default):** `[]`

From [//build/images/BUILD.gn:312](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/images/BUILD.gn#312)

### enable_input_subsystem

**Current value (from the default):** `true`

From [//garnet/bin/ui/scenic/BUILD.gn:14](https://fuchsia.googlesource.com/garnet/+/a01c3dcf48592790417b2f8bf4576675957399b7/bin/ui/scenic/BUILD.gn#14)

### use_vulkan_loader_for_tests
Mesa doesn't properly handle loader-less operation;
their GetInstanceProcAddr implementation returns 0 for some interfaces.
On ARM there may be multiple libvulkan_arms, so they can't all be linked
to.

**Current value (from the default):** `true`

From [//garnet/lib/magma/gnbuild/magma.gni:33](https://fuchsia.googlesource.com/garnet/+/a01c3dcf48592790417b2f8bf4576675957399b7/lib/magma/gnbuild/magma.gni#33)

### vk_loader_debug

**Current value (from the default):** `"warn,error"`

From [//third_party/vulkan_loader_and_validation_layers/loader/BUILD.gn:27](https://fuchsia.googlesource.com/third_party/vulkan_loader_and_validation_layers/+/fdfa431aa1381f85717cf005df30e34002dded0c/loader/BUILD.gn#27)

### zircon_build_root

**Current value (from the default):** `"//zircon"`

From [//garnet/lib/magma/gnbuild/magma.gni:10](https://fuchsia.googlesource.com/garnet/+/a01c3dcf48592790417b2f8bf4576675957399b7/lib/magma/gnbuild/magma.gni#10)

### data_image_size
The size of the minfs data partition image to create. Normally this image
is added to FVM, and can therefore expand as needed. It must be at least
10mb (the default) in order to be succesfully initialized.

**Current value (from the default):** `"10m"`

From [//build/images/BUILD.gn:475](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/images/BUILD.gn#475)

### enable_frame_pointers
Controls whether the compiler emits full stack frames for function calls.
This reduces performance but increases the ability to generate good
stack traces, especially when we have bugs around unwind table generation.
It applies only for Fuchsia targets (see below where it is unset).

TODO(ZX-2361): Theoretically unwind tables should be good enough so we can
remove this option when the issues are addressed.

**Current value (from the default):** `true`

From [//build/config/BUILD.gn:23](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/config/BUILD.gn#23)

### enable_sketchy_subsystem

**Current value (from the default):** `true`

From [//garnet/bin/ui/scenic/BUILD.gn:13](https://fuchsia.googlesource.com/garnet/+/a01c3dcf48592790417b2f8bf4576675957399b7/bin/ui/scenic/BUILD.gn#13)

### select_variant
List of "selectors" to request variant builds of certain targets.
Each selector specifies matching criteria and a chosen variant.
The first selector in the list to match a given target determines
which variant is used for that target.

Each selector is either a string or a scope.  A shortcut selector is
a string; it gets expanded to a full selector.  A full selector is a
scope, described below.

A string selector can match a name in
[`select_variant_shortcuts`](#select_variant_shortcuts).  If it's not a
specific shortcut listed there, then it can be the name of any variant
described in [`known_variants`](#known_variants) and
[`universal_variants`](#universal_variants) (and combinations thereof).
A `selector` that's a simple variant name selects for every binary
built in the target toolchain: `{ host=false variant=selector }`.

If a string selector contains a slash, then it's `"shortcut/filename"`
and selects only the binary in the target toolchain whose `output_name`
matches `"filename"`, i.e. it adds `output_name=["filename"]` to each
selector scope that the shortcut's name alone would yield.

The scope that forms a full selector defines some of these:

    variant (required)
        [string or `false`] The variant that applies if this selector
        matches.  This can be `false` to choose no variant, or a string
        that names the variant.  See
        [`known_variants`](#known_variants) and
        [`universal_variants`](#universal_variants).

The rest below are matching criteria.  All are optional.
The selector matches if and only if all of its criteria match.
If none of these is defined, then the selector always matches.

The first selector in the list to match wins and then the rest of
the list is ignored.  So construct more complex rules by using a
"blacklist" selector with `variant=false` before a catch-all or
"whitelist" selector that names a variant.

Each "[strings]" criterion is a list of strings, and the criterion
is satisfied if any of the strings matches against the candidate string.

    host
        [boolean] If true, the selector matches in the host toolchain.
        If false, the selector matches in the target toolchain.

    testonly
        [boolean] If true, the selector matches targets with testonly=true.
        If false, the selector matches in targets without testonly=true.

    target_type
        [strings]: `"executable"`, `"loadable_module"`, or `"driver_module"`

    output_name
        [strings]: target's `output_name` (default: its `target name`)

    label
        [strings]: target's full label with `:` (without toolchain suffix)

    name
        [strings]: target's simple name (label after last `/` or `:`)

    dir
        [strings]: target's label directory (`//dir` for `//dir:name`).

**Current value (from the default):** `[]`

From [//build/config/BUILDCONFIG.gn:571](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/config/BUILDCONFIG.gn#571)

### synthesize_packages
List of extra packages to synthesize on the fly.  This is only for
things that do not appear normally in the source tree.  Synthesized
packages can contain build artifacts only if they already exist in some
part of the build.  They can contain arbitrary verbatim files.
Synthesized packages can't express dependencies on other packages.

Each element of this list is a scope that is very much like the body of
a package() template invocation (see [//build/package.gni](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/package.gni)).  That scope
must set `name` to the string naming the package, as would be the name
in the package() target written in a GN file.  This must be unique
among all package names.

**Current value (from the default):** `[]`

From [//build/gn/packages.gni:44](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/gn/packages.gni#44)

### zircon_aux_manifests

**Current value (from the default):** `["//out/build-zircon/build-arm64-asan/bootfs.manifest"]`

From [//build/images/manifest.gni:32](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/images/manifest.gni#32)

### amber_repository_dir
Directory containing files named by their merkleroot content IDs in
ASCII hex.  The [//build/image](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/image):amber_publish_blobs target populates
this with copies of build products, but never removes old files.

**Current value (from the default):** `"//root_build_dir/amber-files"`

From [//garnet/go/src/amber/amber.gni:11](https://fuchsia.googlesource.com/garnet/+/a01c3dcf48592790417b2f8bf4576675957399b7/go/src/amber/amber.gni#11)

### current_cpu

**Current value (from the default):** `""`

### expat_build_root

**Current value (from the default):** `"//third_party/expat"`

From [//garnet/lib/magma/gnbuild/magma.gni:7](https://fuchsia.googlesource.com/garnet/+/a01c3dcf48592790417b2f8bf4576675957399b7/lib/magma/gnbuild/magma.gni#7)

### fuchsia_products
List of product definition files describing the packages to build, and
where they are to be installed in images and updates.

A product definition file is a JSON file containing:
monolith:
  a list of packages included in OTA images, base system images, and the
  distribution repository.
preinstall:
  a list of packages pre-installed on the system (also added to the
  distribution repository)
available:
  a list of packages only added to the distribution repository)

If a package is referenced in monolith and in preinstall, monolith takes
priority, and the package will be added to OTA images as part of the
verified boot set of static packages.

If unset, layer will be guessed using //.jiri_manifest and
//{layer}/products/default will be used.

**Current value (from the default):** `[]`

From [//build/gn/packages.gni:26](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/gn/packages.gni#26)

### host_byteorder

**Current value (from the default):** `"undefined"`

From [//build/config/host_byteorder.gni:7](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/config/host_byteorder.gni#7)

### is_debug
Debug build.

**Current value (from the default):** `true`

From [//build/config/BUILDCONFIG.gn:11](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/config/BUILDCONFIG.gn#11)

### use_prebuilt_ffmpeg
Use a prebuilt ffmpeg binary rather than building it locally.  See
[//garnet/bin/mediaplayer/ffmpeg/README.md](https://fuchsia.googlesource.com/garnet/+/a01c3dcf48592790417b2f8bf4576675957399b7/bin/mediaplayer/ffmpeg/README.md) for details.  This is
ignored when building media_player in variant builds (e.g. sanitizers);
in that case, ffmpeg is always built from source so as to be built with
the selected variant's config.  When this is false (either explicitly
or because media_player is a variant build) then //third_party/ffmpeg
must be in the source tree, which requires:
`jiri import -name garnet manifest/ffmpeg https://fuchsia.googlesource.com/garnet`

**Current value (from the default):** `true`

From [//garnet/bin/mediaplayer/ffmpeg/BUILD.gn:14](https://fuchsia.googlesource.com/garnet/+/a01c3dcf48592790417b2f8bf4576675957399b7/bin/mediaplayer/ffmpeg/BUILD.gn#14)

### use_thinlto
Use ThinLTO variant of LTO if use_lto = true.

**Current value (from the default):** `true`

From [//build/config/lto/config.gni:10](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/config/lto/config.gni#10)

### always_zedboot
Build boot images that prefer Zedboot over local boot.

**Current value (from the default):** `false`

From [//build/images/BUILD.gn:484](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/images/BUILD.gn#484)

### zircon_use_asan
Set this if [`zircon_build_dir`](#zircon_build_dir) was built with
`USE_ASAN=true`, e.g. `[//scripts/build-zircon.sh](https://fuchsia.googlesource.com/scripts/+/0aa8130aebd675f5fda2f0bba7881dc4dfd30de4/build-zircon.sh) -A`.  This mainly
affects the defaults for [`zircon_build_dir`](#zircon_build_dir) and
[`zircon_build_abi_dir`](#zircon_build_abi_dir).  It also gets noticed
by [//scripts/fx](https://fuchsia.googlesource.com/scripts/+/0aa8130aebd675f5fda2f0bba7881dc4dfd30de4/fx) commands that rebuild Zircon so that they use `-A`
again next time.

**Current value (from the default):** `false`

From [//build/config/fuchsia/zircon.gni:40](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/config/fuchsia/zircon.gni#40)

### zircon_build_dir
Zircon build directory for `target_cpu`, containing `.manifest` and
`.zbi` files for Zircon's BOOTFS and kernel.  This provides the kernel
and Zircon components used in the boot image.  It also provides the
Zircon shared libraries used at runtime in Fuchsia packages.

If left `""` (the default), then this is computed from
[`zircon_build_abi_dir`](#zircon_build_abi_dir) and
[`zircon_use_asan`](#zircon_use_asan).

**Current value (from the default):** `""`

From [//build/config/fuchsia/zircon.gni:24](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/config/fuchsia/zircon.gni#24)

### thinlto_jobs
Number of parallel ThinLTO jobs.

**Current value (from the default):** `8`

From [//build/config/lto/config.gni:13](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/config/lto/config.gni#13)

### devmgr_config
List of arguments to add to /boot/config/devmgr.
These come after synthesized arguments to configure blobfs and pkgfs,
and the one generated for [`enable_crashpad`](#enable_crashpad).

**Current value (from the default):** `[]`

From [//build/images/BUILD.gn:295](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/images/BUILD.gn#295)

### thinlto_cache_dir
ThinLTO cache directory path.

**Current value (from the default):** `"thinlto-cache"`

From [//build/config/lto/config.gni:16](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/config/lto/config.gni#16)

### zedboot_devmgr_config
List of arguments to populate /boot/config/devmgr in the Zedboot image.

**Current value (from the default):** `["netsvc.netboot=true", "virtcon.font=18x32"]`

From [//build/images/zedboot/BUILD.gn:22](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/images/zedboot/BUILD.gn#22)

### build_sdk_archives
Whether to build SDK tarballs.

**Current value (from the default):** `false`

From [//build/sdk/sdk.gni:11](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/sdk/sdk.gni#11)

### guest_display

**Current value (from the default):** `"scenic"`

From [//garnet/bin/guest/pkg/guest_package.gni:8](https://fuchsia.googlesource.com/garnet/+/a01c3dcf48592790417b2f8bf4576675957399b7/bin/guest/pkg/guest_package.gni#8)

### magma_build_root

**Current value (from the default):** `"//garnet/lib/magma"`

From [//garnet/lib/magma/gnbuild/magma.gni:6](https://fuchsia.googlesource.com/garnet/+/a01c3dcf48592790417b2f8bf4576675957399b7/lib/magma/gnbuild/magma.gni#6)

### target_cpu

**Current value for `target_cpu = "arm64"`:** `"arm64"`

From //root_build_dir/args.gn:1

**Overridden from the default:** `""`

**Current value for `target_cpu = "x64"`:** `"x64"`

From //root_build_dir/args.gn:1

**Overridden from the default:** `""`

### use_lto
Use link time optimization (LTO).

**Current value (from the default):** `false`

From [//build/config/lto/config.gni:7](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/config/lto/config.gni#7)

### zircon_tools_dir
Where to find Zircon's host-side tools that are run as part of the build.

**Current value (from the default):** `"//out/build-zircon/tools"`

From [//build/config/fuchsia/zircon.gni:9](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/config/fuchsia/zircon.gni#9)

### enable_value_subsystem

**Current value (from the default):** `false`

From [//garnet/bin/ui/scenic/BUILD.gn:11](https://fuchsia.googlesource.com/garnet/+/a01c3dcf48592790417b2f8bf4576675957399b7/bin/ui/scenic/BUILD.gn#11)

### extra_authorized_keys_file
Additional SSH authorized_keys file to include in the build.
For example:
  extra_authorized_keys_file=\"$HOME/.ssh/id_rsa.pub\"

**Current value (from the default):** `""`

From [//third_party/openssh-portable/fuchsia/developer-keys/BUILD.gn:11](https://fuchsia.googlesource.com/third_party/openssh-portable/+/961fd14159e8bde1185388dcf4bb240a7fa49841/fuchsia/developer-keys/BUILD.gn#11)

### host_cpu

**Current value (from the default):** `"x64"`

### magma_enable_tracing
Enable this to include fuchsia tracing capability

**Current value (from the default):** `true`

From [//garnet/lib/magma/gnbuild/magma.gni:15](https://fuchsia.googlesource.com/garnet/+/a01c3dcf48592790417b2f8bf4576675957399b7/lib/magma/gnbuild/magma.gni#15)

### scenic_vulkan_swapchain

**Current value (from the default):** `1`

From [//garnet/lib/ui/gfx/BUILD.gn:10](https://fuchsia.googlesource.com/garnet/+/a01c3dcf48592790417b2f8bf4576675957399b7/lib/ui/gfx/BUILD.gn#10)

### select_variant_shortcuts
List of short names for commonly-used variant selectors.  Normally this
is not set as a build argument, but it serves to document the available
set of short-cut names for variant selectors.  Each element of this list
is a scope where `.name` is the short name and `.select_variant` is a
a list that can be spliced into [`select_variant`](#select_variant).

**Current value (from the default):**
```
[{
  name = "host_asan"
  select_variant = [{
  dir = ["//third_party/yasm", "//third_party/vboot_reference", "//garnet/tools/vboot_reference"]
  host = true
  variant = "asan_no_detect_leaks"
}, {
  host = true
  variant = "asan"
}]
}]
```

From [//build/config/BUILDCONFIG.gn:415](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/config/BUILDCONFIG.gn#415)

### amber_repository_blobs_dir

**Current value (from the default):** `"//root_build_dir/amber-files/repository/blobs"`

From [//garnet/go/src/amber/amber.gni:16](https://fuchsia.googlesource.com/garnet/+/a01c3dcf48592790417b2f8bf4576675957399b7/go/src/amber/amber.gni#16)

### rustc_prefix
Sets a custom base directory for `rustc` and `cargo`.
This can be used to test custom Rust toolchains.

**Current value (from the default):** `"//buildtools/linux-x64/rust/bin"`

From [//build/rust/config.gni:16](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/rust/config.gni#16)

### use_scudo
Enable the [Scudo](https://llvm.org/docs/ScudoHardenedAllocator.html)
memory allocator.

**Current value (from the default):** `true`

From [//build/config/scudo/scudo.gni:8](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/config/scudo/scudo.gni#8)

### zedboot_cmdline_args
List of kernel command line arguments to bake into the Zedboot image.
See [//zircon/docs/kernel_cmdline.md](https://fuchsia.googlesource.com/zircon/+/c62196fd9337dd32b242f584884eef0847999866/docs/kernel_cmdline.md) and
[`zedboot_devmgr_config`](#zedboot_devmgr_config).

**Current value (from the default):** `[]`

From [//build/images/zedboot/BUILD.gn:13](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/images/zedboot/BUILD.gn#13)

### zircon_boot_manifests
Manifest files describing files to go into the `/boot` filesystem.
Can be either // source paths or absolute system paths.
`zircon_boot_groups` controls which files are actually selected.

Since Zircon manifest files are relative to a Zircon source directory
rather than to the directory containing the manifest, these are assumed
to reside in a build directory that's a direct subdirectory of the
Zircon source directory and thus their contents can be taken as
relative to `get_path_info(entry, "dir") + "/.."`.

**Current value (from the default):** `["//out/build-zircon/build-arm64/bootfs.manifest"]`

From [//build/images/manifest.gni:44](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/images/manifest.gni#44)

### kernel_cmdline_args
List of kernel command line arguments to bake into the boot image.
See also [//zircon/docs/kernel_cmdline.md](https://fuchsia.googlesource.com/zircon/+/c62196fd9337dd32b242f584884eef0847999866/docs/kernel_cmdline.md) and
[`devmgr_config`](#devmgr_config).

**Current value (from the default):** `[]`

From [//build/images/BUILD.gn:300](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/images/BUILD.gn#300)

### extra_variants
Additional variant toolchain configs to support.
This is just added to [`known_variants`](#known_variants).

**Current value (from the default):** `[]`

From [//build/config/BUILDCONFIG.gn:369](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/config/BUILDCONFIG.gn#369)

### host_tools_dir
This is the directory where host tools intended for manual use by
developers get installed.  It's something a developer might put
into their shell's $PATH.  Host tools that are just needed as part
of the build do not get copied here.  This directory is only for
things that are generally useful for testing or debugging or
whatnot outside of the GN build itself.  These are only installed
by an explicit install_host_tools() rule (see [//build/host.gni](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/host.gni)).

**Current value (from the default):** `"//root_build_dir/tools"`

From [//build/host.gni:13](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/host.gni#13)

### icu_use_data_file
Tells icu to load an external data file rather than rely on the icudata
being linked directly into the binary.

This flag is a bit confusing. As of this writing, icu.gyp set the value to
0 but common.gypi sets the value to 1 for most platforms (and the 1 takes
precedence).

TODO(GYP) We'll probably need to enhance this logic to set the value to
true or false in similar circumstances.

**Current value (from the default):** `true`

From [//third_party/icu/config.gni:15](https://fuchsia.googlesource.com/third_party/icu/+/15006476e9d2f5c7d6691f3658fecff4929aaf68/config.gni#15)

### magma_python_path

**Current value (from the default):** `"/b/s/w/ir/kitchen-workdir/third_party/mako"`

From [//garnet/lib/magma/gnbuild/magma.gni:12](https://fuchsia.googlesource.com/garnet/+/a01c3dcf48592790417b2f8bf4576675957399b7/lib/magma/gnbuild/magma.gni#12)

### msd_intel_gen_build_root

**Current value (from the default):** `"//garnet/drivers/gpu/msd-intel-gen"`

From [//garnet/lib/magma/gnbuild/magma.gni:8](https://fuchsia.googlesource.com/garnet/+/a01c3dcf48592790417b2f8bf4576675957399b7/lib/magma/gnbuild/magma.gni#8)

### sdk_dirs
The directories to search for parts of the SDK.

By default, we search the public directories for the various layers.
In the future, we'll search a pre-built SDK as well.

**Current value (from the default):** `["//garnet/public", "//peridot/public", "//topaz/public"]`

From [//build/config/fuchsia/sdk.gni:10](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/config/fuchsia/sdk.gni#10)

### toolchain_manifests
Manifest files describing target libraries from toolchains.
Can be either // source paths or absolute system paths.

**Current value (from the default):** `["/b/s/w/ir/kitchen-workdir/buildtools/linux-x64/clang/lib/aarch64-fuchsia.manifest"]`

From [//build/images/manifest.gni:11](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/images/manifest.gni#11)

### amlogic_decoder_tests

**Current value (from the default):** `false`

From [//garnet/drivers/video/amlogic-decoder/BUILD.gn:9](https://fuchsia.googlesource.com/garnet/+/a01c3dcf48592790417b2f8bf4576675957399b7/drivers/video/amlogic-decoder/BUILD.gn#9)

### zedboot_cmdline_files
Files containing additional kernel command line arguments to bake into
the Zedboot image.  The contents of these files (in order) come after any
arguments directly in [`zedboot_cmdline_args`](#zedboot_cmdline_args).
These can be GN `//` source pathnames or absolute system pathnames.

**Current value (from the default):** `[]`

From [//build/images/zedboot/BUILD.gn:19](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/images/zedboot/BUILD.gn#19)

### zircon_build_abi_dir
Zircon build directory for `target_cpu`, containing link-time `.so.abi`
files that GN `deps` on [//zircon/public](https://fuchsia.googlesource.com/zircon/+/c62196fd9337dd32b242f584884eef0847999866/public) libraries will link against.
This should not be a sanitizer build.

**Current value (from the default):** `"//out/build-zircon/build-arm64"`

From [//build/config/fuchsia/zircon.gni:14](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/config/fuchsia/zircon.gni#14)

### toolchain_variant
*This should never be set as a build argument.*
It exists only to be set in `toolchain_args`.
See [//build/toolchain/clang_toolchain.gni](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/toolchain/clang_toolchain.gni) for details.
This variable is a scope giving details about the current toolchain:
    `toolchain_variant.base`
        [label] The "base" toolchain for this variant, *often the
        right thing to use in comparisons, not `current_toolchain`.*
        This is the toolchain actually referenced directly in GN
        source code.  If the current toolchain is not
        `shlib_toolchain` or a variant toolchain, this is the same
        as `current_toolchain`.  In one of those derivative
        toolchains, this is the toolchain the GN code probably
        thought it was in.  This is the right thing to use in a test
        like `toolchain_variant.base == target_toolchain`, rather
        rather than comparing against `current_toolchain`.
    `toolchain_variant.name`
        [string] The name of this variant, as used in `variant` fields
        in [`select_variant`](#select_variant) clauses.  In the base
        toolchain and its `shlib_toolchain`, this is `""`.
    `toolchain_variant.suffix`
        [string] This is "-${toolchain_variant.name}", "" if name is empty.
    `toolchain_variant.is_pic_default`
        [bool] This is true in `shlib_toolchain`.
The other fields are the variant's effects as defined in
[`known_variants`](#known_variants).

**Current value (from the default):**
```
{
  base = "//build/toolchain/fuchsia:arm64"
}
```

From [//build/config/BUILDCONFIG.gn:71](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/config/BUILDCONFIG.gn#71)

### crashpad_dependencies
Determines various flavors of build configuration, and which concrete
targets to use for dependencies. Valid values are "standalone",
"chromium", and "fuchsia". Defaulted to "fuchsia" because
"is_fuchsia_tree" is set.

**Current value (from the default):** `"fuchsia"`

From [//third_party/crashpad/build/crashpad_buildconfig.gni:26](https://chromium.googlesource.com/crashpad/crashpad/+/f8b0538406ea35cd19beb33f1f9c48807c670014/build/crashpad_buildconfig.gni#26)

### enable_crashpad
When this is set, Crashpad will be used to handle exceptions (which uploads
crashes to the crash server), rather than crashanalyzer in Zircon (which
prints the crash log to the the system log).

**Current value (from the default):** `false`

From [//build/images/crashpad.gni:9](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/images/crashpad.gni#9)

### escher_use_null_vulkan_config_on_host
Using Vulkan on host (i.e. Linux) is an involved affair that involves
downloading the Vulkan SDK, setting environment variables, and so forth...
all things that are difficult to achieve in a CQ environment.  Therefore,
by default we use a stub implementation of Vulkan which fails to create a
VkInstance.  This allows everything to build, and also allows running Escher
unit tests which don't require Vulkan.

**Current value (from the default):** `true`

From [//garnet/public/lib/escher/BUILD.gn:15](https://fuchsia.googlesource.com/garnet/+/a01c3dcf48592790417b2f8bf4576675957399b7/public/lib/escher/BUILD.gn#15)

### host_os

**Current value (from the default):** `"linux"`

### scenic_ignore_vsync

**Current value (from the default):** `false`

From [//garnet/lib/ui/gfx/BUILD.gn:14](https://fuchsia.googlesource.com/garnet/+/a01c3dcf48592790417b2f8bf4576675957399b7/lib/ui/gfx/BUILD.gn#14)

### select_variant_canonical
*This should never be set as a build argument.*
It exists only to be set in `toolchain_args`.
See [//build/toolchain/clang_toolchain.gni](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/toolchain/clang_toolchain.gni) for details.

**Current value (from the default):** `[]`

From [//build/config/BUILDCONFIG.gn:576](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/config/BUILDCONFIG.gn#576)

### symbol_level
How many symbols to include in the build. This affects the performance of
the build since the symbols are large and dealing with them is slow.
  2 means regular build with symbols.
  1 means minimal symbols, usually enough for backtraces only. Symbols with
internal linkage (static functions or those in anonymous namespaces) may not
appear when using this level.
  0 means no symbols.

**Current value (from the default):** `2`

From [//build/config/BUILD.gn:13](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/config/BUILD.gn#13)

### build_msd_arm_mali

**Current value (from the default):** `true`

From [//garnet/lib/magma/gnbuild/magma.gni:21](https://fuchsia.googlesource.com/garnet/+/a01c3dcf48592790417b2f8bf4576675957399b7/lib/magma/gnbuild/magma.gni#21)

### zircon_system_groups
Groups to include from the Zircon /boot manifest into /system
(instead of into /boot like Zircon's own bootdata.bin does).
Should not include any groups that are also in zircon_boot_groups,
which see.  If zircon_boot_groups is "all" then this should be "".
**TODO(mcgrathr)**: _Could default to "" for `!is_debug`, or "production
build".  Note including `"test"` here places all of Zircon's tests into
`/system/test`, which means that Fuchsia bots run those tests too._

**Current value (from the default):** `"misc,test"`

From [//build/images/BUILD.gn:34](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/images/BUILD.gn#34)

### goma_dir
Absolute directory containing the Goma source code.

**Current value (from the default):** `"/home/swarming/goma"`

From [//build/toolchain/goma.gni:12](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/toolchain/goma.gni#12)

### prebuilt_libvulkan_arm_path

**Current value (from the default):** `""`

From [//garnet/lib/magma/gnbuild/magma.gni:25](https://fuchsia.googlesource.com/garnet/+/a01c3dcf48592790417b2f8bf4576675957399b7/lib/magma/gnbuild/magma.gni#25)

### crashpad_use_boringssl_for_http_transport_socket
This should at least be on for Fuchsia, but DX-382 happened.
TODO(mark): Figure out what went wrong and re-enable.

**Current value (from the default):** `false`

From [//third_party/crashpad/util/net/tls.gni:18](https://chromium.googlesource.com/crashpad/crashpad/+/f8b0538406ea35cd19beb33f1f9c48807c670014/util/net/tls.gni#18)

### build_vsl_gc

**Current value (from the default):** `true`

From [//garnet/lib/magma/gnbuild/magma.gni:22](https://fuchsia.googlesource.com/garnet/+/a01c3dcf48592790417b2f8bf4576675957399b7/lib/magma/gnbuild/magma.gni#22)

### clang_prefix

**Current value (from the default):** `"../buildtools/linux-x64/clang/bin"`

From [//build/config/clang/clang.gni:9](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/config/clang/clang.gni#9)

### fuchsia_packages
List of packages (a GN list of strings).
This list of packages is added to the set of "available" packages, see
`products` for more information.

**Current value for `target_cpu = "arm64"`:** `["garnet/packages/buildbot"]`

From //root_build_dir/args.gn:2

**Overridden from the default:** `[]`

From [//build/gn/packages.gni:31](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/gn/packages.gni#31)

**Current value for `target_cpu = "x64"`:** `["garnet/packages/buildbot"]`

From //root_build_dir/args.gn:2

**Overridden from the default:** `[]`

From [//build/gn/packages.gni:31](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/gn/packages.gni#31)

### magma_enable_developer_build
Enable this to have the msd include a suite of tests and invoke them
automatically when the driver starts.

**Current value (from the default):** `false`

From [//garnet/lib/magma/gnbuild/magma.gni:19](https://fuchsia.googlesource.com/garnet/+/a01c3dcf48592790417b2f8bf4576675957399b7/lib/magma/gnbuild/magma.gni#19)

### scudo_default_options
Default [Scudo](https://llvm.org/docs/ScudoHardenedAllocator.html)
options (before the `SCUDO_OPTIONS` environment variable is read at
runtime).  *NOTE:* This affects only components using the `scudo`
variant (see GN build argument `select_variant`), and does not affect
anything when the `use_scudo` build flag is set instead.

**Current value (from the default):** `["abort_on_error=1", "QuarantineSizeKb=0", "ThreadLocalQuarantineSizeKb=0", "DeallocationTypeMismatch=false", "DeleteSizeMismatch=false", "allocator_may_return_null=true"]`

From [//build/config/scudo/scudo.gni:15](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/config/scudo/scudo.gni#15)

### use_ccache
Set to true to enable compiling with ccache

**Current value (from the default):** `false`

From [//build/toolchain/ccache.gni:9](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/toolchain/ccache.gni#9)

### build_libvulkan
This is a list of targets that will be built as vulkan ICDS. If more than one
target is given then use_vulkan_loader_for_tests must be set to true, as
otherwise tests won't know which libvulkan to use.

**Current value (from the default):** `[]`

From [//garnet/lib/magma/gnbuild/magma.gni:38](https://fuchsia.googlesource.com/garnet/+/a01c3dcf48592790417b2f8bf4576675957399b7/lib/magma/gnbuild/magma.gni#38)

### rust_lto
Sets the default LTO type for rustc bulids.

**Current value (from the default):** `"unset"`

From [//build/rust/config.gni:19](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/rust/config.gni#19)

### system_package_key
The package key to use for signing Fuchsia packages made by the
`package()` template (and the `system_image` packge).  If this
doesn't exist yet when it's needed, it will be generated.  New
keys can be generated with the `pm -k FILE genkey` host command.

**Current value (from the default):** `"//build/development.key"`

From [//build/package.gni:14](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/package.gni#14)

### target_os

**Current value (from the default):** `""`

### zircon_asserts

**Current value (from the default):** `true`

From [//build/config/fuchsia/BUILD.gn:138](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/config/fuchsia/BUILD.gn#138)

### amber_keys_dir
Directory containing signing keys used by amber-publish.

**Current value (from the default):** `"//garnet/go/src/amber/keys"`

From [//garnet/go/src/amber/amber.gni:14](https://fuchsia.googlesource.com/garnet/+/a01c3dcf48592790417b2f8bf4576675957399b7/go/src/amber/amber.gni#14)

### fvm_image_size
The size in bytes of the FVM partition image to create. Normally this is
computed to be just large enough to fit the blob and data images. The
default value is "", which means to size based on inputs. Specifying a size
that is too small will result in build failure.

**Current value (from the default):** `""`

From [//build/images/BUILD.gn:481](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/images/BUILD.gn#481)

### target_sysroot
The absolute path of the sysroot that is used with the target toolchain.

**Current value (from the default):** `""`

From [//build/config/sysroot.gni:7](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/config/sysroot.gni#7)

### zircon_boot_groups
Groups to include from the Zircon /boot manifest into /boot.
This is either "all" or a comma-separated list of one or more of:
  core -- necessary to boot
  misc -- utilities in /bin
  test -- test binaries in /bin and /test

**Current value (from the default):** `"core"`

From [//build/images/BUILD.gn:23](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/images/BUILD.gn#23)

### build_intel_gen

**Current value (from the default):** `false`

From [//garnet/lib/magma/gnbuild/magma.gni:23](https://fuchsia.googlesource.com/garnet/+/a01c3dcf48592790417b2f8bf4576675957399b7/lib/magma/gnbuild/magma.gni#23)

### enable_gfx_subsystem

**Current value (from the default):** `true`

From [//garnet/bin/ui/scenic/BUILD.gn:12](https://fuchsia.googlesource.com/garnet/+/a01c3dcf48592790417b2f8bf4576675957399b7/bin/ui/scenic/BUILD.gn#12)

### glm_build_root

**Current value (from the default):** `"//third_party/glm"`

From [//garnet/lib/magma/gnbuild/magma.gni:9](https://fuchsia.googlesource.com/garnet/+/a01c3dcf48592790417b2f8bf4576675957399b7/lib/magma/gnbuild/magma.gni#9)

### known_variants
List of variants that will form the basis for variant toolchains.
To make use of a variant, set [`select_variant`](#select_variant).

Normally this is not set as a build argument, but it serves to
document the available set of variants.
See also [`universal_variants`](#universal_variants).
Only set this to remove all the default variants here.
To add more, set [`extra_variants`](#extra_variants) instead.

Each element of the list is one variant, which is a scope defining:

  `configs` (optional)
      [list of labels] Each label names a config that will be
      automatically used by every target built in this variant.
      For each config `${label}`, there must also be a target
      `${label}_deps`, which each target built in this variant will
      automatically depend on.  The `variant()` template is the
      recommended way to define a config and its `_deps` target at
      the same time.

  `remove_common_configs` (optional)
  `remove_shared_configs` (optional)
      [list of labels] This list will be removed (with `-=`) from
      the `default_common_binary_configs` list (or the
      `default_shared_library_configs` list, respectively) after
      all other defaults (and this variant's configs) have been
      added.

  `deps` (optional)
      [list of labels] Added to the deps of every target linked in
      this variant (as well as the automatic `${label}_deps` for
      each label in configs).

  `name` (required if configs is omitted)
      [string] Name of the variant as used in
      [`select_variant`](#select_variant) elements' `variant` fields.
      It's a good idea to make it something concise and meaningful when
      seen as e.g. part of a directory name under `$root_build_dir`.
      If name is omitted, configs must be nonempty and the simple names
      (not the full label, just the part after all `/`s and `:`s) of these
      configs will be used in toolchain names (each prefixed by a "-"),
      so the list of config names forming each variant must be unique
      among the lists in `known_variants + extra_variants`.

  `toolchain_args` (optional)
      [scope] Each variable defined in this scope overrides a
      build argument in the toolchain context of this variant.

  `host_only` (optional)
  `target_only` (optional)
      [scope] This scope can contain any of the fields above.
      These values are used only for host or target, respectively.
      Any fields included here should not also be in the outer scope.


**Current value (from the default):**
```
[{
  configs = ["//build/config/lto"]
}, {
  configs = ["//build/config/lto:thinlto"]
}, {
  configs = ["//build/config/profile"]
}, {
  configs = ["//build/config/scudo"]
}, {
  configs = ["//build/config/sanitizers:ubsan"]
}, {
  configs = ["//build/config/sanitizers:ubsan", "//build/config/sanitizers:sancov"]
}, {
  configs = ["//build/config/sanitizers:asan"]
  host_only = {
  remove_shared_configs = ["//build/config:symbol_no_undefined"]
}
}, {
  configs = ["//build/config/sanitizers:asan", "//build/config/sanitizers:sancov"]
  host_only = {
  remove_shared_configs = ["//build/config:symbol_no_undefined"]
}
}, {
  configs = ["//build/config/sanitizers:asan"]
  host_only = {
  remove_shared_configs = ["//build/config:symbol_no_undefined"]
}
  name = "asan_no_detect_leaks"
  toolchain_args = {
  asan_default_options = "detect_leaks=0"
}
}]
```

From [//build/config/BUILDCONFIG.gn:327](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/config/BUILDCONFIG.gn#327)

### rust_toolchain_triple_suffix
Sets the fuchsia toolchain target triple suffix (after arch)

**Current value (from the default):** `"fuchsia"`

From [//build/rust/config.gni:22](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/rust/config.gni#22)

### use_goma
Set to true to enable distributed compilation using Goma.

**Current value (from the default):** `false`

From [//build/toolchain/goma.gni:9](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/toolchain/goma.gni#9)

### zircon_asan_build_dir
Zircon `USE_ASAN=true` build directory for `target_cpu` containing
`bootfs.manifest` with libraries and `devhost.asan`.

If left `""` (the default), then this is computed from
[`zircon_build_dir`](#zircon_build_dir) and
[`zircon_use_asan`](#zircon_use_asan).

**Current value (from the default):** `""`

From [//build/config/fuchsia/zircon.gni:32](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/config/fuchsia/zircon.gni#32)

### dart_pool_depth
Maximum number of Dart processes to run in parallel.

Dart analyzer uses a lot of memory which may cause issues when building
with many parallel jobs e.g. when using goma. To avoid out-of-memory
errors we explicitly reduce the number of jobs.

**Current value (from the default):** `16`

From [//build/dart/toolchain.gni:11](https://fuchsia.googlesource.com/build/+/45dd498c1868d8b01b0a69cd74208a31fa771751/dart/toolchain.gni#11)

## `target_cpu = "arm64"`

### msd_arm_enable_all_cores
Enable all 8 cores, which is faster but emits more heat.

**Current value (from the default):** `true`

From [//garnet/drivers/gpu/msd-arm-mali/src/BUILD.gn:9](https://fuchsia.googlesource.com/garnet/+/a01c3dcf48592790417b2f8bf4576675957399b7/drivers/gpu/msd-arm-mali/src/BUILD.gn#9)

### msd_arm_enable_cache_coherency
With this flag set the system tries to use cache coherent memory if the
GPU supports it.

**Current value (from the default):** `true`

From [//garnet/drivers/gpu/msd-arm-mali/src/BUILD.gn:13](https://fuchsia.googlesource.com/garnet/+/a01c3dcf48592790417b2f8bf4576675957399b7/drivers/gpu/msd-arm-mali/src/BUILD.gn#13)

## `target_cpu = "x64"`

### mesa_build_root

**Current value (from the default):** `"//third_party/mesa"`

From [//garnet/lib/magma/gnbuild/magma.gni:41](https://fuchsia.googlesource.com/garnet/+/a01c3dcf48592790417b2f8bf4576675957399b7/lib/magma/gnbuild/magma.gni#41)

### msd_intel_enable_mapping_cache

**Current value (from the default):** `false`

From [//garnet/drivers/gpu/msd-intel-gen/src/BUILD.gn:8](https://fuchsia.googlesource.com/garnet/+/a01c3dcf48592790417b2f8bf4576675957399b7/drivers/gpu/msd-intel-gen/src/BUILD.gn#8)

### use_mock_magma

**Current value (from the default):** `false`

From [//third_party/mesa/src/intel/vulkan/BUILD.gn:25](https://fuchsia.googlesource.com/third_party/mesa/+/20a71578dc2349dcb36226aa6cb964fe0f0489ec/src/intel/vulkan/BUILD.gn#25)

