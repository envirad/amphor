#include <rtc.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "stm32f4xx.h"

const int day_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
// Глобальные переменные для работы с прерываниями
volatile uint32_t rtc_seconds_counter = 0;
volatile uint8_t rtc_second_flag = 0;



// Проверка на високосный год
static uint8_t is_leap_year(uint16_t year) {
    if (year % 4 != 0) return 0;
    if (year % 100 != 0) return 1;
    return (year % 400 == 0);
}

static bool is_valid_date(uint16_t year, uint8_t month, uint8_t day)
{
	if(year < 0 || month < 1 || month > 12 || day < 1)
		return false;
	uint8_t max_days = day_in_month[month - 1];
	if(month == 2 && is_leap_year(year))
		max_days = 29;
	return day <= max_days;
}

static bool is_valid_time(uint8_t hour, uint8_t minute, uint8_t second)
{
	return 	(hour >= 0 && hour < 24) 		&&
			(minute >= 0 && minute < 60) 	&&
			(second >= 0 && second < 60);
}

bool RTC_ParseDate(const char * date_str, DateTime * dt)
{
	if(strlen(date_str) != 10 || date_str[4] != '-' || date_str[7] != '-')
		return false;
	for(int i = 0; i < 10; i++)
	{
		if(i == 4 || i == 7) continue;
		if(!isdigit((unsigned char)date_str[i]))
			return false;
	}
	char year[5] = {0};
	char month[3] = {0};
	char day[3] = {0};
	memcpy(year, date_str, 4);
	memcpy(month, date_str + 5, 2);
	memcpy(day, date_str + 8, 2);
	uint16_t y = atoi(year);
	uint8_t m = atoi(month);
	uint8_t d = atoi(day);

	if(!is_valid_date(y, m, d))
		return false;
	dt->year = y;
	dt->month = m;
	dt->day = d;
	return true;
}

bool RTC_ParseTime(const char * time_str, DateTime  *dt)
{
    size_t len = strlen(time_str);

    // Проверка допустимых длин и разделителей
    if ((len != 5 && len != 8) ||
        time_str[2] != ':' ||
        (len == 8 && time_str[5] != ':')) {
        return false;
    }

    // Проверка цифровых символов
    for (size_t i = 0; i < len; i++) {
        if (i == 2 || i == 5) continue; // Пропускаем разделители
        if (!isdigit((unsigned char)time_str[i])) {
            return false;
        }
    }

    // Извлечение компонентов времени
    char hour[3] = {0};
    char minute[3] = {0};
    char second[3] = {0};

    memcpy(hour, time_str, 2);
    memcpy(minute, time_str + 3, 2);

    int sec = 0;
    if (len == 8) {
        memcpy(second, time_str + 6, 2);
        sec = atoi(second);
    }

    // Конвертация в числа
    int h = atoi(hour);
    int m = atoi(minute);

    // Проверка валидности
    if (!is_valid_time(h, m, sec)) {
        return false;
    }

    // Запись результата
    dt->hours = h;
    dt->minutes = m;
    dt->seconds = sec;
    return true;
}

// Преобразование BCD в двоичный формат
static uint8_t bcd_to_bin(uint8_t bcd) {
    return (bcd >> 4) * 10 + (bcd & 0x0F);
}

// Преобразование двоичного формата в BCD
static uint8_t bin_to_bcd(uint8_t bin) {
    return ((bin / 10) << 4) | (bin % 10);
}

// Получение даты и времени из RTC
void RTC_GetDateTime(DateTime *dt) {
    uint32_t time_reg, date_reg;

    // Ждем завершения синхронизации
    while (!(RTC->ISR & RTC_ISR_RSF));

    // Атомарное чтение регистров
    do {
        time_reg = RTC->TR;
        date_reg = RTC->DR;
    } while ((time_reg != RTC->TR) || (date_reg != RTC->DR));

    // Преобразование BCD в двоичный формат
    dt->seconds = bcd_to_bin(time_reg & 0x7F);
    dt->minutes = bcd_to_bin((time_reg >> 8) & 0x7F);
    dt->hours = bcd_to_bin((time_reg >> 16) & 0x3F);
    dt->day = bcd_to_bin(date_reg & 0x3F);
    dt->month = bcd_to_bin((date_reg >> 8) & 0x1F);
    dt->year = 2000 + bcd_to_bin((date_reg >> 16) & 0xFF);
}

// Установка даты и времени в RTC
void RTC_SetDateTime(DateTime *dt)
{
    // Формирование регистров в BCD формате
    uint32_t time_reg = bin_to_bcd(dt->seconds) |
                       (bin_to_bcd(dt->minutes) << 8) |
                       (bin_to_bcd(dt->hours) << 16);

    uint32_t date_reg = bin_to_bcd(dt->day) |
                       (bin_to_bcd(dt->month) << 8) |
                       (bin_to_bcd(dt->year - 2000) << 16);

    // Разблокировка RTC
    RTC->WPR = 0xCA;
    RTC->WPR = 0x53;

    // Вход в режим инициализации
    RTC->ISR |= RTC_ISR_INIT;
    while (!(RTC->ISR & RTC_ISR_INITF));

    // Установка времени и даты
    RTC->TR = time_reg;
    RTC->DR = date_reg;

    // Выход из режима инициализации
    RTC->ISR &= ~RTC_ISR_INIT;

    // Блокировка RTC
    RTC->WPR = 0xFF;
}

// Инициализация RTC
void RTC_Init(void) {
    // Включение тактирования PWR и RTC
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    PWR->CR |= PWR_CR_DBP; // Разрешение доступа к RTC

    // Проверка, была ли уже инициализирована RTC
    if (RTC->BKP0R != 0x32F2) {
        // Включение LSE (32.768 kHz)
        RCC->BDCR |= RCC_BDCR_LSEON;
        while (!(RCC->BDCR & RCC_BDCR_LSERDY));

        // Выбор LSE в качестве источника RTC
        RCC->BDCR &= ~RCC_BDCR_RTCSEL;
        RCC->BDCR |= RCC_BDCR_RTCSEL_0;
        RCC->BDCR |= RCC_BDCR_RTCEN;

        // Разблокировка регистров RTC
        RTC->WPR = 0xCA;
        RTC->WPR = 0x53;

        // Вход в режим инициализации
        RTC->ISR |= RTC_ISR_INIT;
        while (!(RTC->ISR & RTC_ISR_INITF));

        // Настройка предделителей (32.768 kHz -> 1 Hz)
        RTC->PRER = (127 << 0) | (255 << 16);

        // Формат времени - 24 часа
        RTC->CR &= ~RTC_CR_FMT;

        // Выход из режима инициализации
        RTC->ISR &= ~RTC_ISR_INIT;

        // Блокировка регистров RTC
        RTC->WPR = 0xFF;

        // Помечаем, что RTC инициализирована
        RTC->BKP0R = 0x32F2;
    }
}

// Установка UNIX-времени
void RTC_SetUnixTime(uint32_t unix_time) {
    // Преобразование UNIX-time в struct tm
    time_t temp_time = (time_t)unix_time;
    struct tm *tm_time = localtime(&temp_time);

    // Формирование структуры DateTime
    DateTime dt = {
        .seconds = tm_time->tm_sec,
        .minutes = tm_time->tm_min,
        .hours = tm_time->tm_hour,
        .day = tm_time->tm_mday,
        .month = tm_time->tm_mon + 1,
        .year = tm_time->tm_year + 1900
    };

    // Установка даты и времени
    RTC_SetDateTime(&dt);
}

// Получение UNIX-времени
uint32_t RTC_GetUnixTime(void) {
    DateTime dt;
    RTC_GetDateTime(&dt);

    // Количество дней с начала эпохи UNIX (1970)
    uint32_t days = 0;

    // Добавляем дни за полные годы
    for (uint16_t y = 1970; y < dt.year; y++) {
        days += is_leap_year(y) ? 366 : 365;
    }

    // Количество дней в месяцах
    const uint8_t days_in_month[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    // Добавляем дни за полные месяцы текущего года
    for (uint8_t m = 1; m < dt.month; m++) {
        days += days_in_month[m-1];

        // Коррекция для февраля в високосный год
        if (m == 2 && is_leap_year(dt.year)) {
            days += 1;
        }
    }

    // Добавляем дни текущего месяца
    days += dt.day - 1;

    // Вычисляем общее количество секунд
    uint32_t total_secs = days * 86400UL;
    total_secs += dt.hours * 3600UL;
    total_secs += dt.minutes * 60UL;
    total_secs += dt.seconds;

    return total_secs;
}

// Преобразование UNIX-времени в структуры даты/времени
void UnixTimeToDateTime(uint32_t unix_time, TimeStruct *time, DateStruct *date) {
    time_t temp_time = (time_t)unix_time;
    struct tm *tm_time = localtime(&temp_time);

    time->seconds = tm_time->tm_sec;
    time->minutes = tm_time->tm_min;
    time->hours = tm_time->tm_hour;

    date->day = tm_time->tm_mday;
    date->month = tm_time->tm_mon + 1;
    date->year = tm_time->tm_year + 1900;
    date->weekday = tm_time->tm_wday;
}

// Преобразование структур даты/времени в UNIX-время
void DateTimeToUnixTime(TimeStruct *time, DateStruct *date, uint32_t *unix_time) {
    struct tm tm_time = {
        .tm_sec = time->seconds,
        .tm_min = time->minutes,
        .tm_hour = time->hours,
        .tm_mday = date->day,
        .tm_mon = date->month - 1,
        .tm_year = date->year - 1900,
        .tm_isdst = -1
    };

    *unix_time = (uint32_t)mktime(&tm_time);
}

// Включение секундных прерываний
void RTC_EnableSecondInterrupt(void) {
    // Разблокировка регистров RTC
    RTC->WPR = 0xCA;
    RTC->WPR = 0x53;

    // Отключение WakeUp таймера
    RTC->CR &= ~RTC_CR_WUTE;
    while (!(RTC->ISR & RTC_ISR_WUTWF));

    // Настройка WakeUp таймера на 1 секунду
    RTC->WUTR = 0; // Используем WUCKSEL для 1 секунды

    // Выбор источника (CK_SPRE, 1Hz)
    RTC->CR &= ~RTC_CR_WUCKSEL;
    RTC->CR |= RTC_CR_WUCKSEL_2; // 010: CK_SPRE (1Hz)

    // Включение прерывания
    RTC->CR |= RTC_CR_WUTIE;

    // Включение WakeUp таймера
    RTC->CR |= RTC_CR_WUTE;

    // Блокировка регистров RTC
    RTC->WPR = 0xFF;

    // Настройка NVIC
    NVIC_SetPriority(RTC_WKUP_IRQn, 1);
    NVIC_EnableIRQ(RTC_WKUP_IRQn);

    // Разрешение прерывания от EXTI линии 22
    EXTI->IMR |= (1 << 22);
    EXTI->RTSR |= (1 << 22);
}

// Обработчик прерываний RTC WakeUp
void RTC_WKUP_IRQHandler(void) {
    if (RTC->ISR & RTC_ISR_WUTF) {
        // Сброс флага прерывания
        RTC->ISR &= ~RTC_ISR_WUTF;

        // Обновление счетчиков
        rtc_seconds_counter++;
        rtc_second_flag = 1;

        // Синхронизация с SysTick
        while (SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk);

        // Сброс флага EXTI
        EXTI->PR = (1 << 22);
    }
}

// Проверка флага секунды
uint8_t RTC_CheckSecondFlag(void) {
    if (rtc_second_flag) {
        rtc_second_flag = 0;
        return 1;
    }
    return 0;
}

// Получение счетчика секунд
uint32_t RTC_GetSecondsCounter(void) {
    return rtc_seconds_counter;
}
