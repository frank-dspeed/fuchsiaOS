// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_TESTING_SCENE_PROVIDER_SCENE_PROVIDER_H_
#define SRC_UI_TESTING_SCENE_PROVIDER_SCENE_PROVIDER_H_

#include <fuchsia/element/cpp/fidl.h>
#include <fuchsia/ui/observation/test/cpp/fidl.h>
#include <fuchsia/ui/test/scene/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include <memory>

namespace ui_testing {

class FakeViewController : public fuchsia::element::ViewController {
 public:
  explicit FakeViewController(
      fidl::InterfaceRequest<fuchsia::element::ViewController> view_controller) {
    view_controller_bindings_.AddBinding(this, std::move(view_controller));
  }
  ~FakeViewController() override = default;

  // TODO(fxbug.dev/106187): Detach client view in Dismiss().
  // |ViewController|
  void Dismiss() override {}

 private:
  fidl::BindingSet<fuchsia::element::ViewController> view_controller_bindings_;
};

class SceneProvider : public fuchsia::ui::test::scene::Controller,
                      public fuchsia::element::GraphicalPresenter {
 public:
  explicit SceneProvider(sys::ComponentContext* context) : context_(context) {}
  ~SceneProvider() override = default;

  // |fuchsia::ui::test::scene::Controller|
  void AttachClientView(fuchsia::ui::test::scene::ControllerAttachClientViewRequest request,
                        AttachClientViewCallback callback) override;

  // |fuchsia::ui::test::scene::Controller|
  void RegisterViewTreeWatcher(
      fidl::InterfaceRequest<fuchsia::ui::observation::geometry::ViewTreeWatcher> geometry_observer,
      RegisterViewTreeWatcherCallback callback) override;

  // |fuchsia::element::GraphicalPresenter|
  void PresentView(
      fuchsia::element::ViewSpec view_spec,
      fidl::InterfaceHandle<fuchsia::element::AnnotationController> annotation_controller,
      fidl::InterfaceRequest<fuchsia::element::ViewController> view_controller,
      PresentViewCallback callback) override;

  // Returns a scene controller interface request handler bound to this object.
  fidl::InterfaceRequestHandler<fuchsia::ui::test::scene::Controller> GetSceneControllerHandler();

  // Returns a graphical presenter interface request handler bound to this
  // object.
  fidl::InterfaceRequestHandler<fuchsia::element::GraphicalPresenter>
  GetGraphicalPresenterHandler();

 private:
  fidl::BindingSet<fuchsia::ui::test::scene::Controller> scene_controller_bindings_;
  fidl::BindingSet<fuchsia::element::GraphicalPresenter> graphical_presenter_bindings_;
  std::optional<FakeViewController> fake_view_controller_;
  fuchsia::element::AnnotationControllerPtr annotation_controller_;
  sys::ComponentContext* context_ = nullptr;
};

}  // namespace ui_testing

#endif  // SRC_UI_TESTING_SCENE_PROVIDER_SCENE_PROVIDER_H_
