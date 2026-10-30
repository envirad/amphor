#ifndef __UART_H__
#define __UART_H__

#include <stdint.h>

#define RX_BUF_SIZE 512

void USART1_Init(uint32_t baudrate);
void USART6_Init(uint32_t baudrate);
void redirect_stderr_to_usart6(void);

uint8_t USART1_IsStringReady(void);
uint8_t USART1_IsBreakReceived(void);
void USART1_FlushInputBuffer(void);
int USART1_GetChar(uint32_t timeout);
char* USART1_GetLine(void);

#endif // __UART_H__
