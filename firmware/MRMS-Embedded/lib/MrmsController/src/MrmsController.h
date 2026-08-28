#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace mrms {

// ── Error codes (IEEE 488.2 / SCPI subset) ─────────────────────────────────
enum class ErrorCode : int16_t {
  None           =    0,
  Command        = -100,
  DataOutOfRange = -222,
  StabTimeout    = -300,
  RelayInterlock = -310,
  Compliance     = -320,
};

// ── Battery tap selection ────────────────────────────────────────────────────
enum class TapValue : uint8_t {
  Off = 0, T2S, T4S, T6S, T8S, T10S, T12S, T14S, T16S,
};

const char *tapToString(TapValue t);
TapValue    tapFromString(const char *s); // "OFF","2S"…"16S"; Off on error

// Nominal battery voltages for each tap
float tapNominalVoltage(TapValue t);

// ── Command type ──────────────────────────────────────────────────────────────
enum class CommandType : uint8_t {
  None = 0,
  // IEEE 488.2
  Idn, Rst, Opc,
  // SOURce
  SourMagnVoltSet, SourMagnVoltGet,
  SourSampCurrSet, SourSampCurrGet,
  SourSampPulsWidtSet,
  SourSampTapSet, SourSampTapGet,
  SourSampTapAutop,
  // SENSe
  SensMagnStabThrSet,
  SensMagnStabCounSet,
  SensMagnStabTimSet,
  // MEASure
  MeasMagnFiel,
  MeasSampTest,
  MeasSampRes,
  // SWEEp
  SweepConf, SweepInit, SweepAbor,
  // SYSTem
  SystErr,
  // Parser errors
  CommandError,
  TooLong,
};

// ── Sweep configuration ──────────────────────────────────────────────────────
struct SweepConfig {
  float vStart{0.0f};
  float vStop{60.0f};
  float vStep{10.0f};
};

// ── Parsed command ────────────────────────────────────────────────────────────
struct ParsedCommand {
  CommandType type{CommandType::None};
  float       floatParam{0.0f};
  int32_t     intParam{0};
  TapValue    tapParam{TapValue::Off};
  SweepConfig sweepParams{};
};

// ── Small FIFO error queue (capacity = 8) ────────────────────────────────────
class ErrorQueue {
public:
  static constexpr uint8_t kCapacity = 8;

  void      push(ErrorCode code);
  ErrorCode pop();
  bool      empty() const { return count_ == 0; }

private:
  ErrorCode buf_[kCapacity]{};
  uint8_t   head_{0};
  uint8_t   count_{0};
};

// ── SCPI controller ───────────────────────────────────────────────────────────
class Controller {
public:
  static constexpr std::size_t kRxBufferSize = 128;

  Controller();

  // ── State getters ──────────────────────────────────────────────────────────
  float       magnetVoltage() const { return magnetVoltage_; }
  float       sampleCurrent() const { return sampleCurrent_; }
  int16_t     pulseWidth()    const { return pulseWidth_; }
  TapValue    tap()           const { return tap_; }
  float       stabThreshold() const { return stabThreshold_; }
  uint8_t     stabCount()     const { return stabCount_; }
  uint16_t    stabTimeout()   const { return stabTimeout_; }
  SweepConfig sweepConfig()   const { return sweepConfig_; }
  ErrorQueue &errorQueue()          { return errorQueue_; }
  bool        isSweeping()    const { return sweeping_; }

  // ── State setters (called from main after hardware operations) ─────────────
  void setMagnetVoltage(float v) { magnetVoltage_ = v; }
  void setTap(TapValue t)        { tap_ = t; }
  void setSweeping(bool s)       { sweeping_ = s; }
  void resetToDefaults();

  // ── Parsing ────────────────────────────────────────────────────────────────
  /** Normalise a SCPI command mnemonic to canonical upper-case short form.
   *  Only the mnemonic is returned; parameters are stripped. */
  static std::string normalizeCommand(const std::string &input);

  /** Parse a complete command line and update internal state. */
  ParsedCommand processCommand(const std::string &input);

  /** Feed one character; returns a ParsedCommand when a line is complete. */
  ParsedCommand feedChar(char c);

private:
  float       magnetVoltage_{0.0f};
  float       sampleCurrent_{10.0f};
  int16_t     pulseWidth_{150};
  TapValue    tap_{TapValue::Off};
  float       stabThreshold_{0.05f};
  uint8_t     stabCount_{5};
  uint16_t    stabTimeout_{2000};
  SweepConfig sweepConfig_{};
  bool        sweeping_{false};

  ErrorQueue  errorQueue_;
  char        rxBuffer_[kRxBufferSize];
  std::size_t rxIndex_;

  static std::string normalizeToken(const std::string &token);
  static std::string normalizeCmdPart(const std::string &upper);
};

} // namespace mrms
