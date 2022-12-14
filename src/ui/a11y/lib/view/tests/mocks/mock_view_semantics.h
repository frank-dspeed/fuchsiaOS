// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_VIEW_TESTS_MOCKS_MOCK_VIEW_SEMANTICS_H_
#define SRC_UI_A11Y_LIB_VIEW_TESTS_MOCKS_MOCK_VIEW_SEMANTICS_H_

#include <lib/fidl/cpp/binding.h>

#include <map>
#include <memory>
#include <optional>

#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_tree.h"
#include "src/ui/a11y/lib/view/view_semantics.h"

namespace accessibility_test {

class MockViewSemantics : public a11y::ViewSemantics {
 public:
  MockViewSemantics();
  ~MockViewSemantics() override;

  // |ViewSemantics|
  void CloseChannel(zx_status_t status) override {}

  // |ViewSemantics|
  void EnableSemanticUpdates(bool enabled) override;

  // |ViewSemantics|
  fxl::WeakPtr<::a11y::SemanticTree> GetTree() override;

  bool semantics_enabled() const { return semantics_enabled_; }

  MockSemanticTree* mock_semantic_tree() { return &mock_semantic_tree_; }

 private:
  bool semantics_enabled_;
  MockSemanticTree mock_semantic_tree_;

  fxl::WeakPtrFactory<::a11y::SemanticTree> semantic_tree_factory_;
};

class MockViewSemanticsFactory : public a11y::ViewSemanticsFactory {
 public:
  MockViewSemanticsFactory() = default;
  ~MockViewSemanticsFactory() override = default;

  // |ViewSemanticsFactory|
  std::unique_ptr<a11y::ViewSemantics> CreateViewSemantics(
      std::unique_ptr<a11y::SemanticTreeService> tree_service_ptr,
      fidl::InterfaceRequest<fuchsia::accessibility::semantics::SemanticTree> semantic_tree_request)
      override;

  MockViewSemantics* GetViewSemantics();

 private:
  MockViewSemantics* view_semantics_;
};

}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_LIB_VIEW_TESTS_MOCKS_MOCK_VIEW_SEMANTICS_H_
