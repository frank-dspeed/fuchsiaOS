// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::internal::core::{message::create_hub, Address, Payload};
use crate::internal::handler;
use crate::message::base::{Audience, MessageEvent, MessengerType};
use crate::message::receptor::Receptor as BaseReceptor;
use crate::registry::base::{
    Command, SettingHandlerFactory, SettingHandlerFactoryError, SettingHandlerResult, State,
};
use crate::registry::registry_impl::RegistryImpl;
use crate::registry::setting_handler::ControllerError;
use crate::switchboard::base::{
    SettingAction, SettingActionData, SettingEvent, SettingRequest, SettingType,
};

use async_trait::async_trait;
use fuchsia_async as fasync;
use futures::channel::mpsc::UnboundedSender;
use futures::channel::oneshot;
use futures::lock::Mutex;
use futures::StreamExt;
use std::collections::HashMap;
use std::sync::Arc;

pub type SwitchboardReceptor = BaseReceptor<Payload, Address>;

struct SettingHandler {
    setting_type: SettingType,
    messenger: handler::message::Messenger,
    state_tx: UnboundedSender<State>,
    next_response: Option<(SettingRequest, SettingHandlerResult)>,
    done_tx: Option<oneshot::Sender<()>>,
    proxy_signature: handler::message::Signature,
}

impl SettingHandler {
    fn process_state(&mut self, state: State) -> SettingHandlerResult {
        self.state_tx.unbounded_send(state).ok();
        Ok(None)
    }

    pub fn set_next_response(&mut self, request: SettingRequest, response: SettingHandlerResult) {
        self.next_response = Some((request, response));
    }

    pub fn notify(&self) {
        self.messenger
            .message(
                handler::Payload::Changed(self.setting_type),
                Audience::Messenger(self.proxy_signature),
            )
            .send()
            .ack();
    }

    fn process_request(&mut self, request: SettingRequest) -> SettingHandlerResult {
        if let Some((match_request, result)) = self.next_response.take() {
            if request == match_request {
                return result;
            }
        }

        Err(ControllerError::UnimplementedRequest(self.setting_type, request))
    }

    fn create(
        messenger: handler::message::Messenger,
        mut receptor: handler::message::Receptor,
        proxy_signature: handler::message::Signature,
        setting_type: SettingType,
        state_tx: UnboundedSender<State>,
        done_tx: Option<oneshot::Sender<()>>,
    ) -> Arc<Mutex<Self>> {
        let handler = Arc::new(Mutex::new(Self {
            messenger,
            setting_type,
            state_tx,
            next_response: None,
            done_tx,
            proxy_signature,
        }));

        let handler_clone = handler.clone();
        fasync::Task::spawn(async move {
            while let Some(event) = receptor.next().await {
                match event {
                    MessageEvent::Message(
                        handler::Payload::Command(Command::HandleRequest(request)),
                        client,
                    ) => {
                        handler::reply(client, handler_clone.lock().await.process_request(request));
                    }
                    MessageEvent::Message(
                        handler::Payload::Command(Command::ChangeState(state)),
                        client,
                    ) => {
                        handler::reply(client, handler_clone.lock().await.process_state(state));
                    }
                    _ => {}
                }
            }

            if let Some(done_tx) = handler_clone.lock().await.done_tx.take() {
                done_tx.send(()).ok();
            }
        })
        .detach();

        handler
    }
}

struct FakeFactory {
    handlers: HashMap<SettingType, handler::message::Signature>,
    request_counts: HashMap<SettingType, u64>,
    messenger_factory: handler::message::Factory,
}

impl FakeFactory {
    pub fn new(messenger_factory: handler::message::Factory) -> Self {
        FakeFactory {
            handlers: HashMap::new(),
            request_counts: HashMap::new(),
            messenger_factory: messenger_factory,
        }
    }

    pub async fn create(
        &mut self,
        setting_type: SettingType,
    ) -> (handler::message::Messenger, handler::message::Receptor) {
        let (client, receptor) =
            self.messenger_factory.create(MessengerType::Unbound).await.unwrap();
        self.handlers.insert(setting_type, client.get_signature());

        (client, receptor)
    }

    pub fn get_request_count(&mut self, setting_type: SettingType) -> u64 {
        if let Some(count) = self.request_counts.get(&setting_type) {
            *count
        } else {
            0
        }
    }
}

#[async_trait]
impl SettingHandlerFactory for FakeFactory {
    async fn generate(
        &mut self,
        setting_type: SettingType,
        _: handler::message::Factory,
        _: handler::message::Signature,
    ) -> Result<handler::message::Signature, SettingHandlerFactoryError> {
        let existing_count = self.get_request_count(setting_type);

        Ok(self
            .handlers
            .get(&setting_type)
            .copied()
            .map(|signature| {
                self.request_counts.insert(setting_type, existing_count + 1);
                signature
            })
            .unwrap())
    }
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_notify() {
    let messenger_factory = create_hub();
    let handler_messenger_factory = handler::message::create_hub();

    let handler_factory = Arc::new(Mutex::new(FakeFactory::new(handler_messenger_factory.clone())));

    let (registry_signature, proxy_handler_signature) = RegistryImpl::create(
        SettingType::Unknown,
        handler_factory.clone(),
        messenger_factory.clone(),
        handler_messenger_factory,
    )
    .await
    .expect("registry creation should succeed");
    let setting_type = SettingType::Unknown;
    let (messenger_client, mut receptor) =
        messenger_factory.create(MessengerType::Addressable(Address::Switchboard)).await.unwrap();

    let (handler_messenger, handler_receptor) =
        handler_factory.lock().await.create(setting_type).await;
    let (state_tx, mut state_rx) = futures::channel::mpsc::unbounded::<State>();
    let handler = SettingHandler::create(
        handler_messenger,
        handler_receptor,
        proxy_handler_signature,
        setting_type,
        state_tx,
        None,
    );

    // Send a listen state and make sure sink is notified.
    {
        assert!(messenger_client
            .message(
                Payload::Action(SettingAction {
                    id: 1,
                    setting_type: setting_type,
                    data: SettingActionData::Listen(1),
                }),
                Audience::Messenger(registry_signature),
            )
            .send()
            .wait_for_acknowledge()
            .await
            .is_ok());

        handler.lock().await.notify();

        while let Some(event) = receptor.next().await {
            if let MessageEvent::Message(Payload::Event(SettingEvent::Changed(changed_type)), _) =
                event
            {
                assert_eq!(changed_type, setting_type);
                break;
            }
        }
    }

    if let Some(state) = state_rx.next().await {
        assert_eq!(state, State::Listen);
    } else {
        panic!("should have received state update");
    }

    // Send an end listen state and make sure sink is notified.
    {
        messenger_client
            .message(
                Payload::Action(SettingAction {
                    id: 1,
                    setting_type: setting_type,
                    data: SettingActionData::Listen(0),
                }),
                Audience::Messenger(registry_signature),
            )
            .send()
            .ack();
    }

    if let Some(state) = state_rx.next().await {
        assert_eq!(state, State::EndListen);
    } else {
        panic!("should have received EndListen state update");
    }

    if let Some(state) = state_rx.next().await {
        assert_eq!(state, State::Teardown);
    } else {
        panic!("should have received Teardown state update");
    }
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_request() {
    let messenger_factory = create_hub();
    let handler_messenger_factory = handler::message::create_hub();
    let handler_factory = Arc::new(Mutex::new(FakeFactory::new(handler_messenger_factory.clone())));

    let (registry_signature, proxy_handler_signature) = RegistryImpl::create(
        SettingType::Unknown,
        handler_factory.clone(),
        messenger_factory.clone(),
        handler_messenger_factory,
    )
    .await
    .expect("registry should be created successfully");
    let setting_type = SettingType::Unknown;
    let (messenger_client, _) =
        messenger_factory.create(MessengerType::Addressable(Address::Switchboard)).await.unwrap();

    let (handler_messenger, handler_receptor) =
        handler_factory.lock().await.create(setting_type).await;
    let (state_tx, _) = futures::channel::mpsc::unbounded::<State>();
    let handler = SettingHandler::create(
        handler_messenger,
        handler_receptor,
        proxy_handler_signature,
        setting_type,
        state_tx,
        None,
    );
    let request_id = 42;

    handler.lock().await.set_next_response(SettingRequest::Get, Ok(None));

    // Send initial request.
    let mut receptor = messenger_client
        .message(
            Payload::Action(SettingAction {
                id: request_id,
                setting_type: setting_type,
                data: SettingActionData::Request(SettingRequest::Get),
            }),
            Audience::Messenger(registry_signature),
        )
        .send();

    while let Some(event) = receptor.next().await {
        if let MessageEvent::Message(
            Payload::Event(SettingEvent::Response(response_id, response)),
            _,
        ) = event
        {
            assert_eq!(request_id, response_id);
            assert!(response.is_ok());
            assert_eq!(None, response.unwrap());
            return;
        }
    }
}

/// Ensures setting handler is only generated once if never torn down.
#[fuchsia_async::run_until_stalled(test)]
async fn test_generation() {
    let messenger_factory = create_hub();
    let handler_messenger_factory = handler::message::create_hub();
    let handler_factory = Arc::new(Mutex::new(FakeFactory::new(handler_messenger_factory.clone())));

    let (messenger_client, _) =
        messenger_factory.create(MessengerType::Addressable(Address::Switchboard)).await.unwrap();
    let (registry_signature, proxy_handler_signature) = RegistryImpl::create(
        SettingType::Unknown,
        handler_factory.clone(),
        messenger_factory.clone(),
        handler_messenger_factory,
    )
    .await
    .expect("registry should be created successfully");
    let setting_type = SettingType::Unknown;
    let request_id = 42;

    let (handler_messenger, handler_receptor) =
        handler_factory.lock().await.create(setting_type).await;
    let (state_tx, _) = futures::channel::mpsc::unbounded::<State>();
    let _handler = SettingHandler::create(
        handler_messenger,
        handler_receptor,
        proxy_handler_signature,
        setting_type,
        state_tx,
        None,
    );

    // Send initial request.
    let _ = get_response(
        messenger_client
            .message(
                Payload::Action(SettingAction {
                    id: request_id,
                    setting_type: setting_type,
                    data: SettingActionData::Listen(1),
                }),
                Audience::Messenger(registry_signature),
            )
            .send(),
    )
    .await;

    // Ensure the handler was only created once.
    assert_eq!(1, handler_factory.lock().await.get_request_count(setting_type));

    // Send followup request.
    let _ = get_response(
        messenger_client
            .message(
                Payload::Action(SettingAction {
                    id: request_id,
                    setting_type: setting_type,
                    data: SettingActionData::Request(SettingRequest::Get),
                }),
                Audience::Messenger(registry_signature),
            )
            .send(),
    )
    .await;

    // Make sure no followup generation was invoked.
    assert_eq!(1, handler_factory.lock().await.get_request_count(setting_type));
}

/// Ensures setting handler is generated multiple times successfully if torn down.
#[fuchsia_async::run_until_stalled(test)]
async fn test_regeneration() {
    let messenger_factory = create_hub();
    let handler_messenger_factory = handler::message::create_hub();
    let handler_factory = Arc::new(Mutex::new(FakeFactory::new(handler_messenger_factory.clone())));

    let (messenger_client, _) =
        messenger_factory.create(MessengerType::Addressable(Address::Switchboard)).await.unwrap();
    let (registry_signature, proxy_handler_signature) = RegistryImpl::create(
        SettingType::Unknown,
        handler_factory.clone(),
        messenger_factory.clone(),
        handler_messenger_factory,
    )
    .await
    .expect("registry should be created successfully");
    let setting_type = SettingType::Unknown;
    let request_id = 42;

    let (handler_messenger, handler_receptor) =
        handler_factory.lock().await.create(setting_type).await;
    let (state_tx, mut state_rx) = futures::channel::mpsc::unbounded::<State>();
    let (done_tx, done_rx) = oneshot::channel();
    let handler = SettingHandler::create(
        handler_messenger,
        handler_receptor,
        proxy_handler_signature,
        setting_type,
        state_tx,
        Some(done_tx),
    );

    // Send initial request.
    assert!(
        get_response(
            messenger_client
                .message(
                    Payload::Action(SettingAction {
                        id: request_id,
                        setting_type,
                        data: SettingActionData::Request(SettingRequest::Get),
                    }),
                    Audience::Messenger(registry_signature),
                )
                .send(),
        )
        .await
        .is_some(),
        "A response was expected"
    );

    // Ensure the handler was only created once.
    assert_eq!(1, handler_factory.lock().await.get_request_count(setting_type));

    // The subsequent teardown should happen here.
    done_rx.await.ok();
    let mut hit_teardown = false;
    loop {
        let state = state_rx.next().await;
        match state {
            Some(State::Teardown) => {
                hit_teardown = true;
                break;
            }
            None => break,
            _ => {}
        }
    }
    assert!(hit_teardown, "Handler should have torn down");
    drop(handler);

    // Now that the handler is dropped, state_rx should be dropped too
    assert!(state_rx.next().await.is_none(), "There should be no more states after teardown");

    let (handler_messenger, handler_receptor) =
        handler_factory.lock().await.create(setting_type).await;
    let (state_tx, _) = futures::channel::mpsc::unbounded::<State>();
    let _handler = SettingHandler::create(
        handler_messenger,
        handler_receptor,
        proxy_handler_signature,
        setting_type,
        state_tx,
        None,
    );

    // Send followup request.
    assert!(
        get_response(
            messenger_client
                .message(
                    Payload::Action(SettingAction {
                        id: request_id,
                        setting_type,
                        data: SettingActionData::Request(SettingRequest::Get),
                    }),
                    Audience::Messenger(registry_signature),
                )
                .send(),
        )
        .await
        .is_some(),
        "A response was expected"
    );

    // Check that the handler was re-generated.
    assert_eq!(2, handler_factory.lock().await.get_request_count(setting_type));
}

async fn get_response(mut receptor: SwitchboardReceptor) -> Option<(u64, SettingHandlerResult)> {
    while let Some(event) = receptor.next().await {
        if let MessageEvent::Message(
            Payload::Event(SettingEvent::Response(response_id, response)),
            _,
        ) = event
        {
            return Some((response_id, response));
        }
    }

    return None;
}
