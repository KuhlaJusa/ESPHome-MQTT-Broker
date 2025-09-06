#include "AsyncBroker.h"
#include "esphome/core/application.h"
#include "lwip_callback.h"

namespace esphome {
namespace asyncbroker {
    
void AsyncBroker::handleConnect(MqttClientHandler* handler, MqttEvent* ev) {
    uint8_t* data = ev->payload.data();
    size_t len = ev->payload.size();

    // variable header at least 12 bytes for connect message (header + 2bytes id length)
    if (len < 12) {
        ESP_LOGW(TAG, "Connection Header smaller than 12");
        handlerManager_.close_connection(handler, nullptr);
        return;
    }

    uint16_t protoNameLen = (data[0] << 8) | data[1];
    uint8_t protoVersion = data[2 + protoNameLen];

    if (protoNameLen == 6 && len < 14){
        ESP_LOGW(TAG, "Packet too short for Protocol Name, len: %d", len);
        handlerManager_.close_connection(handler, nullptr);
        return;
    }

    // Check for valid protocol name length
    if ((protoVersion == 3 && memcmp(&data[2], "MQIsdp", 6) != 0) ||
        (protoVersion >= 4 && memcmp(&data[2], "MQTT", 4) != 0)) {
        ESP_LOGW(TAG, "Protocol Name does not match version %u", protoVersion);
        handlerManager_.close_connection(handler, nullptr);
        return;
    }


    uint8_t connect_flags = data[protoNameLen + 3];
    uint16_t keepalive_time = (data[protoNameLen + 4] << 8) | data[protoNameLen + 5];

    // Payload starts here:
    size_t payloadPos = protoNameLen + 6;

    //already checked len >= 12, so payloadPos should be safe
    // if (payloadPos + 2 > len) {
    //     ESP_LOGW(TAG, "Packet too short for Client ID length");
    //     release_stub(s, false);
    //     return;
    // }

    // Client ID
    uint16_t clientIdLen = (data[payloadPos] << 8) | data[payloadPos + 1];
    payloadPos += 2;

    if (payloadPos + clientIdLen > len) {
        ESP_LOGW(TAG, "Packet too short for Client ID");
        handlerManager_.close_connection(handler, nullptr);
        return;
    }

    // Give id position and length to get_client()
    MqttClient* c = clientManager_.get_client(data + payloadPos, clientIdLen);
    c->protocol_version = protoVersion;
    // if client has a stub, release it
    if (c->handler){
        handlerManager_.close_connection(c->handler, c);
    } 

    // Attach stub to client
    handler->attach_MqttClient(c);

    c->keepalive = keepalive_time;

    bool cleanSession = (connect_flags & 2) >> 1;
    bool session_present = !c->cleanSession;
    // If clean session, clear old subscriptions
    if (cleanSession) {
        for (TopicEntry* te : c->subscribed_topics) {
            topicManager_.remove_subscriber(te, c);
            // Remove from topic's subscriber list
        }
        c->cleanSession = 1;
        // c->published_topics.clear();
        c->subscribed_topics.clear();
        session_present = 0;
    }
    else{
        c->cleanSession = 0;
    }

    ESP_LOGI(TAG, "Accept Client: %s, CleanSession: %d, KeepAlive: %d, ProtoVer: %d",
             c->clientId.c_str(), cleanSession, keepalive_time, protoVersion);

    // accept; ignore details for minimal broker
    // if (s->pcb) sendConnack_pcb(s->pcb, session_present, 0);
    auto connack = std::make_shared<std::vector<uint8_t>>();
    connack->resize(4);  // pre-allocate 4 bytes
    (*connack)[0] = 0x20;             // MQTT CONNACK packet type
    (*connack)[1] = 0x02;             // Remaining length
    (*connack)[2] = session_present;  // Connect Acknowledge Flags
    (*connack)[3] = 0x00;             // Connect Return Code

    handler->_queueMessage(std::move(connack));
}

void AsyncBroker::handleSubscribe(MqttClientHandler* handler, MqttEvent* ev){
    uint8_t* data = ev->payload.data();
    size_t len = ev->payload.size();
    MqttClient* c = handler->_mqttClient;
    if (!c) {
        ESP_LOGW(TAG, "Subscribe received but no client attached");
        handlerManager_.close_connection(handler, nullptr);
        return;
    }

    if (c->protocol_version != 3){
        if (ev->flags != 0x02){
            handlerManager_.close_connection(handler, c);
            return;
        }
    }

    if (len < 3) return;

    uint16_t packetId = (data[0] << 8) | data[1];
    size_t pos = 2;
    size_t topicsCount = 0;

    while (pos + 3 <= len) {
        uint16_t filterLen = (data[pos] << 8) | data[pos+1];
        pos += 2;
        if (pos + filterLen + 1 > len) break;

        const uint8_t* filter = data + pos;  // raw topic bytes
        pos += filterLen;

        uint8_t reqQos = data[pos++]; // requested QoS (ignored for now)
        // (void)reqQos;
        std::string topic_filter(reinterpret_cast<const char*>(filter), filterLen);
        topicManager_.add_subscriber(topic_filter, c);
        c->subscribed_topics.push_back(topicManager_.get_topic(topic_filter));

        // topicManager_.add_subscriber(topicManager_.get_topic(filter, filterLen), handler->_mqttClient);
        topicsCount++;
        ESP_LOGD(TAG, "wanted qos: %d, Subscribed to topic: %s", reqQos, topic_filter.c_str());
    }
    // send SUBACK
    auto suback = std::make_shared<std::vector<uint8_t>>();
    suback->reserve(1+4+2+topicsCount);  // estimate size
    suback->push_back(0x90);    // SUBACK fixed header

    uint32_t rl = 2 + (uint32_t)topicsCount;
    encodeRemainingLength(*suback, rl);

    suback->push_back((uint8_t)(packetId >> 8));
    suback->push_back((uint8_t)(packetId & 0xFF));

    suback->insert(suback->end(), topicsCount, 0x00);

    handler->_queueMessage(std::move(suback));
    // sendSuback_pcb(s->pcb, packetId, topicsCount);
}

void AsyncBroker::handleUnsubscribe(MqttClientHandler* handler, MqttEvent* ev){
    uint8_t* data = ev->payload.data();
    size_t len = ev->payload.size();

    if (len < 3) return;

    uint16_t packetId = (data[0] << 8) | data[1];
    size_t pos = 2;
    size_t topicsCount = 0;

    while (pos + 2 <= len) {
        uint16_t filterLen = (data[pos] << 8) | data[pos+1];
        pos += 2;
        if (pos + filterLen > len) break;

        const uint8_t* filter = data + pos;  // raw topic bytes
        pos += filterLen;

        topicManager_.remove_subscriber(topicManager_.get_topic(filter, filterLen), handler->_mqttClient);
        topicsCount++;
    }
    // send SUBACK
    auto unsuback = std::make_shared<std::vector<uint8_t>>();
    unsuback->resize(4);  // pre-allocate 4 bytes
    (*unsuback)[0] = 0xb0;             // MQTT UNSUBACK packet type
    (*unsuback)[1] = 0x02;             // Remaining length
    (*unsuback)[2] = data[0];           // Packet ID MSB
    (*unsuback)[3] = data[1];           // Packet ID LSB
    handler->_queueMessage(std::move(unsuback));
    // sendSuback_pcb(s->pcb, packetId, topicsCount);
}


void AsyncBroker::handlePublish(MqttClientHandler* handler, MqttEvent* ev){
    uint8_t* data = ev->payload.data();
    size_t len = ev->payload.size();

    if (len < 2) return;

    // Extract topic
    uint16_t topic_len = (data[0] << 8) | data[1];
    if (2 + topic_len > len) return;

    const uint8_t* topic_ptr = data + 2;
    const uint8_t* payload_ptr = data + 2 + topic_len;
    size_t payload_len = len - (2 + topic_len);

    
    //build publish packet to forward
    // ESP_LOGD(TAG, "Publish Received");
    auto publishPacket = std::make_shared<std::vector<uint8_t>>();
    publishPacket->reserve(1 + 4 + len);
    publishPacket->push_back(0x30);
    
    // Remaining length
    uint32_t rl = len;
    encodeRemainingLength(*publishPacket, len);
    
    // Payload
    publishPacket->insert(publishPacket->end(), data, data + len);
    
    std::string topic_str(reinterpret_cast<const char*>(topic_ptr), topic_len);

    // Forward to exact topic subscribers
    TopicEntry* topic = topicManager_.get_topic(topic_ptr, topic_len);
    if (topic) {
        for (auto c : topic->subscribers) {
            if (c->handler && c->handler->_pcb) {
                c->handler->_queueMessage(publishPacket);
                ESP_LOGD(TAG, "Published to Client: %s (Exact), Topic: %s", c->clientId.c_str(), topic_str.c_str());
            }
        }
    }

    // Forward to wildcard subscribers
    for (const auto& [pattern, client] : topicManager_.wildcard_subscriptions_) {
        if (topicManager_.topic_matches(pattern, topic_str)) {
            if (client->handler && client->handler->_pcb) {
                client->handler->_queueMessage(publishPacket);
                ESP_LOGD(TAG, "Published to Client: %s (Wildcard: %s), Topic: %s",
                        client->clientId.c_str(), pattern.c_str(), topic_str.c_str());
            }
        }
    }

    // Calling inside handlePublish
    if (publish_cb_) {
        ESP_LOGVV(TAG, "calling cb function");
        publish_cb_(topic_ptr, topic_len, payload_ptr, payload_len);
    }
}

void AsyncBroker::handlePingreq(MqttClientHandler* handler){
    // ESP_LOGD(TAG, "PINGREQ received");
    auto pingresp = std::make_shared<std::vector<uint8_t>>();
    pingresp->resize(2);
    (*pingresp)[0] = 0xD0; // PINGRESP packet type
    (*pingresp)[1] = 0x00; // Remaining length
    handler->_queueMessage(std::move(pingresp));
}

} // namespace asyncbroker
} // namespace esphome