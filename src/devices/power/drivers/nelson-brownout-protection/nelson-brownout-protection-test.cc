// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "nelson-brownout-protection.h"

#include <fuchsia/hardware/gpio/cpp/banjo-mock.h>
#include <fuchsia/hardware/power/sensor/cpp/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/simple-codec/simple-codec-server.h>

#include <optional>

#include <zxtest/zxtest.h>

#include "src/devices/testing/mock-ddk/mock-device.h"

namespace brownout_protection {

namespace audio_fidl = ::fuchsia::hardware::audio;
namespace signal_fidl = ::fuchsia::hardware::audio::signalprocessing;

class FakeCodec : public audio::SimpleCodecServer, public signal_fidl::SignalProcessing {
 public:
  FakeCodec(zx_device_t* parent) : SimpleCodecServer(parent) {}
  codec_protocol_t GetProto() { return {&this->codec_protocol_ops_, this}; }

  zx_status_t Shutdown() override { return ZX_OK; }

  // The test can check directly the state of AGL enablement in its thread.
  bool agl_enabled() { return agl_enabled_; }

 private:
  zx::status<audio::DriverIds> Initialize() override {
    return zx::ok(audio::DriverIds{.vendor_id = 0, .device_id = 0});
  }
  zx_status_t Reset() override { return ZX_ERR_NOT_SUPPORTED; }
  audio::Info GetInfo() override {
    return {
        .unique_id = "test id",
        .manufacturer = "test man",
        .product_name = "test prod",
    };
  }
  zx_status_t Stop() override { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t Start() override { return ZX_OK; }
  bool IsBridgeable() override { return false; }
  void SetBridgedMode(bool enable_bridged_mode) override {}
  bool SupportsSignalProcessing() override { return true; }
  void SignalProcessingConnect(
      fidl::InterfaceRequest<signal_fidl::SignalProcessing> signal_processing) override {
    signal_processing_binding_.emplace(this, std::move(signal_processing), dispatcher());
  }
  void GetElements(GetElementsCallback callback) override {
    signal_fidl::Element pe;
    pe.set_id(kAglPeId);
    pe.set_type(signal_fidl::ElementType::AUTOMATIC_GAIN_LIMITER);
    pe.set_can_disable(true);
    std::vector<signal_fidl::Element> pes;
    pes.emplace_back(std::move(pe));
    signal_fidl::Reader_GetElements_Response response(std::move(pes));
    signal_fidl::Reader_GetElements_Result result;
    result.set_response(std::move(response));
    callback(std::move(result));
  }
  void SetElementState(uint64_t processing_element_id, signal_fidl::ElementState state,
                       SetElementStateCallback callback) override {
    ASSERT_EQ(processing_element_id, kAglPeId);
    ASSERT_TRUE(state.has_enabled());
    agl_enabled_ = state.enabled();
    callback(signal_fidl::SignalProcessing_SetElementState_Result::WithResponse(
        signal_fidl::SignalProcessing_SetElementState_Response()));
  }
  void WatchElementState(uint64_t processing_element_id,
                         WatchElementStateCallback callback) override {}
  void GetTopologies(GetTopologiesCallback callback) override {
    callback(signal_fidl::Reader_GetTopologies_Result::WithErr(ZX_ERR_NOT_SUPPORTED));
  }
  void SetTopology(uint64_t topology_id, SetTopologyCallback callback) override {
    callback(signal_fidl::SignalProcessing_SetTopology_Result::WithErr(ZX_ERR_NOT_SUPPORTED));
  }
  audio::DaiSupportedFormats GetDaiFormats() override { return {}; }
  zx::status<audio::CodecFormatInfo> SetDaiFormat(const audio::DaiFormat& format) override {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
  audio::GainFormat GetGainFormat() override { return {.min_gain = -103.0f}; }
  audio::GainState GetGainState() override { return gain_state; }
  void SetGainState(audio::GainState state) override { gain_state = state; }
  inspect::Inspector& inspect() { return SimpleCodecServer::inspect(); }

 private:
  static constexpr uint64_t kAglPeId = 1;

  audio::GainState gain_state = {};
  // agl_enabled_ is accessed from different threads in SetAgl() and agl_enabled().
  std::atomic<bool> agl_enabled_ = false;
  std::optional<fidl::Binding<signal_fidl::SignalProcessing>> signal_processing_binding_;
};

class FakePowerSensor : public ddk::PowerSensorProtocol<FakePowerSensor, ddk::base_protocol>,
                        public fidl::WireServer<fuchsia_hardware_power_sensor::Device> {
 public:
  explicit FakePowerSensor(async_dispatcher_t* dispatcher)
      : proto_{.ops = &power_sensor_protocol_ops_, .ctx = this}, dispatcher_(dispatcher) {}

  const power_sensor_protocol_t* GetProto() const { return &proto_; }

  zx_status_t PowerSensorConnectServer(zx::channel server) {
    binding_.emplace(fidl::BindServer(
        dispatcher_, fidl::ServerEnd<fuchsia_hardware_power_sensor::Device>(std::move(server)),
        this));
    return ZX_OK;
  }

  void set_voltage(float voltage) { voltage_ = voltage; }

  void GetPowerWatts(GetPowerWattsCompleter::Sync& completer) override {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }

  void GetVoltageVolts(GetVoltageVoltsCompleter::Sync& completer) override {
    completer.ReplySuccess(voltage_);
  }

 private:
  float voltage_ = 0.0f;
  const power_sensor_protocol_t proto_;
  async_dispatcher_t* const dispatcher_;
  std::optional<fidl::ServerBindingRef<fuchsia_hardware_power_sensor::Device>> binding_;
};

TEST(NelsonBrownoutProtectionTest, Test) {
  auto fake_parent = MockDevice::FakeRootParent();

  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);

  ASSERT_OK(audio::SimpleCodecServer::CreateAndAddToDdk<FakeCodec>(fake_parent.get()));
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  auto codec = child_dev->GetDeviceContext<FakeCodec>();
  FakePowerSensor power_sensor(loop.dispatcher());
  ddk::MockGpio alert_gpio;

  zx::interrupt alert_gpio_interrupt;
  ASSERT_OK(zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &alert_gpio_interrupt));

  {
    zx::interrupt interrupt_dup;
    ASSERT_OK(alert_gpio_interrupt.duplicate(ZX_RIGHT_SAME_RIGHTS, &interrupt_dup));
    alert_gpio.ExpectConfigIn(ZX_OK, GPIO_NO_PULL)
        .ExpectGetInterrupt(ZX_OK, ZX_INTERRUPT_MODE_EDGE_LOW, std::move(interrupt_dup));
  }

  ASSERT_OK(loop.StartThread());
  fake_parent->AddProtocol(ZX_PROTOCOL_CODEC, codec->GetProto().ops, codec->GetProto().ctx,
                           "codec");
  fake_parent->AddProtocol(ZX_PROTOCOL_POWER_SENSOR, power_sensor.GetProto()->ops,
                           power_sensor.GetProto()->ctx, "power-sensor");
  fake_parent->AddProtocol(ZX_PROTOCOL_GPIO, alert_gpio.GetProto()->ops, alert_gpio.GetProto()->ctx,
                           "alert-gpio");

  ASSERT_OK(NelsonBrownoutProtection::Create(nullptr, fake_parent.get()));
  auto* child_dev2 = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev2);
  child_dev2->InitOp();
  EXPECT_FALSE(codec->agl_enabled());

  power_sensor.set_voltage(10.0f);  // Must be less than 11.5 to stay in the brownout state.

  alert_gpio_interrupt.trigger(0, zx::clock::get_monotonic());

  while (!codec->agl_enabled()) {
  }

  power_sensor.set_voltage(12.0f);  // End the brownout state and make sure AGL gets disabled.

  while (codec->agl_enabled()) {
  }

  ASSERT_NO_FATAL_FAILURE(alert_gpio.VerifyAndClear());
}

}  // namespace brownout_protection
