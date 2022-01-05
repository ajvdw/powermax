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

#define TAG "powermax"
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

    virtual void OnStatusChange(const PlinkBuffer  * Buff)
    {
        //call base class implementation first, this will send ACK back and upate internal state.
        PowerMaxAlarm::OnStatusChange(Buff);


        //Now send update to ST and use zone 0 as system state not zone 
        unsigned char zoneId = 0;
        
        arming = false;
        //now our customization:

        switch(Buff->buffer[4])
        {
        case 0x51: //"Arm Home" 
        case 0x53: //"Quick Arm Home"
            //do something...
            SendMQTTMessage("armed_home", GetStrPmaxEventSource(Buff->buffer[3]), zoneId, ALARM_STATE_CHANGE);
            break;

        case 0x52: //"Arm Away"
        case 0x54: //"Quick Arm Away"
            SendMQTTMessage("armed_away", GetStrPmaxEventSource(Buff->buffer[3]), zoneId, ALARM_STATE_CHANGE);
            break;

        case 0x55: //"Disarm"
            SendMQTTMessage("disarmed", GetStrPmaxEventSource(Buff->buffer[3]), zoneId, ALARM_STATE_CHANGE);
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

        SendMQTTMessage("triggered", zoneTrippedStr, 0, ALARM_STATE_CHANGE);

    }

    //Fired when alarm is cancelled
    virtual void OnAlarmCancelled(unsigned char whoDisarmed, const char* whoDisarmedStr)
    {
        //call base class implementation first, to log the event and update states.
        PowerMaxAlarm::OnAlarmCancelled( whoDisarmed, whoDisarmedStr );
        //whoDisarmed    : specifies who cancelled the alarm (for example a keyfob 1), values from PmaxEventSource
        //whoDisarmedStr : text representation of who disarmed
        //Canceled
        SendMQTTMessage("disarmed" , whoDisarmedStr, 0, ALARM_STATE_CHANGE);  
    }

    void SendAlarmState()
    {
        switch(  this->stat )
        {
          case SS_Disarm: 
            SendMQTTMessage( "disarmed", "", 0, ALARM_STATE_CHANGE);
            break;
          case SS_Exit_Delay:
          case SS_Exit_Delay2:
            SendMQTTMessage( "arming", "", 0, ALARM_STATE_CHANGE);
            break;
          case SS_Armed_Home: 
            SendMQTTMessage( "armed_home", "", 0, ALARM_STATE_CHANGE);
            break;
          case SS_Armed_Away:
            SendMQTTMessage( "armed_away", "", 0, ALARM_STATE_CHANGE);
            break;
          case SS_Entry_Delay:    
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

  void SendMQTTMessage(const char* ZoneOrEvent, const char* WhoOrState, const unsigned char zoneID, int zone_or_system_update) {

      //SERIALPORT.println("MQTT Message initialized");
      char message_text[600];
      message_text[0] = '\0';

      //Convert zone ID to text
      char zoneIDtext[10];
      itoa(zoneID, zoneIDtext, 10);

      
      // Translate from pmax.cpp - PmaxLogEvents to hass MQTT accepted payloads.


      DEBUG(LOG_NOTICE,"Creating JSON string for MQTT");
      //Build key JSON headers and structure  
      if (zone_or_system_update == ALARM_STATE_CHANGE) {
        

        //Here we have an alarm status change (zone 0) so put the status into JSON
        strncpy(message_text, "{\r\n\"stat_str\": \"", 600);
        strcat(message_text, ZoneOrEvent);
        strcat(message_text, "\",\r\n\"stat_update_from\": \"");
        strcat(message_text, WhoOrState);
        strcat(message_text, "\"");
        strcat(message_text, "\r\n}\r\n");
        //Send alarm state
      

      // TODO if (mqttClient.publish(mqttAlarmStateTopic, ZoneOrEvent, true) == true) {  // Send translated mqtt message and retain last known status
      //     DEBUG(LOG_NOTICE,"Success sending MQTT message");
      //    } else {
      //     DEBUG(LOG_NOTICE,"Error sending MQTT message");
      //    }   
          

      }
      else if (zone_or_system_update == ZONE_STATE_CHANGE) {
        //Here we have a zone status change so put this information into JSON
        strncpy(message_text, "{\r\n\"zone_id\": \"", 600);
        strcat(message_text, zoneIDtext);
        strcat(message_text, "\",\r\n\"zone_name\": \"");
        strcat(message_text, ZoneOrEvent);
        strcat(message_text, "\",\r\n\"zone_status\": \"");
        strcat(message_text, WhoOrState);
        strcat(message_text, "\"");
        strcat(message_text, "\r\n}\r\n");
        
        //Send zone state

      // char zoneStateTopic[100];
      // zoneStateTopic[0] = '\0';
      // strncpy(zoneStateTopic, hassmqttZoneStateTopic, 100);
      // strcat(zoneStateTopic, zoneIDtext);
        
      // TODO if (mqttClient.publish(zoneStateTopic, message_text, true) == true) {  // Send mqtt message and retain last known status and sends in sub topic with the zoneID.
      //    DEBUG(LOG_NOTICE,"Success sending MQTT message");
      //   } else {
      //   DEBUG(LOG_NOTICE,"Error sending MQTT message");
      //   }  
      }
    
    }  

protected:
  bool serial_handler_() {
    bool packetHandled = false;
    PlinkBuffer commandBuffer ;
    memset(&commandBuffer, 0, sizeof(commandBuffer));
    char oneByte = 0;  
    while (  (os_pmComPortRead(&oneByte, 1) == 1)  ) 
    {     
      if (commandBuffer.size<(MAX_BUFFER_SIZE-1))
      {
        *(commandBuffer.size+commandBuffer.buffer) = oneByte;
        commandBuffer.size++;
      
        if(oneByte == 0x0A) //postamble received, let's see if we have full message
        {
          if(PowerMaxAlarm::isBufferOK(&commandBuffer))
          {
            DEBUG(LOG_INFO,"--- new packet %d ----", millis());
            packetHandled = true;
            this->handlePacket(&commandBuffer);
            commandBuffer.size = 0;
            break;
          }
        }
      }
      else
      {
        DEBUG(LOG_WARNING,"Packet too big detected");
      }
    }

    if(commandBuffer.size > 0)
    {
      packetHandled = true;
      //this will be an invalid packet:
      DEBUG(LOG_WARNING,"Passing invalid packet to packetManager");
      this->handlePacket(&commandBuffer);
    }

    return packetHandled;
  }

};

}  // namespace powermax
}  // namespace mqtt
}  // namespace esphome

