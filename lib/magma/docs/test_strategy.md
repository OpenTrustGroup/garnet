Magma Test Strategy
===================

## Architecture Diagram

* [Magma Block Diagram](block_diagram.svg)

Four major interfaces

* [Vulkan](https://www.khronos.org/vulkan)
* [magma](../include/magma_abi/magma.h)
* [magma system](../src/magma_util/platform/platform_connection.h)
* [msd](../include/msd_abi/msd.h)

Four major components

* libvulkan
* libmagma
* magma_system
* vendor msd (magma service driver)

## Challenges

* Vulkan drivers require hardware for complete testing
    * Each supported gpu requires testing of a different hardware platform
* GPUs are complex pieces of hardware with flaws that may trigger misbehavior infrequently
    * There may be tests that flake rarely
* Vulkan CTS (conformance) takes several hours to run
    * Should be run on a daily build, not part of normal CQ
* Upstreaming libvulkan changes to the vendor
    * Vendor must be provided a build and test environment with which they can validate Vulkan CTS on fuchsia
* Source access to gfxbench is restricted
    * Should we test a binary package?

## Unit Tests

Some of these require hardware; those that don't are included in pre-submit checks for CQ.

* magma_util_tests
    * Coverage 100% of magma_util (not including platform)
* magma_platform_tests:
    * Coverage 100% of magma_util/platform
* magma_system_tests
    * Coverage 100% of magma system
    * Uses mock msd
* vendor msd
    * Coverage 80-100% (may not be worth mocking out some portions of the hardware interacting code)
    * Several mocks used in place of hardware
        * platform mmio
        * platform bus mapper
        * address space
        * mapped batch
* libvulkan
    * Supplied by vendor
    * Uses mock magma system (if not sufficient, becomes a hardware interaction test)

## Hardware Interaction Tests

The interaction between app, libvulkan, msd, and gpu is complex.  Generally speaking the app generates vulkan command buffers and shader programs which are created in a gpu specific binary format by libvulkan. 
Those command buffers as well as other resources are shared with the magma system driver, which maps resources into the gpu's address space and schedules command buffers on the gpu's execution units.

* magma_abi_conformance_tests
    * Does not execute command buffers; rely on Vulkan CTS for command buffer coverage
* msd_abi_conformance_tests
    * Validates a vendor's msd implementation
    * Coverage goal 100%, currently ~50% (MA-451 for implementing vendor specifics)
* vendor specific
    * Shutdown
    * Hang/fault recovery
* vkreadback
    * Validates vulkan end-to-end as simply as possible
* vkloop
    * Validates hang detection and recovery
* vkext
    * Validates fuchsia vulkan extensions
* [Vulkan CTS](https://github.com/KhronosGroup/VK-GL-CTS)
    * Takes several hours to run
    * Should be run at least once a day
    * Vendor must be provided a build and test environment with which they can validate Vulkan CTS on fuchsia

### Hardware required

Fuchsia supported devices with the following gpus:

* Intel Gen 9 - Intel HD Graphics
* ARM Mali - Bifrost
* Verisilicon GC7000

GPUs are complex pieces of hardware with flaws that may trigger misbehavior infrequently. There may be tests that flake rarely.  If detected these should be treated as driver bugs.

## Performance Tests

* [Gfxbench](https://gfxbench.com)
    * A large and complex performance benchmark
    * Fuchsia details, forthcoming.

