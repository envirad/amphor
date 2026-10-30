/*
 * dump.h
 *
 *  Created on: 2 июн. 2025 г.
 *      Author: jar
 */

#ifndef DUMP_H_
#define DUMP_H_
#include <stdint.h>

void HEX_Dump(uint8_t * ptr, uint32_t len, uint32_t offset);
int8_t HEX_DumpFile(char * filename);
int8_t HEX_CreateFile(char * filename);

#endif /* DUMP_H_ */
