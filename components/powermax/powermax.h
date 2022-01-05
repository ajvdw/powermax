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

////////////////////////////////////////////////
    bool zone_motion[MAX_ZONE_COUNT+1] = {0};
    bool arming = false;
    //Inactivity timer wil always default to this value on boot (it is not stored in EEPROM at the moment, though only resets when the Powermax power cycles (hence rarely))
    int inactivity_seconds = 20;
    //Variables for managing zones
    int zones_enrolled_count = MAX_ZONE_COUNT;
    int max_zone_id_enrolled = MAX_ZONE_COUNT;

    void SendMQTTMessage(const char* ZoneOrEvent, const char* WhoOrState, const unsigned char zoneID, int zone_or_system_update); 


    virtual void OnStatusChange(const PlinkBuffer  * Buff)
    {
        //call base class implementation first, this will send ACK back and upate internal state.
        PowerMaxAlarm::OnStatusChange(Buff);


        //Now send update to ST and use zone 0 as system state not zone 
        unsigned char zoneId = 0;
        
        SendMQTTMessage(GetStrPmaxLogEvents(Buff->buffer[4]), GetStrPmaxEventSource(Buff->buffer[3]), zoneId, ALARM_STATE_CHANGE);
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
    const char* getZoneSensorType(unsigned char zoneId)
    {
        if(zoneId < MAX_ZONE_COUNT &&
           zone[zoneId].enrolled)
        {
            return zone[zoneId].sensorType;
        }
        return "Unknown";
    }
 


    virtual void OnStatusUpdatePanel(const PlinkBuffer  * Buff)
    {
        //call base class implementation first, to log the event and update states.
        PowerMaxAlarm::OnStatusUpdatePanel(Buff);

        //Now if it is a zone event then send it to SmartThings
        if (this->isZoneEvent()) {
              const unsigned char zoneId = Buff->buffer[5];
              ZoneEvent eventType = (ZoneEvent)Buff->buffer[6];
              
              SendMQTTMessage(this->getZoneName(zoneId), GetStrPmaxZoneEventTypes(Buff->buffer[6]), zoneId, ZONE_STATE_CHANGE);
              //If it is a Violated (motion) event then set zone activated
              if (eventType == ZE_Violated) {
                  zone_motion[zoneId] = true;
              }
        }
        else
        {
          // ALARM_STATE_CHANGE 
          // this->stat  pending    
            SendAlarmState();
        }
        
    }

    //Fired when system enters alarm state
    virtual void OnAlarmStarted(unsigned char alarmType, const char* alarmTypeStr, unsigned char zoneTripped, const char* zoneTrippedStr)
    {
        //call base class implementation first, to log the event and update states.
        PowerMaxAlarm::OnAlarmStarted(alarmType, alarmTypeStr, zoneTripped, zoneTrippedStr);
        //alarmType      : type of alarm, first 9 values from PmaxLogEvents
        //alarmTypeStr   : text representation of alarmType
        //zoneTripped    : specifies zone that initiated the alarm, values from PmaxEventSource
        //zoneTrippedStr : zone name

        SendMQTTMessage("Triggered", zoneTrippedStr, 0, ALARM_STATE_CHANGE);

    }

    //Fired when alarm is cancelled
    virtual void OnAlarmCancelled(unsigned char whoDisarmed, const char* whoDisarmedStr)
    {
        //call base class implementation first, to log the event and update states.
        PowerMaxAlarm::OnAlarmCancelled( whoDisarmed, whoDisarmedStr );
        //whoDisarmed    : specifies who cancelled the alarm (for example a keyfob 1), values from PmaxEventSource
        //whoDisarmedStr : text representation of who disarmed
        
        SendMQTTMessage("Canceled" , whoDisarmedStr, 0, ALARM_STATE_CHANGE);
    }

    void SendAlarmState()
    {
        switch(  this->stat )
        {
          case SS_Disarm: 
            SendMQTTMessage( "Disarm", "", 0, ALARM_STATE_CHANGE);
            break;
          case SS_Exit_Delay:
          case SS_Exit_Delay2:
            SendMQTTMessage( "Arming", "", 0, ALARM_STATE_CHANGE);
            break;
          case SS_Armed_Home: 
            SendMQTTMessage( "Arm Home", "", 0, ALARM_STATE_CHANGE);
            break;
          case SS_Armed_Away:
            SendMQTTMessage( "Arm Away", "", 0, ALARM_STATE_CHANGE);
            break;    
          case SS_User_Test:
          case SS_Downloading:
          case SS_Programming:
          case SS_Installer:
          case SS_Home_Bypass:
          case SS_Away_Bypass:
          case SS_Ready:
          case SS_Not_Ready:
              // Do NOTHING
              break;

        }
    }
    
    void CheckInactivityTimers() {
        for(int ix=1; ix<=max_zone_id_enrolled; ix++) {
            if (zone_motion[ix]) {
                if ((os_getCurrentTimeSec() - zone[ix].lastEventTime) > inactivity_seconds) {
                    zone_motion[ix]= false;
                    SendMQTTMessage(this->getZoneName(ix), "No Motion", ix, ZONE_STATE_CHANGE);  
                }
            }
        }
    }


};

}  // namespace powermax
}  // namespace mqtt
}  // namespace esphome

