// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_TESTS_MOCKS_MOCK_VIEW_CONTAINER_LISTENER_H_
#define LIB_UI_TESTS_MOCKS_MOCK_VIEW_CONTAINER_LISTENER_H_

#include <fuchsia/cpp/views_v1.h>
#include "lib/fxl/macros.h"

namespace mozart {
namespace test {

using OnMockChildAttachedCallback =
    std::function<void(uint32_t, views_v1::ViewInfo)>;
using OnMockChildUnavailable = std::function<void(uint32_t)>;

class MockViewContainerListener : public views_v1::ViewContainerListener {
 public:
  MockViewContainerListener();
  MockViewContainerListener(OnMockChildAttachedCallback child_attached_callback,
                            OnMockChildUnavailable child_unavailable_callback);
  ~MockViewContainerListener();

 private:
  void OnChildAttached(uint32_t child_key,
                       views_v1::ViewInfo child_view_info,
                       OnChildAttachedCallback callback) override;
  void OnChildUnavailable(uint32_t child_key,
                          OnChildUnavailableCallback callback) override;

  OnMockChildAttachedCallback child_attached_callback_;
  OnMockChildUnavailable child_unavailable_callback_;
  FXL_DISALLOW_COPY_AND_ASSIGN(MockViewContainerListener);
};

}  // namespace test
}  // namespace mozart

#endif  // LIB_UI_TESTS_MOCKS_MOCK_VIEW_CONTAINER_LISTENER_H_
