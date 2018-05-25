#ifndef KIOTL_DEBUG
#define KIOTL_DEBUG

#ifdef DEBUG_ENABLED 
#define DEBUG(...) { Serial.printf(__VA_ARGS__); Serial.println(); }
#define DEBUG_STARTUP { Serial.begin(115200); for(int i=0;i<10;i++) { DEBUG("Startup: %d", i); delay(500L); }; DEBUG("Started"); }
#else
#define DEBUG(...) { }
#define DEBUG_STARTUP { } 
#endif

#ifdef INFO_ENABLED 
#define INFO(...) { Serial.printf(__VA_ARGS__); Serial.println(); }
#define DEBUG_STARTUP { Serial.begin(115200); for(int i=0;i<10;i++) { INFO("Startup: %d", i); delay(500L); }; INFO("Started"); }
#else
#define INFO(...) { }
#endif

#endif



