// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "low_energy_address_manager.h"

#include <lib/async/default.h>

#include "gap.h"
#include "src/connectivity/bluetooth/core/bt-host/common/log.h"
#include "src/connectivity/bluetooth/core/bt-host/sm/util.h"
#include "src/connectivity/bluetooth/core/bt-host/transport/transport.h"

namespace bt::gap {

LowEnergyAddressManager::LowEnergyAddressManager(const DeviceAddress& public_address,
                                                 StateQueryDelegate delegate,
                                                 fxl::WeakPtr<hci::Transport> hci)
    : delegate_(std::move(delegate)),
      hci_(std::move(hci)),
      privacy_enabled_(false),
      public_(public_address),
      needs_refresh_(false),
      refreshing_(false),
      weak_ptr_factory_(this) {
  BT_DEBUG_ASSERT(public_.type() == DeviceAddress::Type::kLEPublic);
  BT_DEBUG_ASSERT(delegate_);
  BT_DEBUG_ASSERT(hci_);
}

LowEnergyAddressManager::~LowEnergyAddressManager() { CancelExpiry(); }

void LowEnergyAddressManager::EnablePrivacy(bool enabled) {
  if (enabled == privacy_enabled_) {
    bt_log(DEBUG, "gap-le", "privacy already %s", (enabled ? "enabled" : "disabled"));
    return;
  }

  privacy_enabled_ = enabled;

  if (!enabled) {
    CleanUpPrivacyState();
    ResolveAddressRequests();
    return;
  }

  needs_refresh_ = true;

  TryRefreshRandomAddress();
}

void LowEnergyAddressManager::EnsureLocalAddress(AddressCallback callback) {
  BT_DEBUG_ASSERT(callback);

  // Report the address right away if it doesn't need refreshing.
  if (!needs_refresh_) {
    callback(current_address());
    return;
  }

  address_callbacks_.push(std::move(callback));
  TryRefreshRandomAddress();
}

void LowEnergyAddressManager::TryRefreshRandomAddress() {
  if (!privacy_enabled_ || !needs_refresh_) {
    bt_log(DEBUG, "gap-le", "address does not need refresh");
    return;
  }

  if (refreshing_) {
    bt_log(DEBUG, "gap-le", "address update in progress");
    return;
  }

  if (!CanUpdateRandomAddress()) {
    bt_log(DEBUG, "gap-le", "deferring local address refresh due to ongoing procedures");
    // Don't stall procedures that requested the current address while in this
    // state.
    ResolveAddressRequests();
    return;
  }

  CancelExpiry();
  refreshing_ = true;

  DeviceAddress random_addr;
  if (irk_) {
    random_addr = sm::util::GenerateRpa(*irk_);
  } else {
    random_addr = sm::util::GenerateRandomAddress(/*is_static=*/false);
  }

  auto cmd = hci::CommandPacket::New(hci_spec::kLESetRandomAddress,
                                     sizeof(hci_spec::LESetRandomAddressCommandParams));
  auto params = cmd->mutable_payload<hci_spec::LESetRandomAddressCommandParams>();
  params->random_address = random_addr.value();

  auto self = weak_ptr_factory_.GetWeakPtr();
  auto cmd_complete_cb = [self, this, random_addr](auto id, const hci::EventPacket& event) {
    if (!self) {
      return;
    }

    refreshing_ = false;

    if (!privacy_enabled_) {
      bt_log(DEBUG, "gap-le", "ignore random address result while privacy is disabled");
      return;
    }

    if (!hci_is_error(event, TRACE, "gap-le", "failed to update random address")) {
      needs_refresh_ = false;
      random_ = random_addr;
      bt_log(INFO, "gap-le", "random address updated: %s", bt_str(*random_));

      // Set the new random address to expire in kPrivateAddressTimeout.
      random_address_expiry_task_.set_handler([this](auto*, auto*, zx_status_t status) {
        if (status == ZX_OK) {
          needs_refresh_ = true;
          TryRefreshRandomAddress();
        }
      });
      random_address_expiry_task_.PostDelayed(async_get_default_dispatcher(),
                                              kPrivateAddressTimeout);
    }

    ResolveAddressRequests();
  };

  hci_->command_channel()->SendCommand(std::move(cmd), std::move(cmd_complete_cb));
}

void LowEnergyAddressManager::CleanUpPrivacyState() {
  privacy_enabled_ = false;
  needs_refresh_ = false;
  CancelExpiry();
}

void LowEnergyAddressManager::CancelExpiry() { random_address_expiry_task_.Cancel(); }

bool LowEnergyAddressManager::CanUpdateRandomAddress() const {
  BT_DEBUG_ASSERT(delegate_);
  return delegate_();
}

void LowEnergyAddressManager::ResolveAddressRequests() {
  auto q = std::move(address_callbacks_);
  bt_log(DEBUG, "gap-le", "using local address %s", current_address().ToString().c_str());
  while (!q.empty()) {
    q.front()(current_address());
    q.pop();
  }
}

}  // namespace bt::gap
