#include "stm32f4xx.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <rtc.h>
#include <time.h>
#include <stdbool.h>
#include "tmp100.h"
#include "uart.h"
#include "w25.h"
#include "files.h"
#include "cli.h"
#include "pd.h"
#include "measure.h"
#include "reed.h"




volatile uint32_t msTicks = 0;
//volatile bool isPowerFault = false;  // Флаг потери питания
volatile bool cStart = false;
volatile uint16_t cTime = 0;
volatile bool isCountComplete = true;

tPDSens Sensitivity = S100;


// Прототипы функций
void LED_Init(void);
void LED_Toggle(void);
//time_t RTC_GetUnixTime(void);


void Process_LED(void);


// Функции задержки
void delay(uint32_t ms)
{
    uint32_t start = msTicks;
    while ((msTicks - start) < ms);
}



/*void delay(uint32_t ms) {
    for(uint32_t i = 0; i < ms * 2000; i++) {
        __NOP();
    }
}*/



void LED_Init(void)
{
    // Включение тактирования GPIOC
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;

    // Настройка PC13 как выход push-pull
    GPIOC->MODER &= ~GPIO_MODER_MODER13;
    GPIOC->MODER |= GPIO_MODER_MODER13_0;
    GPIOC->OTYPER &= ~GPIO_OTYPER_OT_13;
    GPIOC->OSPEEDR |= GPIO_OSPEEDER_OSPEEDR13;
    GPIOC->PUPDR &= ~GPIO_PUPDR_PUPDR13;
}

void LED_Toggle(void)
{
    GPIOC->ODR ^= GPIO_ODR_ODR_13;
    //printf("LED\n");
}

void Process_LED(void)
{
    static uint32_t lastToggle = 0;
    const uint32_t interval = 100; // 500ms;
    MeasResult tmp = measure(1.f, ALL);

    if ((msTicks - lastToggle) >= interval) {
        LED_Toggle();
        lastToggle = msTicks;

        printf("%d\t\t%1.3f\t%2.1f\t%d\t%d\t%d\t%d\n", 	\
        		(int)tmp.time, 							\
				((float)tmp.depth) /10000.f, 			\
        		((float)tmp.temp) / 10, 				\
				(int)tmp.clear, 						\
				(int)tmp.red, 							\
				(int)tmp.green, 						\
				(int)tmp.blue);
    }
}




void Check_I2C_Lines(void) {
    // Настройка PB8, PB9 как выходы
	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    GPIOB->MODER &= ~(GPIO_MODER_MODER8 | GPIO_MODER_MODER9);
    GPIOB->MODER |= GPIO_MODER_MODER8_0 | GPIO_MODER_MODER9_0;
    GPIOB->OTYPER |= GPIO_OTYPER_OT8 | GPIO_OTYPER_OT9;

    // Переключаем линии для проверки
    while(1) {
        GPIOB->ODR ^= GPIO_ODR_OD9 ;
        for(volatile uint32_t i = 0; i < 100; i++);
    }
}


void I2C_Scan(void) {
    for(uint8_t addr = 1; addr < 127; addr++) {
        I2C1->CR1 |= I2C_CR1_START;
        while(!(I2C1->SR1 & I2C_SR1_SB));

        I2C1->DR = addr << 1;
        if(!(I2C1->SR1 & I2C_SR1_AF)) {
            printf("Device %d found\n", addr);
        }
        I2C1->CR1 |= I2C_CR1_STOP;
    }
}

void PowerFault(void)
{
	printf("Panic! Power fault");
	while(1)
	{
		LED_Toggle();
		fprintf(stderr, "Panic!");
		delay(200);
	}
}


int main(void) {
    TimeStruct Time;
    DateStruct Date;
    //char buffer[16];
    // char sectorBuffer[4096];
	// Инициализация аппаратуры
    LED_Init();
    RTC_Init();
    USART1_Init(115200);
    USART6_Init(9600);
    TMP100_Init();
    ADS1115_Init();
    //PowerCtl_Init();
    //Reed_Init();
    W25_Init();
    W25_Reset();
    delay(1000);
    FS_Init();
    time_t unixTime = RTC_GetUnixTime();
    UnixTimeToDateTime(unixTime, &Time, &Date);
    PD_Ctrl_Init();
    SN74LV8154_Init();
    TIM2_InputCapture_Init(); // Таймер для измерения периода
    PD_Ctrl(S100, CLEAR);
    GPIOC->ODR = GPIO_ODR_ODR_13;

    TMP100_SetResolution(0x48, TMP100_RES_12BIT);
    // Перенаправление стандартного ввода/вывода
    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);
    redirect_stderr_to_usart6();

    SystemCoreClockUpdate();
    // Приветственное сообщение
    printf("\r\nSystem started on %d Hz!\r\n", (int)SystemCoreClock);
    printf("\nSPI Flash ID = 0x%04X found, status 0x%02X\n", W25_ReadID(), W25_ReadStatus());
    printf("RTC->TR: %08lX, RTC->DR: %08lX\n", RTC->TR, RTC->DR);
    printf("Date: %04d-%02d-%02d \n", Date.year, Date.month, Date.day);
    FS_Init();
    CLI_Init();
    char cmd[128];
    while(1)
    {
    	if(Reed_IsPowerFault())
    		Reed_PowerOff();
    	printf(".");
    	gets(cmd);
    	printf("%s\n", cmd);
    	CLI_ProcessCommand(cmd);
    }

    /*while(1)
    {
    	Process_LED();
    	if(isPowerFault)
    		PowerFault();
    }*/


}

void SysTick_Handler(void)
{
    msTicks++;

    if(cStart)
    {
        // Сначала подготовим все флаги
        isCountComplete = false;
        cStart = false;

        // Затем выполним аппаратные операции
        GPIOB->BSRR = (1 << 10 << 16); // сброс порта 10 в ноль
        __NOP();
        __NOP();
        GPIOB->BSRR = (1 << 10); // установка его в единицу
        return;
    }

    if(!isCountComplete)
    {
        if(cTime > 0)
        {
            cTime--;
        }
        if(cTime == 0)
        {
            isCountComplete = true;
            GPIOB->BSRR = (1 << 2); // Установка порта в 1
            __NOP();
            __NOP();
            GPIOB->BSRR = (1 << 2 << 16); // Сброс
        }
    }
}


