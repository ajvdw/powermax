#include "esphome/core/log.h"
#include "powermax.h"

namespace esphome {
namespace mqtt {
namespace powermax {

PowerMaxDevice* global_pmd;

void PowerMaxDevice::setup() {
  global_pmd = this;
  this->init();
  ESP_LOGD(TAG, "Setup");

  std::string command_topic = App.get_name() + std::string("/alarm/input");
  this->subscribe(command_topic, &PowerMaxDevice::on_mqtt_receive_);
  ESP_LOGD(TAG, "MQTT subscribed to %s", command_topic.c_str());
}

void PowerMaxDevice::loop() {
  static uint32_t lastMsg = 0;
  static uint32_t lastCmd = 0;
  uint32_t max_zone_id_enrolled = MAX_ZONE_COUNT;
  uint32_t inactivity_seconds = 20;

  // Check Inactivity Timers
  for (int ix = 1; ix <= max_zone_id_enrolled; ix++) {
    if (zone_motion_[ix]) {
      if ((os_getCurrentTimeSec() - zone[ix].lastEventTime) > inactivity_seconds) {
        zone_motion_[ix] = false;
        mqtt_send_(this->getZoneName(ix), "No Motion", ix, ZONE_STATE_CHANGE);
      }
    }
  }
  // Handle incoming messages
  if (this->process_messsages_()) lastCmd = millis();
  // we ensure a small delay between commands, as it can confuse the alarm (it
  // has a slow CPU)
  if (millis() - lastCmd > 300 || millis() < lastCmd) this->sendNextCommand();
  if (this->restoreCommsIfLost())  // if we fail to get PINGs from the alarm -
                                   // we will attempt to restore the connection
    ESP_LOGW(TAG, "Connection lost. Sending RESTORE request.");
  // Broadcast verbose state every 5 seconds
  if (millis() - lastMsg > 5000) {
    lastMsg = millis();
    std::string verbose_topic = App.get_name() + "/alarm/verbose_state";
    if (this->publish(verbose_topic, this->GetVerboseState(), 0, true))
      ESP_LOGD(TAG, "Success sending MQTT verbose state message");
    else
      ESP_LOGD(TAG, "Error sending MQTT verbose state message");
  }
}

bool PowerMaxDevice::process_messsages_() {
  bool packetHandled = false;
  PlinkBuffer commandBuffer;
  memset(&commandBuffer, 0, sizeof(commandBuffer));
  char oneByte = 0;
  while ((os_pmComPortRead(&oneByte, 1) == 1)) {
    if (commandBuffer.size < (MAX_BUFFER_SIZE - 1)) {
      *(commandBuffer.size + commandBuffer.buffer) = oneByte;
      commandBuffer.size++;
      if (oneByte == 0x0A)  // postamble received, let's see if we have full message
      {
        if (PowerMaxAlarm::isBufferOK(&commandBuffer)) {
          ESP_LOGD(TAG, "--- new packet %d ----", millis());
          packetHandled = true;
          this->handlePacket(&commandBuffer);
          commandBuffer.size = 0;
          break;
        }
      }
    } else
      ESP_LOGW(TAG, "Packet too big detected");
  }
  if (commandBuffer.size > 0) {
    packetHandled = true;
    // this will be an invalid packet:
    ESP_LOGW(TAG, "Passing invalid packet to packetManager");
    this->handlePacket(&commandBuffer);
  }
  return packetHandled;
}

void PowerMaxDevice::on_mqtt_receive_(const std::string& topic, const std::string& payload) {
  // do something with topic and payload
  ESP_LOGD(TAG, "Payload %s on topic %s received", payload.c_str(), topic.c_str());

  if (payload == "DISARM")
    this->sendCommand(Pmax_DISARM);
  else if (payload == "ARM_HOME")
    this->sendCommand(Pmax_ARMHOME);
  else if (payload == "ARM_AWAY")
    this->sendCommand(Pmax_ARMAWAY);
  else if (payload == "*REBOOT*")
    ESP.restart();
}

void PowerMaxDevice::mqtt_send_(const char* ZoneOrEvent, const char* WhoOrState, const unsigned char zoneID,
                               int zone_or_system_update) {
  // Translate from pmax.cpp - PmaxLogEvents to hass MQTT accepted payloads.
  ESP_LOGD(TAG, "Creating JSON string for MQTT");
  // Build key JSON headers and structure
  if (zone_or_system_update == ALARM_STATE_CHANGE) {
    // Here we have an alarm status change (zone 0) so put the status into JSON
    std::string message_text;
    message_text += "{\"stat_str\": \"";
    message_text += ZoneOrEvent;
    message_text += "\",\"stat_update_from\": \"";
    message_text += WhoOrState;
    message_text += "\"";
    message_text += "}";

    // Send alarm state
    std::string alarm_state_topic = App.get_name() + "/alarm/state";
    if (this->publish(alarm_state_topic, message_text, 0, true))
      ESP_LOGD(TAG, "Success sending MQTT alarm state message");
    else
      ESP_LOGD(TAG, "Error sending MQTT alarm state message");
  } 
  else if (zone_or_system_update == ZONE_STATE_CHANGE) {
    // Convert zone ID to text
    char zoneIDtext[10];
    itoa(zoneID, zoneIDtext, 10);

    // Here we have a zone status change so put this information into JSON
    std::string message_text;
    message_text += "{\"zone_id\": \"";
    message_text += zoneIDtext;
    message_text += "\",\"zone_name\": \"";
    message_text += ZoneOrEvent;
    message_text += "\",\"zone_status\": \"";
    message_text += WhoOrState;
    message_text += "\"";
    message_text += "}";

    // Send zone state
    std::string zone_state_topic = App.get_name() + "/zone/state/" + zoneIDtext;
    if (this->publish(zone_state_topic, message_text, 0, true))
      ESP_LOGD(TAG, "Success sending MQTT zone state message");
    else
      ESP_LOGD(TAG, "Error sending MQTT zone state message");
  }
}

void PowerMaxDevice::log(int priority, const char* buf) {
  switch (priority) {
    case LOG_EMERG:
    case LOG_ALERT:
    case LOG_CRIT:
    case LOG_ERR:
      ESP_LOGE(TAG, "%s", buf);
      break;
    case LOG_WARNING:
      ESP_LOGW(TAG, "%s", buf);
      break;
    case LOG_NOTICE:
    case LOG_INFO:
      ESP_LOGI(TAG, "%s", buf);
      break;
    case LOG_DEBUG:
      ESP_LOGD(TAG, "%s", buf);
      break;
  }
}

void PowerMaxDevice::OnStatusChange(const PlinkBuffer* Buff) {
  // call base class implementation first, this will send ACK back and upate
  // internal state.
  PowerMaxAlarm::OnStatusChange(Buff);
  // Now send update to ST and use zone 0 as system state not zone
  unsigned char zoneId = 0;

  // now our customization:

  switch (Buff->buffer[4]) {
    case 0x51:  //"Arm Home"
    case 0x53:  //"Quick Arm Home"
      // do something...
      mqtt_send_("armed_home", GetStrPmaxEventSource(Buff->buffer[3]), zoneId, ALARM_STATE_CHANGE);
      break;

    case 0x52:  //"Arm Away"
    case 0x54:  //"Quick Arm Away"
      mqtt_send_("armed_away", GetStrPmaxEventSource(Buff->buffer[3]), zoneId, ALARM_STATE_CHANGE);
      break;

    case 0x55:  //"Disarm"
      mqtt_send_("disarmed", GetStrPmaxEventSource(Buff->buffer[3]), zoneId, ALARM_STATE_CHANGE);
      break;
  }
}

void PowerMaxDevice::OnStatusUpdatePanel(const PlinkBuffer* Buff) {
  // call base class implementation first, to log the event and update states.
  PowerMaxAlarm::OnStatusUpdatePanel(Buff);

  // Now if it is a zone event then send it to SmartThings
  if (this->isZoneEvent()) {
    const unsigned char zoneId = Buff->buffer[5];
    ZoneEvent eventType = (ZoneEvent)Buff->buffer[6];

    mqtt_send_(this->getZoneName(zoneId), GetStrPmaxZoneEventTypes(Buff->buffer[6]), zoneId, ZONE_STATE_CHANGE);
    // If it is a Violated (motion) event then set zone activated
    if (eventType == ZE_Violated) {
      zone_motion[zoneId] = true;
    }
  } else {
    // ALARM_STATE_CHANGE
    switch (this->stat) {
      case SS_Disarm:
        mqtt_send_("disarmed", "", 0, ALARM_STATE_CHANGE);
        break;
      case SS_Exit_Delay:
      case SS_Exit_Delay2:
        mqtt_send_("arming", "", 0, ALARM_STATE_CHANGE);
        break;
      case SS_Armed_Home:
        mqtt_send_("armed_home", "", 0, ALARM_STATE_CHANGE);
        break;
      case SS_Armed_Away:
        mqtt_send_("armed_away", "", 0, ALARM_STATE_CHANGE);
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
}

// Fired when system enters alarm state
void PowerMaxDevice::OnAlarmStarted(unsigned char alarmType, const char* alarmTypeStr, unsigned char zoneTripped,
                                    const char* zoneTrippedStr) {
  // call base class implementation first, to log the event and update states.
  PowerMaxAlarm::OnAlarmStarted(alarmType, alarmTypeStr, zoneTripped, zoneTrippedStr);
  // alarmType      : type of alarm, first 9 values from PmaxLogEvents
  // alarmTypeStr   : text representation of alarmType
  // zoneTripped    : specifies zone that initiated the alarm, values from
  // PmaxEventSource zoneTrippedStr : zone name

  mqtt_send_("triggered", zoneTrippedStr, 0, ALARM_STATE_CHANGE);
}

// Fired when alarm is cancelled
void PowerMaxDevice::OnAlarmCancelled(unsigned char whoDisarmed, const char* whoDisarmedStr) {
  // call base class implementation first, to log the event and update states.
  PowerMaxAlarm::OnAlarmCancelled(whoDisarmed, whoDisarmedStr);
  // whoDisarmed    : specifies who cancelled the alarm (for example a keyfob
  // 1), values from PmaxEventSource whoDisarmedStr : text representation of who
  // disarmed Canceled
  mqtt_send_("disarmed", whoDisarmedStr, 0, ALARM_STATE_CHANGE);
}

}  // namespace powermax
}  // namespace mqtt
}  // namespace esphome

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// This file contains OS specific implementation for ESP8266 used by PowerMax
// library If you build for other platrorms (like Linux or Windows, don't include
// this file, but provide your own)
int log_console_setlogmask(int mask) {
  static int lastmask;  // To satisfy the library, logging level is set in yaml
  int oldmask = lastmask;
  if (mask == 0) return oldmask; /* POSIX definition for 0 mask */
  lastmask = mask;
  return oldmask;
}

void os_debugLog(int priority, bool raw, const char* function, int line, const char* formt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, formt);
  vsnprintf(buf, sizeof(buf), formt, ap);
  va_end(ap);
  esphome::mqtt::powermax::global_pmd->log(priority, buf);
}

void os_usleep(int microseconds) { delay(microseconds / 1000); }

unsigned long os_getCurrentTimeSec() {
  static unsigned int wrapCnt = 0;
  static unsigned long lastVal = 0;
  unsigned long currentVal = millis();

  if (currentVal < lastVal) wrapCnt++;

  lastVal = currentVal;
  unsigned long seconds = currentVal / 1000;

  // millis will wrap each 50 days, as we are interested only in seconds, let's
  // keep the wrap counter
  return (wrapCnt * 4294967) + seconds;
}

int os_pmComPortRead(void* readBuff, int bytesToRead) {
  int dwTotalRead = 0;
  while (bytesToRead > 0) {
    for (int ix = 0; ix < 10; ix++)
      if (esphome::mqtt::powermax::global_pmd->available())
        break;
      else
        delay(5);

    if (esphome::mqtt::powermax::global_pmd->available() == false) break;

    *((char*)readBuff) = esphome::mqtt::powermax::global_pmd->read();
    dwTotalRead++;
    readBuff = ((char*)readBuff) + 1;
    bytesToRead--;
  }

  return dwTotalRead;
}

int os_pmComPortWrite(const void* dataToWrite, int bytesToWrite) {
  esphome::mqtt::powermax::global_pmd->write_array((const uint8_t*)dataToWrite, bytesToWrite);
  return bytesToWrite;
}

bool os_pmComPortClose() { return true; }

bool os_pmComPortInit(const char* portName) { return true; }

void os_strncat_s(char* dst, int dst_size, const char* src) { strncat(dst, src, dst_size); }

int os_cfg_getPacketTimeout() { return PACKET_TIMEOUT_DEFINED; }

// see PowerMaxAlarm::setDateTime for details of the parameters, if your OS does
// not have a RTC clock, simply return false
bool os_getLocalTime(unsigned char& year, unsigned char& month, unsigned char& day, unsigned char& hour,
                     unsigned char& minutes, unsigned char& seconds) {
  return false;  // IZIZTODO
}
