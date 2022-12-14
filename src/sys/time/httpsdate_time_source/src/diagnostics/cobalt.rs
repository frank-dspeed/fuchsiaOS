// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::datatypes::{HttpsSample, Phase};
use crate::diagnostics::{Diagnostics, Event};
use fidl_fuchsia_cobalt::HistogramBucket;
use fuchsia_cobalt::{CobaltConnector, CobaltSender, ConnectionType};
use fuchsia_zircon as zx;
use futures::Future;
use parking_lot::Mutex;
use time_metrics_registry::{
    HttpsdateBoundSizeMetricDimensionPhase as CobaltPhase, HTTPSDATE_BOUND_SIZE_METRIC_ID,
    HTTPSDATE_POLL_LATENCY_INT_BUCKETS_FLOOR,
    HTTPSDATE_POLL_LATENCY_INT_BUCKETS_NUM_BUCKETS as RTT_BUCKETS,
    HTTPSDATE_POLL_LATENCY_INT_BUCKETS_STEP_SIZE, HTTPSDATE_POLL_LATENCY_METRIC_ID, PROJECT_ID,
};

const RTT_BUCKET_SIZE: zx::Duration =
    zx::Duration::from_micros(HTTPSDATE_POLL_LATENCY_INT_BUCKETS_STEP_SIZE as i64);
const RTT_BUCKET_FLOOR: zx::Duration =
    zx::Duration::from_micros(HTTPSDATE_POLL_LATENCY_INT_BUCKETS_FLOOR);

/// A `Diagnostics` implementation that uploads diagnostics metrics to Cobalt.
pub struct CobaltDiagnostics {
    /// Client connection to Cobalt.
    sender: Mutex<CobaltSender>,
    /// Last known phase of the algorithm.
    phase: Mutex<Phase>,
}

impl CobaltDiagnostics {
    /// Create a new `CobaltDiagnostics`, and future that must be polled to upload to Cobalt.
    pub fn new() -> (Self, impl Future<Output = ()>) {
        let (sender, fut) =
            CobaltConnector::default().serve(ConnectionType::project_id(PROJECT_ID));
        (Self { sender: Mutex::new(sender), phase: Mutex::new(Phase::Initial) }, fut)
    }

    /// Calculate the bucket number in the latency metric for a given duration.
    fn round_trip_time_bucket(duration: &zx::Duration) -> u32 {
        Self::cobalt_bucket(*duration, RTT_BUCKETS, RTT_BUCKET_SIZE, RTT_BUCKET_FLOOR)
    }

    /// Calculate the bucket index for a time duration. Indices follow the rules for Cobalt
    /// histograms - bucket 0 is underflow, and num_buckets + 1 is overflow.
    fn cobalt_bucket(
        duration: zx::Duration,
        num_buckets: u32,
        bucket_size: zx::Duration,
        underflow_floor: zx::Duration,
    ) -> u32 {
        let overflow_threshold = underflow_floor + (bucket_size * num_buckets);
        if duration < underflow_floor {
            0
        } else if duration > overflow_threshold {
            num_buckets + 1
        } else {
            ((duration - underflow_floor).into_nanos() / bucket_size.into_nanos()) as u32 + 1
        }
    }

    fn success(&self, sample: &HttpsSample) {
        let phase = self.phase.lock();
        let mut sender = self.sender.lock();
        sender.log_event_count(
            HTTPSDATE_BOUND_SIZE_METRIC_ID,
            [<Phase as Into<CobaltPhase>>::into(*phase)],
            0i64, // period_duration, not used
            sample.final_bound_size.into_micros(),
        );

        let mut bucket_counts = [0u64; RTT_BUCKETS as usize + 2];
        for bucket_idx in
            sample.polls.iter().map(|poll| Self::round_trip_time_bucket(&poll.round_trip_time))
        {
            bucket_counts[bucket_idx as usize] += 1;
        }
        let histogram_buckets = bucket_counts
            .iter()
            .enumerate()
            .filter(|(_, count)| **count > 0)
            .map(|(index, count)| HistogramBucket { index: index as u32, count: *count })
            .collect::<Vec<_>>();
        sender.log_int_histogram(HTTPSDATE_POLL_LATENCY_METRIC_ID, (), histogram_buckets);
    }

    fn phase_update(&self, phase: &Phase) {
        *self.phase.lock() = *phase;
    }
}

impl Diagnostics for CobaltDiagnostics {
    fn record<'a>(&self, event: Event<'a>) {
        match event {
            Event::NetworkCheckSuccessful => (),
            Event::Success(sample) => self.success(sample),
            Event::Failure(_) => (), // currently, no failures are registered with cobalt
            Event::Phase(phase) => self.phase_update(&phase),
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::datatypes::Poll;
    use fidl_fuchsia_cobalt::{CobaltEvent, CountEvent, EventPayload};
    use futures::{channel::mpsc, stream::StreamExt};
    use lazy_static::lazy_static;
    use std::{collections::HashSet, iter::FromIterator};

    const TEST_INITIAL_PHASE: Phase = Phase::Initial;
    const TEST_BOUND_SIZE: zx::Duration = zx::Duration::from_millis(101);
    const TEST_STANDARD_DEVIATION: zx::Duration = zx::Duration::from_millis(20);
    const ONE_MICROS: zx::Duration = zx::Duration::from_micros(1);
    const TEST_CENTER_OFFSET: zx::Duration = zx::Duration::from_micros(20);
    const TEST_TIME: zx::Time = zx::Time::from_nanos(123_456_789);
    const TEST_RTT_BUCKET: u32 = 2;
    const TEST_RTT_2_BUCKET: u32 = 4;
    const OVERFLOW_RTT: zx::Duration = zx::Duration::from_seconds(10);
    const RTT_BUCKET_SIZE: zx::Duration =
        zx::Duration::from_micros(HTTPSDATE_POLL_LATENCY_INT_BUCKETS_STEP_SIZE as i64);

    const POLL_OFFSET_RTT_BUCKET_SIZE: zx::Duration = zx::Duration::from_micros(10000);
    const POLL_OFFSET_RTT_FLOOR: zx::Duration = zx::Duration::from_micros(0);

    lazy_static! {
        static ref TEST_INITIAL_PHASE_COBALT: CobaltPhase = TEST_INITIAL_PHASE.into();
        static ref TEST_RTT: zx::Duration =
            RTT_BUCKET_FLOOR + RTT_BUCKET_SIZE * TEST_RTT_BUCKET - ONE_MICROS;
        static ref TEST_RTT_2: zx::Duration =
            RTT_BUCKET_FLOOR + RTT_BUCKET_SIZE * TEST_RTT_2_BUCKET - ONE_MICROS;
        static ref TEST_RTT_OFFSET_BUCKET: u32 = ((*TEST_RTT - POLL_OFFSET_RTT_FLOOR).into_nanos()
            / POLL_OFFSET_RTT_BUCKET_SIZE.into_nanos())
            as u32
            + 1;
        static ref TEST_RTT_2_OFFSET_BUCKET: u32 =
            ((*TEST_RTT_2 - POLL_OFFSET_RTT_FLOOR).into_nanos()
                / POLL_OFFSET_RTT_BUCKET_SIZE.into_nanos()) as u32
                + 1;
    }

    /// Create a `CobaltDiagnostics` and a receiver to inspect events it produces.
    fn diagnostics_for_test() -> (CobaltDiagnostics, mpsc::Receiver<CobaltEvent>) {
        let (send, recv) = mpsc::channel(10);
        (
            CobaltDiagnostics {
                sender: Mutex::new(CobaltSender::new(send)),
                phase: Mutex::new(TEST_INITIAL_PHASE),
            },
            recv,
        )
    }

    #[fuchsia::test]
    fn test_round_trip_time_bucket() {
        let bucket_1_rtt = RTT_BUCKET_FLOOR + ONE_MICROS;
        let bucket_5_rtt_1 = bucket_1_rtt + RTT_BUCKET_SIZE * 4;
        let overflow_rtt = RTT_BUCKET_FLOOR + RTT_BUCKET_SIZE * (RTT_BUCKETS + 2);
        let overflow_rtt_2 =
            RTT_BUCKET_FLOOR + RTT_BUCKET_SIZE * RTT_BUCKETS + zx::Duration::from_minutes(2);
        let overflow_adjacent_rtt = RTT_BUCKET_FLOOR + RTT_BUCKET_SIZE * RTT_BUCKETS - ONE_MICROS;
        let underflow_rtt = RTT_BUCKET_FLOOR - ONE_MICROS;

        assert_eq!(CobaltDiagnostics::round_trip_time_bucket(&bucket_1_rtt), 1);
        assert_eq!(CobaltDiagnostics::round_trip_time_bucket(&bucket_5_rtt_1), 5);
        assert_eq!(CobaltDiagnostics::round_trip_time_bucket(&overflow_rtt), RTT_BUCKETS + 1);
        assert_eq!(CobaltDiagnostics::round_trip_time_bucket(&overflow_rtt_2), RTT_BUCKETS + 1);
        assert_eq!(CobaltDiagnostics::round_trip_time_bucket(&overflow_adjacent_rtt), RTT_BUCKETS);
        assert_eq!(CobaltDiagnostics::round_trip_time_bucket(&underflow_rtt), 0);
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn test_success_single_poll() {
        let (cobalt, event_recv) = diagnostics_for_test();
        cobalt.record(Event::Success(&HttpsSample {
            utc: TEST_TIME,
            monotonic: TEST_TIME,
            standard_deviation: TEST_STANDARD_DEVIATION,
            final_bound_size: TEST_BOUND_SIZE,
            polls: vec![Poll::with_round_trip_time(*TEST_RTT)],
        }));
        assert_eq!(
            event_recv.take(2).collect::<Vec<_>>().await,
            vec![
                CobaltEvent {
                    metric_id: HTTPSDATE_BOUND_SIZE_METRIC_ID,
                    event_codes: vec![*TEST_INITIAL_PHASE_COBALT as u32],
                    component: None,
                    payload: EventPayload::EventCount(CountEvent {
                        period_duration_micros: 0,
                        count: TEST_BOUND_SIZE.into_micros()
                    })
                },
                CobaltEvent {
                    metric_id: HTTPSDATE_POLL_LATENCY_METRIC_ID,
                    event_codes: vec![],
                    component: None,
                    payload: EventPayload::IntHistogram(vec![HistogramBucket {
                        index: TEST_RTT_BUCKET,
                        count: 1
                    }])
                }
            ]
        );
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn test_success_after_phase_update() {
        let (cobalt, mut event_recv) = diagnostics_for_test();
        cobalt.record(Event::Success(&HttpsSample {
            utc: TEST_TIME,
            monotonic: TEST_TIME,
            standard_deviation: TEST_STANDARD_DEVIATION,
            final_bound_size: TEST_BOUND_SIZE,
            polls: vec![Poll::with_round_trip_time(*TEST_RTT)],
        }));
        let events = event_recv.by_ref().take(2).collect::<Vec<_>>().await;
        assert_eq!(events[0].event_codes, vec![*TEST_INITIAL_PHASE_COBALT as u32]);

        cobalt.record(Event::Phase(Phase::Converge));
        cobalt.record(Event::Success(&HttpsSample {
            utc: TEST_TIME,
            monotonic: TEST_TIME,
            standard_deviation: TEST_STANDARD_DEVIATION,
            final_bound_size: TEST_BOUND_SIZE,
            polls: vec![Poll::with_round_trip_time(*TEST_RTT_2)],
        }));
        let events = event_recv.take(2).collect::<Vec<_>>().await;
        assert_eq!(events[0].event_codes, vec![CobaltPhase::Converge as u32]);
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn test_success_multiple_rtt() {
        let (cobalt, event_recv) = diagnostics_for_test();
        cobalt.record(Event::Success(&HttpsSample {
            utc: TEST_TIME,
            monotonic: TEST_TIME,
            standard_deviation: TEST_STANDARD_DEVIATION,
            final_bound_size: TEST_BOUND_SIZE,
            polls: vec![
                Poll::with_round_trip_time(*TEST_RTT),
                Poll::with_round_trip_time(*TEST_RTT_2),
                Poll::with_round_trip_time(*TEST_RTT_2),
            ],
        }));
        let mut events = event_recv.take(2).collect::<Vec<_>>().await;
        assert_eq!(
            events[0],
            CobaltEvent {
                metric_id: HTTPSDATE_BOUND_SIZE_METRIC_ID,
                event_codes: vec![*TEST_INITIAL_PHASE_COBALT as u32],
                component: None,
                payload: EventPayload::EventCount(CountEvent {
                    period_duration_micros: 0,
                    count: TEST_BOUND_SIZE.into_micros()
                })
            }
        );
        assert_eq!(events[1].metric_id, HTTPSDATE_POLL_LATENCY_METRIC_ID);
        assert!(events[1].event_codes.is_empty());
        assert!(events[1].component.is_none());
        match events.remove(1).payload {
            EventPayload::IntHistogram(buckets) => {
                let expected_buckets: HashSet<HistogramBucket> = HashSet::from_iter(vec![
                    HistogramBucket { index: TEST_RTT_BUCKET, count: 1 },
                    HistogramBucket { index: TEST_RTT_2_BUCKET, count: 2 },
                ]);
                assert_eq!(expected_buckets, HashSet::from_iter(buckets));
            }
            p => panic!("Got unexpected payload: {:?}", p),
        }
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn test_success_overflow_rtt() {
        let (cobalt, event_recv) = diagnostics_for_test();
        cobalt.record(Event::Success(&HttpsSample {
            utc: TEST_TIME,
            monotonic: TEST_TIME,
            standard_deviation: TEST_STANDARD_DEVIATION,
            final_bound_size: TEST_BOUND_SIZE,
            polls: vec![Poll {
                round_trip_time: OVERFLOW_RTT,
                center_offset: Some(TEST_CENTER_OFFSET),
            }],
        }));
        assert_eq!(
            event_recv.take(2).collect::<Vec<_>>().await,
            vec![
                CobaltEvent {
                    metric_id: HTTPSDATE_BOUND_SIZE_METRIC_ID,
                    event_codes: vec![*TEST_INITIAL_PHASE_COBALT as u32],
                    component: None,
                    payload: EventPayload::EventCount(CountEvent {
                        period_duration_micros: 0,
                        count: TEST_BOUND_SIZE.into_micros()
                    })
                },
                CobaltEvent {
                    metric_id: HTTPSDATE_POLL_LATENCY_METRIC_ID,
                    event_codes: vec![],
                    component: None,
                    payload: EventPayload::IntHistogram(vec![HistogramBucket {
                        index: RTT_BUCKETS + 1,
                        count: 1
                    }])
                },
            ]
        );
    }
}
