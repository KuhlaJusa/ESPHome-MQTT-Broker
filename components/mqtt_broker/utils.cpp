#include "minibroker.h"
#include "esphome/core/application.h"

namespace esphome {
namespace mini_broker {

void MiniBroker::encodeRemainingLength(std::vector<uint8_t>& out, uint32_t value) {
    do {
        uint8_t byte = value % 128;
        value /= 128;
        if (value > 0) byte |= 0x80;
        out.push_back(byte);
    } while (value > 0);
}

int MiniBroker::decodeRemainingLength(const uint8_t* buf, size_t bufLen, uint32_t &length, size_t &consumed) {
    length = 0;
    consumed = 0;
    uint32_t multiplier = 1;

    for (size_t i = 0; i < bufLen && consumed < 4; ++i, ++consumed) {
        uint8_t encodedByte = buf[i];
        length += (encodedByte & 127) * multiplier;

        if ((encodedByte & 128) == 0) {
            return 0; // success
        }

        multiplier *= 128;
    }
    return -1; // incomplete
}

void MiniBroker::subscribeClientToTopic(Client* c, const uint8_t* topic, size_t len) {
    // Lookup or create TopicEntry
    TopicEntry* te = get_topic(topic, len);

    // Check if client is already subscribed to this topic
    for (Client* sub : te->subscribers) {
        if (sub == c) return; // already subscribed
    }

    te->subscribers.push_back(c);
    c->subscribed_topics.push_back(te);

    ESP_LOGD(TAG, "Client: %s subscribed to: %.*s", 
             c->clientId.c_str(), len, topic);
}

void MiniBroker::unsubscribeClientFromTopic(Client* c, const uint8_t* topic, size_t len) {
    // Lookup the topic entry
    TopicEntry* te = get_topic(topic, len);

    // Remove client from the topic's subscriber list
    unsubscribeClientFromTopic(c, te);

    // Remove topic from client's subscribed_topics
    auto& clientTopics = c->subscribed_topics;
    clientTopics.erase(std::remove(clientTopics.begin(), clientTopics.end(), te), clientTopics.end());
}

void MiniBroker::unsubscribeClientFromTopic(Client* c, TopicEntry* te) {
    te->subscribers.erase(
        std::remove(te->subscribers.begin(), te->subscribers.end(), c),
        te->subscribers.end()
    );
    ESP_LOGD(TAG, "Client %s unsubscribed from %s", c->clientId.c_str(), te->topicPtr->c_str());
    return;
}

} // namespace mqtt_broker
} // namespace esphome