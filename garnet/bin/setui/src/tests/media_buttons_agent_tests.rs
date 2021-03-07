// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::media_buttons;
use crate::agent::Invocation;
use crate::agent::Lifespan;
use crate::agent::{Context, Payload};
use crate::event::{self, Event};
use crate::input::{ButtonType, VolumeGain};
use crate::message::base::{Audience, MessageEvent, MessengerType};
use crate::service;
use crate::service_context::ServiceContext;
use crate::tests::fakes::input_device_registry_service::InputDeviceRegistryService;
use crate::tests::fakes::service_registry::ServiceRegistry;
use fidl_fuchsia_ui_input::MediaButtonsEvent;
use futures::lock::Mutex;
use futures::StreamExt;
use media_buttons::MediaButtonsAgent;
use std::collections::HashSet;
use std::convert::TryFrom;
use std::sync::Arc;

struct FakeServices {
    input_device_registry: Arc<Mutex<InputDeviceRegistryService>>,
}

// Returns a registry and input related services with which it is populated.
async fn create_services() -> (Arc<Mutex<ServiceRegistry>>, FakeServices) {
    let service_registry = ServiceRegistry::create();

    let input_device_registry_service_handle =
        Arc::new(Mutex::new(InputDeviceRegistryService::new()));
    service_registry.lock().await.register_service(input_device_registry_service_handle.clone());

    (service_registry, FakeServices { input_device_registry: input_device_registry_service_handle })
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_media_buttons_proxied() {
    let service_hub = service::message::create_hub();
    // Create the agent receptor for use by the agent.
    let agent_receptor = service_hub
        .create(MessengerType::Unbound)
        .await
        .expect("Unable to create agent messenger")
        .1;
    let signature = agent_receptor.get_signature();

    // Create the messenger where we will send the invocations.
    let (agent_messenger, _) =
        service_hub.create(MessengerType::Unbound).await.expect("Unable to create agent messenger");
    // Create the receptor which will receive the broadcast events.
    let mut event_receptor = service::build_event_listener(&service_hub).await;

    // Create the agent context and agent.
    let context = Context::new(agent_receptor, service_hub, HashSet::new(), None).await;
    MediaButtonsAgent::create(context).await;

    // Setup the fake services.
    let (service_registry, fake_services) = create_services().await;
    let service_context =
        ServiceContext::create(Some(ServiceRegistry::serve(service_registry)), None);

    // Create and send the invocation with faked services.
    let invocation = Invocation { lifespan: Lifespan::Service, service_context };
    let mut reply_receptor = agent_messenger
        .message(Payload::Invocation(invocation).into(), Audience::Messenger(signature))
        .send();
    let mut completion_result = None;
    while let Some(event) = reply_receptor.next().await {
        if let MessageEvent::Message(payload, _) = event {
            if let Ok(Payload::Complete(result)) = Payload::try_from(payload) {
                completion_result = Some(result);
                break;
            }
        }
    }

    // Validate that the setup is complete.
    assert!(
        matches!(completion_result, Some(Ok(()))),
        "Did not receive a completion event from the invocation message"
    );

    // The agent should now be initialized. Send a media button event.
    fake_services.input_device_registry.lock().await.send_media_button_event(MediaButtonsEvent {
        volume: Some(1),
        mic_mute: Some(true),
        pause: None,
        camera_disable: None,
        ..MediaButtonsEvent::EMPTY
    });

    // Track the events to make sure they came in.
    let mut mic_mute_received = false;
    let mut volume_received = false;
    while let Ok((payload, _)) = event_receptor.next_payload().await {
        if let service::Payload::Event(event::Payload::Event(Event::MediaButtons(event))) = payload
        {
            match event {
                event::media_buttons::Event::OnButton(ButtonType::MicrophoneMute(true)) => {
                    mic_mute_received = true;
                    if volume_received {
                        break;
                    }
                }
                event::media_buttons::Event::OnVolume(VolumeGain::Up) => {
                    volume_received = true;
                    if mic_mute_received {
                        break;
                    }
                }
                _ => {}
            }
        }
    }

    // Validate that we received all expected events.
    assert!(mic_mute_received);
    assert!(volume_received);
}
