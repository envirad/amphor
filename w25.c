#include "w25.h"
#include <string.h>
#include <stdio.h>
#include "dump.h"


// Пины SPI2 (PB13-SCK, PB14-MISO, PB15-MOSI)
#define W25_CS_PORT    GPIOB
#define W25_CS_PIN     12  // Используем номер пина вместо макроса

// Простая функция задержки
static void Delay(uint32_t count) {
    for(volatile uint32_t i = 0; i < count; i++);
}

// Инициализация SPI2 с правильными настройками
static void SPI2_Init(void) {
    // Включение тактирования
    RCC->APB1ENR |= RCC_APB1ENR_SPI2EN;
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;

    // Настройка выводов SPI: PB13, PB14, PB15
    GPIOB->MODER &= ~(GPIO_MODER_MODER13 | GPIO_MODER_MODER14 | GPIO_MODER_MODER15);
    GPIOB->MODER |= (2 << GPIO_MODER_MODER13_Pos) |
                    (2 << GPIO_MODER_MODER14_Pos) |
                    (2 << GPIO_MODER_MODER15_Pos);

    GPIOB->AFR[1] |= (5 << GPIO_AFRH_AFSEL13_Pos) |  // AF5 для SPI2
                     (5 << GPIO_AFRH_AFSEL14_Pos) |
                     (5 << GPIO_AFRH_AFSEL15_Pos);

    // Конфигурация SPI - КРИТИЧЕСКИЕ ИЗМЕНЕНИЯ:
    SPI2->CR1 = SPI_CR1_MSTR |          // Режим Master
                SPI_CR1_BR_1 |          // APB1/16 (5.25 МГц)
                SPI_CR1_SSI |           // Internal SS
                SPI_CR1_SSM;            // Software NSS

    SPI2->CR2 = 0;
    SPI2->CR1 |= SPI_CR1_SPE;           // Enable SPI

    // Добавляем задержку для стабилизации
    Delay(1000);
}

// Инициализация вывода CS с подтяжкой
static void CS_Init(void) {
    GPIOB->MODER &= ~(3U << (W25_CS_PIN * 2));
    GPIOB->MODER |= (1U << (W25_CS_PIN * 2));
    GPIOB->OTYPER &= ~(1U << W25_CS_PIN);
    GPIOB->OSPEEDR |= (3U << (W25_CS_PIN * 2));
    GPIOB->PUPDR |= (1U << (W25_CS_PIN * 2));  // Подтяжка к VCC

    W25_CS_PORT->BSRR = (1U << W25_CS_PIN);  // High
    Delay(1000);
}

static void CS_Low() { W25_CS_PORT->BSRR = (1 << (W25_CS_PIN + 16)); }
static void CS_High() { W25_CS_PORT->BSRR = (1 << W25_CS_PIN); }

// Передача/прием байта с проверкой флагов
static uint8_t SPI_Transfer(uint8_t data) {
    // Ожидаем освобождения буфера передачи
    while (!(SPI2->SR & SPI_SR_TXE));
    SPI2->DR = data;

    // Ожидаем завершения приема
    while (!(SPI2->SR & SPI_SR_RXNE));
    return SPI2->DR;
}

void W25_Init(void) {
    // Важно: сначала инициализация CS, потом SPI
    //printf("W25-I-Init\n");
	CS_Init();
    SPI2_Init();

    // Большая задержка после включения (минимум 100 мс)
    for(int i = 0; i < 1000000; i++);

    // Сброс и дополнительная инициализация
    W25_Reset();

    // Дополнительная задержка после сброса
    Delay(100000);
}

// Добавляем функцию проверки защиты
static void CheckProtection() {
    uint8_t status = W25_ReadStatus();

    // Если установлены биты защиты (BP0-BP3)
    if(status & 0x3C) {
        // Снимаем защиту
        CS_Low();
        SPI_Transfer(0x06); // WRITE_ENABLE
        CS_High();

        CS_Low();
        SPI_Transfer(0x01); // WRITE_STATUS
        SPI_Transfer(0x00); // Статус = 0 (без защиты)
        CS_High();

        W25_WaitForReady();
    }
}

uint32_t W25_ReadID(void) {
    uint8_t cmd = W25_READ_ID;
    uint8_t buf[3] = {0};

    CS_Low();
    SPI_Transfer(cmd);
    buf[0] = SPI_Transfer(0);
    buf[1] = SPI_Transfer(0);
    buf[2] = SPI_Transfer(0);
    CS_High();

    return (buf[0] << 16) | (buf[1] << 8) | buf[2];
}

void W25_ReadData(uint32_t addr, uint8_t *buf, uint32_t len) {
	//printf("W25-I-Read data from %08X, length %d\n", addr, len);
	uint8_t cmd[4] = {
        W25_READ_DATA,
        (uint8_t)(addr >> 16),
        (uint8_t)(addr >> 8),
        (uint8_t)addr
    };

    CS_Low();
    for(int i = 0; i < 4; i++) SPI_Transfer(cmd[i]);
    for(uint32_t i = 0; i < len; i++) buf[i] = SPI_Transfer(0);
    CS_High();
}

void W25_WriteEnable(void) {
	//printf("W25-I-Write enable\n");
    CS_Low();
    SPI_Transfer(W25_WRITE_ENABLE);
    CS_High();
}

uint8_t W25_ReadStatus(void) {
    CS_Low();
    SPI_Transfer(W25_READ_STATUS1);
    uint8_t status = SPI_Transfer(0);
    CS_High();
    //printf("W25-I-Status: %02X\n", status);
    return status;
}

void W25_WaitForReady(void) {
    while(W25_ReadStatus() & 0x01) {
        Delay(1000); // Простая задержка
    }
}

void W25_SectorErase(uint32_t addr) {
    CheckProtection(); // Проверка защиты
    //printf("W25-I-Erasing sector %08X\n", addr);

    W25_WriteEnable();
    W25_WaitForReady();

    uint8_t cmd[4] = {
        W25_SECTOR_ERASE,
        (uint8_t)(addr >> 16),
        (uint8_t)(addr >> 8),
        (uint8_t)(addr & 0xFF)
    };

    CS_Low();
    for(int i = 0; i < 4; i++) SPI_Transfer(cmd[i]);
    CS_High();

    // Увеличиваем время ожидания для стирания
    for(int i = 0; i < 500; i++) {
        if(!(W25_ReadStatus() & 0x01)) break;
        Delay(10000);
    }
}

void W25_ChipErase(void)
{
	for(int i = 0; i < 4096; i++)
	{
		W25_SectorErase(i * W25_SECTOR_SIZE);
		printf(".");
	}
	printf("\n");
}

void W25_PageProgram(uint32_t addr, uint8_t *buf, uint16_t len) {
    if(len > W25_PAGE_SIZE) len = W25_PAGE_SIZE;
    //printf("W25-I-Write data from %08X, length %d\n", addr, len);
    W25_WriteEnable();
    W25_WaitForReady();

    uint8_t cmd[4] = {
        W25_PAGE_PROGRAM,
        (uint8_t)(addr >> 16),
        (uint8_t)(addr >> 8),
        (uint8_t)addr
    };

    CS_Low();
    for(int i = 0; i < 4; i++) SPI_Transfer(cmd[i]);
    for(uint16_t i = 0; i < len; i++) SPI_Transfer(buf[i]);
    CS_High();

    W25_WaitForReady();
}

void W25_SectorProgram(uint32_t addr, uint8_t *buf)
{
	const uint8_t blocks_in_sector = W25_SECTOR_SIZE / W25_PAGE_SIZE;
	for (uint8_t i = 0; i < blocks_in_sector; i++)
		W25_PageProgram(addr + i * W25_PAGE_SIZE, buf + i * W25_PAGE_SIZE, W25_PAGE_SIZE);
}

void W25_Reset(void) {
	//printf("W25-I-Reset\n");
    CS_Low();
    SPI_Transfer(W25_ENABLE_RESET);
    CS_High();

    CS_Low();
    SPI_Transfer(W25_RESET_DEVICE);
    CS_High();

    Delay(10000); // Задержка 10 мс (примерно)
    W25_WaitForReady();
}
