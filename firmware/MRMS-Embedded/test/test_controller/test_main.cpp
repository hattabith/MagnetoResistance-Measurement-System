#include <unity.h>

#include "MrmsController.h"

using mrms::Controller;
using mrms::EventType;

void test_normalize_command_trims_and_uppercases() {
  const std::string value = Controller::normalizeCommand("  relay on  ");
  TEST_ASSERT_EQUAL_STRING("RELAY ON", value.c_str());
}

void test_relay_commands_change_state() {
  Controller controller;

  auto event = controller.processCommand("ON");
  TEST_ASSERT_EQUAL(EventType::Status, event.type);
  TEST_ASSERT_TRUE(event.relayStateChanged);
  TEST_ASSERT_TRUE(controller.relayState());

  event = controller.processCommand("RELAY OFF");
  TEST_ASSERT_EQUAL(EventType::Status, event.type);
  TEST_ASSERT_TRUE(event.relayStateChanged);
  TEST_ASSERT_FALSE(controller.relayState());

  event = controller.processCommand("TOGGLE");
  TEST_ASSERT_EQUAL(EventType::Status, event.type);
  TEST_ASSERT_TRUE(controller.relayState());
}

void test_status_and_read_do_not_change_state() {
  Controller controller;

  controller.processCommand("ON");

  auto event = controller.processCommand("STATUS");
  TEST_ASSERT_EQUAL(EventType::Status, event.type);
  TEST_ASSERT_FALSE(event.relayStateChanged);
  TEST_ASSERT_TRUE(event.relayState);

  event = controller.processCommand("READ");
  TEST_ASSERT_EQUAL(EventType::Status, event.type);
  TEST_ASSERT_FALSE(event.relayStateChanged);
  TEST_ASSERT_TRUE(event.relayState);
}

void test_help_and_unknown_command() {
  Controller controller;

  auto event = controller.processCommand("help");
  TEST_ASSERT_EQUAL(EventType::Help, event.type);

  event = controller.processCommand("something_else");
  TEST_ASSERT_EQUAL(EventType::UnknownCommand, event.type);
  TEST_ASSERT_EQUAL_STRING("something_else", event.commandText.c_str());
}

void test_feed_char_builds_line_and_processes() {
  Controller controller;

  TEST_ASSERT_EQUAL(EventType::None, controller.feedChar('O').type);
  TEST_ASSERT_EQUAL(EventType::None, controller.feedChar('N').type);
  auto event = controller.feedChar('\n');

  TEST_ASSERT_EQUAL(EventType::Status, event.type);
  TEST_ASSERT_TRUE(controller.relayState());
}

void test_feed_char_ignores_carriage_return() {
  Controller controller;

  auto event = controller.feedChar('\r');
  TEST_ASSERT_EQUAL(EventType::None, event.type);
}

void test_feed_char_reports_command_too_long_and_recovers() {
  Controller controller;

  for (std::size_t i = 0; i < Controller::kRxBufferSize; ++i) {
    auto event = controller.feedChar('A');
    if (i == Controller::kRxBufferSize - 1) {
      TEST_ASSERT_EQUAL(EventType::CommandTooLong, event.type);
    } else {
      TEST_ASSERT_EQUAL(EventType::None, event.type);
    }
  }

  TEST_ASSERT_EQUAL(EventType::None, controller.feedChar('O').type);
  TEST_ASSERT_EQUAL(EventType::None, controller.feedChar('F').type);
  TEST_ASSERT_EQUAL(EventType::None, controller.feedChar('F').type);
  auto event = controller.feedChar('\n');

  TEST_ASSERT_EQUAL(EventType::Status, event.type);
  TEST_ASSERT_FALSE(controller.relayState());
}

int main(int argc, char **argv) {
  UNITY_BEGIN();

  RUN_TEST(test_normalize_command_trims_and_uppercases);
  RUN_TEST(test_relay_commands_change_state);
  RUN_TEST(test_status_and_read_do_not_change_state);
  RUN_TEST(test_help_and_unknown_command);
  RUN_TEST(test_feed_char_builds_line_and_processes);
  RUN_TEST(test_feed_char_ignores_carriage_return);
  RUN_TEST(test_feed_char_reports_command_too_long_and_recovers);

  return UNITY_END();
}
