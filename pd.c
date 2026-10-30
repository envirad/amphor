/*
 * pd.c
 *
 *  Created on: Jun 1, 2025
 *      Author: jar
 */
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include "stm32f4xx.h"
#include "pd.h"

bool isLowFrequency = false;


void SN74LV8154_Init(void)
{
	//1. Включить тактирование портов
	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
	GPIOA->MODER &= 0xFFFF0000; // Выводы PA0..PA7 на вход, остальные не трогаем
	GPIOA->PUPDR &= 0xFFFF0000; // Выводы PA0..PA7 без подтяжек, остальные не трогаем

    GPIOB->MODER &= ~(GPIO_MODER_MODER0|GPIO_MODER_MODER1|GPIO_MODER_MODER2|GPIO_MODER_MODER10);
    GPIOB->MODER |= GPIO_MODER_MODER0_0|GPIO_MODER_MODER1_0|GPIO_MODER_MODER2_0|GPIO_MODER_MODER10_0;
    GPIOB->OTYPER &= ~(GPIO_OTYPER_OT_0|GPIO_OTYPER_OT_1|GPIO_OTYPER_OT_2|GPIO_OTYPER_OT_10);
    GPIOB->OSPEEDR |= GPIO_OSPEEDER_OSPEEDR0|GPIO_OSPEEDER_OSPEEDR1|GPIO_OSPEEDER_OSPEEDR2|GPIO_OSPEEDER_OSPEEDR10;
    GPIOB->PUPDR &= ~(GPIO_PUPDR_PUPDR0|GPIO_PUPDR_PUPDR1|GPIO_PUPDR_PUPDR2|GPIO_PUPDR_PUPDR10);
    //             bit set   bit reset
    GPIOB->BSRR = (1 << 10)|(1 << 2 << 16); // Исходное состояние: CCLR высокий уровень, RCLK -- низкий. Остальное неважно.

}

void SN74LV8154_Start(uint16_t time)
{
    while(!isCountComplete); // Ждем завершения предыдущего измерения
    cTime = time;
    cStart = true;
}

void TIM2_InputCapture_Init()
{
    // 1. Включить тактирование TIM2 и GPIOA
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

    // 2. Настройка PA15 (TIM2_CH1) как Alternate Function
    GPIOA->MODER &= ~GPIO_MODER_MODER15;      // Сброс режима
    GPIOA->MODER |= GPIO_MODER_MODER15_1;      // Alternate Function
    GPIOA->AFR[1] |= (1 << GPIO_AFRH_AFSEL15_Pos); // AF1 (TIM2_CH1)

    // 3. Настройка TIM2 для Input Capture (по фронту)
    TIM2->CCMR1 |= TIM_CCMR1_CC1S_0;          // CC1 как вход (IC1 mapped to TI1)
    TIM2->CCER |= TIM_CCER_CC1E;              // Включить захват
    TIM2->PSC = 839;                       	// Предделитель (если APB1 = 84 МГц → 1 МГц)
    TIM2->ARR = 0xFFFF;                       // Автоперезагрузка на максимум
    TIM2->CR1 |= TIM_CR1_CEN;                 // Запустить таймер
    TIM2->CCMR1 |= TIM_CCMR1_IC1F_0 | TIM_CCMR1_IC1F_1;
}


void PD_Ctrl_Init(void)
{
	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    GPIOB->MODER &= ~(GPIO_MODER_MODER3|GPIO_MODER_MODER4|GPIO_MODER_MODER5|GPIO_MODER_MODER6);
    GPIOB->MODER |= GPIO_MODER_MODER3_0|GPIO_MODER_MODER4_0|GPIO_MODER_MODER5_0|GPIO_MODER_MODER6_0;
    GPIOB->OTYPER &= ~(GPIO_OTYPER_OT_3|GPIO_OTYPER_OT_4|GPIO_OTYPER_OT_5|GPIO_OTYPER_OT_6);
    GPIOB->OSPEEDR |= GPIO_OSPEEDER_OSPEEDR3|GPIO_OSPEEDER_OSPEEDR4|GPIO_OSPEEDER_OSPEEDR5|GPIO_OSPEEDER_OSPEEDR6;
    GPIOB->PUPDR &= ~(GPIO_PUPDR_PUPDR3|GPIO_PUPDR_PUPDR4|GPIO_PUPDR_PUPDR5|GPIO_PUPDR_PUPDR6);
}

// Установка чувствительности и канала фотоприемника.
void PD_Ctrl(tPDSens sens, tPDBand band)
{
	uint8_t tmp = (uint8_t) sens + 4 * (uint8_t) band;
	GPIOB->BSRR = 15 << 16 << 3;
	GPIOB->BSRR = tmp << 3;
	delay(20);
}

uint32_t SN74LV8154_Read(void)
{
	uint32_t tmp = 0, tmp2 = 0;
	while(!isCountComplete); // ждем окончания счета
	GPIOB->BSRR = 3 << 16;
	tmp = GPIOA->IDR & 255;
	GPIOB->BSRR = 2|(3 << 16);
	tmp |= (GPIOA->IDR & 255) << 8;
	GPIOB->BSRR = 1|(3 << 16);
	tmp2 = (GPIOA->IDR & 255);
	GPIOB->BSRR = 3|(3 << 16);
	tmp2 |= (GPIOA->IDR & 255) << 8;
	return tmp + (tmp2 << 16);
}


float Measure_Frequency_High(float measurement_time)
{
	uint32_t exp = (int)(measurement_time * 1000.0);
	SN74LV8154_Start(exp);
	uint32_t count = SN74LV8154_Read();
	return (float)count / measurement_time;
}

float Measure_Frequency_Low()
{
    uint32_t t1 = 0, t2 = 0;
    TIM2_InputCapture_Init();
    __disable_irq();
    // Ждём первый фронт (подъём)
    while (!(TIM2->SR & TIM_SR_CC1IF));  // Ожидаем флаг захвата
    t1 = TIM2->CCR1;                     // Запоминаем первое значение
    TIM2->SR &= ~TIM_SR_CC1IF;           // Сбрасываем флаг

    // Ждём второй фронт (спад)
    while (!(TIM2->SR & TIM_SR_CC1IF));
    t2 = TIM2->CCR1;
    TIM2->CNT = 0;
    TIM2->SR &= ~TIM_SR_CC1IF;
    __enable_irq();

    int period = (t2 - t1);    // В десятках мкс (если PSC=840-1)

    float freq = 100000.f/(float)period; // В Гц
    return freq;
}

float PD_MeasureFrequency(float measurement_time)
{
	if(isLowFrequency)
		return Measure_Frequency_Low();
	return Measure_Frequency_High(measurement_time);
}

