#include <Arduino.h>
#include <FS.h>

#include "kiotl_debug.h"
#include <CRC32.h>


#ifndef KIOTL_RTC
#define KIOTL_RTC

#define MAGIC_NUMBER 35
#define DATA_OFFSET 16

// CRC function used to ensure data validity
uint32_t rtcCalculateCRC32(const uint8_t *data, size_t length); 

// Structure which will be stored in RTC memory.
// First field is CRC32, which is calculated based on the
// rest of structure contents.
// Any fields can go after CRC32.
// We use byte array as an example.
struct {
  uint32_t crc32;
  uint32_t uncommited_count;
  uint32_t last_good_transaction_id;
  uint32_t restarts;
  byte data[512-DATA_OFFSET];
} rtcData;


void rtcCalcCrcAndWriteToRtc() {
  // Update CRC32 of data
  rtcData.crc32 = rtcCalculateCRC32(((uint8_t*) &rtcData) + DATA_OFFSET, sizeof(rtcData) - DATA_OFFSET);
  // And write data
  if (ESP.rtcUserMemoryWrite(0, (uint32_t*) &rtcData, sizeof(rtcData))) {
  }
}

bool rtcShouldTryUpdateBasedOnRtc() {
    if (rtcData.last_good_transaction_id == 0) {
        rtcData.last_good_transaction_id++;
        rtcCalcCrcAndWriteToRtc();
        return true;
    }
    return false;
}

uint32_t rtcGetUncommittedCount() {
    return rtcData.uncommited_count;
}

uint32_t rtcGetLastGoodTransactionId() {
    return rtcData.last_good_transaction_id;
}

void rtcAllClicksCommitted() {
    rtcData.last_good_transaction_id++;
    rtcData.uncommited_count = 0;    
    rtcData.restarts = 0;
    rtcCalcCrcAndWriteToRtc();
    DEBUG("Clicks committed.");
}

void rtcDeepSleep() {
  DEBUG("Deep sleep");
  ESP.deepSleep(0, WAKE_RF_DEFAULT);
}

void rtcConditionalRestart() {
  DEBUG("Decrementing count and restarting since WIFI did not work.");
    if (rtcData.restarts<5) {
      rtcData.uncommited_count--;
      rtcData.restarts++;
      rtcCalcCrcAndWriteToRtc();
      ESP.restart();
    } else {
      DEBUG("Giving up -> too many restarts because of Wifi not working.");
      rtcDeepSleep();
    }    
}

// Read and initialize RTC if needed, keep it in memory and add another click.
void rtcReadAndAddClick() {
  // Read struct from RTC memory
  if (ESP.rtcUserMemoryRead(0, (uint32_t*) &rtcData, sizeof(rtcData))) {
    uint32_t crcOfData = rtcCalculateCRC32(((uint8_t*) &rtcData) + DATA_OFFSET, sizeof(rtcData) - DATA_OFFSET);
    // Initialize RTC
    if (crcOfData != rtcData.crc32) {
      for (int i = 0; i < sizeof(rtcData); i++) {
        rtcData.data[i] = MAGIC_NUMBER;
      } 
      DEBUG("Bad rtc data detected");
      rtcData.uncommited_count = 0;    
      rtcData.last_good_transaction_id = 0;
      rtcData.restarts = 0;
    }
    else {
      // Increase uncommited_count
      rtcData.uncommited_count++;
      DEBUG("Current count is %d", rtcData.uncommited_count);
    }
  }
  rtcCalcCrcAndWriteToRtc();
}

uint32_t rtcCalculateCRC32(const uint8_t *data, size_t length)
{
  CRC32 crc;
  while (length--) {
    uint8_t c = *data++;
    crc.update(c);
  }
  return crc.finalize();
}

uint32_t rtcCalculateCRC32fromFile(File f)
{
  CRC32 crc;
  while (f.available()) {
    uint8_t c = f.read();
    crc.update(c);
  }
  return crc.finalize();
}

#endif

