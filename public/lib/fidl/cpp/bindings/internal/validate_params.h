// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS_INTERNAL_VALIDATE_PARAMS_H_
#define LIB_FIDL_CPP_BINDINGS_INTERNAL_VALIDATE_PARAMS_H_

namespace f1dl {
namespace internal {

class ArrayValidateParams {
 public:
  // ArrayValidateParams takes ownership of |in_element_validate params|.
  ArrayValidateParams(uint32_t in_expected_num_elements,
                      bool in_element_is_nullable,
                      ArrayValidateParams* in_element_validate_params)
      : expected_num_elements(in_expected_num_elements),
        element_is_nullable(in_element_is_nullable),
        element_validate_params(in_element_validate_params) {}

  ~ArrayValidateParams() {
    if (element_validate_params)
      delete element_validate_params;
  }

  ArrayValidateParams(const ArrayValidateParams&) = delete;
  ArrayValidateParams& operator=(const ArrayValidateParams&) = delete;

  // TODO(vtl): The members of this class shouldn't be public.

  // If |expected_num_elements| is not 0, the array is expected to have exactly
  // that number of elements.
  uint32_t expected_num_elements;

  // Whether the elements are nullable.
  bool element_is_nullable;

  // Validation information for elements. It is either a pointer to another
  // instance of ArrayValidateParams (if elements are arrays or maps), or
  // nullptr. In the case of maps, this is used to validate the value array.
  ArrayValidateParams* element_validate_params;
};

}  // namespace internal
}  // namespace f1dl

#endif  // LIB_FIDL_CPP_BINDINGS_INTERNAL_VALIDATE_PARAMS_H_
