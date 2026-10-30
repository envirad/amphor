// tmp100.c
#include "tmp100.h"
#include "stm32f4xx.h"
#include <stdio.h>
#include <math.h>


void __delay(uint32_t ms) {
    for(uint32_t i = 0; i < ms * 2000; i++) {
        __NOP();
    }
}

// Добавим функцию для проверки состояния I2C
void I2C1_CheckStatus(void) {
    if(I2C1->SR1 & I2C_SR1_AF) {
        I2C1->SR1 &= ~I2C_SR1_AF; // Сброс флага ошибки
        // Здесь можно добавить обработку ошибки
    }
    if(I2C1->SR1 & I2C_SR1_ARLO) {
        I2C1->SR1 &= ~I2C_SR1_ARLO; // Сброс флага арбитража
    }
    if(I2C1->SR1 & I2C_SR1_BERR) {
        I2C1->SR1 &= ~I2C_SR1_BERR; // Сброс флага шинной ошибки
    }
}

void TMP100_Init(void) {
    // 1. Включение тактирования
    RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;

    // 2. Настройка GPIO - более надежная версия
    GPIOB->MODER &= ~(GPIO_MODER_MODER8 | GPIO_MODER_MODER9);
    GPIOB->MODER |= GPIO_MODER_MODER8_1 | GPIO_MODER_MODER9_1; // Alternate function

    GPIOB->OTYPER |= GPIO_OTYPER_OT8 | GPIO_OTYPER_OT9; // Open-drain
    //GPIOB->PUPDR |= GPIO_PUPDR_PUPDR8_0 | GPIO_PUPDR_PUPDR9_0; // Pull-up

    // Явная установка альтернативной функции (AF4 для I2C1)
    GPIOB->AFR[1] &= ~(GPIO_AFRH_AFSEL8 | GPIO_AFRH_AFSEL9);
    GPIOB->AFR[1] |= (4 << GPIO_AFRH_AFSEL8_Pos) | (4 << GPIO_AFRH_AFSEL9_Pos);

    // 3. Сброс и настройка I2C
    I2C1->CR1 &= ~I2C_CR1_PE; // Disable I2C before configuration

    // Рассчет параметров для 100 kHz при APB1 = 42 MHz
    I2C1->CR2 = (42 << I2C_CR2_FREQ_Pos); // 42 MHz
    I2C1->CCR = 210; // CCR = APB1/(2*I2C_Freq) = 42MHz/(2*100kHz) = 210
    I2C1->TRISE = 43; // TRISE = (1000ns / (1/42MHz)) + 1 = 43

    I2C1->CR1 |= I2C_CR1_PE; // Enable I2C

    // Добавим небольшую задержку для стабилизации
    for(volatile uint32_t i = 0; i < 1000; i++);
}

/**
  * @brief Запись байта в регистр датчика TMP100
  * @param devAddr: Адрес устройства I2C (7-битный)
  * @param regAddr: Адрес регистра
  * @param data: Данные для записи
  * @retval 0 в случае успеха, -1 при ошибке
  */
int TMP100_WriteReg(uint8_t devAddr, uint8_t regAddr, uint8_t data) {
    // 1. Генерация START
    I2C1->CR1 |= I2C_CR1_START;
    while (!(I2C1->SR1 & I2C_SR1_SB) && !(I2C1->SR1 & I2C_SR1_AF)) {}

    if (I2C1->SR1 & I2C_SR1_AF) {
        I2C1->SR1 &= ~I2C_SR1_AF;
        I2C1->CR1 |= I2C_CR1_STOP;
        return -1; // Ошибка START
    }

    // 2. Отправка адреса устройства + бита записи
    I2C1->DR = (devAddr << 1) & ~0x01;
    while (!(I2C1->SR1 & I2C_SR1_ADDR) && !(I2C1->SR1 & I2C_SR1_AF)) {}

    if (I2C1->SR1 & I2C_SR1_AF) {
        I2C1->SR1 &= ~I2C_SR1_AF;
        I2C1->CR1 |= I2C_CR1_STOP;
        return -1; // Нет ответа от устройства
    }

    (void)I2C1->SR2; // Чтение SR2 для сброса ADDR

    // 3. Отправка адреса регистра
    while (!(I2C1->SR1 & I2C_SR1_TXE) && !(I2C1->SR1 & I2C_SR1_AF)) {}
    I2C1->DR = regAddr;

    // 4. Отправка данных
    while (!(I2C1->SR1 & I2C_SR1_TXE) && !(I2C1->SR1 & I2C_SR1_AF)) {}
    I2C1->DR = data;

    // 5. Ожидание завершения передачи
    while (!(I2C1->SR1 & I2C_SR1_BTF) && !(I2C1->SR1 & I2C_SR1_AF)) {}

    // 6. Генерация STOP
    I2C1->CR1 |= I2C_CR1_STOP;

    return 0; // Успех
}

/**
  * @brief Настройка разрешения датчика
  * @param devAddr: Адрес устройства I2C
  * @param resolution: Одно из значений TMP100_RES_xBIT
  * @retval 0 в случае успеха, -1 при ошибке
  */
int TMP100_SetResolution(uint8_t devAddr, uint8_t resolution) {
    // Читаем текущее значение регистра конфигурации
    uint8_t config = TMP100_ReadReg(devAddr, TMP100_REG_CONFIG);
    if (config == 0xFF) return -1; // Ошибка чтения

    // Меняем только биты разрешения (R1 и R0), сохраняя остальные настройки
    config = (config & ~0x60) | (resolution & 0x60);

    // Записываем новое значение
    return TMP100_WriteReg(devAddr, TMP100_REG_CONFIG, config);
}

/**
  * @brief Чтение значения температуры
  * @param devAddr: Адрес устройства I2C
  * @return Сырое 16-битное значение температуры (12 бит в старших разрядах)
  */
/*uint16_t TMP100_ReadTempRaw(uint8_t devAddr) {
    uint8_t msb = TMP100_ReadReg(devAddr, TMP100_REG_TEMP);
    uint8_t lsb = TMP100_ReadReg(devAddr, TMP100_REG_TEMP); // Читаем следующий регистр
    return (msb << 8) | lsb;
}*/

uint16_t TMP100_ReadTempRaw(uint8_t devAddr) {
    // 1. Устанавливаем указатель на регистр температуры
    I2C1->CR1 |= I2C_CR1_START;
    while(!(I2C1->SR1 & I2C_SR1_SB));

    I2C1->DR = (devAddr << 1) & ~0x01; // Адрес + запись
    while(!(I2C1->SR1 & I2C_SR1_ADDR));
    (void)I2C1->SR2; // Сброс ADDR

    I2C1->DR = TMP100_REG_TEMP; // Указываем регистр температуры
    while(!(I2C1->SR1 & I2C_SR1_BTF));

    // 2. Повторный START для чтения двух байт
    I2C1->CR1 |= I2C_CR1_START;
    while(!(I2C1->SR1 & I2C_SR1_SB));

    I2C1->DR = (devAddr << 1) | 0x01; // Адрес + чтение
    while(!(I2C1->SR1 & I2C_SR1_ADDR));
    (void)I2C1->SR2; // Сброс ADDR

    // 3. Чтение двух байт с правильными ACK/NACK
    I2C1->CR1 |= I2C_CR1_ACK; // Разрешаем ACK для первого байта

    while(!(I2C1->SR1 & I2C_SR1_RXNE)); // Ждём первый байт
    uint8_t msb = I2C1->DR;

    I2C1->CR1 &= ~I2C_CR1_ACK; // Запрещаем ACK перед вторым байтом

    while(!(I2C1->SR1 & I2C_SR1_RXNE)); // Ждём второй байт
    uint8_t lsb = I2C1->DR;

    I2C1->CR1 |= I2C_CR1_STOP; // STOP condition

    return (msb << 8) | lsb;
}
/**
  * @brief Чтение регистра датчика
  * @param devAddr: Адрес устройства I2C
  * @param regAddr: Адрес регистра
  * @return Прочитанное значение или 0xFF при ошибке
  */
uint8_t TMP100_ReadReg(uint8_t devAddr, uint8_t regAddr) {
    // 1. Отправляем адрес регистра для чтения
    I2C1->CR1 |= I2C_CR1_START;
    while(!(I2C1->SR1 & I2C_SR1_SB) && !(I2C1->SR1 & I2C_SR1_AF)); // Ждем START или ошибки

    if(I2C1->SR1 & I2C_SR1_AF) {
        I2C1->SR1 &= ~I2C_SR1_AF;
        I2C1->CR1 |= I2C_CR1_STOP;
        return 0;
    }

    I2C1->DR = (devAddr << 1) & ~0x01; // Адрес + запись
    while(!(I2C1->SR1 & I2C_SR1_ADDR) && !(I2C1->SR1 & I2C_SR1_AF));

    if(I2C1->SR1 & I2C_SR1_AF) {
        I2C1->SR1 &= ~I2C_SR1_AF;
        I2C1->CR1 |= I2C_CR1_STOP;
        return 0;
    }

    (void)I2C1->SR2; // Читаем SR2 для сброса ADDR

    // Отправляем адрес регистра
    while(!(I2C1->SR1 & I2C_SR1_TXE) && !(I2C1->SR1 & I2C_SR1_AF));
    I2C1->DR = regAddr;

    // 2. Повторный START для чтения
    while(!(I2C1->SR1 & I2C_SR1_BTF) && !(I2C1->SR1 & I2C_SR1_AF));
    I2C1->CR1 |= I2C_CR1_START;
    while(!(I2C1->SR1 & I2C_SR1_SB) && !(I2C1->SR1 & I2C_SR1_AF));

    // Отправляем адрес + чтение
    I2C1->DR = (devAddr << 1) | 0x01;
    while(!(I2C1->SR1 & I2C_SR1_ADDR) && !(I2C1->SR1 & I2C_SR1_AF));

    if(I2C1->SR1 & I2C_SR1_AF) {
        I2C1->SR1 &= ~I2C_SR1_AF;
        I2C1->CR1 |= I2C_CR1_STOP;
        return 0;
    }

    (void)I2C1->SR2; // Сбрасываем ADDR

    // Настраиваем на прием одного байта
    I2C1->CR1 &= ~I2C_CR1_ACK;

    // Ждем данные
    while(!(I2C1->SR1 & I2C_SR1_RXNE) && !(I2C1->SR1 & I2C_SR1_AF));
    uint8_t data = I2C1->DR;

    // STOP condition
    I2C1->CR1 |= I2C_CR1_STOP;

    return data;
}


float TMP100_GetTemperature(uint8_t devAddr) {
    uint16_t raw = TMP100_ReadTempRaw(devAddr);

    // Проверка текущего разрешения
    uint8_t config = TMP100_ReadReg(devAddr, TMP100_REG_CONFIG);
    uint8_t resolution = config & 0x60;

    if(resolution != TMP100_RES_12BIT) {
        TMP100_SetResolution(devAddr, TMP100_RES_12BIT);
        for(volatile uint32_t i = 0; i < 100000; i++); // Задержка для переконфигурации
    }

    // Преобразование для 12-битного режима
    int16_t temp = (int16_t)(raw >> 4);
    if(temp & 0x800) temp |= 0xF000; // Знаковое расширение

    float result = temp * 0.0625f;

    // Округление до десятых (опционально)
    return (int)(result * 10) / 10.0f;
}

// Инициализация PB7 для RDY
void ADS1115_Init(void) {
    // 1. Включение тактирования GPIOB
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;

    // 2. Настройка PB7 как вход с подтяжкой
    GPIOB->MODER &= ~GPIO_MODER_MODER7;
    GPIOB->PUPDR |= GPIO_PUPDR_PUPDR7_0; // Pull-up
}

// Чтение сырых данных
int16_t ADS1115_ReadRaw(uint8_t devAddr) {
    /*// Ждем готовности (RDY = LOW)
    //while(GPIOB->IDR & GPIO_IDR_ID7);
    // Чтение регистра конверсии (2 байта)
    I2C1->CR1 |= I2C_CR1_START;
    while(!(I2C1->SR1 & I2C_SR1_SB));

    I2C1->DR = (devAddr << 1) | 0x01; // Адрес + чтение
    while(!(I2C1->SR1 & I2C_SR1_ADDR));
    (void)I2C1->SR2;

    // Чтение двух байт
    I2C1->CR1 |= I2C_CR1_ACK;
    while(!(I2C1->SR1 & I2C_SR1_RXNE));
    uint8_t msb = I2C1->DR;

    I2C1->CR1 &= ~I2C_CR1_ACK;
    while(!(I2C1->SR1 & I2C_SR1_RXNE));
    uint8_t lsb = I2C1->DR;

    I2C1->CR1 |= I2C_CR1_STOP;
    return (int16_t)((msb << 8) | lsb);*/
	return TMP100_ReadTempRaw(devAddr);
}

// Чтение напряжения
float ADS1115_ReadVoltage(uint8_t devAddr) {
    uint8_t config[3] = {
        ADS1115_REG_CONFIG,
        (ADS1115_OS_SINGLE | ADS1115_MUX_SINGLE_0 | ADS1115_PGA_4V096) >> 8,
        (ADS1115_DR_128SPS | ADS1115_COMP_QUE_DISABLE) & 0xFF
    };

    // 1. Запись конфигурации с таймаутом
    uint32_t timeout = 100000; // ~100ms при 42MHz
    I2C1->CR1 |= I2C_CR1_START;
    while(!(I2C1->SR1 & I2C_SR1_SB) && --timeout);
    if(!timeout)
    	{
    		printf("\nADS config set failed\n");
    		return NAN;
    	}

    I2C1->DR = (devAddr << 1) & ~0x01;
    timeout = 100000;
    while(!(I2C1->SR1 & I2C_SR1_ADDR) && --timeout) {
        if(I2C1->SR1 & I2C_SR1_AF) {
            I2C1->SR1 &= ~I2C_SR1_AF;
            I2C1->CR1 |= I2C_CR1_STOP;
            return NAN;
        }
    }
    if(!timeout) return NAN;

    (void)I2C1->SR2; // Сброс ADDR

    for(int i = 0; i < 3; i++) {
        timeout = 100000;
        while(!(I2C1->SR1 & I2C_SR1_TXE) && --timeout);
        if(!timeout) return NAN;
        I2C1->DR = config[i];
    }

    timeout = 100000;
    while(!(I2C1->SR1 & I2C_SR1_BTF) && --timeout);
    I2C1->CR1 |= I2C_CR1_STOP;

    // 2. Ожидание готовности с таймаутом (PB7)
    timeout = 1000000; // ~1s
    //while((GPIOB->IDR & GPIO_IDR_ID7) && --timeout);
    //if(!timeout)
    //{
    // 	printf("\nADS not ready\n");
    // 	return NAN;
    //}
    __delay(20);

    // 3. Чтение результата
    int16_t raw = ADS1115_ReadRaw(devAddr);
    return (raw * 4.096f) / 32768.0f;
}


