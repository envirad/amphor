/*
 * pd.h
 *
 *  Created on: Jun 1, 2025
 *      Author: jar
 */

#ifndef PD_H_
#define PD_H_

#include <stdbool.h>

typedef enum
{
	S0 = 0,
	S2 = 2,
	S20 = 1,
	S100 = 3
} tPDSens;

typedef enum
{
	RED 	= 0,
	BLUE 	= 2,
	CLEAR 	= 1,
	GREEN 	= 3
} tPDBand;

extern tPDSens Sensitivity;

extern volatile bool cStart;
extern volatile uint16_t cTime;
extern volatile bool isCountComplete;
extern bool isLowFrequency;

float Measure_Frequency_Low(void);
float Measure_Frequency_High(float);
float PD_MeasureFrequency(float);
void PD_Ctrl_Init(void);
void PD_Ctrl(tPDSens, tPDBand);
void SN74LV8154_Init(void);
void SN74LV8154_Start(uint16_t);
uint32_t SN74LV8154_Read(void);
void TIM2_InputCapture_Init();

#endif /* PD_H_ */
