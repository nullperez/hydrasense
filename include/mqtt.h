#ifndef MQTT_H_
#define MQTT_H_

#include <stdint.h>
#include <stdbool.h>

#define MQTT_MAX_TOPIC      32
#define MQTT_MAX_PAYLOAD    64

void mqttReset();
void mqttHandler();
void processMqttResponse(const char* resp);
bool mqttIsConnected();
bool mqttPublish(const char* topic, const char* data);
uint8_t getMqttState();
const char* getMqttStateName();

#endif /* MQTT_H_ */
