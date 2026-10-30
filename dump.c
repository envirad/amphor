/*
 * dump.c
 *
 *  Created on: 2 июн. 2025 г.
 *      Author: jar
 */
#include <stdio.h>
#include "dump.h"
#include "files.h"
#include "cli.h"
#include "string.h"

uint8_t dump_buffer[4096];

void HEX_Dump_PrintChar(char byte)
{
	if(byte < 32)
	{
		printf("%c", '.');
	}
	else
	{
		printf("%c", byte);
	}
}

void HEX_Dump(uint8_t * ptr, uint32_t len, uint32_t offset)
{
	for(int i = 0; i < len / 16; i++)
	{
		printf("%08X\t", i * 16 + offset);
		for(int j = 0; j < 16; j++)
		{
			printf("%02X ", (uint8_t) ptr[i*16 + j]);
		}
		printf("\t\t\t");
		for(int j = 0; j < 16; j++)
		{
			HEX_Dump_PrintChar((uint8_t) ptr[i*16 + j]);
		}
		printf("\n");
		if((i + 1) % 16 == 0)
			printf("\n");
	}
}

int8_t HEX_DumpFile(char * filename)
{
	int8_t file_id = FS_FindFile(filename);
	if(file_id == -1)
	{
		CLI_PrintMessage("DUMP", MSG_FATAL, ERR_REC_FILE_NOT_FOUND);
		return -1;
	}
	uint32_t blocks_used = FS_GetFileUsedBlocks(file_id);
	for(uint32_t i = 0; i < blocks_used; i++)
	{
		FS_ReadFile(file_id, i, dump_buffer);
		HEX_Dump(dump_buffer, 4096, i * 4096);
	}
}

void write_text(char * buffer)
{
	char string_buffer[256];
	uint16_t string_num = 0;
	uint32_t string_pos = 0;
	while(1)
	{
		printf("%04X\t", string_num);
		gets(string_buffer);
		printf("%s", string_buffer);
		if(strncmp(string_buffer, "ZZ", 2) == 0)
			break;
		strcpy(buffer + string_pos, string_buffer);
		string_pos += strlen(string_buffer);
	}
	string_buffer[string_pos] = 4; // EOF
	return;
}

int8_t HEX_CreateFile(char * filename)
{

	char sys_name[13];
	FS_ConvertToSystemName(filename, sys_name);
	int8_t file_id = FS_CreateFile(sys_name, 1);
	write_text(dump_buffer);
	HEX_Dump(dump_buffer, 256, 0);
	FS_WriteFile(file_id, 0, dump_buffer);
}
