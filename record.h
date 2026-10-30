/*
 * record.h
 *
 *  Created on: Jun 1, 2025
 *      Author: jar
 */

#ifndef RECORD_H_
#define RECORD_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define REC_BUF_BLOCK_SIZE 4096
#define REC_BUF_BLOCKS 1

typedef struct __attribute__((packed)){
	unsigned int mantissa:13;
	unsigned int order:3;
} myFloat16;

typedef struct __attribute__((packed)) {
	int16_t temp;
	uint16_t depth;
	myFloat16 clear;
	myFloat16 red;
	myFloat16 green;
	myFloat16 blue;
	uint32_t time;
} RecordFrame;

extern uint8_t RecordBuffer[REC_BUF_BLOCK_SIZE * REC_BUF_BLOCKS]; // Буфер для сохранения собираемой информации
extern uint32_t RecordBuffer_endptr;

void Record_NewRecord(uint8_t);
void Record_ListRecord(char * filename);
void Record_TypeRecord(char * filename);



#endif /* RECORD_H_ */
