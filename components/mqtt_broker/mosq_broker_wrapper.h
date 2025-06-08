/* 
This block ensures compatibility between C and C++ code.
ESPHome components are based on C++ Code, but the espressif mosquitto port is using C Code. 
https://github.com/espressif/esp-protocols/tree/master/components/mosquitto
*/

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "mosq_broker.h" // Include the C header for the MQTT broker implementation

#ifdef __cplusplus
}
#endif