/*
 * measure.h
 *
 *  Created on: Jun 1, 2025
 *      Author: jar
 */
#include <time.h>

#ifndef MEASURE_H_
#define MEASURE_H_

// Битовая маска для параметров измерений
#define TIME_BIT    (1 << 0)
#define DEPTH_BIT   (1 << 1)
#define TEMP_BIT    (1 << 2)
#define CLR_BIT   	(1 << 3)
#define RED_BIT     (1 << 4)
#define GREEN_BIT   (1 << 5)
#define BLUE_BIT    (1 << 6)

#define ALL_MEASUREMENTS (TIME_BIT | DEPTH_BIT | TEMP_BIT | CLR_BIT | RED_BIT | GREEN_BIT | BLUE_BIT)
#define ALL ALL_MEASUREMENTS

typedef struct {
	time_t time;
	int16_t temp;
	uint16_t depth;
	float clear;
	float red;
	float green;
	float blue;
} MeasResult;

MeasResult measure(float, uint8_t);
void Measure_PrintHeader(uint8_t);
void Measure_PrintValueStr(MeasResult result, uint8_t set);

#endif /* MEASURE_H_ */
