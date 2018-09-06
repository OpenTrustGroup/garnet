// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_COMPONENT_CPP_EXPOSED_OBJECT_H_
#define LIB_COMPONENT_CPP_EXPOSED_OBJECT_H_

#include "object_dir.h"

namespace component {

// |ExposedObject| is a base class that exposes an |ObjectDir| interface to
// derived classes. It automatically handles lifecycle management, namely
// removing itself from a parent |Object| at destruction time.
//
// This is the preferred mechanism to expose a long-lasting object hierarchy for
// inspection.
class ExposedObject {
 public:
  // Static method for generating unique names with a given prefix.
  // Every child object requires a unique name; if you don't necessarily care
  // about the names of child objects use this method to generate a unique one.
  // Example: UniqueName("table_entry") -> "table_entry:0xa".
  static std::string UniqueName(const std::string& prefix);

  // Constructs a new exposed object with the given name. Call this constructor
  // from derived class constructors.
  explicit ExposedObject(const std::string& name);
  virtual ~ExposedObject();

  // Explicitly set the parent of this object. This method handles removing the
  // object from its current parent, if any, and attaching it to the new parent.
  void set_parent(const ObjectDir* parent);

  // Gets the |ObjectDir| representation of this object.
  ObjectDir object_dir() { return object_dir_; }

  // Gets the |Object| this is wrapping.
  fbl::RefPtr<Object> object() { return object_dir_.object(); }

 protected:
  // Adds a child to this object.
  void add_child(ExposedObject* child);

 private:
  // Helper to remove this object from its parent.
  void remove_from_parent();

  // Helper to move this object to a different parent.
  // If new_parent is null, the object becomes parentless.
  void move_parents(const ObjectDir* new_parent);

  // The object's current parent, if any.
  ObjectDir parent_;

  // The object itself, accessible through object().
  const ObjectDir object_dir_;
};

}  // namespace component

#endif  // LIB_COMPONENT_CPP_EXPOSED_OBJECT_H_
