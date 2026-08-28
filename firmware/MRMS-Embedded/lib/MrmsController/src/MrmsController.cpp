#include "MrmsController.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace mrms {

// ═══════════════════════════════════════════════════════════════════════════════
// Internal helpers
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

std::string trimCopy(const std::string &input) {
  std::size_t first = 0;
  while (first < input.size() &&
         std::isspace(static_cast<unsigned char>(input[first])) != 0) {
    ++first;
  }
  std::size_t last = input.size();
  while (last > first &&
         std::isspace(static_cast<unsigned char>(input[last - 1])) != 0) {
    --last;
  }
  return input.substr(first, last - first);
}

std::string toUpper(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
  return s;
}

// SCPI token short/long pairs (short form = minimum required abbreviation).
// Convention: token is valid if it starts with shortForm and is a prefix of
// longForm, i.e.  shortForm <= token <= longForm  (by length).
struct TokenPair {
  const char *shortForm;
  const char *longForm;
};

static const TokenPair kTokenPairs[] = {
  {"SOUR", "SOURCE"},     {"MAGN", "MAGNET"},     {"VOLT", "VOLTAGE"},
  {"SAMP", "SAMPLE"},     {"CURR", "CURRENT"},     {"PULS", "PULSE"},
  {"WIDT", "WIDTH"},      {"AUTOP", "AUTOPICK"},
  {"SENS", "SENSE"},      {"STAB", "STABILITY"},
  {"THR", "THRESHOLD"},   {"COUN", "COUNT"},        {"TIM", "TIMEOUT"},
  {"MEAS", "MEASURE"},    {"FIEL", "FIELD"},         {"TEST", "TESTPULSE"},
  {"RES", "RESISTANCE"},
  {"SWEE", "SWEEP"},      {"CONF", "CONFIGURE"},   {"INIT", "INITIATE"},
  {"ABOR", "ABORT"},
  {"SYST", "SYSTEM"},     {"ERR", "ERROR"},
  {nullptr, nullptr}
};

} // namespace

// ═══════════════════════════════════════════════════════════════════════════════
// TapValue helpers
// ═══════════════════════════════════════════════════════════════════════════════

const char *tapToString(TapValue t) {
  switch (t) {
  case TapValue::T2S:  return "2S";
  case TapValue::T4S:  return "4S";
  case TapValue::T6S:  return "6S";
  case TapValue::T8S:  return "8S";
  case TapValue::T10S: return "10S";
  case TapValue::T12S: return "12S";
  case TapValue::T14S: return "14S";
  case TapValue::T16S: return "16S";
  default:             return "OFF";
  }
}

TapValue tapFromString(const char *s) {
  if (std::strcmp(s, "2S")  == 0) return TapValue::T2S;
  if (std::strcmp(s, "4S")  == 0) return TapValue::T4S;
  if (std::strcmp(s, "6S")  == 0) return TapValue::T6S;
  if (std::strcmp(s, "8S")  == 0) return TapValue::T8S;
  if (std::strcmp(s, "10S") == 0) return TapValue::T10S;
  if (std::strcmp(s, "12S") == 0) return TapValue::T12S;
  if (std::strcmp(s, "14S") == 0) return TapValue::T14S;
  if (std::strcmp(s, "16S") == 0) return TapValue::T16S;
  return TapValue::Off;
}

float tapNominalVoltage(TapValue t) {
  switch (t) {
  case TapValue::T2S:  return  8.4f;
  case TapValue::T4S:  return 16.8f;
  case TapValue::T6S:  return 25.2f;
  case TapValue::T8S:  return 33.6f;
  case TapValue::T10S: return 42.0f;
  case TapValue::T12S: return 50.4f;
  case TapValue::T14S: return 58.8f;
  case TapValue::T16S: return 67.2f;
  default:             return  0.0f;
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
// ErrorQueue
// ═══════════════════════════════════════════════════════════════════════════════

void ErrorQueue::push(ErrorCode code) {
  if (count_ == kCapacity) {
    return; // drop oldest (overflow silently)
  }
  buf_[(head_ + count_) % kCapacity] = code;
  ++count_;
}

ErrorCode ErrorQueue::pop() {
  if (count_ == 0) {
    return ErrorCode::None;
  }
  ErrorCode code = buf_[head_];
  head_ = (head_ + 1) % kCapacity;
  --count_;
  return code;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Controller
// ═══════════════════════════════════════════════════════════════════════════════

Controller::Controller() : rxBuffer_{0}, rxIndex_{0} {}

void Controller::resetToDefaults() {
  magnetVoltage_ = 0.0f;
  sampleCurrent_ = 10.0f;
  pulseWidth_    = 150;
  tap_           = TapValue::Off;
  stabThreshold_ = 0.05f;
  stabCount_     = 5;
  stabTimeout_   = 2000;
  sweepConfig_   = {};
  sweeping_      = false;
}

// ── Token normalisation ───────────────────────────────────────────────────────

std::string Controller::normalizeToken(const std::string &token) {
  for (const TokenPair *p = kTokenPairs; p->shortForm != nullptr; ++p) {
    std::size_t sLen = std::strlen(p->shortForm);
    std::size_t lLen = std::strlen(p->longForm);
    if (token.size() >= sLen && token.size() <= lLen &&
        token.compare(0, sLen, p->shortForm) == 0 &&
        std::strncmp(token.c_str(), p->longForm, token.size()) == 0) {
      return std::string(p->shortForm);
    }
  }
  return token;
}

// Normalise only the command mnemonic (everything before any space / without
// parameters), already converted to upper-case.
std::string Controller::normalizeCmdPart(const std::string &upper) {
  // Special IEEE commands starting with '*' are returned as-is.
  if (!upper.empty() && upper[0] == '*') {
    return upper;
  }

  std::string result;
  std::size_t start = 0;

  while (true) {
    std::size_t colonPos = upper.find(':', start);
    std::string seg = (colonPos == std::string::npos)
                          ? upper.substr(start)
                          : upper.substr(start, colonPos - start);

    // Preserve trailing '?' on the last segment
    bool query = (!seg.empty() && seg.back() == '?');
    if (query) {
      seg.pop_back();
    }

    seg = normalizeToken(seg);

    if (!result.empty()) {
      result += ':';
    }
    result += seg;
    if (query) {
      result += '?';
    }

    if (colonPos == std::string::npos) {
      break;
    }
    start = colonPos + 1;
  }

  return result;
}

std::string Controller::normalizeCommand(const std::string &input) {
  std::string s = toUpper(trimCopy(input));
  // Strip parameter part (everything from first space)
  std::size_t sp = s.find(' ');
  std::string cmd = (sp == std::string::npos) ? s : s.substr(0, sp);
  return normalizeCmdPart(cmd);
}

// ── Command processing ────────────────────────────────────────────────────────

ParsedCommand Controller::processCommand(const std::string &input) {
  std::string s = toUpper(trimCopy(input));

  // Split mnemonic and parameters
  std::size_t sp = s.find(' ');
  std::string cmdRaw   = (sp == std::string::npos) ? s : s.substr(0, sp);
  std::string paramStr = (sp == std::string::npos) ? "" : trimCopy(s.substr(sp + 1));

  std::string cmd = normalizeCmdPart(cmdRaw);

  ParsedCommand pc{};

  if (cmd.empty()) {
    return pc;
  }

  // ── IEEE 488.2 ─────────────────────────────────────────────────────────────
  if (cmd == "*IDN?") { pc.type = CommandType::Idn;  return pc; }
  if (cmd == "*RST")  { pc.type = CommandType::Rst; resetToDefaults(); return pc; }
  if (cmd == "*OPC?") { pc.type = CommandType::Opc;  return pc; }

  // ── SOURce:MAGNet:VOLTage ──────────────────────────────────────────────────
  if (cmd == "SOUR:MAGN:VOLT?") {
    pc.type = CommandType::SourMagnVoltGet;
    return pc;
  }
  if (cmd == "SOUR:MAGN:VOLT") {
    if (paramStr.empty()) {
      errorQueue_.push(ErrorCode::Command);
      pc.type = CommandType::CommandError;
      return pc;
    }
    float v = std::strtof(paramStr.c_str(), nullptr);
    if (v < 0.0f || v > 60.0f) {
      errorQueue_.push(ErrorCode::DataOutOfRange);
      pc.type = CommandType::CommandError;
      return pc;
    }
    magnetVoltage_ = v;
    pc.type        = CommandType::SourMagnVoltSet;
    pc.floatParam  = v;
    return pc;
  }

  // ── SOURce:SAMPle:CURRent ─────────────────────────────────────────────────
  if (cmd == "SOUR:SAMP:CURR?") {
    pc.type = CommandType::SourSampCurrGet;
    return pc;
  }
  if (cmd == "SOUR:SAMP:CURR") {
    if (paramStr.empty()) {
      errorQueue_.push(ErrorCode::Command);
      pc.type = CommandType::CommandError;
      return pc;
    }
    float v = std::strtof(paramStr.c_str(), nullptr);
    if (v < 0.1f || v > 50.0f) {
      errorQueue_.push(ErrorCode::DataOutOfRange);
      pc.type = CommandType::CommandError;
      return pc;
    }
    sampleCurrent_ = v;
    pc.type        = CommandType::SourSampCurrSet;
    pc.floatParam  = v;
    return pc;
  }

  // ── SOURce:SAMPle:PULSe:WIDTh ────────────────────────────────────────────
  if (cmd == "SOUR:SAMP:PULS:WIDT") {
    if (paramStr.empty()) {
      errorQueue_.push(ErrorCode::Command);
      pc.type = CommandType::CommandError;
      return pc;
    }
    long v = std::strtol(paramStr.c_str(), nullptr, 10);
    if (v < 10 || v > 200) {
      errorQueue_.push(ErrorCode::DataOutOfRange);
      pc.type = CommandType::CommandError;
      return pc;
    }
    pulseWidth_ = static_cast<int16_t>(v);
    pc.type     = CommandType::SourSampPulsWidtSet;
    pc.intParam = static_cast<int32_t>(v);
    return pc;
  }

  // ── SOURce:SAMPle:TAP:AUTOPick ───────────────────────────────────────────
  if (cmd == "SOUR:SAMP:TAP:AUTOP") {
    pc.type = CommandType::SourSampTapAutop;
    return pc;
  }

  // ── SOURce:SAMPle:TAP ─────────────────────────────────────────────────────
  if (cmd == "SOUR:SAMP:TAP?") {
    pc.type = CommandType::SourSampTapGet;
    return pc;
  }
  if (cmd == "SOUR:SAMP:TAP") {
    if (paramStr.empty()) {
      errorQueue_.push(ErrorCode::Command);
      pc.type = CommandType::CommandError;
      return pc;
    }
    if (paramStr == "OFF") {
      tap_         = TapValue::Off;
      pc.type      = CommandType::SourSampTapSet;
      pc.tapParam  = TapValue::Off;
      return pc;
    }
    TapValue t = tapFromString(paramStr.c_str());
    if (t == TapValue::Off && paramStr != "OFF") {
      errorQueue_.push(ErrorCode::DataOutOfRange);
      pc.type = CommandType::CommandError;
      return pc;
    }
    tap_        = t;
    pc.type     = CommandType::SourSampTapSet;
    pc.tapParam = t;
    return pc;
  }

  // ── SENSe:MAGNet:STABility ────────────────────────────────────────────────
  if (cmd == "SENS:MAGN:STAB:THR") {
    if (paramStr.empty()) {
      errorQueue_.push(ErrorCode::Command);
      pc.type = CommandType::CommandError;
      return pc;
    }
    float v = std::strtof(paramStr.c_str(), nullptr);
    if (v <= 0.0f) {
      errorQueue_.push(ErrorCode::DataOutOfRange);
      pc.type = CommandType::CommandError;
      return pc;
    }
    stabThreshold_ = v;
    pc.type        = CommandType::SensMagnStabThrSet;
    pc.floatParam  = v;
    return pc;
  }
  if (cmd == "SENS:MAGN:STAB:COUN") {
    if (paramStr.empty()) {
      errorQueue_.push(ErrorCode::Command);
      pc.type = CommandType::CommandError;
      return pc;
    }
    long v = std::strtol(paramStr.c_str(), nullptr, 10);
    if (v < 1 || v > 255) {
      errorQueue_.push(ErrorCode::DataOutOfRange);
      pc.type = CommandType::CommandError;
      return pc;
    }
    stabCount_ = static_cast<uint8_t>(v);
    pc.type    = CommandType::SensMagnStabCounSet;
    pc.intParam = static_cast<int32_t>(v);
    return pc;
  }
  if (cmd == "SENS:MAGN:STAB:TIM") {
    if (paramStr.empty()) {
      errorQueue_.push(ErrorCode::Command);
      pc.type = CommandType::CommandError;
      return pc;
    }
    long v = std::strtol(paramStr.c_str(), nullptr, 10);
    if (v < 100 || v > 10000) {
      errorQueue_.push(ErrorCode::DataOutOfRange);
      pc.type = CommandType::CommandError;
      return pc;
    }
    stabTimeout_ = static_cast<uint16_t>(v);
    pc.type      = CommandType::SensMagnStabTimSet;
    pc.intParam  = static_cast<int32_t>(v);
    return pc;
  }

  // ── MEASure ───────────────────────────────────────────────────────────────
  if (cmd == "MEAS:MAGN:FIEL?") { pc.type = CommandType::MeasMagnFiel; return pc; }
  if (cmd == "MEAS:SAMP:TEST?") { pc.type = CommandType::MeasSampTest; return pc; }
  if (cmd == "MEAS:SAMP:RES?")  { pc.type = CommandType::MeasSampRes;  return pc; }

  // ── SWEEp ─────────────────────────────────────────────────────────────────
  if (cmd == "SWEE:CONF") {
    // paramStr = "V_start,V_stop,V_step"
    float vals[3] = {0.0f, 60.0f, 10.0f};
    const char *p = paramStr.c_str();
    for (int i = 0; i < 3 && p != nullptr && *p != '\0'; ++i) {
      char *end = nullptr;
      vals[i] = std::strtof(p, &end);
      p       = (end && *end == ',') ? end + 1 : nullptr;
    }
    if (vals[2] <= 0.0f || vals[0] > 60.0f || vals[1] > 60.0f) {
      errorQueue_.push(ErrorCode::DataOutOfRange);
      pc.type = CommandType::CommandError;
      return pc;
    }
    sweepConfig_.vStart      = vals[0];
    sweepConfig_.vStop       = vals[1];
    sweepConfig_.vStep       = vals[2];
    pc.type                  = CommandType::SweepConf;
    pc.sweepParams.vStart    = vals[0];
    pc.sweepParams.vStop     = vals[1];
    pc.sweepParams.vStep     = vals[2];
    return pc;
  }
  if (cmd == "SWEE:INIT") {
    pc.type = CommandType::SweepInit;
    return pc;
  }
  if (cmd == "SWEE:ABOR") {
    pc.type = CommandType::SweepAbor;
    return pc;
  }

  // ── SYSTem:ERRor? ─────────────────────────────────────────────────────────
  if (cmd == "SYST:ERR?") { pc.type = CommandType::SystErr; return pc; }

  // ── Unknown command ────────────────────────────────────────────────────────
  errorQueue_.push(ErrorCode::Command);
  pc.type = CommandType::CommandError;
  return pc;
}

// ── feedChar ──────────────────────────────────────────────────────────────────

ParsedCommand Controller::feedChar(char c) {
  if (c == '\r') {
    return ParsedCommand{};
  }

  if (c == '\n') {
    rxBuffer_[rxIndex_] = '\0';
    ParsedCommand pc = processCommand(std::string(rxBuffer_));
    rxIndex_ = 0;
    return pc;
  }

  if (rxIndex_ < kRxBufferSize - 1) {
    rxBuffer_[rxIndex_++] = c;
    return ParsedCommand{};
  }

  // Buffer overflow
  rxIndex_ = 0;
  errorQueue_.push(ErrorCode::Command);
  ParsedCommand pc{};
  pc.type = CommandType::TooLong;
  return pc;
}

} // namespace mrms
