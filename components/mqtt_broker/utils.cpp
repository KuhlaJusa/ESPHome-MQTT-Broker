#include "AsyncBroker.h"
#include "esphome/core/application.h"
#include "lwip_callback.h"

namespace esphome {
namespace asyncbroker {

void AsyncBroker::encodeRemainingLength(std::vector<uint8_t>& out, uint32_t value) {
    if (value > 0x0FFFFFFF) {
        ESP_LOGE("mqtt", "Remaining length too large: %u", value);
        value = 0x0FFFFFFF;
    }
    do {
        uint8_t byte = value % 128;
        value /= 128;
        if (value > 0) byte |= 0x80;
        out.push_back(byte);
    } while (value > 0);
}

// void AsyncBroker::encodeRemainingLength(const uint8_t* topic_ptr, size_t topic_len, uint32_t value) {
//     do {
//         uint8_t byte = value % 128;
//         value /= 128;
//         if (value > 0) byte |= 0x80;
//         out.push_back(byte);
//     } while (value > 0);
// }

} //namespace asyncbroker
} //namespace esphome