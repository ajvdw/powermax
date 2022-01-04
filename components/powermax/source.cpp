#include "./pmax/pmax.h"
#include <Hash.h>
//#include <WiFiClient.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <WiFiManager.h>
#include <EEPROM.h>
#include <PubSubClient.h>       
#define PM_SOFTSERIAL
#ifdef PM_SOFTSERIAL
  #include <SoftwareSerial.h>
  SoftwareSerial softSerial (0, 2); // D3-D4
  #define SERIALPORT softSerial
  #define DEBUGPORT Serial
#else
  #define SERIALPORT Serial
  #define DEBUGPORT Serial
#endif

//////////////////// IMPORTANT DEFINES, ADJUST TO YOUR NEEDS //////////////////////


//This enables control over your system, when commented out, alarm is 'read only' (esp will read the state, but will never ardm/disarm)
#define PM_ALLOW_CONTROL

#define Firmware_Date __DATE__
#define Firmware_Time __TIME__

#define JsonHeaderText "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nConnection: close\r\n\r\n"
// MQTT SETUP

#define PM_ENABLE_MQTT_ACCESS
#define PM_ALLOW_CONTROL_MQTT

//MQTT Fail retry interval, default set to 5 seconds, if it fails 3 times it will be set to wait 60 seconds and then try three more times continously.
//static unsigned long REFRESH_INTERVAL = 5000; // ms
//static unsigned long lastRefreshTime = 0;
//int mqttFail = 0;
long lastReconnectAttempt = 0;
long nrReconnectAttempts = 0;

//MQTT topic for Alarm state information output from Powermax alarm
const char* mqttAlarmStateTopic = "powermax/alarm/state";
//MQTT topic for Zone state information output from Powermax alarm
const char* mqttZoneStateTopic = "powermax/zone/state";
const char* hassmqttZoneStateTopic = "powermax/zone/state/";
//MQTT topic for MQTT input to powermax alarm
const char* mqttAlarmInputTopic = "powermax/alarm/input";
//MQTT topic for MQTT verbose output, to catch or show more details.
const char* mqttAlarmStateTopicVerbose = "powermax/alarm/verbose_state";

//Inactivity timer wil always default to this value on boot (it is not stored in EEPROM at the moment, though only resets when the Powermax power cycles (hence rarely))
int inactivity_seconds = 20;

bool arming = false;

//Global variables for managing zones
int zones_enrolled_count = MAX_ZONE_COUNT;
int max_zone_id_enrolled = MAX_ZONE_COUNT;

#define ALARM_STATE_CHANGE 0
#define ZONE_STATE_CHANGE 1


//////////////////////////////////////////////////////////////////////////////////
//NOTE: PowerMaxAlarm class should contain ONLY functionality of Powerlink
//If you want to (for example) send an SMS on arm/disarm event, don't add it to PowerMaxAlarm
//Instead create a new class that inherits from PowerMaxAlarm, and override required function
class MyPowerMax : public PowerMaxAlarm
{
public:
    bool zone_motion[MAX_ZONE_COUNT+1] = {0};
    virtual void OnStatusChange(const PlinkBuffer  * Buff)
    {
        //call base class implementation first, this will send ACK back and upate internal state.
        PowerMaxAlarm::OnStatusChange(Buff);

#ifdef PM_ENABLE_MQTT_ACCESS
        //Now send update to ST and use zone 0 as system state not zone 
        unsigned char zoneId = 0;
        SendMQTTMessage(GetStrPmaxLogEvents(Buff->buffer[4]), GetStrPmaxEventSource(Buff->buffer[3]), zoneId, ALARM_STATE_CHANGE);
        arming = false;
        //now our customization:
#endif

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
 
#ifdef PM_ENABLE_MQTT_ACCESS

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
#endif
};

//////////////////////////////////////////////////////////////////////////////////

MyPowerMax pm;
ESP8266WebServer server(80);        // The Webserver
ESP8266HTTPUpdateServer httpUpdater;

int telnetDbgLevel = LOG_NO_FILTER; //by default only NO FILTER messages are logged to telnet clients 






#ifdef PM_ENABLE_MQTT_ACCESS
WiFiClient mqttWiFiClient;
PubSubClient mqttClient(mqttWiFiClient);

// MQTT RELATED -- START ---
void SendMQTTMessage(const char* ZoneOrEvent, const char* WhoOrState, const unsigned char zoneID, int zone_or_system_update) {

  //SERIALPORT.println("MQTT Message initialized");
  char message_text[600];
  message_text[0] = '\0';

  //Convert zone ID to text
  char zoneIDtext[10];
  itoa(zoneID, zoneIDtext, 10);

  char hassZoneOrEvent[50];

  strcpy(hassZoneOrEvent,"unknown");
  
  // Translate from pmax.cpp - PmaxLogEvents to hass MQTT accepted payloads.
  if(ZoneOrEvent=="Arm Home" || ZoneOrEvent=="Quick Arm Home")
  { 
    strcpy(hassZoneOrEvent,"armed_home"); 
  }
  else if(ZoneOrEvent=="Arm Away" || ZoneOrEvent=="Quick Arm Away")
  {
    strcpy(hassZoneOrEvent,"armed_away");
  }
  else if(ZoneOrEvent=="Disarm")
  {
    strcpy(hassZoneOrEvent,"disarmed");
  }
  else if(ZoneOrEvent=="Arming")
  {
    strcpy(hassZoneOrEvent,"arming");
  }
  else if(ZoneOrEvent=="Triggered" )
  {
    strcpy(hassZoneOrEvent,"triggered");
  }
  else if(ZoneOrEvent=="Canceled" )
  {
    strcpy(hassZoneOrEvent,"disarmed");
  }


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
   

    if (mqttClient.publish(mqttAlarmStateTopic, hassZoneOrEvent, true) == true) {  // Send translated mqtt message and retain last known status
       DEBUG(LOG_NOTICE,"Success sending MQTT message");
      } else {
       DEBUG(LOG_NOTICE,"Error sending MQTT message");
      }   
      

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

    char zoneStateTopic[100];
    zoneStateTopic[0] = '\0';
    strncpy(zoneStateTopic, hassmqttZoneStateTopic, 100);
    strcat(zoneStateTopic, zoneIDtext);
    
    if (mqttClient.publish(zoneStateTopic, message_text, true) == true) {  // Send mqtt message and retain last known status and sends in sub topic with the zoneID.
       DEBUG(LOG_NOTICE,"Success sending MQTT message");
      } else {
       DEBUG(LOG_NOTICE,"Error sending MQTT message");
      }  
  }
 
}


void MQTTcallback(char topic[], byte* payload, unsigned int length) {
#ifdef PM_ALLOW_CONTROL_MQTT // Only allow callback of commands if PM_ALLOW_CONTROL is enabled.

  payload[length] = '\0';
  String alarm_command = String((char*)payload); // Decode byte to string.. (note string has reputation of causing memory issues??)

 // SERIALPORT.println(alarm_command);//alarm_command);

  if (alarm_command=="DISARM")
  {
    handleDisarm();
  } 
  else if (alarm_command=="ARM_HOME")
  {
    handleArmHome(); 
  }
  else if (alarm_command=="ARM_AWAY")
  {
    handleArmAway();
  }
  else if (alarm_command=="*REBOOT*")
  {
    ESP.restart();
  }
  
#endif
 

}

//MQTT Reconnect if not connected

boolean reconnect() {
  if (mqttClient.connect("PowerMaxClient", MQTT_USER, MQTT_PASS)) {
    // Once connected, publish an announcement...
    //client.publish("outTopic","hello world");
    // ... and resubscribe
    mqttClient.subscribe(mqttAlarmInputTopic);
  }

    
  return mqttClient.connected();
}

// MQTT RELATED -- DONE --

#endif


#define PRINTF_BUF 512 // define the tmp buffer size (change if desired)
void LOG(const char *format, ...)
{
  char buf[PRINTF_BUF];
  va_list ap;
  
  va_start(ap, format);
  vsnprintf(buf, sizeof(buf), format, ap);
#ifdef PM_ENABLE_TELNET_ACCESS  
  if(telnetClient.connected())
  {
    telnetClient.write((const uint8_t *)buf, strlen(buf));
  }
#endif  
  va_end(ap);
}

void setup(void){

  SERIALPORT.begin(9600); //connect to PowerMax
  DEBUGPORT.begin(115200);
  
  DEBUGPORT.println("Started");

  
#ifdef PM_ENABLE_MQTT_ACCESS
    mqttClient.setServer(IP_FOR_MQTT, atoi(PORT_FOR_MQTT));
    mqttClient.setCallback(MQTTcallback);
#endif

  server.onNotFound(handleNotFound);
  server.begin();


  //if you have a fast board (like PowerMax Complete) you can pass 0 to init function like this: pm.init(0);
  //this will speed up the boot process, keep it as it is, if you have issues downloading the settings from the board.
  pm.init();
}





bool serialHandler(PowerMaxAlarm* pm) {
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
          pm->handlePacket(&commandBuffer);
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
    pm->handlePacket(&commandBuffer);
  }

  return packetHandled;
}

void loop(void)
{
  pm.CheckInactivityTimers();

  static unsigned long lastMsg = 0;
  if(serialHandler(&pm) == true)
  {
    lastMsg = millis();
  }

  if(millis() - lastMsg > 300 || millis() < lastMsg) //we ensure a small delay between commands, as it can confuse the alarm (it has a slow CPU)
  {
    pm.sendNextCommand();
  }

  if(pm.restoreCommsIfLost()) //if we fail to get PINGs from the alarm - we will attempt to restore the connection
  {
      DEBUG(LOG_WARNING,"Connection lost. Sending RESTORE request.");   
  }   
}

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
  int oldmask = telnetDbgLevel;
  if(mask == 0)
    return oldmask; /* POSIX definition for 0 mask */
  telnetDbgLevel = mask;
  return oldmask;
} 


void os_debugLog(int priority, bool raw, const char *function, int line, const char *format, ...)
{
  if(priority <= telnetDbgLevel)
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
          if(SERIALPORT.available())
          {
            break;
          }
          delay(5);
        }
        
        if(SERIALPORT.available() == false)
        {
            break;
        }

        *((char*)readBuff) = SERIALPORT.read();
        dwTotalRead ++;
        readBuff = ((char*)readBuff) + 1;
        bytesToRead--;
    }

    return dwTotalRead;
}

int os_pmComPortWrite(const void* dataToWrite, int bytesToWrite)
{
    SERIALPORT.write((const uint8_t*)dataToWrite, bytesToWrite);
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
//////End of OS specific part of PM library/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
