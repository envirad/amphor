/*
 * reed.c
 *
 *  Created on: Jun 7, 2025
 *      Author: jar
 */
#include <reed.h>
#include <stdio.h>
volatile bool isPowerFault = false;

void Reed_Init()
{
    // Включение тактирования порта C
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;

    // Настройка PC10 (PGOOD) как вход с подтяжкой вверх
    GPIOC->MODER &= ~GPIO_MODER_MODER10; // Input mode
    GPIOC->PUPDR = (GPIOC->PUPDR & ~GPIO_PUPDR_PUPD10) | (1 << GPIO_PUPDR_PUPD10_Pos); // Pull-up

    // Настройка PC12 (PHOLD) как выход Push-Pull
    GPIOC->MODER = (GPIOC->MODER & ~GPIO_MODER_MODER12) | (1 << GPIO_MODER_MODER12_Pos); // Output
    GPIOC->OTYPER &= ~GPIO_OTYPER_OT_12; // Push-Pull
    GPIOC->OSPEEDR |= (3 << GPIO_OSPEEDR_OSPEED12_Pos); // High speed
    GPIOC->PUPDR &= ~GPIO_PUPDR_PUPDR12; // No pull-up/pull-down

    // Изначально PHOLD опущен (0)
    GPIOC->ODR &= ~GPIO_ODR_ODR_12;
    // Включение тактирования SYSCFG
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;

    // Настройка EXTI10 на порт C (PC10)
    SYSCFG->EXTICR[2] &= ~SYSCFG_EXTICR3_EXTI10; // Clear
    SYSCFG->EXTICR[2] |= SYSCFG_EXTICR3_EXTI10_PC; // PC10 -> EXTI10

    // Настройка прерывания по спаду (Falling Edge)
    EXTI->FTSR |= EXTI_FTSR_TR10;

    // Разрешение прерывания EXTI10
    EXTI->IMR |= EXTI_IMR_MR10;

    // Настройка приоритета и разрешение прерывания в NVIC
    NVIC_SetPriority(EXTI15_10_IRQn, 0); // Высокий приоритет
    NVIC_EnableIRQ(EXTI15_10_IRQn);
}

void Reed_PowerOff(void)
{
	printf("?PWR-I-Power switched off\n");
	__disable_irq();
	GPIOC->ODR &= ~GPIO_ODR_ODR_12;
}

bool Reed_IsPowerFault(void)
{
	return isPowerFault;
}

void EXTI15_10_IRQHandler(void) {
    if (EXTI->PR & EXTI_PR_PR10) { // Проверка флага EXTI10
        EXTI->PR = EXTI_PR_PR10; // Сброс флага

        // 1. Мгновенно поднять PHOLD (PC12)
        GPIOC->ODR |= GPIO_ODR_ODR_12;

        // 2. Установить флаг для главного цикла
        isPowerFault = true;
    }
}
