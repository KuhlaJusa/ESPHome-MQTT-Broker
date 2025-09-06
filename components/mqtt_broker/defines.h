#pragma once
// #include <memory>
// #include <vector>
// #include <stdint.h>

namespace asyncbroker {

#define RESPONSE_MIN_INFLIGH 2 * 1460   // allow 2 MSS packets
#define RESPONSE_MAX_INFLIGH 16 * 1024  // but no more than 16k, no need to blow it, since same data is kept in local Q

#define MAX_EVENTS_PER_CLIENT 16
#define MAX_PACKETS_PER_SECOND 16

#define POLL_INTERVAL_S 5 * 2 
#define TIMOUT_INTERVAL_MS 30000

#define MAX_CLIENTS 16

#define RESERVED_TOPICS 16
#define RESERVED_SUBSCRIBER_PER_TOPIC 16



} //namespace esphome