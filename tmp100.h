#ifndef TMP100_H
#define TMP100_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Конфигурация */
#define TMP100_DEFAULT_ADDRESS  0x48  // Базовый адрес (может быть 0x48-0x4F в зависимости от A0-A2)

/* Регистры датчика */
#define TMP100_REG_TEMP         0x00  // Регистр температуры (Read Only)
#define TMP100_REG_CONFIG       0x01  // Регистр конфигурации
#define TMP100_REG_TLOW         0x02  // Регистр нижнего порога
#define TMP100_REG_THIGH        0x03  // Регистр верхнего порога

/* Биты конфигурационного регистра */
#define TMP100_CONFIG_SHUTDOWN  0x01  // Режим пониженного энергопотребления
#define TMP100_CONFIG_THERM     0x02  // Режим термостата
#define TMP100_CONFIG_POL       0x04  // Полярность вывода ALERT
#define TMP100_CONFIG_F0        0x08  // Бит F0 для настройки ALERT
#define TMP100_CONFIG_F1        0x10  // Бит F1 для настройки ALERT
#define TMP100_CONFIG_R0        0x20  // Бит разрешения R0
#define TMP100_CONFIG_R1        0x40  // Бит разрешения R1

/* Режимы разрешения */
#define TMP100_RES_9BIT        0x00  // 9 бит (0.5°C)
#define TMP100_RES_10BIT       0x20  // 10 бит (0.25°C)
#define TMP100_RES_11BIT       0x40  // 11 бит (0.125°C)
#define TMP100_RES_12BIT       0x60  // 12 бит (0.0625°C)

/* Состояния ошибок */
#define TMP100_OK              0
#define TMP100_ERROR           -1

/* ADS1115 Конфигурация */
#define ADS1115_ADDRESS         0x48  // Базовый адрес (GND=0x48, VDD=0x49, SDA=0x4A, SCL=0x4B)
#define ADS1115_REG_CONV        0x00  // Регистр данных
#define ADS1115_REG_CONFIG      0x01  // Регистр конфигурации
#define ADS1115_REG_LO_THRESH   0x02  // Нижний порог
#define ADS1115_REG_HI_THRESH   0x03  // Верхний порог

/* Биты конфигурации */
#define ADS1115_OS_SINGLE       0x8000  // Запуск преобразования
#define ADS1115_MUX_SINGLE_0    0x4000  // Канал AIN0
#define ADS1115_PGA_4V096       0x0200  // Усиление ±4.096V
#define ADS1115_MODE_CONTIN     0x0000  // Непрерывный режим
#define ADS1115_DR_128SPS       0x0080  // 128 samples/sec
#define ADS1115_COMP_MODE_TRAD  0x0000  // Традиционный компаратор
#define ADS1115_COMP_POL_LOW    0x0000  // Активный низкий уровень
#define ADS1115_COMP_NONLAT     0x0000  // Не фиксировать
#define ADS1115_COMP_QUE_DISABLE 0x0003 // Отключить компаратор
#define ADS1115_HI_THRESH_RDY   0x8000  // Для активации режима RDY
#define ADS1115_LO_THRESH_RDY   0x0000  // Для активации режима RDY
#define ADS1115_COMP_POL_LOW    0x0000  // RDY активен LOW

/**
  * @brief Инициализация интерфейса I2C для работы с TMP100
  * @note Использует PB8 (SCL) и PB9 (SDA)
  */
void TMP100_Init(void);

/**
  * @brief Запись данных в регистр датчика
  * @param devAddr: 7-битный адрес устройства I2C
  * @param regAddr: Адрес регистра
  * @param data: Данные для записи
  * @retval TMP100_OK в случае успеха, TMP100_ERROR при ошибке
  */
int TMP100_WriteReg(uint8_t devAddr, uint8_t regAddr, uint8_t data);

/**
  * @brief Чтение регистра датчика
  * @param devAddr: 7-битный адрес устройства I2C
  * @param regAddr: Адрес регистра
  * @return Прочитанное значение или 0xFF при ошибке
  */
uint8_t TMP100_ReadReg(uint8_t devAddr, uint8_t regAddr);

/**
  * @brief Чтение сырого значения температуры
  * @param devAddr: 7-битный адрес устройства I2C
  * @return 16-битное значение температуры (12 бит в старших разрядах)
  */
uint16_t TMP100_ReadTempRaw(uint8_t devAddr);

/**
  * @brief Получение температуры в градусах Цельсия
  * @param devAddr: 7-битный адрес устройства I2C
  * @return Температура в градусах Цельсия
  */
float TMP100_GetTemperature(uint8_t devAddr);

/**
  * @brief Установка разрешения датчика
  * @param devAddr: 7-битный адрес устройства I2C
  * @param resolution: Одно из значений TMP100_RES_xBIT
  * @retval TMP100_OK в случае успеха, TMP100_ERROR при ошибке
  */
int TMP100_SetResolution(uint8_t devAddr, uint8_t resolution);
void ADS1115_Init(void);
int16_t ADS1115_ReadRaw(uint8_t devAddr);
float ADS1115_ReadVoltage(uint8_t devAddr);

#ifdef __cplusplus
}
#endif

#endif /* TMP100_H */
