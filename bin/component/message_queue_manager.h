// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_SRC_COMPONENT_MESSAGE_QUEUE_MANAGER_H_
#define APPS_MODULAR_SRC_COMPONENT_MESSAGE_QUEUE_MANAGER_H_

#include <functional>
#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
#include <utility>

#include "apps/ledger/services/internal/internal.fidl.h"
#include "apps/modular/services/component/message_queue.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/string.h"
#include "lib/ftl/macros.h"

namespace modular {

class MessageQueueStorage;

// Manages message queues for components. One MessageQueueManager
// instance is used by all ComponentContextImpl instances, and manages
// the message queues for all component instances. The
// ComponentContext instance is responsible for deleting the message
// queues it has created, otherwise they are persisted.
class MessageQueueManager {
 public:
  MessageQueueManager(ledger::LedgerRepository* ledger_repository);
  ~MessageQueueManager();

  void ObtainMessageQueue(const std::string& component_instance_id,
                          const std::string& queue_name,
                          fidl::InterfaceRequest<MessageQueue> request);

  void DeleteMessageQueue(const std::string& component_instance_id,
                          const std::string& queue_name);

  void GetMessageSender(const std::string& queue_token,
                        fidl::InterfaceRequest<MessageSender> request);

  // Used by AgentRunner to look for new messages on queues which have
  // a trigger associated with it. If a queue corresponding to
  // |component_instance_id| x |queue_name| does not exist, a new one
  // is created.
  //
  // Registering a new watcher stomps over any existing watcher.
  void RegisterWatcher(const std::string& component_instance_id,
                       const std::string& queue_name,
                       const std::function<void()>& watcher);
  void DropWatcher(const std::string& component_instance_id,
                   const std::string& queue_name);

 private:
  // Generates a random string to use as a queue token.
  static std::string GenerateQueueToken();

  // Gets the component instance id and queue name from the ledger for
  // the given queue token.
  void GetComponentInstanceQueueName(
      const std::string& queue_token,
      std::function<void(ledger::Status status,
                         bool found,
                         const std::string& component_instance_id,
                         const std::string& queue_name)> callback);

  // Gets the queue token from the ledger for the given component
  // instance id and queue name.
  void GetQueueToken(
      const std::string& component_instance_id,
      const std::string& queue_name,
      std::function<void(ledger::Status status,
                         bool found,
                         const std::string& queue_token)> callback);

  // Returns the |MessageQueueStorage| for the queue_token. Creates it
  // if it doesn't exist yet.
  MessageQueueStorage* GetOrMakeMessageQueueStorage(
      const std::string& component_instance_id,
      const std::string& queue_name,
      const std::string& queue_token);

  // Returns the |MessageQueueStorage| or create one.
  void GetOrMakeMessageQueue(
      const std::string& component_instance_id,
      const std::string& queue_name,
      std::function<void(ledger::Status status, MessageQueueStorage* storage)>
          callback);

  ledger::LedgerPtr ledger_;
  ledger::PagePtr page_;

  // A map of queue_token to |MessageStorageQueue|.
  std::unordered_map<std::string, std::unique_ptr<MessageQueueStorage>>
      message_queues_;

  class StringPairHash {
   public:
    std::size_t operator()(const std::pair<std::string, std::string>& p) const;
  };

  using ComponentQueuePair = std::pair<std::string, std::string>;

  // A map of component instance id and queue name to queue tokens. Entries will
  // only be here while a |MessageQueueStorage| exists.
  std::unordered_map<ComponentQueuePair, std::string, StringPairHash>
      message_queue_tokens_;

  // A map of component instance id and queue name to watcher callbacks. If a
  // watcher is registered before a |MessageQueueStorage| exists then it is
  // stashed here until a |MessageQueueStorage| is available.
  std::unordered_map<ComponentQueuePair, ftl::Closure, StringPairHash>
      pending_watcher_callbacks_;

  FTL_DISALLOW_COPY_AND_ASSIGN(MessageQueueManager);
};

}  // namespace modular

#endif  // APPS_MODULAR_SRC_COMPONENT_MESSAGE_QUEUE_MANAGER_H_
