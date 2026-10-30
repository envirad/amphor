/*
 * measure.c
 *
 *  Created on: Jun 1, 2025
 *      Author: jar
 */
#include <rtc.h>
#include <stdio.h>
#include <stdbool.h>
#include "measure.h"
#include "tmp100.h"
#include "pd.h"
#include "cli.h"

static const char * header_fields[7] = {
		"UNIX Time\t",
		"Depth",
		"Temp",
		"CLEAR",
		"RED",
		"GREEN",
		"BLUE"
};

float gain_convert(tPDSens sens)
{
	switch(sens)
	{
	case S0:
		return 0.f;
	case S2:
		return 0.02f;
	case S20:
		return 0.2f;
	case S100:
		return 1.f;
	}
}
MeasResult measure(float exposure, uint8_t set)
{
	Sensitivity = (tPDSens)sys_params.gain;
	float gain = gain_convert(Sensitivity);
	MeasResult tmp = {0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f};
	if(set & TIME_BIT)
		tmp.time = RTC_GetUnixTime();
	if(set & DEPTH_BIT)
		tmp.depth = (uint16_t)(ADS1115_ReadVoltage(0x4A) * 10000.0f);
	if(set & TEMP_BIT)
		tmp.temp = (int16_t)(TMP100_GetTemperature(0x48) * 10);
	if(Sensitivity == S0)
		return tmp;
	if(set & CLR_BIT)
	{
		PD_Ctrl(Sensitivity, CLEAR);
		tmp.clear = PD_MeasureFrequency(exposure) / gain;
		if(tmp.clear * gain < 200.f)
		{
			isLowFrequency = true;
		}
		else
		{
			isLowFrequency = false;
		}
	}
	if(set & RED_BIT)
	{
		PD_Ctrl(Sensitivity, RED);
		tmp.red = PD_MeasureFrequency(exposure) / gain;
	}
	if(set & GREEN_BIT)
	{
		PD_Ctrl(Sensitivity, GREEN);
		tmp.green = PD_MeasureFrequency(exposure) /gain;
	}
	if(set & BLUE_BIT)
	{
		PD_Ctrl(Sensitivity, BLUE);
		tmp.blue = PD_MeasureFrequency(exposure) / gain;
	}
	return tmp;
}

void Measure_PrintHeader(uint8_t set)
{

	for(uint8_t i = 0; i < 8; i++)
	{
		if(set & (1 << i))
			printf("%s\t", header_fields[i]);
	}
	printf("\n");
}

void Measure_PrintValueStr(MeasResult result, uint8_t set)
{
	if(set & TIME_BIT)
		printf("%10d\t\t", (uint32_t) result.time);
	if(set & DEPTH_BIT)
		printf("%1.4f\t", result.depth/10000.f);
	if(set & TEMP_BIT)
		printf("%2.1f\t", result.temp/10.f);
	if(set & CLR_BIT)
		printf("%6d\t", (unsigned int)result.clear);
	if(set & RED_BIT)
		printf("%6d\t", (unsigned int)result.red);
	if(set & GREEN_BIT)
		printf("%6d\t", (unsigned int)result.green);
	if(set & BLUE_BIT)
		printf("%6d\t", (unsigned int)result.blue);
	printf("\n");
}
