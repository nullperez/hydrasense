/*
 * sensors.h
 *
 *  Created on: Mar 26, 2025
 *      Author: gianc
 */

#ifndef SENSORS_H_
#define SENSORS_H_

#include <stdint.h>

void initSensors();
uint8_t readValvePosition();
float readFlowMeter();
float readPressure();



#endif /* SENSORS_H_ */
