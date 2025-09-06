#include "AsyncBroker.h"
#include "esphome/core/application.h"
#include "lwip_callback.h"

namespace esphome {
namespace asyncbroker {

void MqttClientHandler::_onData(uint8_t* data, size_t len) {
    if (!rxQueue) return;

    uint8_t* pdata = (uint8_t*)data;
    size_t offset = 0;

    // If we have a partial packet buffered, try to complete it first
    if (!buf.empty()) {

        // Copy only what we need to complete header or first part of packet
        size_t toCopy = std::min(len, 5 - buf.size());
        if (toCopy > 0) buf.reserve(buf.size() + toCopy);
        buf.insert(buf.end(), pdata, pdata + toCopy);
        offset += toCopy;

        // Try parsing full packet length
        uint8_t headerLen = 0;
        size_t payloadLen = getMqttPayloadLength(buf.data(), buf.size(), &headerLen);

        //should not happen with tcp, but just in case
        if (payloadLen == -2) {
            ESP_LOGW(TAG, "MQTT Packet Remaining Length too large, dropping");
            buf.clear();
            return;
        }
        if (payloadLen < 0) return; // incomplete, wait for next data

        size_t fullPacketLen = headerLen + payloadLen;
        
        // Copy remaining bytes to complete the packet
        toCopy = std::min(fullPacketLen  - buf.size(), len - offset);
        if (toCopy > 0) buf.reserve(buf.size() + toCopy);
        buf.insert(buf.end(), pdata + offset, pdata + offset + toCopy);
        offset += toCopy;

        if (buf.size() < fullPacketLen) return; // incomplete packet

        uint8_t packetType = buf[0] >> 4;
        uint8_t flags = buf[0] & 0x0F;
        safeEnqueueMqttEvent(rxQueue, packetType, flags, buf.data() + headerLen, payloadLen);

        buf.clear();
    }

    // Now process any full packets directly from pdata
    while (offset < len) {

        // 2️⃣ No leftover: check if enough for minimum header
        if (len - offset < 2) {
            buf.reserve(buf.size() + (len - offset));
            buf.insert(buf.end(), pdata + offset, pdata + len);
            return;
        }

        uint8_t headerLen = 0;
        size_t payloadLen = getMqttPayloadLength(pdata + offset, len - offset, &headerLen);
        if (payloadLen == -2) {
            ESP_LOGW(TAG, "MQTT Packet Remaining Length too large, dropping");
            buf.clear();
            return;
        }
        size_t fullPacketLen = headerLen + payloadLen;
        if (payloadLen < 0 || (len - offset) < fullPacketLen) {
            // incomplete, store in buffer
            buf.reserve(buf.size() + (len - offset));
            buf.insert(buf.end(), pdata + offset, pdata + len);
            return;
        }

        // We have a full packet in pdata
        uint8_t packetType = pdata[offset] >> 4;
        uint8_t flags = pdata[offset] & 0x0F;
        safeEnqueueMqttEvent(rxQueue, packetType, flags, pdata + offset + headerLen, payloadLen);

        offset += fullPacketLen;
    }
}

ssize_t MqttClientHandler::getMqttPayloadLength(const uint8_t* data, size_t len, uint8_t* headerLen) {
    if (len < 2) return -1; // need at least 2 bytes for type + first length byte

    size_t multiplier = 1;
    size_t value = 0;
    size_t i = 1; // first byte is packet type/flags
    uint8_t encodedByte = 0;

    do {
        if (i >= len) return -1; // incomplete remaining length
        encodedByte = data[i++];
        value += (encodedByte & 127) * multiplier;
        multiplier *= 128;
        if (multiplier > 128*128*128*128) return -2; // too large
    } while (encodedByte & 128);

    *headerLen = i; // fixed header length = type/flags + remaining length bytes
    return value;   // payload length
}

bool MqttClientHandler::check_and_perform_disconnect(uint8_t packetType) {
    if (packetType != 14) return false; // not DISCONNECT
    ESP_LOGD(TAG, "MQTT Disconnect Event");
    detach();
    return true;
}

void MqttClientHandler::safeEnqueueMqttEvent(QueueHandle_t _queue, uint8_t packetType, uint8_t flags, const uint8_t* payloadData, size_t len) {
    if (!_queue) return;
    if(check_and_perform_disconnect(packetType)) return;

    // Check if the queue is full
    if (uxQueueSpacesAvailable(_queue) <= 0) {
        ESP_LOGW(TAG, "MQTT RX Queue Full, dropping packet");
        return;
    }

    // Create event only if queue has space
    auto ev = std::make_unique<MqttEvent>();
    ev->packetType = packetType;
    ev->flags = flags;
    ev->payload.resize(len);
    memcpy(ev->payload.data(), payloadData, len);

    ESP_LOGD(TAG, "MQTT Event: type=%u flags=%u len=%u", packetType, flags, (unsigned)len);
    // ESP_LOG_BUFFER_HEXDUMP(TAG, payloadData, len, ESP_LOG_DEBUG);

    // Release ownership to queue
    MqttEvent* raw = ev.release();
    if (xQueueSend(_queue, &raw, 0) == pdTRUE) {
        // Only notify if successfully enqueued
        handler_manager_.notify_broker();
    } else {
        ESP_LOGW(TAG, "MQTT RX Queue Full on send, dropping packet");
        delete raw;
        // shared_ptr auto cleans up
    }
}

} //namespace asyncbroker
} //namespace esphome