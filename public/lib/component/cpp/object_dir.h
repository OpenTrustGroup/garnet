// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_COMPONENT_CPP_OBJECT_DIR_H_
#define LIB_COMPONENT_CPP_OBJECT_DIR_H_

#include <fbl/atomic.h>

#include "expose.h"

namespace component {

// An |ObjectPath| describes a specific path between children objects, which may
// be defined statically within a file to impose type-safety for find()
// operations.
//
// Example:
// const ObjectPath CONTENTS = {"container", "child", "contents"};
// obj->find(CONTENTS);
using ObjectPath = std::initializer_list<const char*>;

// |ObjectDir| is a wrapper class around the raw |Object| class.
// While |Object| deals with the individual properties, metrics, and children
// related to a single |Object|, |ObjectDir| provides a lightweight wrapper
// around the |Object| interface to support higher-level operations, including:
// * Tree traversal
// * Property setters
// * Metric setters
// * Children setters/getters
//
// This class is thread-safe; it simply wraps a single |Object|, which is itself
// thread-safe.
class ObjectDir {
 public:
  // Constructs an empty |ObjectDir|, satisfying !!(*this) == false.
  ObjectDir();

  // Constructs an |ObjectDir| wrapping the given |Object|.
  explicit ObjectDir(fbl::RefPtr<Object> object);

  // Construct a new |ObjectDir| wrapping the given raw object pointer.
  static ObjectDir Wrap(Object* object) {
    return ObjectDir(fbl::WrapRefPtr(object));
  }

  // Construct a new |ObjectDir| wrapping a new Object with the given name.
  static ObjectDir Make(std::string name) {
    return ObjectDir(fbl::AdoptRef(new Object(std::move(name))));
  }

  // The boolean value of an |ObjectDir| is true if and only if the wrapped
  // object reference is not empty.
  operator bool() const { return object_.get(); }

  // Obtains a reference to the wrapped |Object|.
  fbl::RefPtr<Object> object() const { return object_; }

  fbl::String name() const { return (*this) ? object_->name() : ""; }

  // Finds a child |Object| by path, and wraps it in an |ObjectDir|.
  // If |initialize| is true, this method initialized all objects along the path
  // that do not exist. Otherwise, it returns an empty |ObjectDir| if any
  // |Object| along the path does not exist.
  ObjectDir find(ObjectPath path, bool initialize = true) const;

  // Sets a property on this object to the given value.
  void set_prop(std::string name, const std::string& value) const {
    inner_set_prop({}, std::move(name), Property(value.c_str()));
  }

  // Sets a property on the child specified by path to the given value.
  // All objects along the path that do not exist will be initialized.
  void set_prop(ObjectPath path, std::string name,
                const std::string& value) const {
    inner_set_prop(std::move(path), std::move(name), Property(value.c_str()));
  }

  // Sets a property on this object to use the given callback for its value.
  void set_prop(std::string name, Property::ValueCallback callback) const {
    inner_set_prop({}, std::move(name), Property(std::move(callback)));
  }

  // Sets a property on the child specified by path to use the given callback
  // for its value.
  // All objects along the path that do not exist will be initialized.
  void set_prop(ObjectPath path, std::string name,
                Property::ValueCallback callback) const {
    inner_set_prop(std::move(path), std::move(name),
                   Property(std::move(callback)));
  }

  // Sets a metric on this object to the given value.
  void set_metric(std::string name, Metric metric) const {
    set_metric({}, std::move(name), std::move(metric));
  }

  // Sets a metric on the child specified by path to use the given value.
  // All objects along the path that do not exist will be initialized.
  void set_metric(ObjectPath path, std::string name, Metric metric) const;

  // Adds to a metric on this object.
  template <typename T>
  void add_metric(std::string name, T amount) const {
    add_metric({}, std::move(name), amount);
  }

  // Subtracts from a metric on this object.
  template <typename T>
  void sub_metric(std::string name, T amount) const {
    sub_metric({}, std::move(name), amount);
  }

  // Adds to a metric on a child specified by path.
  // All objects along the path that do not exist will be initialized.
  template <typename T>
  void add_metric(ObjectPath path, std::string name, T amount) const {
    find(path).object()->AddMetric(name, amount);
  }

  // Subtracts from a metric on a child specified by path.
  // All objects along the path that do not exist will be initialized.
  template <typename T>
  void sub_metric(ObjectPath path, std::string name, T amount) const {
    find(path).object()->SubMetric(name, amount);
  }

  // Sets a child on this object to the given object.
  void set_child(fbl::RefPtr<Object> obj) const {
    set_child({}, std::move(obj));
  }

  // Sets a child on the child specified by path to the given object.
  // All objects along the path that do not exist will be initialized.
  void set_child(ObjectPath path, fbl::RefPtr<Object> obj) const;

  // Sets the dynamic child callback on this object.
  void set_children_callback(Object::ChildrenCallback callback) const {
    set_children_callback({}, std::move(callback));
  }

  // Sets the dynamic child callback on the child specified by path.
  // All objects along the path that do not exist will be initialized.
  void set_children_callback(ObjectPath path,
                             Object::ChildrenCallback callback) const;

 private:
  // Inner implementation of setting properties by path.
  void inner_set_prop(ObjectPath path, std::string name,
                      Property property) const;

  // The wrapper object.
  fbl::RefPtr<Object> object_;
};

}  // namespace component

#endif  // LIB_COMPONENT_CPP_OBJECT_DIR_H_
