#include <Arduino.h>
#include <ArduinoOTA.h>
#include <Time.h>
#include <TimeLib.h>
#include "kiotl_DSTadjust.h"

#include <ESP8266WiFi.h>

	
#include "FS.h"
#include "kiotl_debug.h"
#include "kiotl_rtc.h"

#define KIOTL_SSID "SSID"
#define KIOTL_SSID_PASS "SSID_PASS"
#define KIOTL_OTA_PASS "OTA_PASS"

#define KIOTL_SSID_OPEN "SSID_OPEN"

#ifndef KIOTL_OTA
#define KIOTL_OTA


/*
 * Typically you would declare it and do this in setup:

KiotlOTA ota("FallbackDefaultPassword"); // Arduino OTA default password, in case everything is corrupted.
void setup() {
 // do something most important (like updating RTC memory)

 // Load config:
 ota.loadConfig();

 // extract config, do other non-important stuff. 
 // call ota.readKey() and/or ota.setAPMode() as you please.
  
 // at the end, call begin():
 ota.begin();
 */
class KiotlOTA {
private:
   
  String _config_file;
  bool _ap_mode = false; 
  String* _keys = NULL;
  String* _values = NULL;
  int _size = 0;
  String _prefix;
  long _tz_seconds = 0;
  unsigned long _connect_started = 0;
  bool _connected = false;
  bool _time_set = false;
  bool _time_set_first = false;
  simpleDSTadjust _northAmericaDST;
  
  // Initialized in constructor;
  FS* _fs;
  String _password;
  String _ap_ssid_password;
  bool _free_ssid_tried = false;
  int _wifi_connection_seconds;

  void clearKeysAndValues() {
    _size = 0;
    delete[] (_keys);
    delete[] (_values);
  }

  String hostname() {
    return _prefix + (String)ESP.getChipId();    
  }
  
  void setupWifi(const char *ssid, const char* pass) {
    DEBUG("Starting wifi");
    WiFi.begin(ssid, pass);
    WiFi.persistent(false);
    WiFi.mode(WIFI_OFF);
    WiFi.hostname(hostname().c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);    
    _connect_started = now();
  }

  void setupAP() {
    DEBUG("Setting up AP");
    WiFi.hostname(hostname().c_str());
    WiFi.mode(WIFI_AP);
    WiFi.softAP(hostname().c_str(), _ap_ssid_password.c_str());  
    ArduinoOTA.setHostname(hostname().c_str());
    ArduinoOTA.setPassword(_password.c_str());
    ArduinoOTA.onStart([]() {
        DEBUG("StartOTA\r\n");
    });
    ArduinoOTA.onEnd(std::bind([](FS *fs) {
        fs->end();
        DEBUG("\r\nEnd OTA\r\n");
        ESP.restart();
    }, _fs));
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        DEBUG("OTA Progress: %u%%\r\n", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        DEBUG("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) DEBUG("Auth Failed\r\n")
        else if (error == OTA_BEGIN_ERROR) DEBUG("Begin Failed\r\n")
        else if (error == OTA_CONNECT_ERROR) DEBUG("Connect Failed\r\n")
        else if (error == OTA_RECEIVE_ERROR) DEBUG("Receive Failed\r\n")
        else if (error == OTA_END_ERROR) DEBUG("End Failed\r\n");
        delay(60000);
        ESP.restart();
    });
    ArduinoOTA.begin();
  }
  
public:
  KiotlOTA(String default_password, String ap_ssid_password,
      String config_file,
      String prefix,
      int timezone_seconds,
      int wifi_connection_seconds = 10) :
      // Initialize DST time zone start / end + offset
      _northAmericaDST(/* start */ Second, Sun, Mar, 2, 3600,  
                       /* end   */ First,  Sun, Nov, 2, 0)
      {
    _fs = &SPIFFS;
    _config_file = config_file;
    _prefix = prefix;
    _password = default_password; 
    _ap_ssid_password = ap_ssid_password;
    _wifi_connection_seconds = wifi_connection_seconds;
    _keys = new String[0];
    _values = new String[0];
    _tz_seconds = timezone_seconds;    
  }

  virtual ~KiotlOTA() {
    clearKeysAndValues();
  }

  void setAPMode(bool apmode) {
    _ap_mode = apmode;
  }

  void loadConfig() {    
    DEBUG("Loading config, my hostname is %s", hostname().c_str());
    _fs->begin();
    // determine number of strings:
    File f = SPIFFS.open(_config_file, "r");
    if (!f) {
      DEBUG("Empty SPIFFS.");
      return;
    }
    uint32_t crc = rtcCalculateCRC32fromFile(f);
    f.seek(0);
    // new char[size] and delete[]
    int stringCounter = 0;
    while(f.available()) {
      f.readStringUntil('\n');
      stringCounter++;
    }
    f.seek(0);
    clearKeysAndValues();
    _keys = new String[stringCounter];
    _values = new String[stringCounter];
    _size = stringCounter;
    String line;
    stringCounter = 0;
    while(f.available()) {
      line = f.readStringUntil('\n');
      // Split line into key and value;
      int i = line.indexOf('=');
      String key = line.substring(0, i);
      String value = line.substring(i+1);
      if (value[value.length()-1] == '\r') {
        value = value.substring(0, value.length()-1);
      }
      _keys[stringCounter] = key;
      _values[stringCounter] = value;
      DEBUG("key=%s, value=%s", key.c_str(),value.c_str());
      stringCounter++;      
    }
    f.close();
    f = SPIFFS.open(String(_config_file) + ".crc", "r");
    if (!f) {
      DEBUG("No crc file.");
      clearKeysAndValues();
      return;
    }
    line = f.readStringUntil(' ');
    DEBUG("crc from file: '%s'", line.c_str());
    DEBUG("crc calculated: '%u'", crc);
    char str[20];
    sprintf( str, "%u", crc );
    if(line != str) {
      // Clear everything -> files are corrupted.
      DEBUG("Corrupted files.");
      clearKeysAndValues();
    }
    f.close();
    _fs->end();
  }

  String readKey(String key) {
    for (int i = 0; i < _size; i++) {
      if (_keys[i] == key) {
        return _values[i];
      }
    }
    return "";
  }

  void begin() {
    DEBUG("Let's begin");
    // We need to decide now if we should start in AP mode or not.
    String ssid = readKey(KIOTL_SSID);
    String pass = readKey(KIOTL_SSID_PASS);
    String ota_password = readKey(KIOTL_OTA_PASS);
    if (ota_password.length() > 0) {
      // Override OTA password.
      _password = ota_password;
    }
    // Override AP mode in case ssid not specified
    if (ssid.length() == 0) {
      _ap_mode = true;    
    }
    if (_ap_mode) {
      setupAP();
    } else {
      setupWifi(ssid.c_str(), pass.c_str());
    }
  }

  bool wifiConnected() {
    if (_ap_mode) {
      return false;
    }
    if (_connected) {
      return true;
    }
    bool res = WiFi.isConnected();
    if (res) {
      configTime(_tz_seconds, 0, "time.nist.gov", "time.windows.com", "ca.pool.ntp.org");
      _connected = true; 
    }
    return res;
  }

  void checkTime() {
    if (_connected) {
      time_t this_second;
      time(&this_second);
      if (this_second && hour()==0) {
        if(minute() == 0)
        {
          setTime(this_second);
          _time_set = true;
        } 
      } else {
          _time_set = false;
      }
    }
  }

  bool timeSetFirstTime() {
     if (_time_set && !_time_set_first) {
       _time_set_first = true;
       return true;
     }
     return false;
  }

  bool timeSet() {
    return _time_set_first;
  }

  bool wifiConnectedFirstTime() {
    if (_connected) {
      return false;
    }
    return wifiConnected();
  }

  bool usesOpenSSID() {
    return _free_ssid_tried;
  }

  bool wifiError() {
    if (_ap_mode) {
      return false;
    }
    bool res = !_connected && (now()-_connect_started > _wifi_connection_seconds);
    if (res && !_free_ssid_tried) { // not(_ap_mode) assumed
       String ssid = readKey(KIOTL_SSID_OPEN); 
       _free_ssid_tried = true;
       if (ssid.length() > 0) {
         setupWifi(ssid.c_str(), "");
	 return false;
       }
    }
    return res;
  }

  void handle() {
    if (_ap_mode) {
      ArduinoOTA.handle();
    }
    checkTime();
  }  

  time_t getTime() {
    return _northAmericaDST.time(NULL);
  }
};

#endif
