#include "AsyncBroker.h"

namespace esphome {
namespace asyncbroker {

size_t MqttResponseMessage::ack(size_t len, uint32_t /*time*/) {
    // If the whole message is now acked...
    if (_acked + len > _data->size()) {
      // Return the number of extra bytes acked (they will be carried on to the next message)
      const size_t extra = _acked + len - _data->size();
      _acked = _data->size();
      return extra;
    }
    // Return that no extra bytes left.
    _acked += len;
    return 0;
}

size_t MqttResponseMessage::write(tcp_pcb *pcb) {
    if (!pcb) return 0;
    if (_sent >= _data->size()) return 0;

    size_t space = tcp_sndbuf(pcb);
    if (space == 0) return 0;

    size_t len = std::min(_data->size() - _sent, space);
    err_t err = tcp_write(pcb, _data->data() + _sent, len, TCP_WRITE_FLAG_COPY);
    // size_t written = client->add(_data->data() + _sent, len, ASYNC_WRITE_FLAG_COPY);  //  ASYNC_WRITE_FLAG_MORE
    if (err != ERR_OK) return 0;

    _sent += len;
    return len;
}

size_t MqttResponseMessage::send(tcp_pcb *pcb) {
    size_t written = write(pcb);
    if (written > 0) {
        if (tcp_output(pcb) != ERR_OK) {
            return 0;
        }
    }
    return written;
}

} //namespace asyncbroker
} //namespace esphome