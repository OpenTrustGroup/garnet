// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_core/audio_device_manager.h"

#include <fbl/algorithm.h>
#include <string>

#include "garnet/bin/media/audio_core/audio_core_impl.h"
#include "garnet/bin/media/audio_core/audio_in_impl.h"
#include "garnet/bin/media/audio_core/audio_link.h"
#include "garnet/bin/media/audio_core/audio_output.h"
#include "garnet/bin/media/audio_core/audio_plug_detector.h"
#include "garnet/bin/media/audio_core/mixer/fx_loader.h"
#include "garnet/bin/media/audio_core/throttle_output.h"

namespace media {
namespace audio {

AudioDeviceManager::AudioDeviceManager(AudioCoreImpl* service)
    : service_(service) {}

AudioDeviceManager::~AudioDeviceManager() {
  Shutdown();
  FXL_DCHECK(devices_.is_empty());
}

// Configure this admin singleton object to manage audio device instances.
zx_status_t AudioDeviceManager::Init() {
  // Give AudioDeviceSettings a chance to ensure its storage is happy.
  AudioDeviceSettings::Initialize();

  // Instantiate and initialize the default throttle output.
  auto throttle_output = ThrottleOutput::Create(this);
  if (throttle_output == nullptr) {
    FXL_LOG(WARNING)
        << "AudioDeviceManager failed to create default throttle output!";
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t res = throttle_output->Startup();
  if (res != ZX_OK) {
    FXL_LOG(WARNING)
        << "AudioDeviceManager failed to initalize the throttle output (res "
        << res << ")";
    throttle_output->Shutdown();
  }
  throttle_output_ = std::move(throttle_output);

  // Start monitoring for plug/unplug events of pluggable audio output devices.
  res = plug_detector_.Start(this);
  if (res != ZX_OK) {
    FXL_LOG(WARNING) << "AudioDeviceManager failed to start plug detector (res "
                     << res << ")";
    return res;
  }

  // Initialize the FxLoader and load the device effect library, if present.
  res = fx_loader_.LoadLibrary();
  if (res == ZX_ERR_ALREADY_EXISTS) {
    FXL_LOG(ERROR) << "FxLoader already started!";
  } else if (res != ZX_OK) {
    FXL_LOG(WARNING) << "FxLoader::LoadLibrary failed (res: " << res << ")";
  }

  return res;
}

// We are no longer managing audio devices, unwind everything.
void AudioDeviceManager::Shutdown() {
  // Step #1: Stop monitoring plug/unplug events and cancel any pending settings
  // commit task.  We are shutting down and no longer care about these things.
  plug_detector_.Stop();
  commit_settings_task_.Cancel();

  // Step #2: Shut down each active AudioIn in the system.
  while (!audio_ins_.is_empty()) {
    auto audio_in = audio_ins_.pop_front();
    audio_in->Shutdown();
  }

  // Step #3: Shut down each active AudioOut in the system.
  while (!audio_outs_.is_empty()) {
    auto audio_out = audio_outs_.pop_front();
    audio_out->Shutdown();
  }

  // Step #4: Shut down each device which is waiting for initialization.
  while (!devices_pending_init_.is_empty()) {
    auto device = devices_pending_init_.pop_front();
    device->Shutdown();
  }

  // Step #5: Shut down each currently active device in the system.
  while (!devices_.is_empty()) {
    auto device = devices_.pop_front();
    device->Shutdown();
    FinalizeDeviceSettings(*device);
  }

  // Step #6: Close and unload the device effect library SO.
  fx_loader_.UnloadLibrary();

  // Step #7: Shut down the throttle output.
  throttle_output_->Shutdown();
  throttle_output_ = nullptr;
}

void AudioDeviceManager::AddDeviceEnumeratorClient(zx::channel ch) {
  bindings_.AddBinding(
      this, fidl::InterfaceRequest<fuchsia::media::AudioDeviceEnumerator>(
                std::move(ch)));
}

zx_status_t AudioDeviceManager::AddDevice(
    const fbl::RefPtr<AudioDevice>& device) {
  FXL_DCHECK(device != nullptr);
  FXL_DCHECK(device != throttle_output_);
  FXL_DCHECK(!device->InContainer());

  zx_status_t res = device->Startup();
  if (res != ZX_OK) {
    device->Shutdown();
  }

  devices_pending_init_.insert(device);
  return res;
}

void AudioDeviceManager::ActivateDevice(
    const fbl::RefPtr<AudioDevice>& device) {
  FXL_DCHECK(device != nullptr);
  FXL_DCHECK(device != throttle_output_);

  // Have we already been removed from the pending list?  If so, the device is
  // already shutting down and there is nothing to be done.
  if (!device->InContainer()) {
    return;
  }

  // TODO(johngro): remove this when system gain is fully deprecated.
  // For now, set each output "device" gain to the "system" gain value.
  if (device->is_output()) {
    UpdateDeviceToSystemGain(device.get());
  }

  // Determine whether this device's persistent settings are actually unique,
  // or if they collide with another device's unique ID.
  //
  // If these settings are currently unique in the system, attempt to load the
  // persisted settings from disk, or create a new persisted settings file for
  // this device if the file is either absent or corrupt.
  //
  // If these settings are not unique, then copy the settings of the device we
  // conflict with, and use them without persistence. Currently, when device
  // instances conflict, we persist only the first instance's settings.
  DeviceSettingsSet::iterator collision;
  fbl::RefPtr<AudioDeviceSettings> settings = device->device_settings();
  FXL_DCHECK(settings != nullptr);
  if (persisted_device_settings_.insert_or_find(settings, &collision)) {
    settings->InitFromDisk();
  } else {
    const uint8_t* id = settings->uid().data;
    char id_buf[33];
    snprintf(id_buf, sizeof(id_buf),
             "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
             id[0], id[1], id[2], id[3], id[4], id[5], id[6], id[7], id[8],
             id[9], id[10], id[11], id[12], id[13], id[14], id[15]);
    FXL_LOG(INFO) << "Warning: Device ID (" << device->token()
                  << ") shares a persistent unique ID (" << id_buf
                  << ") with another device in the system.  Initial Settings "
                     "will be cloned from this device, and not persisted";
    settings->InitFromClone(*collision);
  }

  // Is this device configured to be ignored? If so, remove (don't activate) it.
  if (settings->ignore_device()) {
    RemoveDevice(device);
    return;
  }

  // Move the device over to the set of active devices.
  devices_.insert(devices_pending_init_.erase(*device));
  device->SetActivated();

  // TODO(mpuryear): Create this device instance's FxProcessor here?

  // Now that we have our gain settings (restored from disk, cloned from
  // others, or default), reapply them via the device itself.  We do this in
  // order to allow the device the chance to apply its own internal limits,
  // which may not permit the values which had been read from disk.
  //
  // TODO(johngro): Clean this pattern up, it is really awkward.  On the one
  // hand, we would really like the settings to be completely independent from
  // the devices, but on the other hand, there are limits for various settings
  // which may be need imposed by the device's capabilities.
  constexpr uint32_t kAllSetFlags =
      ::fuchsia::media::SetAudioGainFlag_GainValid |
      ::fuchsia::media::SetAudioGainFlag_MuteValid |
      ::fuchsia::media::SetAudioGainFlag_AgcValid;
  ::fuchsia::media::AudioGainInfo gain_info;
  settings->GetGainInfo(&gain_info);
  device->SetGainInfo(gain_info, kAllSetFlags);

  // TODO(mpuryear): Configure the FxProcessor based on settings, here?

  // Notify interested users of this new device. Check whether this will become
  // the new default device, so we can set 'is_default' in the notification
  // properly. Right now, "default" device is defined simply as last-plugged.
  ::fuchsia::media::AudioDeviceInfo info;
  device->GetDeviceInfo(&info);

  auto last_plugged = FindLastPlugged(device->type());
  info.is_default =
      (last_plugged && (last_plugged->token() == device->token()));

  for (auto& client : bindings_.bindings()) {
    client->events().OnDeviceAdded(info);
  }

  // Reconsider our current routing policy now that a new device has arrived.
  if (device->plugged()) {
    zx_time_t plug_time = device->plug_time();
    OnDevicePlugged(device, plug_time);
  }

  // Check whether the default device has changed; if so, update users.
  UpdateDefaultDevice(device->is_input());

  // Commit (or schedule a commit for) any dirty settings.
  CommitDirtySettings();
}

void AudioDeviceManager::RemoveDevice(const fbl::RefPtr<AudioDevice>& device) {
  FXL_DCHECK(device != nullptr);
  FXL_DCHECK(device->is_output() || (static_cast<AudioDevice*>(device.get()) !=
                                     throttle_output_.get()));

  device->PreventNewLinks();
  device->Unlink();

  if (device->activated()) {
    OnDeviceUnplugged(device, device->plug_time());
  }

  // TODO(mpuryear): Persist any final remaining device-effect settings?

  device->Shutdown();
  FinalizeDeviceSettings(*device);

  // TODO(mpuryear): Delete this device instance's FxProcessor here?

  if (device->InContainer()) {
    auto& device_set = device->activated() ? devices_ : devices_pending_init_;
    device_set.erase(*device);

    // If device was active: reset the default & notify clients of the removal.
    if (device->activated()) {
      UpdateDefaultDevice(device->is_input());

      for (auto& client : bindings_.bindings()) {
        client->events().OnDeviceRemoved(device->token());
      }
    }
  }
}

void AudioDeviceManager::HandlePlugStateChange(
    const fbl::RefPtr<AudioDevice>& device, bool plugged, zx_time_t plug_time) {
  FXL_DCHECK(device != nullptr);

  // Update our bookkeeping for device's plug state. If no change, we're done.
  if (!device->UpdatePlugState(plugged, plug_time)) {
    return;
  }

  if (plugged) {
    OnDevicePlugged(device, plug_time);
  } else {
    OnDeviceUnplugged(device, plug_time);
  }

  // Check whether the default device has changed; if so, update users.
  UpdateDefaultDevice(device->is_input());
}

void AudioDeviceManager::OnSystemGainChanged() {
  for (auto& device : devices_) {
    if (device.is_output()) {
      UpdateDeviceToSystemGain(&device);
      NotifyDeviceGainChanged(device);
    }
  }
}

void AudioDeviceManager::GetDevices(GetDevicesCallback cbk) {
  std::vector<::fuchsia::media::AudioDeviceInfo> ret;

  for (const auto& dev : devices_) {
    if (dev.token() != ZX_KOID_INVALID) {
      ::fuchsia::media::AudioDeviceInfo info;
      dev.GetDeviceInfo(&info);
      info.is_default =
          (dev.token() ==
           (dev.is_input() ? default_input_token_ : default_output_token_));
      ret.push_back(std::move(info));
    }
  }

  cbk(fidl::VectorPtr<::fuchsia::media::AudioDeviceInfo>(std::move(ret)));
}

void AudioDeviceManager::GetDeviceGain(uint64_t device_token,
                                       GetDeviceGainCallback cbk) {
  auto dev = devices_.find(device_token);

  ::fuchsia::media::AudioGainInfo info = {0};
  if (dev.IsValid()) {
    FXL_DCHECK(dev->device_settings() != nullptr);
    dev->device_settings()->GetGainInfo(&info);
    cbk(device_token, std::move(info));
  } else {
    cbk(ZX_KOID_INVALID, std::move(info));
  }
}

void AudioDeviceManager::SetDeviceGain(
    uint64_t device_token, ::fuchsia::media::AudioGainInfo gain_info,
    uint32_t set_flags) {
  auto dev = devices_.find(device_token);
  if (!dev.IsValid()) {
    return;
  }

  // Change the gain and then report the new settings to our clients.
  dev->SetGainInfo(gain_info, set_flags);
  NotifyDeviceGainChanged(*dev);
  CommitDirtySettings();
}

void AudioDeviceManager::GetDefaultInputDevice(
    GetDefaultInputDeviceCallback cbk) {
  cbk(default_input_token_);
}

void AudioDeviceManager::GetDefaultOutputDevice(
    GetDefaultOutputDeviceCallback cbk) {
  cbk(default_output_token_);
}

void AudioDeviceManager::SelectOutputsForAudioOut(AudioOutImpl* audio_out) {
  FXL_DCHECK(audio_out);
  FXL_DCHECK(audio_out->format_info_valid());
  FXL_DCHECK(ValidateRoutingPolicy(routing_policy_));

  // TODO(johngro): Add a way to assert that we are on the message loop thread.

  // Regardless of policy, link the special throttle output to every AudioOut.
  LinkOutputToAudioOut(throttle_output_.get(), audio_out);

  switch (routing_policy_) {
    case fuchsia::media::AudioOutputRoutingPolicy::ALL_PLUGGED_OUTPUTS: {
      for (auto& obj : devices_) {
        FXL_DCHECK(obj.is_input() || obj.is_output());
        auto device = static_cast<AudioDevice*>(&obj);
        if (device->is_output() && device->plugged()) {
          LinkOutputToAudioOut(static_cast<AudioOutput*>(device), audio_out);
        }
      }
    } break;

    case fuchsia::media::AudioOutputRoutingPolicy::LAST_PLUGGED_OUTPUT: {
      fbl::RefPtr<AudioOutput> last_plugged = FindLastPluggedOutput();
      if (last_plugged != nullptr) {
        LinkOutputToAudioOut(last_plugged.get(), audio_out);
      }

    } break;
  }

  // Figure out the initial minimum clock lead time requirement.
  audio_out->RecomputeMinClockLeadTime();
}

void AudioDeviceManager::LinkOutputToAudioOut(AudioOutput* output,
                                              AudioOutImpl* audio_out) {
  FXL_DCHECK(output);
  FXL_DCHECK(audio_out);

  // Do not create any links if the AudioOut's output format is not yet set.
  // Links will be created during SelectOutputsForAudioOut when the AudioOut
  // format is finally set via AudioOutImpl::SetStreamType.
  if (!audio_out->format_info_valid())
    return;

  std::shared_ptr<AudioLink> link = AudioObject::LinkObjects(
      fbl::WrapRefPtr(audio_out), fbl::WrapRefPtr(output));
  // TODO(johngro): get rid of the throttle output.  See MTWN-52
  if ((link != nullptr) && (output == throttle_output_.get())) {
    FXL_DCHECK(link->source_type() == AudioLink::SourceType::Packet);
    audio_out->SetThrottleOutput(
        std::static_pointer_cast<AudioLinkPacketSource>(std::move(link)));
  }
}

void AudioDeviceManager::AddAudioIn(fbl::RefPtr<AudioInImpl> audio_in) {
  FXL_DCHECK(audio_in != nullptr);
  FXL_DCHECK(!audio_in->InContainer());
  audio_ins_.push_back(audio_in);

  fbl::RefPtr<AudioDevice> source;
  if (audio_in->loopback()) {
    source = FindLastPluggedOutput(true);
  } else {
    source = FindLastPluggedInput(true);
  }

  if (source != nullptr) {
    FXL_DCHECK(source->driver() != nullptr);
    auto initial_format = source->driver()->GetSourceFormat();

    if (initial_format) {
      audio_in->SetInitialFormat(std::move(*initial_format));
    }

    if (source->plugged()) {
      AudioObject::LinkObjects(source, audio_in);
    }
  }
}

void AudioDeviceManager::RemoveAudioIn(AudioInImpl* audio_in) {
  FXL_DCHECK(audio_in != nullptr);
  FXL_DCHECK(audio_in->InContainer());
  audio_ins_.erase(*audio_in);
}

void AudioDeviceManager::ScheduleMainThreadTask(fit::closure task) {
  FXL_DCHECK(service_);
  service_->ScheduleMainThreadTask(std::move(task));
}

fbl::RefPtr<AudioDevice> AudioDeviceManager::FindLastPlugged(
    AudioObject::Type type, bool allow_unplugged) {
  FXL_DCHECK((type == AudioObject::Type::Output) ||
             (type == AudioObject::Type::Input));
  AudioDevice* best = nullptr;

  // TODO(johngro): Consider tracking last-plugged times in a fbl::WAVLTree, so
  // this operation becomes O(1). N is pretty low right now, so the benefits do
  // not currently outweigh the complexity of maintaining this index.
  for (auto& obj : devices_) {
    auto device = static_cast<AudioDevice*>(&obj);
    if ((device->type() != type) ||
        device->device_settings()->disallow_auto_routing()) {
      continue;
    }

    if ((best == nullptr) || (!best->plugged() && device->plugged()) ||
        ((best->plugged() == device->plugged()) &&
         (best->plug_time() < device->plug_time()))) {
      best = device;
    }
  }

  FXL_DCHECK((best == nullptr) || (best->type() == type));
  if (!allow_unplugged && best && !best->plugged())
    return nullptr;

  return fbl::WrapRefPtr(best);
}

// Our policy governing the routing of audio outputs has changed. For the output
// considered "preferred" (because it was most-recently-added), nothing changes;
// all other outputs will toggle on or off, depending on the policy chosen.
void AudioDeviceManager::SetRoutingPolicy(
    fuchsia::media::AudioOutputRoutingPolicy routing_policy) {
  if (!ValidateRoutingPolicy(routing_policy)) {
    FXL_LOG(ERROR) << "Out-of-range RoutingPolicy("
                   << fidl::ToUnderlying(routing_policy) << ")";
    // TODO(mpuryear) Once AudioCore has a way to know which connection made
    // this request, terminate that connection now rather than doing nothing.
    return;
  }

  if (routing_policy == routing_policy_) {
    return;
  }

  routing_policy_ = routing_policy;
  fbl::RefPtr<AudioOutput> last_plugged_output = FindLastPluggedOutput();

  // Iterate thru all of our audio devices -- only a subset are affected.
  for (auto& dev_obj : devices_) {
    // Input devices are unaffected by changes in output-routing.
    if (dev_obj.is_input()) {
      continue;
    }

    // Only plugged-in (output) devices are affected by output-routing.
    auto output = static_cast<AudioOutput*>(&dev_obj);
    if (!output->plugged()) {
      continue;
    }

    // If device is most-recently plugged, it is also unaffected by this policy
    // change -- either way, it will continue to be attached to every AudioOut.
    FXL_DCHECK(output != throttle_output_.get());
    if (output == last_plugged_output.get()) {
      continue;
    }

    // We've excluded inputs, unplugged outputs and the most-recently-plugged
    // output. For each remaining output (based on the new policy), we ...
    if (routing_policy ==
        fuchsia::media::AudioOutputRoutingPolicy::LAST_PLUGGED_OUTPUT) {
      // ...disconnect it (i.e. link each AudioOut to Last-Plugged only), or...
      dev_obj.UnlinkSources();
    } else {
      // ...attach it (i.e. link each AudioOut to all output devices).
      for (auto& obj : audio_outs_) {
        FXL_DCHECK(obj.is_audio_out());
        auto audio_out = static_cast<AudioOutImpl*>(&obj);
        LinkOutputToAudioOut(output, audio_out);
      }
    }
  }

  // After a route change, recalculate minimum clock lead time requirements.
  for (auto& obj : audio_outs_) {
    FXL_DCHECK(obj.is_audio_out());
    auto audio_out = static_cast<AudioOutImpl*>(&obj);
    audio_out->RecomputeMinClockLeadTime();
  }
}

void AudioDeviceManager::OnDeviceUnplugged(
    const fbl::RefPtr<AudioDevice>& device, zx_time_t plug_time) {
  FXL_DCHECK(device);
  FXL_DCHECK(ValidateRoutingPolicy(routing_policy_));

  // First, see if the device is last-plugged (before updating its plug state).
  bool was_last_plugged = FindLastPlugged(device->type()) == device;

  // Update the device's plug state. If no change, then we are done.
  if (!device->UpdatePlugState(false, plug_time)) {
    return;
  }

  // This device is newly-unplugged. Unlink all its current connections.
  device->Unlink();

  // If the device which was unplugged was not the last plugged device in the
  // system, then there has been no change in who was the last plugged device,
  // and no updates to the routing state are needed.
  if (was_last_plugged) {
    if (device->is_output()) {
      // This was an output.  If we are applying 'last plugged output' policy,
      // go over our list of AudioOuts and link them to the most recently
      // plugged output (if any).  Then go over our list of audio ins and do
      // the same for each of the loopback audio ins.  Note: the current hack
      // routing policy for inputs is always 'last plugged'
      FXL_DCHECK(static_cast<AudioOutput*>(device.get()) !=
                 throttle_output_.get());

      fbl::RefPtr<AudioOutput> replacement = FindLastPluggedOutput();
      if (replacement) {
        if (routing_policy_ ==
            fuchsia::media::AudioOutputRoutingPolicy::LAST_PLUGGED_OUTPUT) {
          for (auto& audio_out : audio_outs_) {
            LinkOutputToAudioOut(replacement.get(), &audio_out);
          }
        }

        LinkToAudioIns(replacement);
      }
    } else {
      // Removed device was the most-recently-plugged input device. Determine
      // the new most-recently-plugged input (if any remain), and iterate our
      // AudioIn list to link each non-loopback AudioIn to the new default.
      FXL_DCHECK(device->is_input());

      fbl::RefPtr<AudioInput> replacement = FindLastPluggedInput();
      if (replacement) {
        LinkToAudioIns(replacement);
      }
    }
  }

  // If removed device was an output, recompute the AudioOut minimum lead time.
  if (device->is_output()) {
    for (auto& audio_out : audio_outs_) {
      audio_out.RecomputeMinClockLeadTime();
    }
  }
}

void AudioDeviceManager::OnDevicePlugged(const fbl::RefPtr<AudioDevice>& device,
                                         zx_time_t plug_time) {
  FXL_DCHECK(device);

  if (device->is_output()) {
    // This new device is an output. Inspect the AudioOut list; "do the right
    // thing" based on our routing policy. If last-plugged policy, change the
    // target for every AudioOut to this device (assuming it IS  most-recently-
    // plugged). If all-plugged policy, add this output to the list.
    //
    // Then, apply last-plugged policy to all AudioIns with loopback sources.
    // The policy mentioned above currently only pertains to Output Routing.
    fbl::RefPtr<AudioOutput> last_plugged = FindLastPluggedOutput();
    auto output = static_cast<AudioOutput*>(device.get());

    FXL_DCHECK(ValidateRoutingPolicy(routing_policy_));

    bool lp_policy =
        (routing_policy_ ==
         fuchsia::media::AudioOutputRoutingPolicy::LAST_PLUGGED_OUTPUT);
    bool is_lp = (output == last_plugged.get());

    if (is_lp && lp_policy) {
      for (auto& unlink_tgt : devices_) {
        if (unlink_tgt.is_output() && (&unlink_tgt != device.get())) {
          unlink_tgt.UnlinkSources();
        }
      }
    }
    if (is_lp || !lp_policy) {
      for (auto& audio_out : audio_outs_) {
        LinkOutputToAudioOut(output, &audio_out);

        // If we are adding a new link (regardless of whether we may or may
        // not have removed old links based on the specific active policy)
        // because of an output becoming plugged in, we need to recompute the
        // minimum clock lead time requirement, and perhaps update users as to
        // what it is supposed to be.
        //
        // TODO(johngro) : In theory, this could be optimized.  We don't
        // *technically* need to go over the entire set of links and find the
        // largest minimum lead time requirement if we know (for example) that
        // we just added a link, but didn't remove any.  Right now, we are
        // sticking to the simple approach because we know that N (the total
        // number of outputs an input is linked to) is small, and maintaining
        // optimized/specialized logic for computing this value would start to
        // become a real pain as we start to get more complicated in our
        // approach to policy based routing.
        audio_out.RecomputeMinClockLeadTime();
      }
    }

    // 'loopback' audio ins should listen to this output now
    if (is_lp) {
      LinkToAudioIns(device);
    }
  } else {
    FXL_DCHECK(device->is_input());

    fbl::RefPtr<AudioInput> last_plugged = FindLastPluggedInput();
    auto input = static_cast<AudioInput*>(device.get());

    // non-'loopback' audio ins should listen to this input now
    if (input == last_plugged.get()) {
      LinkToAudioIns(device);
    }
  }
}

// New device arrived and is the most-recently-plugged.
// *If device is an output, all 'loopback' audio ins should
// listen to this output going forward (default output).
// *If device is an input, then all NON-'loopback' audio ins
// should listen to this input going forward (default input).
void AudioDeviceManager::LinkToAudioIns(
    const fbl::RefPtr<AudioDevice>& device) {
  bool link_to_loopbacks = device->is_output();

  for (auto& audio_in : audio_ins_) {
    if (audio_in.loopback() == link_to_loopbacks) {
      audio_in.UnlinkSources();
      AudioObject::LinkObjects(device, fbl::WrapRefPtr(&audio_in));
    }
  }
}

void AudioDeviceManager::FinalizeDeviceSettings(const AudioDevice& device) {
  const auto& settings = device.device_settings();
  if ((settings == nullptr) || !settings->InContainer()) {
    return;
  }

  settings->Commit(true);
  persisted_device_settings_.erase(*settings);
}

void AudioDeviceManager::NotifyDeviceGainChanged(const AudioDevice& device) {
  ::fuchsia::media::AudioGainInfo info;
  FXL_DCHECK(device.device_settings() != nullptr);
  device.device_settings()->GetGainInfo(&info);

  for (auto& client : bindings_.bindings()) {
    client->events().OnDeviceGainChanged(device.token(), info);
  }
}

void AudioDeviceManager::UpdateDefaultDevice(bool input) {
  const auto new_dev = FindLastPlugged(input ? AudioObject::Type::Input
                                             : AudioObject::Type::Output);
  uint64_t new_id = new_dev ? new_dev->token() : ZX_KOID_INVALID;
  uint64_t& old_id = input ? default_input_token_ : default_output_token_;

  if (old_id != new_id) {
    for (auto& client : bindings_.bindings()) {
      client->events().OnDefaultDeviceChanged(old_id, new_id);
    }
    old_id = new_id;
  }
}

void AudioDeviceManager::UpdateDeviceToSystemGain(AudioDevice* device) {
  constexpr uint32_t set_flags = ::fuchsia::media::SetAudioGainFlag_GainValid |
                                 ::fuchsia::media::SetAudioGainFlag_MuteValid;
  ::fuchsia::media::AudioGainInfo set_cmd = {
      service_->system_gain_db(),
      service_->system_muted() ? ::fuchsia::media::AudioGainInfoFlag_Mute : 0u};

  FXL_DCHECK(device != nullptr);
  device->SetGainInfo(set_cmd, set_flags);
  CommitDirtySettings();
}

void AudioDeviceManager::CommitDirtySettings() {
  zx::time next = zx::time::infinite();

  for (auto& settings : persisted_device_settings_) {
    zx::time tmp = settings.Commit();
    if (tmp < next) {
      next = tmp;
    }
  }

  // If our commit task is waiting to fire, try to cancel it.
  if (commit_settings_task_.is_pending()) {
    commit_settings_task_.Cancel();
  }

  // If we need to try to update in the future, schedule a our commit task to do
  // so.
  if (next != zx::time::infinite()) {
    commit_settings_task_.PostForTime(service_->dispatcher(), next);
  }
}

}  // namespace audio
}  // namespace media
