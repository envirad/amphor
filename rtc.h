#ifndef RTC_H
#define RTC_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// Преобразование UNIX-времени в структуры даты/времени
typedef struct {
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
} TimeStruct;

typedef struct {
    uint8_t day;
    uint8_t month;
    uint16_t year;
    uint8_t weekday;
} DateStruct;

// Структура для хранения даты и времени
typedef struct {
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t day;
    uint8_t month;
    uint16_t year;
} DateTime;

// Инициализация RTC
void RTC_Init(void);

// Установка UNIX-времени
void RTC_SetUnixTime(uint32_t unix_time);

// Получение UNIX-времени
uint32_t RTC_GetUnixTime(void);

void RTC_GetDateTime(DateTime *);
void RTC_SetDateTime(DateTime *);

void UnixTimeToDateTime(uint32_t unix_time, TimeStruct *time, DateStruct *date);
void DateTimeToUnixTime(TimeStruct *time, DateStruct *date, uint32_t *unix_time);
bool RTC_ParseDate(const char * date_str, DateTime * dt);
bool RTC_ParseTime(const char * time_str, DateTime * dt);


// Работа с секундными прерываниями
void RTC_EnableSecondInterrupt(void);
uint8_t RTC_CheckSecondFlag(void);
uint32_t RTC_GetSecondsCounter(void);

// Обработчик прерываний (должен вызываться из вектора прерываний)
void RTC_WKUP_IRQHandler(void);

#endif // RTC_H
