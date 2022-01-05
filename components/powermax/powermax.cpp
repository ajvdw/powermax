#include "powermax.h"
#include "esphome/core/log.h"


esphome::uart::UARTDevice *global_uart;

static const char *const TAG = "powermax";

namespace esphome {
namespace mqtt {
namespace powermax {


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

}  // namespace powermax
}  // namespace mqtt
}  // namespace esphome

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//This file contains OS specific implementation for ESP8266 used by PowerMax library
//If you build for other platrorms (like Linux or Windows, don't include this file, but provide your own)

int log_console_setlogmask(int mask)
{
  static int lastmask; // To satisfy the library, logging level is set in yaml

  int oldmask = lastmask;
  if(mask == 0)
    return oldmask; /* POSIX definition for 0 mask */
  lastmask = mask;

  return oldmask;
} 

void os_debugLog(int priority, bool raw, const char *function, int line, const char *formt, ...)
{ 

  char buf[256];
  va_list ap;
  va_start(ap, formt);
  vsnprintf(buf, sizeof(buf), formt, ap);  
  va_end(ap);

  ESP_LOGE( tag, LOG_STR_ARG(buf) );

 /*
  switch( priority )
  {
    case LOG_EMERG:	
    case LOG_ALERT:
    case LOG_CRIT:	
    case LOG_ERR:
      ESP_LOGE(TAG,buf);
      break;
    case LOG_WARNING:
      ESP_LOGW(TAG,buf);
      break;
    case LOG_NOTICE:
    case LOG_INFO:
      ESP_LOGI(TAG,buf);
      break;
    case LOG_DEBUG
      ESP_LOGD(TAG,buf);
      break;
    
  }
  */

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

