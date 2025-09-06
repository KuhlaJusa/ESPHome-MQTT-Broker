#include "AsyncBroker.h"
#include "esphome/core/application.h"
#include "lwip_callback.h"

namespace esphome {
namespace asyncbroker {

TopicManager::TopicManager(size_t reserve_subs) : reserve_subscribers_(reserve_subs) {topics_.reserve(RESERVED_TOPICS);}

// Get or create a topic by raw bytes
TopicEntry* TopicManager::get_topic(const uint8_t* topic_ptr, size_t topic_len) {
    std::string key(reinterpret_cast<const char*>(topic_ptr), topic_len);

    auto it = topics_.find(key);
    if (it != topics_.end()) {
        return it->second.get();
    }

    ESP_LOGD(TAG, "Creating new topic: %s", key.c_str());
    // Topic not found → create new
    auto te = std::make_unique<TopicEntry>();
    te->subscribers.reserve(reserve_subscribers_);
    auto result = topics_.emplace(std::move(key), std::move(te));
    result.first->second->topicPtr = &result.first->first;

    return result.first->second.get();
}

// Get or create a topic by string
TopicEntry* TopicManager::get_topic(const std::string& topic) {
    return get_topic(reinterpret_cast<const uint8_t*>(topic.data()), topic.size());
}


// Add a subscriber to a topic
void TopicManager::add_subscriber(TopicEntry* topic, MqttClient* client) {
    if (!topic || !client) {
        ESP_LOGW(TAG, "add_subscriber called with null topic or client");
        return;
    }
    auto& subs = topic->subscribers;
    if (std::find(subs.begin(), subs.end(), client) == subs.end()) {
        subs.push_back(client);
        ESP_LOGD(TAG, "Client subscribed to topic: %s", topic->topicPtr->c_str());
    }
}

// Add a subscriber by topic string (handles wildcards)
void TopicManager::add_subscriber(const std::string& topic, MqttClient* client) {
    if (!client) return;

    if (is_wildcard(topic)) {
        // Check if already subscribed
        for (auto& [t, c] : wildcard_subscriptions_) {
            if (t == topic && c == client) {
                ESP_LOGD(TAG, "Client already subscribed to wildcard topic: %s", topic.c_str());
                return;
            }
        }

        // Not found, add new subscription
        wildcard_subscriptions_.emplace_back(topic, client);
        ESP_LOGD(TAG, "Client subscribed to wildcard topic: %s", topic.c_str());
    } else {
        TopicEntry* te = get_topic(topic);
        add_subscriber(te, client);
    }
}

// Remove a subscriber from a topic
void TopicManager::remove_subscriber(TopicEntry* topic, MqttClient* client) {
    if (!topic || !client) {
        ESP_LOGW(TAG, "add_subscriber called with null topic or client");
        return;
    }
    auto& subs = topic->subscribers;
    subs.erase(std::remove(subs.begin(), subs.end(), client), subs.end());
    ESP_LOGD(TAG, "Client unsubscribed from topic: %s", topic->topicPtr->c_str());
}

// Remove a client from all topics (exact and wildcard)
void TopicManager::remove_client(MqttClient* client) {
    if (!client) return;

    // Exact topics
    for (auto& [_, topic] : topics_) {
        remove_subscriber(topic.get(), client);
    }

    // Wildcard subscriptions
    wildcard_subscriptions_.erase(
        std::remove_if(wildcard_subscriptions_.begin(), wildcard_subscriptions_.end(),
                       [client](const auto& pair) { return pair.second == client; }),
        wildcard_subscriptions_.end());
}

// // Publish a message to a topic (exact + wildcard delivery)
// void TopicManager::publish(const std::string& topic, const std::string& payload) {
//     // Exact matches
//     auto it = topics_.find(topic);
//     if (it != topics_.end()) {
//         for (auto* client : it->second->subscribers) {
//             client->send_message(topic, payload);
//         }
//     }

//     // Wildcard matches
//     for (const auto& [pattern, client] : wildcard_subscriptions_) {
//         if (topic_matches(pattern, topic)) {
//             client->send_message(topic, payload);
//         }
//     }
// }

// Utility: check if a topic contains MQTT wildcards
bool TopicManager::is_wildcard(const std::string& topic) const {
    return topic.find('+') != std::string::npos || topic.find('#') != std::string::npos;
}

// Utility: match a topic against a wildcard pattern
bool TopicManager::topic_matches(const std::string& pattern, const std::string& topic) const {
    size_t p_pos = 0, t_pos = 0;

    while (p_pos < pattern.length() && t_pos < topic.length()) {
        size_t p_next = pattern.find('/', p_pos);
        size_t t_next = topic.find('/', t_pos);

        std::string p_token = pattern.substr(p_pos, p_next - p_pos);
        std::string t_token = topic.substr(t_pos, t_next - t_pos);

        if (p_token == "#") return true;
        if (p_token != "+" && p_token != t_token) return false;

        if (p_next == std::string::npos || t_next == std::string::npos) break;
        p_pos = p_next + 1;
        t_pos = t_next + 1;
    }

    // Final checks: allow trailing `#`
    if (p_pos < pattern.length()) {
        return pattern.substr(p_pos) == "#";
    }

    return p_pos == pattern.length() && t_pos == topic.length();
}


} //namespace asyncbroker
} //namespace esphome