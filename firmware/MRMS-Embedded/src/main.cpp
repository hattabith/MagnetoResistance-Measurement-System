/**
 * MRMS Firmware — MagnetoResistance Measurement System
 *
 * Implements the SCPI-subset control protocol specified in
 * docs/ControlProtocolSpecification.md (version 1.2).
 *
 * Hardware: SparkFun ProMicro 16 MHz (ATmega32U4)
 *   ADS1115  (I2C)  — Hall sensor (CH0), sample voltage (CH1), current (CH2)
 *   TAP_PINS[8]     — Battery tap relays 2S … 16S (Break-Before-Make)
 *   MAGNET_PWM_PIN  — PWM → RC → external PSU control (0 – MAGNET_V_MAX V)
 *   SAMPLE_EN_PIN   — Enable sample current source
 *   SAMPLE_POL_PIN  — Polarity: HIGH = +I, LOW = −I
 */

#include <Arduino.h>
#include <Adafruit_ADS1X15.h>
#include <MrmsController.h>

// ─── Firmware identity ────────────────────────────────────────────────────────
static const char kIdentity[] PROGMEM =
    "KPI-Lab,MR-MEAS-4P,SN2026-001,v1.2.0";

// ─── Pin assignments ─────────────────────────────────────────────────────────
// Battery tap relays: index 0 = 2S, 1 = 4S, … 7 = 16S
static const uint8_t TAP_PINS[8] = {4, 5, 6, 7, 8, 10, 14, 20};

// Magnet PSU control — FastPWM on OC1A (Timer1), mapped 0 → 0 V, 255 → MAGNET_V_MAX
static constexpr uint8_t MAGNET_PWM_PIN = 9;
static constexpr float   MAGNET_V_MAX   = 60.0f;

// Sample current source
static constexpr uint8_t SAMPLE_EN_PIN  = 15; // HIGH = source enabled
static constexpr uint8_t SAMPLE_POL_PIN = 16; // HIGH = +I, LOW = −I

// ADS1115 channel assignments
static constexpr uint8_t ADC_CH_HALL    = 0; // Hall sensor
static constexpr uint8_t ADC_CH_VSAMP  = 1; // Sample voltage
static constexpr uint8_t ADC_CH_ISAMP  = 2; // Sample current (shunt)
static constexpr float   SHUNT_OHMS    = 100.0f;

// ─── Constants ────────────────────────────────────────────────────────────────
// Headroom voltage added to estimated R × I when choosing the best tap
static constexpr float TAP_HEADROOM_V = 2.0f;

static constexpr uint32_t UART_BAUD = 115200;

// ─── Globals ──────────────────────────────────────────────────────────────────
static Adafruit_ADS1115 ads;
static mrms::Controller  controller;

// ─── HAL: magnet PSU ─────────────────────────────────────────────────────────

static void setMagnetVoltage(float volts) {
  volts = constrain(volts, 0.0f, MAGNET_V_MAX);
  uint8_t duty = static_cast<uint8_t>((volts / MAGNET_V_MAX) * 255.0f + 0.5f);
  analogWrite(MAGNET_PWM_PIN, duty);
  controller.setMagnetVoltage(volts);
}

// ─── HAL: battery tap relays ──────────────────────────────────────────────────

// Open all tap relays
static void openAllTaps() {
  for (uint8_t i = 0; i < 8; ++i) {
    digitalWrite(TAP_PINS[i], LOW);
  }
}

// Break-Before-Make tap switch; pass TapValue::Off to open all
static bool setTapRelay(mrms::TapValue newTap) {
  mrms::TapValue current = controller.tap();

  // Step 1 — open current relay
  if (current != mrms::TapValue::Off) {
    uint8_t idx = static_cast<uint8_t>(current) - 1;
    digitalWrite(TAP_PINS[idx], LOW);
    delay(5); // brief break time
  }

  // Step 2 — close new relay
  if (newTap != mrms::TapValue::Off) {
    uint8_t idx = static_cast<uint8_t>(newTap) - 1;
    if (idx >= 8) {
      controller.errorQueue().push(mrms::ErrorCode::RelayInterlock);
      return false;
    }
    digitalWrite(TAP_PINS[idx], HIGH);
  }

  controller.setTap(newTap);
  return true;
}

// ─── HAL: sample current source ──────────────────────────────────────────────

static void enableSampleCurrent(bool positive) {
  digitalWrite(SAMPLE_POL_PIN, positive ? HIGH : LOW);
  delay(1);
  digitalWrite(SAMPLE_EN_PIN, HIGH);
}

static void disableSampleCurrent() {
  digitalWrite(SAMPLE_EN_PIN, LOW);
}

// ─── HAL: ADS1115 readings ────────────────────────────────────────────────────

static float readHallTesla() {
  // ADS1115 with GAIN_ONE: ±4.096 V, 0.125 mV/LSB
  // Hall sensor: sensitivity and zero-offset calibration would be applied here.
  // For now we use a direct voltage-to-field factor; 1 V = 1 T (placeholder).
  int16_t raw  = ads.readADC_SingleEnded(ADC_CH_HALL);
  float   volt = ads.computeVolts(raw);
  return volt; // Replace with: (volt - HALL_ZERO_V) / HALL_SENS_V_PER_T
}

static float readSampleVoltage() {
  int16_t raw = ads.readADC_SingleEnded(ADC_CH_VSAMP);
  return ads.computeVolts(raw);
}

static float readSampleCurrentAmps() {
  int16_t raw  = ads.readADC_SingleEnded(ADC_CH_ISAMP);
  float   volt = ads.computeVolts(raw);
  return volt / SHUNT_OHMS;
}

// ─── Measurement: single test pulse ──────────────────────────────────────────

struct TestPulseResult {
  float rApprox;
  float vMeas;
  float iMeas;
};

static TestPulseResult doTestPulse() {
  int16_t pw = controller.pulseWidth();
  enableSampleCurrent(true);
  delay(static_cast<unsigned long>(pw));
  float vMeas = readSampleVoltage();
  float iMeas = readSampleCurrentAmps();
  disableSampleCurrent();

  float rApprox = (iMeas > 1e-6f) ? (vMeas / iMeas) : 0.0f;
  return {rApprox, vMeas, iMeas};
}

// ─── Measurement: Delta-Mode (±I) resistance ─────────────────────────────────

struct DeltaResult {
  float r;
  float vPlus;
  float vMinus;
};

static DeltaResult doDeltaMeasure() {
  int16_t pw = controller.pulseWidth();

  // +I pulse
  enableSampleCurrent(true);
  delay(static_cast<unsigned long>(pw));
  float vPlus = readSampleVoltage();
  disableSampleCurrent();
  delay(5); // settling

  // −I pulse
  enableSampleCurrent(false);
  delay(static_cast<unsigned long>(pw));
  float vMinus = readSampleVoltage();
  disableSampleCurrent();

  float iA = controller.sampleCurrent() * 1e-3f;
  float r   = (iA > 1e-6f) ? ((vPlus - vMinus) / (2.0f * iA)) : 0.0f;
  return {r, vPlus, vMinus};
}

// ─── AutoPick tap logic ───────────────────────────────────────────────────────

static void doAutoPickTap() {
  // 1. Switch to 2S baseline
  if (!setTapRelay(mrms::TapValue::T2S)) {
    return;
  }
  delay(50);

  // 2. Short test pulse to estimate R
  TestPulseResult tp = doTestPulse();

  // 3. Required voltage = I_set × R_approx + headroom
  float iSet = controller.sampleCurrent() * 1e-3f;
  float vReq = iSet * tp.rApprox + TAP_HEADROOM_V;

  // 4. Choose smallest tap that covers vReq
  mrms::TapValue best = mrms::TapValue::T16S;
  for (uint8_t ti = static_cast<uint8_t>(mrms::TapValue::T2S);
       ti <= static_cast<uint8_t>(mrms::TapValue::T16S); ++ti) {
    mrms::TapValue tv = static_cast<mrms::TapValue>(ti);
    if (mrms::tapNominalVoltage(tv) >= vReq) {
      best = tv;
      break;
    }
  }

  // 5. Switch to chosen tap
  setTapRelay(best);

  // 6. Respond
  Serial.print(mrms::tapToString(best));
  Serial.print(',');
  Serial.print(tp.rApprox, 1);
  Serial.print(',');
  Serial.println(mrms::tapNominalVoltage(best), 1);
}

// ─── Sweep state machine ──────────────────────────────────────────────────────

namespace {
struct SweepState {
  uint16_t step;
  float    vCurrent;
  float    prevField;
  uint8_t  stableCount;
  uint32_t phaseStart; // millis() when current phase started

  enum class Phase : uint8_t { SetVoltage, Stabilize, Measure } phase;
};
} // namespace

static SweepState sweep;
static bool       sweepAbortRequested = false;

static void sweepStart() {
  mrms::SweepConfig cfg = controller.sweepConfig();
  sweep.step        = 1;
  sweep.vCurrent    = cfg.vStart;
  sweep.prevField   = readHallTesla();
  sweep.stableCount = 0;
  sweep.phaseStart  = millis();
  sweep.phase       = SweepState::Phase::SetVoltage;
  sweepAbortRequested = false;
  controller.setSweeping(true);
}

static void sweepTick() {
  if (!controller.isSweeping()) {
    return;
  }

  if (sweepAbortRequested) {
    // Emergency stop
    setMagnetVoltage(0.0f);
    disableSampleCurrent();
    openAllTaps();
    controller.setTap(mrms::TapValue::Off);
    controller.setSweeping(false);
    Serial.println(F("SWEEP:ABORTED"));
    return;
  }

  mrms::SweepConfig cfg = controller.sweepConfig();
  uint32_t now = millis();

  switch (sweep.phase) {
  case SweepState::Phase::SetVoltage:
    setMagnetVoltage(sweep.vCurrent);
    sweep.prevField   = readHallTesla();
    sweep.stableCount = 0;
    sweep.phaseStart  = now;
    sweep.phase       = SweepState::Phase::Stabilize;
    break;

  case SweepState::Phase::Stabilize: {
    float field = readHallTesla();
    float dB    = fabsf(field - sweep.prevField);
    sweep.prevField = field;

    if (dB < controller.stabThreshold()) {
      ++sweep.stableCount;
    } else {
      sweep.stableCount = 0;
    }

    bool stable  = (sweep.stableCount >= controller.stabCount());
    bool timeout = ((now - sweep.phaseStart) >= controller.stabTimeout());

    if (stable || timeout) {
      if (timeout && !stable) {
        controller.errorQueue().push(mrms::ErrorCode::StabTimeout);
      }
      sweep.phase     = SweepState::Phase::Measure;
      sweep.phaseStart = now;
    }
    break;
  }

  case SweepState::Phase::Measure: {
    float field = readHallTesla();
    DeltaResult dr = doDeltaMeasure();

    // DATA <step>,<V_set>,<B_field>,<R_sample>,<V_plus>,<V_minus>,<Tap>,<Status>
    Serial.print(F("DATA "));
    Serial.print(sweep.step);
    Serial.print(',');
    Serial.print(sweep.vCurrent, 1);
    Serial.print(',');
    Serial.print(field, 4);
    Serial.print(',');
    Serial.print(dr.r, 2);
    Serial.print(',');
    Serial.print(dr.vPlus, 5);
    Serial.print(',');
    Serial.print(dr.vMinus, 5);
    Serial.print(',');
    Serial.print(mrms::tapToString(controller.tap()));
    Serial.println(F(",OK"));

    // Advance to next step
    sweep.vCurrent += cfg.vStep;
    ++sweep.step;

    if (sweep.vCurrent > cfg.vStop + 1e-4f) {
      // All steps done
      controller.setSweeping(false);
      Serial.println(F("SWEEP:COMPLETED"));
    } else {
      sweep.phase      = SweepState::Phase::SetVoltage;
      sweep.phaseStart = now;
    }
    break;
  }
  }
}

// ─── Command dispatch ─────────────────────────────────────────────────────────

static void handleCommand(const mrms::ParsedCommand &pc) {
  using CT = mrms::CommandType;

  switch (pc.type) {

  case CT::None:
    return;

  case CT::Idn: {
    char buf[48];
    strncpy_P(buf, kIdentity, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    Serial.println(buf);
    return;
  }

  case CT::Rst:
    setMagnetVoltage(0.0f);
    openAllTaps();
    controller.setTap(mrms::TapValue::Off);
    disableSampleCurrent();
    controller.setSweeping(false);
    sweepAbortRequested = false;
    // resetToDefaults() already called inside processCommand
    return;

  case CT::Opc:
    Serial.println('1');
    return;

  case CT::SourMagnVoltSet:
    setMagnetVoltage(pc.floatParam);
    return;

  case CT::SourMagnVoltGet:
    Serial.println(controller.magnetVoltage(), 1);
    return;

  case CT::SourSampCurrSet:
    // sampleCurrent_ already updated by processCommand
    return;

  case CT::SourSampCurrGet:
    Serial.println(controller.sampleCurrent(), 1);
    return;

  case CT::SourSampPulsWidtSet:
    // pulseWidth_ already updated by processCommand
    return;

  case CT::SourSampTapSet:
    if (!setTapRelay(pc.tapParam)) {
      Serial.println(F("-310,\"Relay interlocking error\""));
    }
    return;

  case CT::SourSampTapGet:
    Serial.println(mrms::tapToString(controller.tap()));
    return;

  case CT::SourSampTapAutop:
    doAutoPickTap();
    return;

  case CT::SensMagnStabThrSet:
  case CT::SensMagnStabCounSet:
  case CT::SensMagnStabTimSet:
    // State already updated by processCommand; no response
    return;

  case CT::MeasMagnFiel:
    Serial.println(readHallTesla(), 4);
    return;

  case CT::MeasSampTest: {
    TestPulseResult tp = doTestPulse();
    Serial.print(tp.rApprox, 1);
    Serial.print(',');
    Serial.print(tp.vMeas, 3);
    Serial.print(',');
    Serial.println(tp.iMeas, 4);
    return;
  }

  case CT::MeasSampRes: {
    DeltaResult dr = doDeltaMeasure();
    Serial.print(dr.r, 2);
    Serial.print(',');
    Serial.print(dr.vPlus, 5);
    Serial.print(',');
    Serial.println(dr.vMinus, 5);
    return;
  }

  case CT::SweepConf:
    // sweepConfig_ already updated by processCommand; no response
    return;

  case CT::SweepInit:
    if (!controller.isSweeping()) {
      sweepStart();
    }
    return;

  case CT::SweepAbor:
    if (controller.isSweeping()) {
      sweepAbortRequested = true;
    } else {
      Serial.println(F("SWEEP:ABORTED"));
    }
    return;

  case CT::SystErr: {
    mrms::ErrorCode code = controller.errorQueue().pop();
    Serial.print(static_cast<int16_t>(code));
    Serial.print(',');
    switch (code) {
    case mrms::ErrorCode::None:           Serial.println(F("\"No error\""));               break;
    case mrms::ErrorCode::Command:        Serial.println(F("\"Command error\""));           break;
    case mrms::ErrorCode::DataOutOfRange: Serial.println(F("\"Data out of range\""));       break;
    case mrms::ErrorCode::StabTimeout:    Serial.println(F("\"Stabilization timeout\""));   break;
    case mrms::ErrorCode::RelayInterlock: Serial.println(F("\"Relay interlocking error\"")); break;
    case mrms::ErrorCode::Compliance:     Serial.println(F("\"Compliance voltage exceeded\"")); break;
    default:                              Serial.println(F("\"Unknown error\""));           break;
    }
    return;
  }

  case CT::CommandError:
    // Error already pushed to queue by processCommand
    return;

  case CT::TooLong:
    return;
  }
}

// ─── Serial reader ────────────────────────────────────────────────────────────

static void readSerialCommands() {
  while (Serial.available() > 0) {
    char c = static_cast<char>(Serial.read());
    mrms::ParsedCommand pc = controller.feedChar(c);
    if (pc.type != mrms::CommandType::None) {
      handleCommand(pc);
    }
  }
}

// ─── setup / loop ────────────────────────────────────────────────────────────

void setup() {
  // Magnet PSU PWM
  pinMode(MAGNET_PWM_PIN, OUTPUT);
  analogWrite(MAGNET_PWM_PIN, 0);

  // Sample current source
  pinMode(SAMPLE_EN_PIN,  OUTPUT);
  pinMode(SAMPLE_POL_PIN, OUTPUT);
  digitalWrite(SAMPLE_EN_PIN,  LOW);
  digitalWrite(SAMPLE_POL_PIN, HIGH);

  // Battery tap relays
  for (uint8_t i = 0; i < 8; ++i) {
    pinMode(TAP_PINS[i], OUTPUT);
    digitalWrite(TAP_PINS[i], LOW);
  }

  Serial.begin(UART_BAUD);
  uint32_t start = millis();
  while (!Serial && (millis() - start < 2500)) {
    delay(10);
  }

  if (!ads.begin()) {
    Serial.println(F("ERR ADS1115_NOT_FOUND"));
  } else {
    ads.setGain(GAIN_ONE);
  }

  Serial.println(F("MRMS ready. Send *IDN? to identify."));
}

void loop() {
  readSerialCommands();
  sweepTick();
}