// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_FUZZING_DECODER_ENCODER_H_
#define LIB_FIDL_CPP_FUZZING_DECODER_ENCODER_H_

#include <lib/fidl/cpp/wire/message.h>
#include <stdint.h>
#include <string.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <vector>

#ifdef __cplusplus
extern "C" {
#endif

namespace fidl {
namespace fuzzing {

// DecoderEncoderProgress encodes the progress of attempted `DecoderEncoder` implementations. Order
// matters here, that is, subsequent (larger) enum values imply further progress in a
// `DecoderEncoder`.
enum DecoderEncoderProgress {
  // First operation failed.
  //
  // The first operation will be transactional header validation if the message is
  // a transactional message, and will be decode if the message is just a regular
  // FIDL type.
  NoProgress = 0,

  // The `fidl::IncomingHeaderAndMessage` type initialization has been successful.
  //
  // This step involves transactional header validation if applicable.
  InitializedForDecoding,

  // First attempt to decode succeeded.
  FirstDecodeSuccess,

  // First attempt to re-encode decoded message succeeded.
  FirstEncodeSuccess,

  // Additional checks on handle rights triggered by `::fidl::OutgoingToIncomingMessage` succeeded.
  FirstEncodeVerified,

  // Second attempt to decode (decode what was just encoded and verified) succeeded.
  SecondDecodeSuccess,

  // Second attempt to encode (encode object resulting from `SecondDecodeSuccess` step) succeeded.
  SecondEncodeSuccess,
};

// DecoderEncoderStatus encapsulates data that results from attempting to decode and encode a
// particular a collection of bytes and handles as a particular FIDL type.
struct DecoderEncoderStatus {
  DecoderEncoderProgress progress;
  fidl::Status status;

  // First encoding data, relevant for `progress >= DecoderEncoderProgress::FirstEncodeSuccess`.
  std::vector<uint8_t> first_encoded_bytes;
  // TODO(fxbug.dev/72895): Add first handles for koid check.

  // Second encoding data, relevant for `progress >= DecoderEncoderProgress::SecondEncodeSuccess`.
  std::vector<uint8_t> second_encoded_bytes;
  // TODO(fxbug.dev/72895): Add second handles for koid check.
};

// An DecoderEncoder is a function that encapsulates the FIDL type-specific logic for attempting to
// decode and (if decode succeeds) re-encode a FIDL message via the interface documented at
// https://fuchsia.dev/fuchsia-src/reference/fidl/bindings/llcpp-bindings#encoding-decoding.
// Note that a function pointer is used instead of `::std::function` to facilitate header-only
// constexpr globals of type `::std::array<DecoderEncoder, n>`.
using DecoderEncoder = DecoderEncoderStatus (*)(uint8_t* bytes, uint32_t num_bytes,
                                                zx_handle_t* handles,
                                                // handle type and rights. The ith index corresponds
                                                // to the ith handle in |handles|.
                                                fidl_channel_handle_metadata_t* handle_metadata,
                                                uint32_t num_handles);

struct DecoderEncoderForType {
  const char* const fidl_type_name;
  const bool has_flexible_envelope;
  const DecoderEncoder decoder_encoder;
};

}  // namespace fuzzing
}  // namespace fidl

#ifdef __cplusplus
}  // extern "C"
#endif

namespace fidl {
namespace fuzzing {

template <typename T>
DecoderEncoderStatus DecoderEncoderImpl(uint8_t* bytes, uint32_t num_bytes, zx_handle_t* handles,
                                        fidl_channel_handle_metadata_t* handle_metadata,
                                        uint32_t num_handles) {
  DecoderEncoderStatus status = {
      .progress = DecoderEncoderProgress::NoProgress,
      .status = fidl::Status::Ok(),
  };

  std::optional<fidl::WireFormatMetadata> wire_format_metadata_initialize_later;
  std::optional<fidl::EncodedMessage> incoming_initialize_later;
  constexpr bool kTransactionalMessage = fidl::IsFidlTransactionalMessage<T>::value;
  static_assert(!kTransactionalMessage);
  wire_format_metadata_initialize_later.emplace(
      fidl::internal::WireFormatMetadataForVersion(fidl::internal::WireFormatVersion::kV2));
  incoming_initialize_later = fidl::EncodedMessage::Create(cpp20::span<uint8_t>(bytes, num_bytes),
                                                           handles, handle_metadata, num_handles);
  fidl::WireFormatMetadata& wire_format_metadata = wire_format_metadata_initialize_later.value();
  fidl::EncodedMessage& incoming = incoming_initialize_later.value();

  status.progress = DecoderEncoderProgress::InitializedForDecoding;

  std::optional<fitx::result<fidl::Error, fidl::DecodedValue<T>>> decoded_initialize_later;
  decoded_initialize_later.emplace(
      fidl::InplaceDecode<T>(std::move(incoming), wire_format_metadata));
  fitx::result<fidl::Error, fidl::DecodedValue<T>>& decoded = decoded_initialize_later.value();

  if (!decoded.is_ok()) {
    status.status = decoded.error_value();
    return status;
  }
  status.progress = DecoderEncoderProgress::FirstDecodeSuccess;

  T* value = decoded.value().pointer();

  // By specifying |AllowUnownedInputRef|, we fuzz the code paths used in production message
  // passing, which uses multiple iovecs referencing input objects instead of copying.
  // TODO(fxbug.dev/45252): Use FIDL at rest.
  fidl::unstable::OwnedEncodedMessage<T> encoded(::fidl::internal::AllowUnownedInputRef{},
                                                 fidl::internal::WireFormatVersion::kV2, value);

  if (!encoded.ok()) {
    status.status = encoded.GetOutgoingMessage();
    return status;
  }
  status.progress = DecoderEncoderProgress::FirstEncodeSuccess;

  auto message_bytes = encoded.GetOutgoingMessage().CopyBytes();
  status.first_encoded_bytes =
      ::std::vector<uint8_t>(message_bytes.data(), message_bytes.data() + message_bytes.size());

  // TODO(fxbug.dev/72895): Add handles for koid check.

  auto conversion = ::fidl::OutgoingToIncomingMessage(encoded.GetOutgoingMessage());

  if (!conversion.ok()) {
    status.status = conversion.error();
    return status;
  }
  status.progress = DecoderEncoderProgress::FirstEncodeVerified;

  std::optional<fitx::result<fidl::Error, fidl::DecodedValue<T>>> decoded2_initialize_later;
  decoded2_initialize_later.emplace(
      fidl::InplaceDecode<T>(std::move(conversion.incoming_message()), wire_format_metadata));
  fitx::result<fidl::Error, fidl::DecodedValue<T>>& decoded2 = decoded2_initialize_later.value();

  if (!decoded2.is_ok()) {
    status.status = decoded2.error_value();
    return status;
  }
  status.progress = DecoderEncoderProgress::SecondDecodeSuccess;

  // In contrast to |encoded| above, |encoded2| encodes using a less common path that is explicitly
  // encoded by users and fully owns the content of the encoded message. One example of this is
  // in-process messaging.
  T* value2 = decoded2.value().pointer();
  // TODO(fxbug.dev/45252): Use FIDL at rest.
  fidl::unstable::OwnedEncodedMessage<T> encoded2(fidl::internal::WireFormatVersion::kV2, value2);

  if (!encoded2.ok()) {
    status.status = encoded2.GetOutgoingMessage();
    return status;
  }
  status.progress = DecoderEncoderProgress::SecondEncodeSuccess;

  auto message_bytes2 = encoded2.GetOutgoingMessage().CopyBytes();
  status.second_encoded_bytes =
      ::std::vector<uint8_t>(message_bytes2.data(), message_bytes2.data() + message_bytes2.size());

  // TODO(fxbug.dev/72895): Add handles for koid check.

  return status;
}

}  // namespace fuzzing
}  // namespace fidl

#endif  // LIB_FIDL_CPP_FUZZING_DECODER_ENCODER_H_
