# Escher

Escher is a physically based renderer.

## Features

 * Volumetric soft shadows
 * Color bleeding
 * Light diffusion
 * Lens effect

## Building for Fuchsia
Escher is part of the default Fuchsia build.  The "waterfall" demo is installed
as `system/bin/waterfall`.

## Building for Linux
Escher can also build on Linux.  In order to do so, you need to:
  * add the Jiri "escher_linux_dev" manifest, then Jiri update
    ```
    cd $FUCHSIA_DIR
    jiri import escher_linux_dev https://fuchsia.googlesource.com/manifest
    jiri update
    ```
  * install build dependencies
    ```
    sudo apt install libxinerama-dev libxrandr-dev libxcursor-dev libx11-xcb-dev libx11-dev mesa_common_dev
    ```
  * install a GPU driver that supports Vulkan
    * NVIDIA: version >= 367.35
    * Intel: Mesa >= 12.0 ([installation instructions](https://stackoverflow.com/questions/40783620/how-to-install-intel-graphics-drivers-with-vulkan-support-for-ubuntu-16-04-xen/40792607#40792607))
  * install the [LunarG Vulkan SDK](https://lunarg.com/vulkan-sdk/) (installed
    into third_party/vulkansdk when Escher is pulled down by jiri as a part of Fuchsia)
  * set the VULKAN_SDK, VK_LAYER_PATH, and LD_LIBRARY_PATH environment variables, e.g.:
    ```
    export VULKAN_SDK=$FUCHSIA_DIR/garnet/public/lib/escher/third_party/vulkansdk/x86_64
    export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$VULKAN_SDK/lib
    export VK_LAYER_PATH=$VULKAN_SDK/etc/explicit_layer.d
    ```
  * specify that you want to build only the Escher module, for Linux:
    ```
    cd $FUCHSIA_DIR
    fx set x64  --packages garnet/packages/experimental/examples_escher_linux,garnet/packages/experimental/tests_escher_linux,garnet/packages/examples/fidl_echo
    ```
    * See `$FUCHSIA_DIR/docs/getting_source.md` for how to set up the `fx` tool.
    * There may be a spurious error regarding `skia_use_sfntly`; ignore it.
    * `cobalt_client` shouldn't be necessary; it only is due to a bug where the build system expects there to be a non-empty system image.
    * NOTE!! These commands may conflict with the Vulkan SDK on your LD_LIBRARY_PATH.  It is probably best to run these commands in one terminal window, then switch to another and setting LD_LIBRARY_PATH before Building
    and running.
  * Do this once only: `fx full-build`
    * Note: you can exit via Ctrl-C once Zircon is finished building.  
  * BUILD!! AND RUN!!!
    ```
    buildtools/ninja -C out/release-x64/ && out/release-x64/host_x64/waterfall
    ```
