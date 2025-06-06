// #include "esphome/core/log.h"
// #include "esphome/core/hal.h"

// #include "mqtt_broker.h"
// #include "mosq_broker_wrapper.h"

// #include "broker_manager.h"


// namespace esphome {
// namespace mqtt_broker {

// static const char *TAG = "mqtt_broker.component";

//     static void handle_message(char *client, char *topic, char *payload, int len, int qos, int retain){
//         // if (client && strcmp(client, "local_mqtt") == 0 ) {
//         //     // This is our little local client -- do not forward
//         //     return;
//         // }
//         ESP_LOGI(TAG, "handle_message topic:%s", topic);
//         ESP_LOGI(TAG, "handle_message data:%.*s", len, payload);
//         ESP_LOGI(TAG, "handle_message qos=%d, retain=%d", qos, retain);
//         // if (s_local_mqtt && s_agent) {
//         //     int topic_len = strlen(topic) + 1; // null term
//         //     int topic_len_aligned = ALIGN(topic_len);
//         //     int total_msg_len = 2 + 2 /* msg_wrap header */ + topic_len_aligned + len;
//         //     if (total_msg_len > MAX_BUFFER_SIZE) {
//         //         ESP_LOGE(TAG, "Fail to forward, message too long");
//         //         return;
//         //     }
//         //     message_wrap_t *message = (message_wrap_t *)s_buffer;
//         //     message->topic_len = topic_len;
//         //     message->data_len = len;

//         //     memcpy(s_buffer + 4, topic, topic_len);
//         //     memcpy(s_buffer + 4 + topic_len_aligned, payload, len);
//         //     juice_send(s_agent, s_buffer, total_msg_len);
//         // }
//     }

// void start_broker(void* pvParameter) {
//     int broker_return = 0;

//     char ip[] = "0.0.0.0";
//     struct mosq_broker_config config = {
//         .host = ip,  // Listen on all interfaces
//         .port = 1883,       // Standard MQTT port
//         .tls_cfg = NULL,     // No TLS in this example
//         .handle_message_cb = handle_message
//         };

//     mosq_broker_run(&config);
//     // uxTaskGetStackHighWaterMark(NULL)
//     ESP_LOGD(TAG, "MQTT Broker exit with: %d", broker_return);
// }


// }
// }