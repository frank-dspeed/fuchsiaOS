// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/lib/compat/service_offers.h"

#include <lib/driver2/node_add_args.h>
#include <lib/service/llcpp/service.h>

namespace compat {

namespace fcd = fuchsia_component_decl;

std::vector<fuchsia_component_decl::wire::Offer> ServiceOffersV1::CreateOffers(
    fidl::ArenaBase& arena) {
  std::vector<fuchsia_component_decl::wire::Offer> offers;
  for (const auto& service_name : offers_) {
    offers.push_back(driver::MakeOffer(arena, service_name, name_));
  }
  return offers;
}

zx_status_t ServiceOffersV1::Serve(async_dispatcher_t* dispatcher,
                                   component::OutgoingDirectory* outgoing) {
  // Add each service in the device as an service in our outgoing directory.
  // We rename each instance from "default" into the child name, and then rename it back to default
  // via the offer.
  for (const auto& service_name : offers_) {
    const auto instance_path = std::string("svc/").append(service_name).append("/default");
    auto client = service::ConnectAt<fuchsia_io::Directory>(dir_, instance_path.c_str());
    if (client.is_error()) {
      return client.status_value();
    }

    const auto path = std::string("svc/").append(service_name);
    auto result = outgoing->AddDirectoryAt(std::move(*client), path, name_);
    if (result.is_error()) {
      return result.error_value();
    }
    stop_serving_ = [this, outgoing, path]() { (void)outgoing->RemoveDirectoryAt(path, name_); };
  }
  return ZX_OK;
}

}  // namespace compat
