#include "powermax.h"
#include "esphome/core/log.h"

namespace esphome {
namespace mqtt {
namespace powermax {

static const char *const TAG = "powermax";

void PowerMaxAlarm::setup() {
  ESP_LOGD(TAG, "Setup");

}

void PowerMaxAlarm::loop() {
}

}  // namespace powermax
}  // namespace mqtt
}  // namespace esphome

