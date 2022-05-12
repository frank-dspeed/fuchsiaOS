// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_AGENTS_MDNS_AGENT_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_AGENTS_MDNS_AGENT_H_

#include <lib/fit/function.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <memory>

#include "src/connectivity/network/mdns/service/common/reply_address.h"
#include "src/connectivity/network/mdns/service/encoding/dns_message.h"
#include "src/lib/inet/socket_address.h"

namespace mdns {

// Base class for objects that drive mDNS question and record traffic.
//
// Agents that have been 'started' receive all inbound questions and resource records via their
// |ReceiveQuestion| and |ReceiveResource| methods. When the agent owner receives an inbound
// message, it calls those methods for each question and resource in the message. When that's
// done, the owner calls |EndOfMessage| on each agent.
//
// Agents may call any of the protected 'Send' methods (|SendQuestion|, |SendResource| and
// |SendAddresses|) at any time. The owner accumulates the questions and resources and sends
// them in messages. Typically, agents don't have to worry about sending messages. Messages
// are sent for accumulated questions and resources:
//
// 1) after |Start| is called on any agent,
// 2) after an inbound message is processed and all agents have gotten their |EndOfMessage| calls,
// 3) after an agent is removed (in case it wants to say goodbye),
// 4) after the completion of any task posted using |PostTaskForTime|.
//
// If an agent wants a message sent asynchronously with respect to agent start, inbound message
// arrival, agent removal and posted tasks, the agent should call |FlushSentItems|. Calling
// |FlushSentItems| synchronously with those operations isn't harmful.
//
class MdnsAgent : public std::enable_shared_from_this<MdnsAgent> {
 public:
  class Owner {
   public:
    virtual ~Owner() = default;

    // Gets the current time.
    virtual zx::time now() = 0;

    // Posts a task to be executed at the specified time. Scheduled tasks posted
    // by agents that have since been removed are not executed.
    virtual void PostTaskForTime(MdnsAgent* agent, fit::closure task, zx::time target_time) = 0;

    // Sends a question to the multicast address.
    virtual void SendQuestion(std::shared_ptr<DnsQuestion> question,
                              ReplyAddress reply_address) = 0;

    // Sends a resource to the specified address. The default |reply_address|
    // |kV4MulticastReply| sends the resource to the V4 or V6
    // multicast address.
    virtual void SendResource(std::shared_ptr<DnsResource> resource, MdnsResourceSection section,
                              const ReplyAddress& reply_address) = 0;

    // Sends address resources to the specified address. The default
    // |reply_address| |kV4MulticastReply| sends the addresses to the V4 or V6
    // multicast address.
    virtual void SendAddresses(MdnsResourceSection section, const ReplyAddress& reply_address) = 0;

    // Registers the resource for renewal. See |MdnsAgent::Renew| below.
    virtual void Renew(const DnsResource& resource, Media media, IpVersions ip_versions) = 0;

    // Removes the specified agent.
    virtual void RemoveAgent(std::shared_ptr<MdnsAgent> agent) = 0;

    // Flushes sent questions and resources by sending the appropriate messages.
    virtual void FlushSentItems() = 0;
  };

  virtual ~MdnsAgent() {}

  // Starts the agent. This method is never called before a shared pointer to
  // the agent is created, so |shared_from_this| is safe to call.
  // Specializations should call this method first.
  virtual void Start(const std::string& local_host_full_name) { started_ = true; }

  // Presents a received question. |sender_address| identifies the sender of the question. If
  // the question is not marked for unicast response, |reply_address| is the multicast placeholder
  // address with |Media::kBoth| and |IpVersions::kBoth|. If the question is marked for unicast
  // response, |reply_address| is the same as |sender_address|. This agent must not call
  // |RemoveSelf| during a call to this method.
  virtual void ReceiveQuestion(const DnsQuestion& question, const ReplyAddress& reply_address,
                               const ReplyAddress& sender_address) {}

  // Presents a received resource. |sender_address| identifies the sender of the resource. This
  // agent must not call |RemoveSelf| during a call to this method.
  virtual void ReceiveResource(const DnsResource& resource, MdnsResourceSection section,
                               ReplyAddress sender_address) {}

  // Signals the end of a message. This agent must not call |RemoveSelf| during
  // a call to this method.
  virtual void EndOfMessage() {}

  // Tells the agent to quit. Any overrides should call this base implementation.
  virtual void Quit() {
    RemoveSelf();
    if (on_quit_) {
      on_quit_();
      on_quit_ = nullptr;
    }
  }

  // Sets the 'on quit' callback that's called when the agent quits. May be called once at most
  // for a given agent.
  void SetOnQuitCallback(fit::closure on_quit) {
    FX_DCHECK(on_quit);
    FX_DCHECK(!on_quit_);
    on_quit_ = std::move(on_quit);
  }

 protected:
  explicit MdnsAgent(Owner* owner) : owner_(owner) { FX_DCHECK(owner_); }

  bool started() const { return started_; }

  // Gets the current time.
  zx::time now() { return owner_->now(); }

  // Posts a task to be executed at the specified time. Scheduled tasks posted
  // by agents that have since been removed are not executed.
  void PostTaskForTime(fit::closure task, zx::time target_time) {
    owner_->PostTaskForTime(this, std::move(task), target_time);
  }

  // Sends a question to the specified address.
  void SendQuestion(std::shared_ptr<DnsQuestion> question,
                    const ReplyAddress& reply_address) const {
    owner_->SendQuestion(std::move(question), reply_address);
  }

  // Sends a resource to the specified address.
  void SendResource(std::shared_ptr<DnsResource> resource, MdnsResourceSection section,
                    const ReplyAddress& reply_address) const {
    owner_->SendResource(std::move(resource), section, reply_address);
  }

  // Sends address resources to the specified address.
  void SendAddresses(MdnsResourceSection section, const ReplyAddress& reply_address) const {
    owner_->SendAddresses(section, reply_address);
  }

  // Flushes sent questions and resources by sending the appropriate messages. This method is only
  // needed when questions or resources need to be sent asynchronously with respect to |Start|,
  // |ReceiveQuestion|, |ReceiveResource|, |EndOfMessage|, |Quit| or a task posted using
  // |PostTaskForTime|. See the discussion at the top of the file.
  void FlushSentItems() { owner_->FlushSentItems(); }

  // Registers the resource for renewal. Before the resource's TTL expires,
  // an attempt will be made to renew the resource by issuing queries for it.
  // If the renewal is successful, the agent will receive the renewed resource
  // (via |ReceiveResource|) and may choose to renew the resource again.
  // If the renewal fails, the agent will receive a resource record with the
  // same name and type but with a TTL of zero. The section parameter
  // accompanying that resource record will be kExpired.
  //
  // The effect of this call is transient, and there is no way to cancel the
  // renewal. When an agent loses interest in a particular resource, it should
  // simply refrain from renewing the incoming records.
  void Renew(const DnsResource& resource, Media media, IpVersions ip_versions) const {
    owner_->Renew(resource, media, ip_versions);
  }

  // Removes this agent.
  void RemoveSelf() { owner_->RemoveAgent(shared_from_this()); }

 private:
  Owner* owner_;
  bool started_ = false;
  fit::closure on_quit_;
};

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_AGENTS_MDNS_AGENT_H_
