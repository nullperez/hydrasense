/*
 * mqtt.h
 *
 * MQTT client for the SIM7600 (AT+CMQTT* commands). Registers itself as a
 * sim7600 client: it connects once the modem is ready and reconnects with
 * backoff after any failure. Depends only on sim7600.h and the C library.
 */

#ifndef MQTT_H_
#define MQTT_H_

#include <stdint.h>
#include <stdbool.h>

#define MQTT_MAX_TOPIC      32
#define MQTT_MAX_PAYLOAD    64

typedef struct {
    const char* host;           // broker hostname or IP
    uint16_t    port;
    const char* clientId;
    const char* user;           // NULL for no authentication
    const char* pass;
    uint16_t    keepAliveS;     // 0 = default (60)
    uint8_t     qos;            // 0, 1 or 2
} mqtt_config;                  // strings must stay valid (e.g. literals)

bool mqttInit(const mqtt_config* cfg);  // call after sim7600Init()
bool mqttIsConnected(void);
bool mqttPublish(const char* topic, const char* data); // false if not connected or busy
const char* mqttStateName(void);

#endif /* MQTT_H_ */
