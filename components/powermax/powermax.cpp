#include "powermax.h"
#include "esphome/core/log.h"
#include "esphome/components/uart/uart_component.h"

uint8_t DebugLevel; // To satisfy the compiler TODO



namespace esphome {
namespace uart {

UARTComponent *uart_; 

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
  uart_ = (uart::UARTComponent *)this;
}

void PowerMaxAlarm::loop() {
}

}  // namespace powermax
}  // namespace mqtt
}  // namespace uart
}  // namespace esphome

void sha1_pin( char in1, char in2, char *out)
{
  char tmp[25];
  sprintf( tmp, "KorreltjeZout%02x#02x", in1, in2 );
  String c = sha1(tmp);
  sprintf( out, "%s", c.c_str() );  
}

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
          if(uart_->available())
          {
            break;
          }
          delay(5);
        }
        
        if(uart_->available() == false)
        {
            break;
        }

        *((char*)readBuff) = uart_->read();
        dwTotalRead ++;
        readBuff = ((char*)readBuff) + 1;
        bytesToRead--;
    }

    return dwTotalRead;
}

int os_pmComPortWrite(const void* dataToWrite, int bytesToWrite)
{
    uart_->write((const uint8_t*)dataToWrite, bytesToWrite);
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
