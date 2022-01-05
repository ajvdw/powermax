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
class PowerMaxDevice : public PowerMaxAlarm, public uart::UARTDevice, public mqtt::CustomMQTTDevice, public Component {
 public:

  void setup() override;
  void loop() override;

////////////////////////////////////////////////
    bool zone_motion[MAX_ZONE_COUNT+1] = {0};
    bool arming = false;

    virtual void OnStatusChange(const PlinkBuffer  * Buff)
    {
        //call base class implementation first, this will send ACK back and upate internal state.
        PowerMaxAlarm::OnStatusChange(Buff);


        //Now send update to ST and use zone 0 as system state not zone 
        unsigned char zoneId = 0;
//SendMQTTMessage(GetStrPmaxLogEvents(Buff->buffer[4]), GetStrPmaxEventSource(Buff->buffer[3]), zoneId, ALARM_STATE_CHANGE);
        arming = false;
        //now our customization:


        //now our customization:
        switch(Buff->buffer[4])
        {
        case 0x51: //"Arm Home" 
        case 0x53: //"Quick Arm Home"
            //do something...
            break;

        case 0x52: //"Arm Away"
        case 0x54: //"Quick Arm Away"
            //do something...
            break;

        case 0x55: //"Disarm"
            //do someting...
            break;
        }        
    }


};

}  // namespace powermax
}  // namespace mqtt
}  // namespace esphome

