// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Context as _, Error};
use fidl_fuchsia_media::{AudioDeviceEnumeratorMarker, PcmFormat};
use fuchsia_async as fasync;
use fuchsia_audio_device_output::driver::SoftPcmOutput;
use fuchsia_bluetooth::types::PeerId;
use fuchsia_inspect_derive::Inspect;
use fuchsia_zircon::{self as zx, DurationNum};
use futures::stream::{BoxStream, FusedStream};
use futures::task::{Context, Poll};
use futures::{FutureExt, StreamExt};
use std::pin::Pin;

use crate::PcmAudio;

pub struct SawWaveStream {
    format: PcmFormat,
    frequency_hops: Vec<f32>,
    next_frame_timer: fasync::Timer,
    /// the last time we delivered frames.
    last_frame_time: Option<zx::Time>,
}

impl futures::Stream for SawWaveStream {
    type Item = fuchsia_audio_device_output::Result<Vec<u8>>;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let now = zx::Time::get_monotonic();
        if self.last_frame_time.is_none() {
            self.last_frame_time = Some(now - 1.second());
        }
        let last_time = self.last_frame_time.as_ref().unwrap().clone();
        let repeats = (now - last_time).into_seconds();
        if repeats == 0 {
            self.next_frame_timer = fasync::Timer::new(last_time + 1.second());
            let poll = self.next_frame_timer.poll_unpin(cx);
            assert!(poll == Poll::Pending);
            return Poll::Pending;
        }
        let next_freq = self.frequency_hops.remove(0);
        let audio = PcmAudio::create_saw_wave(
            next_freq,
            0.2,
            self.format.clone(),
            self.format.frames_per_second as usize,
        );
        self.frequency_hops.push(next_freq);
        self.last_frame_time = Some(last_time + 1.second());
        Poll::Ready(Some(Ok(audio.buffer)))
    }
}

impl FusedStream for SawWaveStream {
    fn is_terminated(&self) -> bool {
        false
    }
}

impl SawWaveStream {
    pub fn new_big_ben(format: PcmFormat) -> Self {
        Self {
            format,
            // G# - 415.30 F# - 369.99 E - 329.63 B - 246.94
            // Clock Chimes: (silence) E, G#, F#, B, E, F#, G#, E, G#, E, F#, B, B, F#, G# E
            frequency_hops: vec![
                0.0, 329.63, 415.30, 369.99, 246.94, 329.63, 369.99, 415.30, 329.63, 415.30,
                329.63, 369.99, 246.94, 246.94, 369.99, 415.30, 329.63,
            ],
            next_frame_timer: fasync::Timer::new(fasync::Time::INFINITE_PAST),
            last_frame_time: None,
        }
    }
}

pub struct AudioOutStream {}

/// Generate a unique ID to use with audio_core for an input, given the `peer_id` and whether it
/// will be an input device.  Current format is:
/// [{PeerId in big endian, 8 bytes}, 2, 2, 2, 2, 2, 2, 2, 2]
// TODO(fxbug.dev/78139): centralize format and generation
fn unique_id_from_peer(peer_id: &PeerId) -> [u8; 16] {
    let mut unique_id = [2; 16];
    unique_id[0..8].copy_from_slice(&(peer_id.0.to_be_bytes()));
    unique_id
}

const LOCAL_MONOTONIC_CLOCK_DOMAIN: u32 = 0;
impl AudioOutStream {
    pub fn new(
        peer_id: &PeerId,
        pcm_format: PcmFormat,
    ) -> Result<fuchsia_audio_device_output::driver::AudioFrameStream, Error> {
        let id = unique_id_from_peer(peer_id);
        let (client, frame_stream) = SoftPcmOutput::build(
            &id,
            "Google",
            "Bluetooth A2DP",
            LOCAL_MONOTONIC_CLOCK_DOMAIN,
            pcm_format,
            11.millis(),
        )?;

        let svc = fuchsia_component::client::connect_to_protocol::<AudioDeviceEnumeratorMarker>()
            .context("Failed to connect to AudioDeviceEnumerator")?;
        svc.add_device_by_channel("Bluetooth A2DP", false, client)?;

        Ok(frame_stream)
    }
}

#[derive(Clone, Copy, PartialEq, Debug)]
pub enum AudioSourceType {
    AudioOut,
    BigBen,
}

impl core::fmt::Display for AudioSourceType {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> Result<(), std::fmt::Error> {
        write!(
            f,
            "{}",
            match self {
                AudioSourceType::AudioOut => "audio_out",
                AudioSourceType::BigBen => "big_ben",
            }
        )
    }
}

impl std::str::FromStr for AudioSourceType {
    type Err = anyhow::Error;
    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "audio_out" => Ok(AudioSourceType::AudioOut),
            "big_ben" => Ok(AudioSourceType::BigBen),
            _ => Err(format_err!("Unrecognized audio source, use audio_out or big_ben")),
        }
    }
}

pub fn build_stream(
    peer_id: &PeerId,
    pcm_format: PcmFormat,
    source_type: AudioSourceType,
    inspect_parent: Option<&fuchsia_inspect::Node>,
) -> Result<BoxStream<'static, fuchsia_audio_device_output::Result<Vec<u8>>>, Error> {
    Ok(match source_type {
        AudioSourceType::AudioOut => {
            let mut stream = AudioOutStream::new(peer_id, pcm_format)?;
            if let Some(parent) = inspect_parent {
                let _ = stream.iattach(parent, "audio_out_stream");
            }
            stream.boxed()
        }
        AudioSourceType::BigBen => SawWaveStream::new_big_ben(pcm_format).boxed(),
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn unique_id_generation() {
        let peer_id = PeerId(1);
        let peer_id_same = PeerId(1);
        let peer_id_different = PeerId(3);

        assert_eq!(unique_id_from_peer(&peer_id_same), unique_id_from_peer(&peer_id));
        assert_ne!(unique_id_from_peer(&peer_id_different), unique_id_from_peer(&peer_id));
    }
}
