#include <unity.h>

#include "MrmsController.h"

using mrms::CommandType;
using mrms::Controller;
using mrms::ErrorCode;
using mrms::TapValue;

// ─── normalizeCommand ────────────────────────────────────────────────────────

void test_normalize_ieee_commands() {
  TEST_ASSERT_EQUAL_STRING("*IDN?", Controller::normalizeCommand("*IDN?").c_str());
  TEST_ASSERT_EQUAL_STRING("*RST",  Controller::normalizeCommand("*RST").c_str());
  TEST_ASSERT_EQUAL_STRING("*OPC?", Controller::normalizeCommand("*opc?").c_str());
}

void test_normalize_short_forms_unchanged() {
  TEST_ASSERT_EQUAL_STRING("SOUR:MAGN:VOLT?",
                            Controller::normalizeCommand("SOUR:MAGN:VOLT?").c_str());
  TEST_ASSERT_EQUAL_STRING("SENS:MAGN:STAB:THR",
                            Controller::normalizeCommand("SENS:MAGN:STAB:THR").c_str());
  TEST_ASSERT_EQUAL_STRING("SWEE:INIT",
                            Controller::normalizeCommand("SWEE:INIT").c_str());
}

void test_normalize_long_forms_to_short() {
  TEST_ASSERT_EQUAL_STRING("SOUR:MAGN:VOLT",
                            Controller::normalizeCommand("SOURCE:MAGNET:VOLTAGE 20.0").c_str());
  TEST_ASSERT_EQUAL_STRING("SOUR:SAMP:CURR",
                            Controller::normalizeCommand("source:sample:current 10").c_str());
  TEST_ASSERT_EQUAL_STRING("SOUR:SAMP:PULS:WIDT",
                            Controller::normalizeCommand("SOURCE:SAMPLE:PULSE:WIDTH 100").c_str());
  TEST_ASSERT_EQUAL_STRING("SOUR:SAMP:TAP:AUTOP",
                            Controller::normalizeCommand("SOURCE:SAMPLE:TAP:AUTOPICK").c_str());
  TEST_ASSERT_EQUAL_STRING("SENS:MAGN:STAB:THR",
                            Controller::normalizeCommand("SENSE:MAGNET:STABILITY:THRESHOLD 0.1").c_str());
  TEST_ASSERT_EQUAL_STRING("MEAS:MAGN:FIEL?",
                            Controller::normalizeCommand("MEASURE:MAGNET:FIELD?").c_str());
  TEST_ASSERT_EQUAL_STRING("SWEE:CONF",
                            Controller::normalizeCommand("SWEEP:CONFIGURE 0,60,10").c_str());
  TEST_ASSERT_EQUAL_STRING("SYST:ERR?",
                            Controller::normalizeCommand("SYSTEM:ERROR?").c_str());
}

void test_normalize_strips_leading_trailing_whitespace() {
  TEST_ASSERT_EQUAL_STRING("*IDN?", Controller::normalizeCommand("  *IDN?  ").c_str());
  TEST_ASSERT_EQUAL_STRING("SWEE:ABOR",
                            Controller::normalizeCommand("  swee:abor  ").c_str());
}

// ─── IEEE 488.2 ──────────────────────────────────────────────────────────────

void test_idn_query() {
  Controller c;
  auto pc = c.processCommand("*IDN?");
  TEST_ASSERT_EQUAL(CommandType::Idn, pc.type);
}

void test_rst_resets_defaults() {
  Controller c;
  c.processCommand("SOUR:MAGN:VOLT 30.0");
  c.processCommand("SOUR:SAMP:CURR 25.0");
  c.processCommand("*RST");
  TEST_ASSERT_EQUAL_FLOAT(0.0f, c.magnetVoltage());
  TEST_ASSERT_EQUAL_FLOAT(10.0f, c.sampleCurrent());
  TEST_ASSERT_EQUAL(TapValue::Off, c.tap());
}

void test_opc_query() {
  Controller c;
  auto pc = c.processCommand("*OPC?");
  TEST_ASSERT_EQUAL(CommandType::Opc, pc.type);
}

// ─── SOURce ──────────────────────────────────────────────────────────────────

void test_magnet_voltage_set_get() {
  Controller c;
  auto pc = c.processCommand("SOUR:MAGN:VOLT 20.0");
  TEST_ASSERT_EQUAL(CommandType::SourMagnVoltSet, pc.type);
  TEST_ASSERT_EQUAL_FLOAT(20.0f, pc.floatParam);
  TEST_ASSERT_EQUAL_FLOAT(20.0f, c.magnetVoltage());

  pc = c.processCommand("SOUR:MAGN:VOLT?");
  TEST_ASSERT_EQUAL(CommandType::SourMagnVoltGet, pc.type);
}

void test_magnet_voltage_out_of_range() {
  Controller c;
  auto pc = c.processCommand("SOUR:MAGN:VOLT 65.0");
  TEST_ASSERT_EQUAL(CommandType::CommandError, pc.type);
  TEST_ASSERT_EQUAL(ErrorCode::DataOutOfRange, c.errorQueue().pop());
}

void test_sample_current_set_get() {
  Controller c;
  auto pc = c.processCommand("SOUR:SAMP:CURR 10.0");
  TEST_ASSERT_EQUAL(CommandType::SourSampCurrSet, pc.type);
  TEST_ASSERT_EQUAL_FLOAT(10.0f, pc.floatParam);
  TEST_ASSERT_EQUAL_FLOAT(10.0f, c.sampleCurrent());

  pc = c.processCommand("SOUR:SAMP:CURR?");
  TEST_ASSERT_EQUAL(CommandType::SourSampCurrGet, pc.type);
}

void test_sample_current_out_of_range() {
  Controller c;
  auto pc = c.processCommand("SOUR:SAMP:CURR 0.0"); // below 0.1 minimum
  TEST_ASSERT_EQUAL(CommandType::CommandError, pc.type);
}

void test_pulse_width_set() {
  Controller c;
  auto pc = c.processCommand("SOUR:SAMP:PULS:WIDT 150");
  TEST_ASSERT_EQUAL(CommandType::SourSampPulsWidtSet, pc.type);
  TEST_ASSERT_EQUAL(150, pc.intParam);
  TEST_ASSERT_EQUAL(150, c.pulseWidth());
}

void test_pulse_width_out_of_range() {
  Controller c;
  auto pc = c.processCommand("SOUR:SAMP:PULS:WIDT 5"); // below 10
  TEST_ASSERT_EQUAL(CommandType::CommandError, pc.type);

  c.errorQueue().pop(); // clear
  pc = c.processCommand("SOUR:SAMP:PULS:WIDT 250"); // above 200
  TEST_ASSERT_EQUAL(CommandType::CommandError, pc.type);
}

void test_tap_set_get() {
  Controller c;
  auto pc = c.processCommand("SOUR:SAMP:TAP 6S");
  TEST_ASSERT_EQUAL(CommandType::SourSampTapSet, pc.type);
  TEST_ASSERT_EQUAL(TapValue::T6S, pc.tapParam);
  TEST_ASSERT_EQUAL(TapValue::T6S, c.tap());

  pc = c.processCommand("SOUR:SAMP:TAP?");
  TEST_ASSERT_EQUAL(CommandType::SourSampTapGet, pc.type);

  pc = c.processCommand("SOUR:SAMP:TAP OFF");
  TEST_ASSERT_EQUAL(CommandType::SourSampTapSet, pc.type);
  TEST_ASSERT_EQUAL(TapValue::Off, pc.tapParam);
}

void test_tap_invalid_value() {
  Controller c;
  auto pc = c.processCommand("SOUR:SAMP:TAP 3S"); // invalid
  TEST_ASSERT_EQUAL(CommandType::CommandError, pc.type);
  TEST_ASSERT_EQUAL(ErrorCode::DataOutOfRange, c.errorQueue().pop());
}

void test_tap_autopick_command() {
  Controller c;
  auto pc = c.processCommand("SOUR:SAMP:TAP:AUTOP");
  TEST_ASSERT_EQUAL(CommandType::SourSampTapAutop, pc.type);
}

// ─── SENSe ───────────────────────────────────────────────────────────────────

void test_stab_threshold_set() {
  Controller c;
  auto pc = c.processCommand("SENS:MAGN:STAB:THR 0.05");
  TEST_ASSERT_EQUAL(CommandType::SensMagnStabThrSet, pc.type);
  TEST_ASSERT_EQUAL_FLOAT(0.05f, pc.floatParam);
  TEST_ASSERT_EQUAL_FLOAT(0.05f, c.stabThreshold());
}

void test_stab_count_set() {
  Controller c;
  auto pc = c.processCommand("SENS:MAGN:STAB:COUN 5");
  TEST_ASSERT_EQUAL(CommandType::SensMagnStabCounSet, pc.type);
  TEST_ASSERT_EQUAL(5, c.stabCount());
}

void test_stab_timeout_set() {
  Controller c;
  auto pc = c.processCommand("SENS:MAGN:STAB:TIM 2000");
  TEST_ASSERT_EQUAL(CommandType::SensMagnStabTimSet, pc.type);
  TEST_ASSERT_EQUAL(2000, c.stabTimeout());
}

void test_stab_timeout_out_of_range() {
  Controller c;
  auto pc = c.processCommand("SENS:MAGN:STAB:TIM 50"); // below 100
  TEST_ASSERT_EQUAL(CommandType::CommandError, pc.type);
}

// ─── MEASure ─────────────────────────────────────────────────────────────────

void test_measure_commands() {
  Controller c;
  TEST_ASSERT_EQUAL(CommandType::MeasMagnFiel, c.processCommand("MEAS:MAGN:FIEL?").type);
  TEST_ASSERT_EQUAL(CommandType::MeasSampTest, c.processCommand("MEAS:SAMP:TEST?").type);
  TEST_ASSERT_EQUAL(CommandType::MeasSampRes,  c.processCommand("MEAS:SAMP:RES?").type);
}

// ─── SWEEp ───────────────────────────────────────────────────────────────────

void test_sweep_configure() {
  Controller c;
  auto pc = c.processCommand("SWEE:CONF 0.0,60.0,10.0");
  TEST_ASSERT_EQUAL(CommandType::SweepConf, pc.type);
  TEST_ASSERT_EQUAL_FLOAT(0.0f,  pc.sweepParams.vStart);
  TEST_ASSERT_EQUAL_FLOAT(60.0f, pc.sweepParams.vStop);
  TEST_ASSERT_EQUAL_FLOAT(10.0f, pc.sweepParams.vStep);
}

void test_sweep_init_abort_commands() {
  Controller c;
  TEST_ASSERT_EQUAL(CommandType::SweepInit, c.processCommand("SWEE:INIT").type);
  TEST_ASSERT_EQUAL(CommandType::SweepAbor, c.processCommand("SWEE:ABOR").type);
}

// ─── SYSTem ──────────────────────────────────────────────────────────────────

void test_system_error_empty_queue() {
  Controller c;
  auto pc = c.processCommand("SYST:ERR?");
  TEST_ASSERT_EQUAL(CommandType::SystErr, pc.type);
  TEST_ASSERT_EQUAL(ErrorCode::None, c.errorQueue().pop()); // should be empty
}

void test_system_error_queue_fifo() {
  Controller c;
  c.processCommand("SOUR:MAGN:VOLT 99.0"); // → DataOutOfRange
  c.processCommand("unknown_xyz");          // → Command error
  TEST_ASSERT_EQUAL(ErrorCode::DataOutOfRange, c.errorQueue().pop());
  TEST_ASSERT_EQUAL(ErrorCode::Command,        c.errorQueue().pop());
  TEST_ASSERT_TRUE(c.errorQueue().empty());
}

// ─── Unknown command ─────────────────────────────────────────────────────────

void test_unknown_command_pushes_error() {
  Controller c;
  auto pc = c.processCommand("GARBAGE:COMMAND");
  TEST_ASSERT_EQUAL(CommandType::CommandError, pc.type);
  TEST_ASSERT_EQUAL(ErrorCode::Command, c.errorQueue().pop());
}

// ─── feedChar ────────────────────────────────────────────────────────────────

void test_feed_char_builds_line_and_processes() {
  Controller c;
  const char *msg = "SOUR:MAGN:VOLT 30.0\n";
  mrms::ParsedCommand pc{};
  for (const char *p = msg; *p; ++p) {
    pc = c.feedChar(*p);
  }
  TEST_ASSERT_EQUAL(CommandType::SourMagnVoltSet, pc.type);
  TEST_ASSERT_EQUAL_FLOAT(30.0f, c.magnetVoltage());
}

void test_feed_char_crlf_line_ending() {
  Controller c;
  const char *msg = "*IDN?\r\n";
  mrms::ParsedCommand pc{};
  for (const char *p = msg; *p; ++p) {
    pc = c.feedChar(*p);
  }
  TEST_ASSERT_EQUAL(CommandType::Idn, pc.type);
}

void test_feed_char_too_long_reports_error() {
  Controller c;
  for (std::size_t i = 0; i < Controller::kRxBufferSize; ++i) {
    auto pc = c.feedChar('A');
    if (i == Controller::kRxBufferSize - 1) {
      TEST_ASSERT_EQUAL(CommandType::TooLong, pc.type);
    } else {
      TEST_ASSERT_EQUAL(CommandType::None, pc.type);
    }
  }
  // Buffer should reset; next short command should work
  const char *msg = "*RST\n";
  mrms::ParsedCommand pc{};
  for (const char *p = msg; *p; ++p) {
    pc = c.feedChar(*p);
  }
  TEST_ASSERT_EQUAL(CommandType::Rst, pc.type);
}

// ─── tapToString / tapFromString ─────────────────────────────────────────────

void test_tap_string_roundtrip() {
  using mrms::TapValue;
  TEST_ASSERT_EQUAL(TapValue::T2S,  mrms::tapFromString("2S"));
  TEST_ASSERT_EQUAL(TapValue::T16S, mrms::tapFromString("16S"));
  TEST_ASSERT_EQUAL(TapValue::Off,  mrms::tapFromString("OFF"));
  TEST_ASSERT_EQUAL(TapValue::Off,  mrms::tapFromString("invalid"));

  TEST_ASSERT_EQUAL_STRING("6S",  mrms::tapToString(TapValue::T6S));
  TEST_ASSERT_EQUAL_STRING("OFF", mrms::tapToString(TapValue::Off));
}

// ─── Main ────────────────────────────────────────────────────────────────────

int main(int /*argc*/, char ** /*argv*/) {
  UNITY_BEGIN();

  // normalizeCommand
  RUN_TEST(test_normalize_ieee_commands);
  RUN_TEST(test_normalize_short_forms_unchanged);
  RUN_TEST(test_normalize_long_forms_to_short);
  RUN_TEST(test_normalize_strips_leading_trailing_whitespace);

  // IEEE 488.2
  RUN_TEST(test_idn_query);
  RUN_TEST(test_rst_resets_defaults);
  RUN_TEST(test_opc_query);

  // SOURce
  RUN_TEST(test_magnet_voltage_set_get);
  RUN_TEST(test_magnet_voltage_out_of_range);
  RUN_TEST(test_sample_current_set_get);
  RUN_TEST(test_sample_current_out_of_range);
  RUN_TEST(test_pulse_width_set);
  RUN_TEST(test_pulse_width_out_of_range);
  RUN_TEST(test_tap_set_get);
  RUN_TEST(test_tap_invalid_value);
  RUN_TEST(test_tap_autopick_command);

  // SENSe
  RUN_TEST(test_stab_threshold_set);
  RUN_TEST(test_stab_count_set);
  RUN_TEST(test_stab_timeout_set);
  RUN_TEST(test_stab_timeout_out_of_range);

  // MEASure
  RUN_TEST(test_measure_commands);

  // SWEEp
  RUN_TEST(test_sweep_configure);
  RUN_TEST(test_sweep_init_abort_commands);

  // SYSTem
  RUN_TEST(test_system_error_empty_queue);
  RUN_TEST(test_system_error_queue_fifo);

  // Unknown
  RUN_TEST(test_unknown_command_pushes_error);

  // feedChar
  RUN_TEST(test_feed_char_builds_line_and_processes);
  RUN_TEST(test_feed_char_crlf_line_ending);
  RUN_TEST(test_feed_char_too_long_reports_error);

  // Helpers
  RUN_TEST(test_tap_string_roundtrip);

  return UNITY_END();
}
