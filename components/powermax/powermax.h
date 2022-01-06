#pragma once

#include "esphome/components/mqtt/custom_mqtt_device.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/application.h"
#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "pmax.h"

static const char* const TAG = "powermax";

namespace esphome {
namespace mqtt {
namespace powermax {

#define ALARM_STATE_CHANGE 0
#define ZONE_STATE_CHANGE 1

class PowerMaxDevice : public PowerMaxAlarm, public uart::UARTDevice, public mqtt::CustomMQTTDevice, public Component {
 public:
  void setup() override;
  void loop() override;
  void log(int prio, const char* buf);

  virtual void OnStatusChange(const PlinkBuffer* Buff);
  virtual void OnStatusUpdatePanel(const PlinkBuffer* Buff);
  virtual void OnAlarmStarted(unsigned char alarmType, const char* alarmTypeStr, unsigned char zoneTripped,
                              const char* zoneTrippedStr);
  virtual void OnAlarmCancelled(unsigned char whoDisarmed, const char* whoDisarmedStr);

 protected:
  bool zone_motion_[MAX_ZONE_COUNT + 1] = {0};

  bool process_messsages_();
  void on_mqtt_receive_(const std::string& topic, const std::string& payload);
  void mqtt_send_(const char* ZoneOrEvent, const char* WhoOrState, const unsigned char zoneID,
                 int zone_or_system_update);
};

}  // namespace powermax
}  // namespace mqtt
}  // namespace esphome
