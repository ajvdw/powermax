#include "powermax.h"
#include "esphome/core/log.h"

namespace esphome {
namespace mqtt {
namespace powermax {

static const char *const TAG = "powermax";

/*
void setup() {
   *     subscribe("the/topic", &MyCustomMQTTDevice::on_message);
   *     pinMode(5, OUTPUT);
   *   }
   *
   *   // topic and payload parameters can be removed if not needed
   *   // e.g: void on_message() {
   *
   *   void on_message(const std::string &topic, const std::string &payload) {
   *     // do something with topic and payload
   *     if (payload == "ON") {
   *       digitalWrite(5, HIGH);
   *     } else {
   *       digitalWrite(5, LOW);
   *     }
   *   }
*/
void PowerMaxAlarm::setup() {
  ESP_LOGD(TAG, "Setup");

}

void PowerMaxAlarm::loop() {
}

}  // namespace powermax
}  // namespace mqtt
}  // namespace esphome

