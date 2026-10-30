/*
 * reed.h
 *
 *  Created on: Jun 7, 2025
 *      Author: jar
 */

#ifndef REED_H_
#define REED_H_

#include "stm32f4xx.h"
#include <stdbool.h>
extern volatile bool isPowerFault;  // Флаг потери питания

void Reed_Init(void);
void Reed_PowerOff(void);
bool Reed_IsPowerFault(void);
void EXTI15_10_IRQHandler(void);


#endif /* REED_H_ */
