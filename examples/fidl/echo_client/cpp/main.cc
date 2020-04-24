// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/examples/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/service/cpp/service.h>
#include <lib/sys/service/cpp/service_aggregate.h>

#include <iostream>

fidl::InterfaceHandle<fuchsia::io::Directory> StartEchoServer(
    sys::ComponentContext* context,
    fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller) {
  fidl::InterfaceHandle<fuchsia::io::Directory> svc;
  fuchsia::sys::LaunchInfo info{
      .url = "fuchsia-pkg://fuchsia.com/echo_server#meta/echo_server.cmx",
      .directory_request = svc.NewRequest().TakeChannel(),
  };

  auto launcher = context->svc()->Connect<fuchsia::sys::Launcher>();
  launcher->CreateComponent(std::move(info), std::move(controller));
  return svc;
}

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  // Start the echo service.
  //
  // In a real system, the service would be offered to the client instead of
  // being started by the client.
  fuchsia::sys::ComponentControllerPtr controller;
  auto svc = StartEchoServer(context.get(), controller.NewRequest());

  // Example of connecting to a member of a service instance.
  auto default_service = sys::OpenServiceAt<fuchsia::examples::MyService>(svc);
  auto foo = default_service.foo().Connect().Bind();

  // Example of listing instances of a service.
  auto service_aggregate = sys::OpenServiceAggregateAt<fuchsia::examples::MyService>(svc);
  auto instance_names = service_aggregate.ListInstances();
  ZX_ASSERT(!instance_names.empty());
  auto service = sys::OpenServiceAt<fuchsia::examples::MyService>(svc, instance_names[0]);
  auto bar = service.bar().Connect().Bind();

  foo->EchoString("ping", [](fidl::StringPtr value) { std::cout << value << std::endl; });
  bar->EchoString("pong", [&loop](fidl::StringPtr value) {
    std::cout << value << std::endl;
    loop.Quit();
  });

  loop.Run();
  return 0;
}
