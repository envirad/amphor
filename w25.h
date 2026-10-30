#ifndef W25_H
#define W25_H

#include "stm32f4xx.h"
#include <stdint.h>
#include <stdbool.h>

// Размеры памяти
#define W25_SECTOR_SIZE    4096
#define W25_PAGE_SIZE      256

// Команды W25Q
#define W25_READ_ID        0x9f
#define W25_READ_DATA      0x03
#define W25_PAGE_PROGRAM   0x02
#define W25_SECTOR_ERASE   0x20
#define W25_WRITE_ENABLE   0x06
#define W25_READ_STATUS1   0x05
#define W25_ENABLE_RESET   0x66
#define W25_RESET_DEVICE   0x99

// Инициализация
void W25_Init(void);

// Базовые операции
uint32_t W25_ReadID(void);
void W25_ReadData(uint32_t addr, uint8_t *buf, uint32_t len);
void W25_PageProgram(uint32_t addr, uint8_t *buf, uint16_t len);
void W25_SectorErase(uint32_t addr);
void W25_ChipErase(void);

// Вспомогательные функции
void W25_WriteEnable(void);
uint8_t W25_ReadStatus(void);
void W25_WaitForReady(void);
void W25_Reset(void);
//static void CheckProtection(void);
uint16_t W25_Test(uint32_t);
void CheckEntireChip (void);

#endif // W25_H
