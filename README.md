# ESPHome-MQTT-Broker
### Lightweight MQTT Broker for usage in ESPHome

Running a lightweight MQTT broker directly on an ESP device is ideal for small, self-contained networks where a full server infrastructure isn't available. This setup is useful when:  
- **No external MQTT server** (like Mosquitto or Home Assistant) is present.

- **Only a few messages** are exchanged between sensors, buttons, or relays — keeping the system simple and low-latency.

-  **Minimal hardware** is available, yet you still want MQTT-based communication.

This component acts as a bridge between ESPHome and [Espressif's port of the Mosquitto MQTT broker](https://github.com/espressif/esp-protocols/tree/master/components/mosquitto). It integrates the broker implementation directly into devices running ESPHome, making it easy to use MQTT locally without needing an external server. The component can handle basic MQTT communication and supports ESPHome-style automations, allowing triggers based on received MQTT topics.  

> ⚠️ **This component is only available when using the ESP-IDF framework because the broker implementation used was designed for ESP-IDF.**

## Installation

To use this external component, add the following code to the YAML configuration.

```yaml
external_components:
  - source: github://KuhlaJusa/ESPHome-MQTT-Broker
      components: [ mqtt_broker ]
```
## Configuration

```yaml
# Example minimal configuration entry
mqtt_broker:

# IPv6 is required, so the network component must be added.
network:
    enable_ipv6: true
```

The broker implementation requires IPv6, even if it is not in use. Therefore we need to enable the support for it.

### Variables:

- **port** (Optional, int): Set the port number of the MQTT broker. Defaults to `1883`.  

- **on_message_max_age** (Optional, Time (ms) or *'infinite'*): Only used, when *on_message Trigger* are given. For technical details take a look at [Technical Details](#technical-details). Defaults to `1000ms`

- **max_queue_elements** (Optional, int): Only used, when *on_message Trigger* are given. For technical details take a look at [Technical Details](#technical-details). Defaults to `10`.

- **debug** (optional, boolean): Whether the broker will log every received message in the console. If used triggers will be ignored. Defaults to `false`.

### on_message Trigger

As stated in the [ESPHome MQTT Component](https://esphome.io/components/mqtt.html#on-message-trigger):

>With this configuration option you can write complex automations whenever an MQTT message on a specific topic is received. To use the message content, use a lambda template, the message payload is available under the name x inside that lambda.

  ```yaml
  mqtt_broker:
    # ...
    on_message:
      topic: my/custom/topic
      then:
        - switch.turn_on: some_switch
  ```

This component uses the same syntax for [message trigger ](https://esphome.io/components/mqtt.html#on-message-trigger) as the ESPHome MQTT Component.  
Just like the MQTT Component, it also supports multiple `on_message` trigger, by using a YAML list.

Not supported are the `qos` variable, which is not available and the second NOTE, meaning this component does not support the creation of triggers in lambdas.

> ⚠️ Depending on `on_message_max_age` and `max_queue_elements`, there is no guarantee that each and every event will trigger.


## Technical Details

### Memory Footprint Considerations

As stated by the [mosquitto port](https://github.com/espressif/esp-protocols/tree/master/components/mosquitto#memory-footprint-considerations):
>The broker primarily uses the heap for internal data, with minimal use of static/BSS memory. It consumes approximately 60 kB of program memory and minimum 5kB of stack size.
>
> - **Initial Memory Usage:** ~2 kB of heap on startup
> - **Per Client Memory Usage:** ~4 kB of heap for each connected client

Using Triggers the RAM usage might increase due to temporary storage of Messages.

### Broker Task

Depending on the ESP Model, the Broker will run in a slightly different [Task](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/freertos_idf.html#tasks). 

- For true **dual-core** variants like the ESP32 or ESP32-S3, the Broker-Task runs on Core 1 with the same priority as the ESPHome Main-Loop.
- For all other **single-core** Variants, the Broker-Task runs with a slightly lower priority than the ESPHome Main-Loop (Priority 0 vs Priority 1).

### Trigger

As the Broker-Task should run as uninterrupted as possible for a reliable MQTT operation, the possibly heavy automations are being executed from within ESPHome's Main-Loop. Therefore we need a way of communication between the Broker-Task and the Main-Loop. For this we use a shared [Queue](https://docs.espressif.com/projects/esp-idf/en/v5.0/esp32/api-reference/system/freertos.html#queue-api) that can hold a maximum of `max_queue_elements` at once.

- **Broker-Task**: Checks for a potential trigger topic of a message and Copies relevant messages into the Queue and adds a timestamp to it. If the Queue if full, the message is dropped.

- **Main-Loop**: Takes Messages from the Queue and checks if the timestamp is still relevant (depending on `on_message_max_age`). For valid messages the Automations are then being called from within the Main-Loop, allowing the Broker-Task to continue operation. Per call of the Loop at most `max_queue_elements` are being processed.

>⚠️ **Triggers do not influence the normal operation of the MQTT broker.** This means that if a message is dropped because the queue is full, it is still forwarded to subscribed clients.

## See Also
- [ESPHome MQTT Component](https://esphome.io/components/mqtt.html)
- [ESPHome Network Component](https://esphome.io/components/network.html)
- [Espressif's port of the Mosquitto MQTT broker](https://github.com/espressif/esp-protocols/tree/master/components/mosquitto)