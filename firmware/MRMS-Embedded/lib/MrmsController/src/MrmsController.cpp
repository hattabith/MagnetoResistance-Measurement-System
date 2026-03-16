#include "MrmsController.h"

#include <algorithm>
#include <cctype>

namespace mrms {

namespace {

std::string trimCopy(const std::string &input) {
  std::size_t first = 0;
  while (first < input.size() && std::isspace(static_cast<unsigned char>(input[first])) != 0) {
    ++first;
  }

  std::size_t last = input.size();
  while (last > first && std::isspace(static_cast<unsigned char>(input[last - 1])) != 0) {
    --last;
  }

  return input.substr(first, last - first);
}

} // namespace

Controller::Controller() : relayState_(false), rxBuffer_{0}, rxIndex_(0) {}

bool Controller::relayState() const {
  return relayState_;
}

std::string Controller::normalizeCommand(std::string input) {
  std::string normalized = trimCopy(input);
  std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
    return static_cast<char>(std::toupper(ch));
  });
  return normalized;
}

Event Controller::processCommand(const std::string &input) {
  const std::string cmd = normalizeCommand(input);
  Event event{};
  event.relayState = relayState_;

  if (cmd.empty()) {
    return event;
  }

  if (cmd == "HELP") {
    event.type = EventType::Help;
    return event;
  }

  if (cmd == "RELAY ON" || cmd == "ON") {
    relayState_ = true;
    event.type = EventType::Status;
    event.relayState = relayState_;
    event.relayStateChanged = true;
    return event;
  }

  if (cmd == "RELAY OFF" || cmd == "OFF") {
    relayState_ = false;
    event.type = EventType::Status;
    event.relayState = relayState_;
    event.relayStateChanged = true;
    return event;
  }

  if (cmd == "RELAY TOGGLE" || cmd == "TOGGLE") {
    relayState_ = !relayState_;
    event.type = EventType::Status;
    event.relayState = relayState_;
    event.relayStateChanged = true;
    return event;
  }

  if (cmd == "STATUS" || cmd == "READ") {
    event.type = EventType::Status;
    event.relayState = relayState_;
    return event;
  }

  event.type = EventType::UnknownCommand;
  event.commandText = input;
  return event;
}

Event Controller::feedChar(char c) {
  if (c == '\r') {
    return Event{};
  }

  if (c == '\n') {
    rxBuffer_[rxIndex_] = '\0';
    Event event = processCommand(std::string(rxBuffer_));
    rxIndex_ = 0;
    return event;
  }

  if (rxIndex_ < kRxBufferSize - 1) {
    rxBuffer_[rxIndex_++] = c;
    return Event{};
  }

  rxIndex_ = 0;
  Event event{};
  event.type = EventType::CommandTooLong;
  event.relayState = relayState_;
  return event;
}

} // namespace mrms
