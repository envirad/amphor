#include "uart.h"
#include "stm32f4xx.h"
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

// Кольцевой буфер для приема данных
static volatile uint8_t rx_buffer[RX_BUF_SIZE];
static volatile uint32_t rx_head = 0;
static volatile uint32_t rx_tail = 0;
static volatile uint8_t string_ready = 0;
static volatile uint8_t line_buffer[128];
static volatile uint8_t line_ready = 0;
static volatile uint8_t break_received = 0;

// Обработка принятых данных
static void handle_rx_data(uint8_t data)
{
    uint32_t next_head = (rx_head + 1) % RX_BUF_SIZE;

    // Если есть место в буфере
    if (next_head != rx_tail) {
        rx_buffer[rx_head] = data;
        rx_head = next_head;

        // Отмечаем наличие полной строки
        if (data == '\n' || data == '\r') {
            string_ready = 1;
        }
        if(data == 0x03)
        {
        	break_received = 1;
        }
    }
}

// Инициализация USART1
void USART1_Init(uint32_t baudrate)
{
    // Тактирование
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

    // Настройка GPIO PA9 (TX), PA10 (RX)
    GPIOA->MODER &= ~(GPIO_MODER_MODER9 | GPIO_MODER_MODER10);
    GPIOA->MODER |= (2 << GPIO_MODER_MODER9_Pos) | (2 << GPIO_MODER_MODER10_Pos);
    GPIOA->AFR[1] |= (7 << 4) | (7 << 8); // AF7 для USART1

    // Настройка скорости
    SystemCoreClockUpdate();
    uint32_t clock = SystemCoreClock;
    if ((RCC->CFGR & RCC_CFGR_PPRE2) > RCC_CFGR_PPRE2_DIV1) {
        clock *= 2;
    }
    USART1->BRR = (clock + baudrate/2) / baudrate;

    // Включаем USART с прерываниями
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;

    // Настройка NVIC
    NVIC_SetPriority(USART1_IRQn, 0);
    NVIC_EnableIRQ(USART1_IRQn);

    // Задержка стабилизации
    for(volatile int i = 0; i < 100; i++);
}

// Инициализация USART6
void USART6_Init(uint32_t baudrate)
{
    // Тактирование
    RCC->APB2ENR |= RCC_APB2ENR_USART6EN;
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

    // Настройка GPIO PA11 (TX), PA12 (RX)
    GPIOA->MODER &= ~(GPIO_MODER_MODER11 | GPIO_MODER_MODER12);
    GPIOA->MODER |= (2 << GPIO_MODER_MODER11_Pos) | (2 << GPIO_MODER_MODER12_Pos);
    GPIOA->AFR[1] |= (8 << (4*(11-8))) | (8 << (4*(12-8))); // AF8 для USART6

    // Настройка скорости
    uint32_t clock = SystemCoreClock;
    USART6->BRR = (clock + baudrate/2) / baudrate;

    // Включаем USART
    USART6->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;

    // Задержка стабилизации
    for(volatile int i = 0; i < 100; i++);
}

// Обработчики прерываний
void USART1_IRQHandler(void)
{
    if (USART1->SR & USART_SR_RXNE)
    {
        uint8_t data = USART1->DR & 0xFF;
        handle_rx_data(data);
    }
}

// Функции для работы с буфером
uint8_t USART1_IsStringReady(void)
{
    return string_ready;
}

uint8_t USART1_IsBreakReceived(void)
{
	return break_received;
}

void USART1_FlushInputBuffer(void)
{
    rx_head = rx_tail = 0;
    string_ready = 0;
    line_ready = 0;
    break_received = 0;
}

// Функция для чтения символа с таймаутом
int USART1_GetChar(uint32_t timeout)
{
    volatile uint32_t start = SystemCoreClock / 1000 * timeout;
    volatile uint32_t counter = 0;

    while (rx_tail == rx_head) {
        if (counter++ > start) {
            return -1; // Таймаут
        }
    }

    uint8_t c = rx_buffer[rx_tail];
    rx_tail = (rx_tail + 1) % RX_BUF_SIZE;
    return c;
}

// Функция для получения строки (блокирующая)
char* USART1_GetLine(void)
{
    static char line[128];
    uint8_t index = 0;

    // Ожидание начала строки
    while (!string_ready) {
        // Можно добавить обработку других задач
    }

    // Чтение до символа новой строки
    while (index < sizeof(line) - 1) {
        int c = USART1_GetChar(100); // Таймаут 100 мс на символ
        if (c < 0) break; // Таймаут

        if (c == '\n' || c == '\r') {
            // Пропускаем все символы новой строки
            while (USART1_Available() &&
                  (rx_buffer[rx_tail] == '\n' || rx_buffer[rx_tail] == '\r')) {
                rx_tail = (rx_tail + 1) % RX_BUF_SIZE;
            }
            break;
        }

        line[index++] = c;
    }

    line[index] = '\0';
    string_ready = 0;
    return line;
}

// Функции для стандартного ввода/вывода
int __io_getchar(void)
{
    while (rx_tail == rx_head); // Ожидание символа
    uint8_t c = rx_buffer[rx_tail];
    rx_tail = (rx_tail + 1) % RX_BUF_SIZE;
    return c;
}

int __io_putchar(int ch)
{
    USART1->DR = (ch & 0xFF);
    while (!(USART1->SR & USART_SR_TXE));
    return ch;
}

int USART6_Write(int ch)
{
    USART6->DR = (ch & 0xFF);
    while (!(USART6->SR & USART_SR_TXE));
    return ch;
}

// Переопределение системного вывода
int _write(int fd, const char *buf, int count)
{
    if (fd == STDERR_FILENO)
    {
        for (int i = 0; i < count; i++)
        {
            USART6_Write(buf[i]);
        }
        return count;
    }
    else if (fd == STDOUT_FILENO)
    {
        for (int i = 0; i < count; i++)
        {
            __io_putchar(buf[i]);
        }
        return count;
    }

    return -1;
}

void redirect_stderr_to_usart6(void)
{
    setvbuf(stderr, NULL, _IONBF, 0);
}
