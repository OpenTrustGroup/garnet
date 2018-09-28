// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CAMERA_MANAGER_CAMERA_MANAGER_IMPL_H_
#define GARNET_BIN_MEDIA_CAMERA_MANAGER_CAMERA_MANAGER_IMPL_H_

#include <list>

#include <ddk/debug.h>
#include <ddk/driver.h>
#include <fbl/unique_ptr.h>
#include <fuchsia/camera/cpp/fidl.h>
#include <garnet/bin/media/camera_manager/video_device_client.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fsl/io/device_watcher.h>
#include <lib/fxl/functional/closure.h>

namespace camera {
// Implements camera::Manager FIDL service.  Keeps track of the cameras and
// other video input devices that are plugged in, making that information
// available to applications.  Also, keeps track of the connections to a
// device, ensuring that applications do not open more connections than the
// device can support.
class CameraManagerImpl : public fuchsia::camera::Manager {
 public:
  // In addition to shuting down the camera::Manager service, this destructor
  // will attempt to cancel all video streams, even if they are connected
  // directly from the device driver to the application.
  ~CameraManagerImpl();

  // This initialization is passed the async::Loop because it will be stepping
  // the loop forward until all the devices are enumerated. |loop| should be
  // the async loop associated with the default dispatcher.
  // This constructor will not return until all existing camera devices have
  // been enumerated and set up.
  CameraManagerImpl(async::Loop *loop);

  // Returns a list of all the video devices that are currently plugged in
  // and enumerated.  The camera_id field of the DeviceInfo is used to specify
  // a device in GetFormats, GetStream and GetStreamAndBufferCollection.
  void GetDevices(GetDevicesCallback callback);

  // Get all the available formats for a camera.
  // TODO(CAM-17): Add pagination to support cameras with over 16 formats.
  void GetFormats(uint64_t camera_id, GetFormatsCallback callback);

  // Establish a camera stream connection, which allows camera image data
  // to be passed over a set of buffers.
  void CreateStream(fuchsia::camera::CameraAccessRequest request,
                    fuchsia::sysmem::BufferCollectionInfo buffer_collection,
                    fidl::InterfaceRequest<fuchsia::camera::Stream> stream);

  // Get a camera stream, and have the camera manager allocate the buffers,
  // assuming no special memory requirements.
  // TODO(CAM-16): Fill out this function, or delete it from the interface.
  void GetStreamAndBufferCollection(
      fuchsia::camera::CameraAccessRequest request,
      fidl::InterfaceRequest<fuchsia::camera::Stream> stream,
      GetStreamAndBufferCollectionCallback callback) {}

 private:
  // Called when a device is enumerated, or when this class starts, and
  // discovers all the current devices in the system.
  void OnDeviceAdded(int dir_fd, std::string filename);

  // Called by the device once it finishes initializing.
  void OnDeviceStartupComplete(uint64_t camera_id, zx_status_t status);

  std::list<std::unique_ptr<VideoDeviceClient>> active_devices_;
  // List of not-yet-activated cameras, waiting to get information from
  // the driver.
  std::list<std::unique_ptr<VideoDeviceClient>> inactive_devices_;

  std::unique_ptr<fsl::DeviceWatcher> device_watcher_;

  std::unique_ptr<component::StartupContext> context_;
  fidl::BindingSet<Manager> bindings_;
};

}  // namespace camera

#endif  // GARNET_BIN_MEDIA_CAMERA_MANAGER_CAMERA_MANAGER_IMPL_H_
