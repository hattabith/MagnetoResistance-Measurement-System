#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace mrms {

enum class EventType {
  None,
  Help,
  Status,
  UnknownCommand,
  CommandTooLong,
};

struct Event {
  EventType type = EventType::None;
  bool relayState = false;
  bool relayStateChanged = false;
  std::string commandText;
};

class Controller {
public:
  static constexpr std::size_t kRxBufferSize = 64;

  Controller();

  bool relayState() const;

  static std::string normalizeCommand(std::string input);
  Event processCommand(const std::string &input);
  Event feedChar(char c);

private:
  bool relayState_;
  char rxBuffer_[kRxBufferSize];
  std::size_t rxIndex_;
};

} // namespace mrms
