#pragma once

#include "esphome/components/uart/uart.h"
#include "esphome/core/application.h"
#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/components/mqtt/custom_mqtt_device.h"

#include "pmax.h"

namespace esphome {
namespace mqtt {
namespace uart {
namespace powermax {

#define PRINTF_BUF 512

/** This class is a helper class for custom components that communicate using
 * MQTT. It has 5 helper functions that you can use (square brackets indicate optional):
 *
 *  - `subscribe(topic, function_pointer, [qos])`
 *  - `subscribe_json(topic, function_pointer, [qos])`
 *  - `publish(topic, payload, [qos], [retain])`
 *  - `publish_json(topic, payload_builder, [qos], [retain])`
 *  - `is_connected()`
 */
class PowerMaxAlarm : public UARTDevice, public CustomMQTTDevice, public Component {
 public:

  void setup() override;
  void loop() override;

};

}  // namespace powermax
}  // namespace uart
}  // namespace mqtt
}  // namespace esphome

