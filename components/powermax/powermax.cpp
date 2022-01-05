#include "powermax.h"
#include "esphome/core/log.h"

namespace esphome {
namespace mqtt {
namespace powermax {

PowerMaxDevice *global_pmd;

void PowerMaxDevice::setup() {
  global_pmd = this;
  this->init();
  ESP_LOGD(TAG, "Setup");

  std::string command_topic = App.get_name() + std::string("/input");
  this->subscribe( command_topic, &PowerMaxDevice::on_message);
  ESP_LOGD(TAG, "MQTT subscribed to %s", command_topic.c_str());
}

void PowerMaxDevice::loop() {
  this->CheckInactivityTimers();
  static uint32_t lastMsg = 0;
  static uint32_t lastCmd = 0;


  //Handle incoming messages
  if( this->serial_handler_() )
    lastCmd = millis();
  //we ensure a small delay between commands, as it can confuse the alarm (it has a slow CPU)
  if(millis() - lastCmd > 300 || millis() < lastCmd) 
    this->sendNextCommand();
  if( this->restoreCommsIfLost()) //if we fail to get PINGs from the alarm - we will attempt to restore the connection
  {  
    ESP_LOGW(TAG,"Connection lost. Sending RESTORE request.");   
  }

  if( millis() - lastMsg > 5000 )
  {
    lastMsg = millis(); 
    std::string verbose_topic = App.get_name() + "/alarm/verbose_state";
    this->publish(verbose_topic, this->GetVerboseState(), 0, true );
  }
}

void PowerMaxDevice::on_message(const std::string &topic, const std::string &payload) {
  // do something with topic and payload
  ESP_LOGD(TAG,"Payload %s on topic %s received",payload.c_str(), topic.c_str());

  if (payload=="DISARM")
    this->sendCommand(Pmax_DISARM);  
  else if (payload=="ARM_HOME")
    this->sendCommand(Pmax_ARMHOME);
  else if (payload=="ARM_AWAY")
    this->sendCommand(Pmax_ARMAWAY);  
  else if (payload=="*REBOOT*")
    ESP.restart();
}

 void PowerMaxDevice::mqtt_send(const char* ZoneOrEvent, const char* WhoOrState, const unsigned char zoneID, int zone_or_system_update) {      
      // Translate from pmax.cpp - PmaxLogEvents to hass MQTT accepted payloads.
      ESP_LOGD(TAG,"Creating JSON string for MQTT");
      //Build key JSON headers and structure  
      if (zone_or_system_update == ALARM_STATE_CHANGE) {
        //Here we have an alarm status change (zone 0) so put the status into JSON
        std::string message_text;
        message_text += "{\"stat_str\": \"";
        message_text += ZoneOrEvent;
        message_text += "\",\"stat_update_from\": \"";
        message_text += WhoOrState;
        message_text += "\"";
        message_text += "}";

        //Send alarm state
        std::string alarm_state_topic = App.get_name() + "/alarm/state";
        if( this->publish(alarm_state_topic, message_text, 0, true ) )
          ESP_LOGD(TAG,"Success sending MQTT message");
        else 
          ESP_LOGD(TAG,"Error sending MQTT message"); 
      }
      else if (zone_or_system_update == ZONE_STATE_CHANGE) {
        //Here we have a zone status change so put this information into JSON
        std::string message_text;
        message_text += "{\"zone_id\": \"";
        message_text +=  zoneIDtext;
        message_text +=  "\",\"zone_name\": \"";
        message_text +=  ZoneOrEvent;
        message_text +=  "\",\"zone_status\": \"";
        message_text +=  WhoOrState;
        message_text +=  "\"";
        message_text +=  "}";
        
        //Send zone state
        std::string zone_state_topic = App.get_name() + "/zone/state/";
        //Convert zone ID to text
        char zoneIDtext[10]; 
        itoa(zoneID, zoneIDtext, 10);
        zone_state_topic += zoneIDtext;

        if( this->publish(zone_state_topic, message_text, 0, true ) )
          ESP_LOGD(TAG,"Success sending MQTT message");
        else
          ESP_LOGD(TAG,"Error sending MQTT message");  
      }
    }  


void PowerMaxDevice::log( int priority, const char* buf) {
    switch( priority )
    {
      case LOG_EMERG:	
      case LOG_ALERT:
      case LOG_CRIT:	
      case LOG_ERR:
        ESP_LOGE(TAG,"%s",buf);
        break;
      case LOG_WARNING:
        ESP_LOGW(TAG,"%s",buf);
        break;
      case LOG_NOTICE:
      case LOG_INFO:
        ESP_LOGI(TAG,"%s",buf);
        break;
      case LOG_DEBUG:
        ESP_LOGD(TAG,"%s",buf);
        break;
    }    
}

}  // namespace powermax
}  // namespace mqtt
}  // namespace esphome

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//This file contains OS specific implementation for ESP8266 used by PowerMax library
//If you build for other platrorms (like Linux or Windows, don't include this file, but provide your own)

int log_console_setlogmask(int mask) {
  static int lastmask; // To satisfy the library, logging level is set in yaml
  int oldmask = lastmask;
  if(mask == 0)
    return oldmask; /* POSIX definition for 0 mask */
  lastmask = mask;
  return oldmask;
} 

void os_debugLog(int priority, bool raw, const char *function, int line, const char *formt, ...) { 
  char buf[256];
  va_list ap;
  va_start(ap, formt);
  vsnprintf(buf, sizeof(buf), formt, ap);  
  va_end(ap);
  esphome::mqtt::powermax::global_pmd->log(priority, buf);   
}

void os_usleep(int microseconds) {
    delay(microseconds / 1000);
}

unsigned long os_getCurrentTimeSec() {
  static unsigned int wrapCnt = 0;
  static unsigned long lastVal = 0;
  unsigned long currentVal = millis();

  if(currentVal < lastVal)
    wrapCnt++;

  lastVal = currentVal;
  unsigned long seconds = currentVal/1000;
  
  //millis will wrap each 50 days, as we are interested only in seconds, let's keep the wrap counter
  return (wrapCnt*4294967) + seconds;
}

int os_pmComPortRead(void* readBuff, int bytesToRead) {
    int dwTotalRead = 0;
    while(bytesToRead > 0)
    {
        for(int ix=0; ix<10; ix++)
          if(esphome::mqtt::powermax::global_pmd->available())
            break;
          else
            delay(5);
        
        if(esphome::mqtt::powermax::global_pmd->available() == false)
            break;

        *((char*)readBuff) = esphome::mqtt::powermax::global_pmd->read();
        dwTotalRead ++;
        readBuff = ((char*)readBuff) + 1;
        bytesToRead--;
    }

    return dwTotalRead;
}

int os_pmComPortWrite(const void* dataToWrite, int bytesToWrite) {
    esphome::mqtt::powermax::global_pmd->write_array((const uint8_t*)dataToWrite, bytesToWrite);
    return bytesToWrite;
}

bool os_pmComPortClose() {
    return true;
}

bool os_pmComPortInit(const char* portName) {
    return true;
} 

void os_strncat_s(char* dst, int dst_size, const char* src) {
    strncat(dst, src, dst_size);
}

int os_cfg_getPacketTimeout() {
    return PACKET_TIMEOUT_DEFINED;
}

//see PowerMaxAlarm::setDateTime for details of the parameters, if your OS does not have a RTC clock, simply return false
bool os_getLocalTime(unsigned char& year, unsigned char& month, unsigned char& day, unsigned char& hour, unsigned char& minutes, unsigned char& seconds) {
    return false; //IZIZTODO
}

