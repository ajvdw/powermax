#pragma once

#include "esphome/components/uart/uart.h"
#include "esphome/core/application.h"
#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/components/mqtt/custom_mqtt_device.h"

#include "pmax.h"

static const char *const TAG = "powermax";

namespace esphome {
namespace mqtt {
namespace powermax {

#define ALARM_STATE_CHANGE 0
#define ZONE_STATE_CHANGE 1


/** This class is a helper class for custom components that communicate using
 * MQTT. It has 5 helper functions that you can use (square brackets indicate optional):
 *
 *  - `subscribe(topic, function_pointer, [qos])`
 *  - `subscribe_json(topic, function_pointer, [qos])`
 *  - `publish(topic, payload, [qos], [retain])`
 *  - `publish_json(topic, payload_builder, [qos], [retain])`
 *  - `is_connected()`
 */
class PowerMaxDevice : public PowerMaxAlarm, public uart::UARTDevice, public mqtt::CustomMQTTDevice, public Component {
 public:

  void setup() override;
  void loop() override;
  void log(int prio, const char* buf);
  void on_message(const std::string &topic, const std::string &payload);
  void mqtt_send(const char* ZoneOrEvent, const char* WhoOrState, const unsigned char zoneID, int zone_or_system_update);
 
  virtual void OnStatusChange(const PlinkBuffer  * Buff);
  const char* getZoneSensorType(unsigned char zoneId);
  virtual void OnStatusUpdatePanel(const PlinkBuffer  * Buff);
  virtual void OnAlarmStarted(unsigned char alarmType, const char* alarmTypeStr, unsigned char zoneTripped, const char* zoneTrippedStr);
  virtual void OnAlarmCancelled(unsigned char whoDisarmed, const char* whoDisarmedStr);
  void SendAlarmState();
  void CheckInactivityTimers();
protected:
  bool serial_handler_();

  bool zone_motion[MAX_ZONE_COUNT+1] = {0};
  bool arming = false;
  //Inactivity timer wil always default to this value on boot (it is not stored in EEPROM at the moment, though only resets when the Powermax power cycles (hence rarely))
  int inactivity_seconds = 20;
  //Variables for managing zones
  int zones_enrolled_count = MAX_ZONE_COUNT;
  int max_zone_id_enrolled = MAX_ZONE_COUNT;

};

}  // namespace powermax
}  // namespace mqtt
}  // namespace esphome

