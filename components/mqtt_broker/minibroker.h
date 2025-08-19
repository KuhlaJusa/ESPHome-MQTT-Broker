#pragma once

extern "C" {
#include "lwip/err.h"
#include "lwip/tcp.h"
}

#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace esphome {
namespace mini_broker {

static const char *const TAG = "mini_broker";

class MiniBroker;
struct TopicEntry;
struct ClientStub;
struct Client;

//One Connection Stub per Client, and allow fast topic lookup
struct Client {
  ClientStub* stub = nullptr;
  bool cleanSession = 1;
  uint16_t keepalive = 60; // seconds
  std::string clientId;
  std::vector<TopicEntry*> subscribed_topics; // pointers to existing TopicEntry objects
  std::vector<TopicEntry*> published_topics; // pointers to existing TopicEntry objects
};

// ClientStub is used to manage the connection state and buffer for each client
struct ClientStub {
  bool inactive = false;
  uint32_t last_activity_ms = 0;
  MiniBroker* parent;
  tcp_pcb* pcb = nullptr;
  Client* realClient = nullptr;
  std::vector<uint8_t> buf;
};

//TopicEntry represents a topic and its subscribers
struct TopicEntry {
  const std::string* topicPtr;
  std::vector<Client*> subscribers;
};

class MiniBroker{
public:
  MiniBroker(uint16_t port = 1883);
  
  ~MiniBroker();
  
  bool begin();
  void loop_iteration();
  void stop();
  void set_port(uint16_t port);
  using PublishCallback = void(*)(const uint8_t* topic, size_t topic_len,
                                  const uint8_t* payload, size_t payload_len);
  void set_publish_cb(PublishCallback cb);
  
private:
  
  void client_read_buffer(ClientStub* s);

  // MiniBroker(const MiniBroker&) = delete;
  // MiniBroker& operator=(const MiniBroker&) = delete;

  PublishCallback publish_cb_ = nullptr;
  uint16_t port_;
  tcp_pcb* listen_pcb_;

  std::vector<Client*> clients_;
  std::vector<ClientStub*> stubs_;
  // std::vector<TopicEntry*> topics_;
  std::unordered_map<std::string, TopicEntry*> topics_;

  // pool management
  ClientStub* get_free_stub();
  Client* get_client(const char* idData, size_t idLen);
  TopicEntry* get_topic(const uint8_t* topic_ptr, size_t topic_len);
  TopicEntry* get_topic(const std::string& topic);

  //utils
  static void release_stub(ClientStub* c, bool forced);
  static int decodeRemainingLength(const uint8_t* buf, size_t bufLen, uint32_t &length, size_t &consumed);
  static void encodeRemainingLength(std::vector<uint8_t>& out, uint32_t value);
  void subscribeClientToTopic(Client* c, const uint8_t* topic, size_t len);
  void unsubscribeClientFromTopic(Client* c, const uint8_t* filter, size_t len);
  void unsubscribeClientFromTopic(Client* c, TopicEntry* te);
  

  // ---------- lwIP/tcp callbacks ----------
  static err_t lwip_accept_cb_static(void* arg, struct tcp_pcb* newpcb, err_t err);
  static void client_err_cb_static(void* arg, err_t err);
  static err_t client_sent_cb_static(void* arg, struct tcp_pcb* tpcb, u16_t len);
  static err_t client_recv_cb_static(void* arg, struct tcp_pcb* tpcb, struct pbuf* p, err_t err);
  static err_t tcp_poll_cb_static(void* arg, tcp_pcb* pcb);

  // ------------------ MQTT ---------------------
  void handleConnect(ClientStub* s, uint8_t* data, size_t len);
  static void sendConnack_pcb(tcp_pcb* pcb, uint8_t sp_flag, uint8_t ack_flag);

  void handlePingreq(ClientStub* s);
  static void sendPingresp_pcb(tcp_pcb* pcb);

  void handleDisconnect(ClientStub* s);

  void handleSubscribe(ClientStub* s, uint8_t* data, size_t len);
  static void sendSuback_pcb(tcp_pcb* pcb, uint16_t packetId, size_t topics);

  void handleUnsubscribe(ClientStub* s, uint8_t* data, size_t len);
  static void sendUnsuback_pcb(tcp_pcb* pcb, uint16_t packetId);

  void handlePublish(ClientStub* c, uint8_t flags, uint8_t* data, size_t len);
  void build_publish_message(std::vector<uint8_t>& out,
                                       const uint8_t* topic_ptr, size_t topic_len,
                                       const uint8_t* payload, size_t payload_len);
  static void sendPublish_pcb(tcp_pcb* pcb, const std::vector<uint8_t>& packet);
};
}}