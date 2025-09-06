#pragma once

extern "C" {
    #include "lwip/err.h"
    #include "lwip/tcp.h"
    #include "lwip/tcpip.h"
    // #include "lwip/tcp_priv.h"
    // #include "lwip/inet.h"
}

extern struct tcp_pcb *tcp_active_pcbs;   // forward declare
extern struct tcp_pcb *tcp_tw_pcbs;       // TIME-WAIT list
extern union tcp_listen_pcbs_t tcp_listen_pcbs; // listening sockets

// #include "MqttClientHandler.h"
#include <list>
#include <mutex>
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>

#include "freertos/task.h"
#include "defines.h"
#include "SPSCQueue.h"
#include "esphome/core/application.h"
namespace esphome {
namespace asyncbroker {
static const char *const TAG = "async_broker";

using MqttResponse_SharedData_t = std::shared_ptr<std::vector<uint8_t>>;
// using MqttEventPtr = std::unique_ptr<MqttEvent>;
class AsyncBroker;
class MqttClientHandler;
class MqttResponseMessage;
struct MqttEvent;
struct MqttClient;
struct TopicEntry;


struct TopicEntry {
  const std::string* topicPtr = nullptr;
  std::vector<MqttClient*> subscribers;
};

class TopicManager {
public:
    TopicManager(size_t reserve_subs = RESERVED_SUBSCRIBER_PER_TOPIC);

    // Get or create a topic by raw bytes
    TopicEntry* get_topic(const uint8_t* topic_ptr, size_t topic_len);

    // Get or create a topic by string
    TopicEntry* get_topic(const std::string& topic);

    // Add a subscriber to a topic
    void add_subscriber(TopicEntry* topic, MqttClient* client);
    void add_subscriber(const std::string& topic, MqttClient* client);
    
    // Remove a subscriber from a topic
    void remove_subscriber(TopicEntry* topic, MqttClient* client);

    // Remove a client from all topics
    void remove_client(MqttClient* client);

    // Optional: get all topics
    const std::unordered_map<std::string, std::unique_ptr<TopicEntry>>& topics() const {
        return topics_;
    }
    std::vector<std::pair<std::string, MqttClient*>> wildcard_subscriptions_;  // NEW
    void publish(const std::string& topic, const std::string& payload);
    bool is_wildcard(const std::string& topic) const;
    bool topic_matches(const std::string& pattern, const std::string& topic) const;
private:
    size_t reserve_subscribers_;
    std::unordered_map<std::string, std::unique_ptr<TopicEntry>> topics_;
};


struct MqttClient {
    std::recursive_mutex _client_lock;
    portMUX_TYPE _lock_clients = portMUX_INITIALIZER_UNLOCKED;
    MqttClientHandler* handler = nullptr;
    bool cleanSession = 1;
    uint16_t keepalive = 60; // seconds
    std::string clientId;
    uint8_t protocol_version = 4;
    std::vector<TopicEntry*> subscribed_topics; // pointers to existing TopicEntry objects
    // std::vector<TopicEntry*> published_topics; // pointers to existing TopicEntry objects
    void disconnect() {
        handler = nullptr;
        // cleanSession = true;
        // keepalive = 60;
        // clientId.clear(); keep clientId for reconnects
        // subscribed_topics.clear();
        // published_topics.clear();
    }
};

struct MqttEvent {
    uint8_t packetType;
    uint8_t flags;
    std::vector<uint8_t> payload;
};

// MqttClientManager

class MqttClientManager {
public:
    MqttClientManager(size_t pool_size = MAX_CLIENTS);

    // Get existing client by ID or allocate a free one
    MqttClient* get_client(const uint8_t* idData, size_t idLen);

    // Optional: find client by handler
    MqttClient* find_by_handler(MqttClientHandler* handler);

    // Optional: remove client (mark as unused)
    void remove_client(MqttClient* client);

    std::vector<std::unique_ptr<MqttClient>>& clients() { return clients_; }
    void close_connection(MqttClientHandler* handler, MqttClient* client);
private:
    std::vector<std::unique_ptr<MqttClient>> clients_;
};


//HandlerManager

class HandlerManager{
public:
    HandlerManager(size_t pool_size = MAX_CLIENTS){};

    // returns a handler that is currently not associated with a tcp connection
    MqttClientHandler* get_free_handler();
    
    void _addHandler(MqttClientHandler *handler);
    void _handleDisconnect(MqttClientHandler *handler);
    std::list<MqttClientHandler*> _handlers;
    void close_connection(MqttClientHandler* handler, MqttClient* client);
    TaskHandle_t _broker_task_handle = NULL;

    void set_broker_task(TaskHandle_t handle) {
        _broker_task_handle = handle;
    }

    void notify_broker() {
        // ESP_LOGI(TAG, "task_handle: %p", _broker_task_handle);
        if (_broker_task_handle) {

            xTaskNotifyGive(_broker_task_handle);
        }
        else{
            ESP_LOGW(TAG, "No broker task handle set, cannot notify");
        }
    }
private:
    std::list<MqttClientHandler*> _connected_handlers;
    portMUX_TYPE _conHandler_lock = portMUX_INITIALIZER_UNLOCKED;
    #ifdef ESP32
        mutable std::recursive_mutex _conHandler_queue_lock;
    #endif

    void _adjust_inflight_window();
};


//AsyncBroker

class AsyncBroker{
    
public:
    AsyncBroker(uint16_t port = 1883) : _port(port){};
    ~AsyncBroker() {
        stop();
    };

    //use these from any task
    bool start();
    bool stop();

    //called from inside tcpip thread, do not use
    void start_broker();
    void stop_broker();

    void set_port(uint16_t port){ _port = port; }
    using PublishCallback = void(*)(const uint8_t* topic, size_t topic_len,
    const uint8_t* payload, size_t payload_len);
    void set_publish_cb(PublishCallback cb) { publish_cb_ = cb;}
    err_t OnConnect(struct tcp_pcb* newpcb, err_t err);
    TaskHandle_t _broker_task_handle = NULL;

    HandlerManager handlerManager_{MAX_CLIENTS};
    MqttClientManager clientManager_{MAX_CLIENTS};
    TopicManager topicManager_{RESERVED_SUBSCRIBER_PER_TOPIC};
private:
    uint16_t _port;
    tcp_pcb* _listen_pcb;
    //loop task that processes the mqtt events
    PublishCallback publish_cb_ = nullptr;


    // std::list<Client*> clients_;
    // std::unordered_map<std::string, TopicEntry*> topics_;
    
    void parse_event(MqttClientHandler* handler, MqttEvent* ev);
    void broker_task();


    void encodeRemainingLength(std::vector<uint8_t>& out, uint32_t value);
    //MQTT
      // ------------------ MQTT ---------------------
    void handleConnect(MqttClientHandler* handler, MqttEvent* ev);
    void handlePublish(MqttClientHandler* handler, MqttEvent* ev);
    void handlePingreq(MqttClientHandler* handler);
    void handleSubscribe(MqttClientHandler* handler, MqttEvent* ev);
    void handleUnsubscribe(MqttClientHandler* handler, MqttEvent* ev);


    std::string to_hex_string(const std::vector<uint8_t>& data) {
        std::string out;
        out.reserve(data.size() * 3);  // pre-allocate
        char buf[4];
        for (size_t i = 0; i < data.size(); i++) {
            snprintf(buf, sizeof(buf), "%02X", data[i]);  // uppercase hex
            out += buf;
            if (i != data.size() - 1) out += ' ';
        }
        return out;
    };

    static const char *tcp_state_str(enum tcp_state s) {
        switch (s) {
            case CLOSED:      return "CLOSED";
            case LISTEN:      return "LISTEN";
            case SYN_SENT:    return "SYN_SENT";
            case SYN_RCVD:    return "SYN_RCVD";
            case ESTABLISHED: return "ESTABLISHED";
            case FIN_WAIT_1:  return "FIN_WAIT_1";
            case FIN_WAIT_2:  return "FIN_WAIT_2";
            case CLOSE_WAIT:  return "CLOSE_WAIT";
            case CLOSING:     return "CLOSING";
            case LAST_ACK:    return "LAST_ACK";
            case TIME_WAIT:   return "TIME_WAIT";
            default:          return "UNKNOWN";
        }
    }

    void dump_active_pcbs(void) {
        struct tcp_pcb *pcb;

        ESP_LOGI(TAG, "=== Active TCP PCBs ===");
        for (pcb = tcp_active_pcbs; pcb != NULL; pcb = pcb->next) {
            ESP_LOGI(TAG,
                    " local %s:%d -> remote %s:%d  state=%s",
                    ipaddr_ntoa(&pcb->local_ip), pcb->local_port,
                    ipaddr_ntoa(&pcb->remote_ip), pcb->remote_port,
                    tcp_state_str(pcb->state));
        }
    }
};


//MqttResponse

class MqttResponseMessage {

private:
    MqttResponse_SharedData_t _data;
    size_t _sent{0};   // num of bytes already sent
    size_t _acked{0};  // num of bytes acked

public:
    MqttResponseMessage() 
        : _data(std::make_shared<std::vector<uint8_t>>()), _sent(0), _acked(0) {}
    MqttResponseMessage(MqttResponse_SharedData_t data) : _data(data){};

    MqttResponseMessage(const uint8_t *data, size_t len) : _data(std::make_shared<std::vector<uint8_t>>(data, data+len)){};

    size_t ack(size_t len, uint32_t time = 0);


    size_t write(tcp_pcb *pcb);
    size_t send(tcp_pcb *pcb);

    // returns true if full message's length were acked
    bool finished() {
      return _acked == _data->size();
    }

    bool sent() {
      return _sent == _data->size();
    }
};

// MqttClientHandler

class MqttClientHandler {
private:
    HandlerManager& handler_manager_;


    std::vector<uint8_t> buf;
    uint32_t _lastId{0};
    size_t _inflight{0};                    // num of unacknowledged bytes that has been written to socket buffer
    size_t _max_inflight{RESPONSE_MAX_INFLIGH};  // max num of unacknowledged bytes that could be written to socket buffer
    // PeekableQueue<MqttResponse_SharedData_t, MAX_EVENTS_PER_CLIENT> _responseQueue;
    static const int QUEUE_LENGTH = MAX_EVENTS_PER_CLIENT;
    static const int ITEM_SIZE = sizeof(MqttEvent*);
    
    uint32_t _rx_packet_count = 0;
    uint32_t _rx_last_tick = 0;
    uint32_t _last_activity_time = 0;
    
    bool _queueResponse(const char *message, size_t len);
    bool _queueResponse(MqttResponse_SharedData_t &&msg);

    
    void _onData(uint8_t* data, size_t len);
    bool check_and_perform_disconnect(uint8_t packetType);
    ssize_t getMqttPayloadLength(const uint8_t* data, size_t len, uint8_t* headerLen);
    void safeEnqueueMqttEvent(QueueHandle_t _queue, uint8_t packetType, uint8_t flags, const uint8_t* payloadData, size_t len);
    
public:
    uint16_t _handlerId = 0;
    MqttClient *_mqttClient = nullptr;
    QueueHandle_t rxQueue;  // queue handle
    SPSCQueue<MqttResponseMessage, MAX_EVENTS_PER_CLIENT> _responseQueue;
    tcp_pcb *_pcb = nullptr;
    MqttClientHandler(HandlerManager& hm, uint16_t id);
    ~MqttClientHandler();

    void attach(tcp_pcb* client_pcb);
    void attach_MqttClient(MqttClient* mqtt_client);
    void detach(bool abort = false, bool from_err = false);


    bool _queueMessage(const uint8_t* data, size_t len);
    bool _queueMessage(MqttResponse_SharedData_t &&msg);
    bool _queueMessage(const MqttResponse_SharedData_t &msg);
    void _runQueue();
    void _onAck(u16_t len);
    /**
         * @param message data
         * @return true on success
         * @return false on queue overflow or no client connected
         */
    bool write(MqttResponse_SharedData_t message) {
        return connected() && _queueResponse(std::move(message));
    };

    // close client's connection
    void close(MqttClient* caller);

    // getters

    // AsyncClient *client() {
    //     return _asyncClient;
    // }
    bool valid_link() const {
        return (!_mqttClient || _mqttClient->handler == this);
    }
    bool connected() const {
        return _pcb && valid_link();
    }
    uint32_t lastId() const {
        return _lastId;
    }
    // size_t packetsWaiting() const {
    //     return _responseQueue.size();
    // };

    /**
         * @brief Sets max amount of bytes that could be written to client's socket while awaiting delivery acknowledge
         * used to throttle message delivery length to tradeoff memory consumption
         * @note actual amount of data written could possible be a bit larger but no more than available socket buff space
         *
         * @param value
         */
    void set_max_inflight_bytes(size_t value);

    /**
         * @brief Get current max inflight bytes value
         *
         * @return size_t
         */
    size_t get_max_inflight_bytes() const {
        return _max_inflight;
    }

    // system callbacks (do not call if from user code!)
    err_t lwip_recv_cb(struct tcp_pcb* tpcb, struct pbuf* p, err_t err);
    void lwip_err_cb(err_t err);
    err_t lwip_poll_cb(struct tcp_pcb* tpcb);
    err_t lwip_sent_cb(struct tcp_pcb* tpcb, u16_t len);
};




} //namespace asyncbroker
} //namespace esphome
