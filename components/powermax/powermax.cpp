#include "powermax.h"
#include "esphome/core/log.h"

uint8_t DebugLevel; // To satisfy the compiler TODO

esphome::uart::UARTDevice *global_uart;

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
void PowerMaxDevice::setup() {
  ESP_LOGD(TAG, "Setup");

  global_uart = (uart::UARTDevice *)this;

  this->init();
}

void PowerMaxDevice::loop() {

  this->CheckInactivityTimers();

  static uint32_t lastMsg = 0;

  //Handle incoming messages
  if( this->serial_handler_() )
    lastMsg = millis();

  //we ensure a small delay between commands, as it can confuse the alarm (it has a slow CPU)
  if(millis() - lastMsg > 300 || millis() < lastMsg) 
    this->sendNextCommand();

  if( this->restoreCommsIfLost()) //if we fail to get PINGs from the alarm - we will attempt to restore the connection
      DEBUG(LOG_WARNING,"Connection lost. Sending RESTORE request.");   

}

void SendMQTTMessage(const char* ZoneOrEvent, const char* WhoOrState, const unsigned char zoneID, int zone_or_system_update) 
{

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


}  // namespace powermax
}  // namespace mqtt
}  // namespace esphome

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//This file contains OS specific implementation for ESP8266 used by PowerMax library
//If you build for other platrorms (like Linux or Windows, don't include this file, but provide your own)

int log_console_setlogmask(int mask)
{
  int oldmask = DebugLevel;
  if(mask == 0)
    return oldmask; /* POSIX definition for 0 mask */
  DebugLevel = mask;
  return oldmask;
} 


void os_debugLog(int priority, bool raw, const char *function, int line, const char *format, ...)
{
  if(priority <= DebugLevel)
  {
    char buf[PRINTF_BUF];
    va_list ap;
    
    va_start(ap, format);
    vsnprintf(buf, sizeof(buf), format, ap);
    
    va_end(ap);
  
    yield();
  }
}

void os_usleep(int microseconds)
{
    delay(microseconds / 1000);
}

unsigned long os_getCurrentTimeSec()
{
  static unsigned int wrapCnt = 0;
  static unsigned long lastVal = 0;
  unsigned long currentVal = millis();

  if(currentVal < lastVal)
  {
    wrapCnt++;
  }

  lastVal = currentVal;
  unsigned long seconds = currentVal/1000;
  
  //millis will wrap each 50 days, as we are interested only in seconds, let's keep the wrap counter
  return (wrapCnt*4294967) + seconds;
}

int os_pmComPortRead(void* readBuff, int bytesToRead)
{
    int dwTotalRead = 0;
    while(bytesToRead > 0)
    {
        for(int ix=0; ix<10; ix++)
        {
          if(global_uart->available())
          {
            break;
          }
          delay(5);
        }
        
        if(global_uart->available() == false)
        {
            break;
        }

        *((char*)readBuff) = global_uart->read();
        dwTotalRead ++;
        readBuff = ((char*)readBuff) + 1;
        bytesToRead--;
    }

    return dwTotalRead;
}

int os_pmComPortWrite(const void* dataToWrite, int bytesToWrite)
{
    global_uart->write_array((const uint8_t*)dataToWrite, bytesToWrite);
    return bytesToWrite;
}

bool os_pmComPortClose()
{
    return true;
}

bool os_pmComPortInit(const char* portName) {
    return true;
} 

void os_strncat_s(char* dst, int dst_size, const char* src)
{
    strncat(dst, src, dst_size);
}

int os_cfg_getPacketTimeout()
{
    return PACKET_TIMEOUT_DEFINED;
}

//see PowerMaxAlarm::setDateTime for details of the parameters, if your OS does not have a RTC clock, simply return false
bool os_getLocalTime(unsigned char& year, unsigned char& month, unsigned char& day, unsigned char& hour, unsigned char& minutes, unsigned char& seconds)
{
    return false; //IZIZTODO
}

