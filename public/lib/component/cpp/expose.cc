// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/service.h>
#include <lib/fxl/strings/string_printf.h>

#include "expose.h"

namespace component {

void Property::Set(std::string value) {
  use_callback_ = false;
  value_ = std::move(value);
}
void Property::Set(ValueCallback callback) {
  use_callback_ = true;
  callback_ = std::move(callback);
}

std::string Property::Get() const {
  if (use_callback_) {
    return callback_();
  }
  return value_;
}

void Metric::SetInt(int64_t value) {
  type_ = INT;
  int_value_ = value;
}

void Metric::SetUInt(uint64_t value) {
  type_ = UINT;
  uint_value_ = value;
}

void Metric::SetDouble(double value) {
  type_ = DOUBLE;
  double_value_ = value;
}

void Metric::SetCallback(ValueCallback callback) {
  type_ = CALLBACK;
  callback_ = std::move(callback);
}

std::string Metric::ToString() const {
  switch (type_) {
    case INT:
      return fxl::StringPrintf("%ld", int_value_);
    case UINT:
      return fxl::StringPrintf("%lu", uint_value_);
    case DOUBLE:
      return fxl::StringPrintf("%f", double_value_);
    case CALLBACK:
      Metric temp;
      callback_(&temp);
      return temp.ToString();
  }
}

fuchsia::inspect::Metric Metric::ToFidl(const char* name) const {
  fuchsia::inspect::Metric ret;
  switch (type_) {
    case INT:
      ret.value.set_int_value(int_value_);
      break;
    case UINT:
      ret.value.set_uint_value(uint_value_);
      break;
    case DOUBLE:
      ret.value.set_double_value(double_value_);
      break;
    case CALLBACK:
      Metric temp;
      callback_(&temp);
      return temp.ToFidl(name);
  }
  ret.key = name;
  return ret;
}

Metric IntMetric(int64_t value) {
  Metric ret;
  ret.SetInt(value);
  return ret;
}

Metric UIntMetric(uint64_t value) {
  Metric ret;
  ret.SetUInt(value);
  return ret;
}

Metric DoubleMetric(double value) {
  Metric ret;
  ret.SetDouble(value);
  return ret;
}

Metric CallbackMetric(Metric::ValueCallback callback) {
  Metric ret;
  ret.SetCallback(std::move(callback));
  return ret;
}

void Object::ReadData(ReadDataCallback callback) {
  fbl::AutoLock lock(&mutex_);
  callback(ToFidl());
}

void Object::ListChildren(ListChildrenCallback callback) {
  StringOutputVector out;
  PopulateChildVector(&out);
  callback(std::move(out));
}

void Object::OpenChild(::fidl::StringPtr name,
                       ::fidl::InterfaceRequest<Inspect> child_channel,
                       OpenChildCallback callback) {
  auto child = GetChild(name->data());
  if (!child) {
    callback(false);
    return;
  }

  fbl::AutoLock lock(&(child->mutex_));
  child->bindings_.AddBinding(child, std::move(child_channel));
  callback(true);
}

fbl::RefPtr<Object> Object::GetChild(fbl::String name) {
  fbl::AutoLock lock(&mutex_);
  auto it = children_.find(name.data());
  if (it == children_.end()) {
    return fbl::RefPtr<Object>();
  }
  return it->second;
}

void Object::SetChild(fbl::RefPtr<Object> child) {
  fbl::AutoLock lock(&mutex_);
  auto it = children_.find(child->name().data());
  if (it != children_.end()) {
    it->second.swap(child);
  } else {
    children_.insert(std::make_pair(child->name().data(), child));
  }
}

fbl::RefPtr<Object> Object::TakeChild(fbl::String name) {
  fbl::AutoLock lock(&mutex_);
  auto it = children_.find(name.data());
  if (it == children_.end()) {
    return fbl::RefPtr<Object>();
  }
  auto ret = it->second;
  children_.erase(it);
  return ret;
}

void Object::SetChildrenCallback(ChildrenCallback callback) {
  fbl::AutoLock lock(&mutex_);
  lazy_object_callback_ = std::move(callback);
}

void Object::ClearChildrenCallback() {
  fbl::AutoLock lock(&mutex_);
  ChildrenCallback temp;
  lazy_object_callback_.swap(temp);
}

void Object::SetProperty(const std::string& name, Property value) {
  fbl::AutoLock lock(&mutex_);
  properties_[name] = std::move(value);
}

void Object::SetMetric(const std::string& name, Metric metric) {
  fbl::AutoLock lock(&mutex_);
  metrics_[name] = std::move(metric);
}

void Object::GetContents(LazyEntryVector* out_vector) {
  fbl::AutoLock lock(&mutex_);
  out_vector->push_back({kChanId, ".channel", V_TYPE_FILE});
  out_vector->push_back({kDataId, ".data", V_TYPE_FILE});
  uint64_t index = kSpecialIdMax;

  // Each child gets a unique ID, which is just its index in the directory.
  // If the set of children changes between successive calls to readdir(3), it
  // is possible we will miss children.
  //
  // This behavior is documented at the declaration point.
  for (const auto& it : children_) {
    out_vector->push_back({index++, it.second->name_, V_TYPE_DIR});
  }
  if (lazy_object_callback_) {
    ObjectVector lazy_objects;
    lazy_object_callback_(&lazy_objects);
    for (const auto& obj : lazy_objects) {
      out_vector->push_back({index++, obj->name(), V_TYPE_DIR});
    }
  }
}

zx_status_t Object::GetFile(fbl::RefPtr<Vnode>* out_vnode, uint64_t id,
                            fbl::String name) {
  fbl::AutoLock lock(&mutex_);
  if (id == kChanId) {
    // `.channel` is a Service file that binds incoming channels to this
    // Inspect implementation.
    auto ref = fbl::WrapRefPtr(this);
    *out_vnode = fbl::MakeRefCounted<fs::Service>([ref](zx::channel chan) {
      fbl::AutoLock lock(&ref->mutex_);
      ref->bindings_.AddBinding(
          ref, fidl::InterfaceRequest<Inspect>(std::move(chan)));
      return ZX_OK;
    });
    return ZX_OK;
  } else if (id == kDataId) {
    // `.data` is a pseudofile that simply dumps out the properties and
    // metrics for this object in a TAB-separated format. This is just
    // for debugging.
    auto ref = fbl::WrapRefPtr(this);
    *out_vnode =
        fbl::MakeRefCounted<fs::BufferedPseudoFile>([ref](fbl::String* out) {
          std::string output;
          {
            fbl::AutoLock lock(&ref->mutex_);
            for (const auto& prop : ref->properties_) {
              // Lines starting with "P" are Properties.
              fxl::StringAppendf(&output, "P\t%s\t%s\n", prop.first.c_str(),
                                 prop.second.Get().c_str());
            }
            for (const auto& metric : ref->metrics_) {
              // Lines starting with "M" are Metrics.
              fxl::StringAppendf(&output, "M\t%s\t%s\n", metric.first.c_str(),
                                 metric.second.ToString().c_str());
            }
          }
          *out = output;
          return ZX_OK;
        });
    return ZX_OK;
  }

  // If the file isn't a special file, search for the name as a child object.
  auto it = children_.find(name.c_str());
  if (it == children_.end()) {
    // If the named child is not found, search through the lazy objects if the
    // callback is set.
    if (lazy_object_callback_) {
      ObjectVector lazy_objects;
      lazy_object_callback_(&lazy_objects);
      for (const auto& obj : lazy_objects) {
        if (obj->name() == name.c_str()) {
          *out_vnode = obj;
          return ZX_OK;
        }
      }
    }
    return ZX_ERR_NOT_FOUND;
  }
  *out_vnode = it->second;
  return ZX_OK;
}

void Object::PopulateChildVector(StringOutputVector* out_vector)
    __TA_EXCLUDES(mutex_) {
  // Lock the local child vector. No need to lock children since we are only
  // reading their constant name.
  fbl::AutoLock lock(&mutex_);
  for (const auto& it : children_) {
    out_vector->push_back(it.second->name().data());
  }
  if (lazy_object_callback_) {
    ObjectVector lazy_objects;
    lazy_object_callback_(&lazy_objects);
    for (const auto& obj : lazy_objects) {
      out_vector->push_back(obj->name().data());
    }
  }
}

fuchsia::inspect::Object Object::ToFidl() __TA_REQUIRES(mutex_) {
  fuchsia::inspect::Object ret;
  ret.name = name_.data();
  for (const auto& it : properties_) {
    ret.properties.push_back({it.first, it.second.Get()});
  }
  for (const auto& it : metrics_) {
    ret.metrics.push_back(it.second.ToFidl(it.first.c_str()));
  }
  return ret;
}

}  // namespace component
