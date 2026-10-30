/*
 * record.c
 *
 *  Created on: Jun 1, 2025
 *      Author: jar
 */


#include <stdio.h>
#include "record.h"
#include "measure.h"
#include <math.h>
#include <string.h>
#include "cli.h"
#include "uart.h"
#include "dump.h"
#include "disk.h"
#include "files.h"

uint8_t RecordBuffer[REC_BUF_BLOCK_SIZE * REC_BUF_BLOCKS]; // Буфер для сохранения собираемой информации
uint32_t RecordBuffer_endptr = 0;
bool isBreak = false;

static const float pow10[] = {1e-2f, 1e-1f, 1e0f, 1e1f, 1e2f, 1e3f, 1e4f, 1e5f};

myFloat16 CompressFloat(float value) {
    myFloat16 result = {0, 0};

    // Обработка нуля и значений вне диапазона
    if (value < 0.01f) return result;
    if (value > 819100000.0f) { // 8191 * 10^5
        result.mantissa = 8191;
        result.order = 7; // порядок 5 (5+2=7)
        return result;
    }

    // Основной алгоритм сжатия
    for (uint8_t idx = 0; idx < 8; idx++) {
        float scaled = value / pow10[idx];
        if (scaled < 8191.5f) { // +0.5 для корректного округления
            uint16_t mantissa = (uint16_t)(scaled + 0.5f); // Округление

            // Коррекция граничных значений
            if (mantissa < 1) mantissa = 1;
            if (mantissa > 8191) mantissa = 8191;

            result.mantissa = mantissa;
            result.order = idx;
            return result;
        }
    }

    // Запасной вариант, если не шмагло
    result.mantissa = 8191;
    result.order = 7;
    return result;
}

float DecompressFloat(myFloat16 f) {
    if (f.mantissa == 0) return 0.0f;
    return (float)f.mantissa * pow10[f.order];
}

RecordFrame Record_MakeFrame(MeasResult measure)
{
	RecordFrame tmp;
	tmp.time = (uint32_t)measure.time;
	tmp.depth = measure.depth;
	tmp.temp = measure.temp;
	tmp.clear = CompressFloat(measure.clear);
	tmp.red = CompressFloat(measure.red);
	tmp.green = CompressFloat(measure.green);
	tmp.blue = CompressFloat(measure.blue);
	return tmp;
}

MeasResult Record_UnpackFrame(RecordFrame frame)
{
	MeasResult tmp;
	tmp.time = (time_t) frame.time;
	tmp.depth = frame.depth;
	tmp.temp = frame.temp;
	tmp.clear = DecompressFloat(frame.clear);
	tmp.red = DecompressFloat(frame.red);
	tmp.green = DecompressFloat(frame.green);
	tmp.blue = DecompressFloat(frame.blue);
	return tmp;
}

int32_t Record_AddFrame(RecordFrame frame)
{
	if(RecordBuffer_endptr + sizeof(frame) > REC_BUF_BLOCK_SIZE * REC_BUF_BLOCKS)
	{
		//CLI_PrintMessage("REC", MSG_INFO, ERR_REC_BUFFER_FULL);
		return(-1);
	}

	memcpy(RecordBuffer + RecordBuffer_endptr, &frame, sizeof(frame));
	RecordBuffer_endptr += sizeof(frame);
	return RecordBuffer_endptr;
}
void rub_frame(RecordFrame * frame)
// Функция для заполнения кадра значением 0xFF для остановки вывода
{
	memset(&frame, 0xff, sizeof(frame));
}

RecordFrame Record_ReadFrame(uint32_t number)
{
	union
	{
		RecordFrame frame;
		uint8_t array[sizeof(RecordFrame)];
	} frame_bytes;
	uint32_t offset = number * sizeof(RecordFrame);
	memcpy(frame_bytes.array, RecordBuffer + offset, sizeof(RecordFrame));
	return frame_bytes.frame;
}

int8_t flush_buffer_to_file(int8_t file_id, uint32_t block_offset)
// Запись в файл данных, уже находящихся в памяти со сдвигом относительно его начала
{
	// Сначала забьем оставшиеся незаполненными кадры
	uint32_t last_endptr = RecordBuffer_endptr;
	RecordFrame frame;
	int8_t result;
	rub_frame(&frame);
	while(Record_AddFrame(frame) != -1);
	// Теперь последовательно, по одному сектору, пишем.
	uint32_t sector_count = (last_endptr / REC_BUF_BLOCK_SIZE); 	// Количество полных секторов
	if(last_endptr % REC_BUF_BLOCK_SIZE > 0)
		sector_count ++; 								// добавляем неполный сектор
	for(int i = 0; i < sector_count; i++)
	{
		result = FS_WriteFileExtendable(file_id, block_offset + i, RecordBuffer + i);
		if(result == -4 || result == -5) // Ошибки файловой системы
		{
			CLI_PrintMessage("REC", MSG_FATAL, ERR_REC_NO_ROOM);
			return -1;
		}
		else if(result == -1)
		{
			CLI_PrintMessage("REC", MSG_FATAL, ERR_REC_TO_MANY_FILES);
			return -1;
		}
	}
	RecordBuffer_endptr = last_endptr;
	return 0;
}

void grab_data_to_buffer(uint8_t set)
{
	RecordBuffer_endptr = 0;
	isBreak = false;
	RecordFrame frame;
	int32_t endptr = 0;
	uint32_t count = 0;

	USART1_FlushInputBuffer();
	while(endptr != -1) // Пока не закончится буфер
	{
		USART1_FlushInputBuffer();
		MeasResult tmp = measure((float)sys_params.exposure / 1000.f, set);
		frame = Record_MakeFrame(tmp);
		endptr = Record_AddFrame(frame);
		// Формирование вывода -- точка на каждом измерении, номер и новая строка -- на каждом 16-м
		printf(".");
		count++;
		if(!(count % 16))
			printf("%04X\n", RecordBuffer_endptr);
		// Проверка -- не было ли прерывания от пользователя или по отключению
		if(USART1_IsBreakReceived())
		{
			CLI_PrintMessage("REC", MSG_UNEXPECTED, ERR_REC_BREAK);
			isBreak = true;
			return;
		}
		if(Reed_IsPowerFault())
		{
			CLI_PrintMessage("REC", MSG_UNEXPECTED, ERR_REC_POWER_FAULT);
			isBreak = true;
			return;
		}
	}
}

void Record_NewRecord(uint8_t set)
{
	int8_t file_id = Disk_NewFile();
	uint32_t block_offset = 0;
	isBreak = false;
	if(file_id < 0)
		return CLI_PrintMessage("REC", MSG_FATAL, ERR_REC_TO_MANY_FILES);
	while(!isBreak)
	{
		grab_data_to_buffer(set);
		if (flush_buffer_to_file(file_id, block_offset) == -1)
			return;
		block_offset += REC_BUF_BLOCKS;
	}
	if(Reed_IsPowerFault())
		Reed_PowerOff();

}

/*void Record_NewRecord(uint8_t set)
{
	RecordBuffer_endptr = 0;
	RecordFrame frame;
	uint32_t block_offset = 0;
	int32_t result = 0;
	uint32_t count = 0;
	bool isBreak = false;
	USART1_FlushInputBuffer();
	int8_t id = Disk_NewFile();
	if(id < 0)
	{
		CLI_PrintMessage("REC", MSG_FATAL, ERR_REC_TO_MANY_FILES);
		return;
	}
restart:
	while(result != -1)
	{
		USART1_FlushInputBuffer();
		MeasResult tmp = measure((float)sys_params.exposure / 1000.f, set);
		frame = Record_MakeFrame(tmp);
		result = Record_AddFrame(frame);
		printf(".");
		count++;
		if(!(count % 16))
			printf("%04X\n", RecordBuffer_endptr);
		if(USART1_IsBreakReceived())
		{
			CLI_PrintMessage("REC", MSG_UNEXPECTED, ERR_REC_BREAK);
			isBreak = true;
			break;
		}
	}
	uint8_t blocks_unsaved = 1 + (unsigned int)  RecordBuffer_endptr / REC_BUF_BLOCK_SIZE ;
	if(blocks_unsaved > REC_BUF_BLOCKS)
		blocks_unsaved = REC_BUF_BLOCKS;
	result = FS_WriteFileExtendable(id, block_offset, RecordBuffer);
	if(result == -4 || result == -5)
	{
		CLI_PrintMessage("REC", MSG_FATAL, ERR_REC_NO_ROOM);
		return;
	}
	else if(result == -1)
	{
		CLI_PrintMessage("REC", MSG_FATAL, ERR_REC_TO_MANY_FILES);
		return;
	}


	block_offset += blocks_unsaved;
	blocks_unsaved = 0;
	if(!isBreak)
	{
		rub_frame(&frame);
		Record_AddFrame(frame);
		RecordBuffer_endptr = 0;
		printf("New chunk\n");
		goto restart;
	}
	printf("%d frames recorded", (int) count);
	//CLI_PrintMessage("REC", MSG_INFO, ERR_REC_BUFFER_FULL);
}*/

uint32_t find_last_frame(void)
{
	RecordFrame frame = Record_ReadFrame(0);
	uint32_t idx = 0;
	while((uint32_t) frame.time != 0xFFFFFFFF && idx < REC_BUF_BLOCKS * REC_BUF_BLOCK_SIZE / sizeof(frame))
	{
		frame = Record_ReadFrame(idx++);
	}
	printf("LastFrame %d\n", idx);
	return idx;
}

/*int8_t list_record_from_file(char * filename)
{
	uint32_t frame_number = 0;
	RecordFrame tmp;
	MeasResult tmp2;
	int8_t file_id = FS_FindFile(filename);
	printf("ID = %d\n", file_id);
	uint32_t block_offset = 0;
	if(file_id < 0)
	{
		return -1;
	}
	Measure_PrintHeader(ALL);
	uint32_t blocks_used = FS_GetFileUsedBlocks(file_id);
	printf("Blocks used, %d\n", blocks_used);
	while(blocks_used-- > 0)
	{
		printf("Blocks used, %d\n", blocks_used);
		FS_ReadFile(file_id, block_offset ++, RecordBuffer);
		RecordBuffer_endptr = find_last_frame() * sizeof(RecordFrame);
		while(frame_number < RecordBuffer_endptr / sizeof(RecordFrame))
		{
			tmp = Record_ReadFrame(frame_number++);
			tmp2 = Record_UnpackFrame(tmp);
			Measure_PrintValueStr(tmp2, ALL);
		}

	}


	return 0;
}*/
void list_record_from_buffer(uint8_t set)
{

	uint32_t frame_number = 0;
	RecordFrame tmp;
	MeasResult tmp2;
	while(frame_number < RecordBuffer_endptr / sizeof(RecordFrame) && frame_number < REC_BUF_BLOCKS * 256)
	{
		tmp = Record_ReadFrame(frame_number++);
		tmp2 = Record_UnpackFrame(tmp);
		Measure_PrintValueStr(tmp2, ALL);
	}
}

int8_t list_record_from_file(char * filename, uint8_t set)
{
	uint32_t current_block = 0;
	memset(RecordBuffer, 0xFF, REC_BUF_BLOCK_SIZE * REC_BUF_BLOCKS);
	int8_t file_id = FS_FindFile(filename);
	if(file_id < 0)
		return -1;
	uint32_t blocks_used = FS_GetFileUsedBlocks(file_id);
	for(current_block = 0; current_block < blocks_used; current_block++)
	{
		FS_ReadFile(file_id, current_block, RecordBuffer);
		RecordBuffer_endptr = find_last_frame() * sizeof(RecordFrame);
		list_record_from_buffer(set);
	}

}

void Record_ListRecord(char * filename)
{
	if(strcasecmp (filename, "MEM") != 0)
	{
		int8_t id = list_record_from_file(filename, ALL);
		if(id < 0)
		{
			CLI_PrintMessage("REC", MSG_FATAL, ERR_REC_FILE_NOT_FOUND);
			return;
		}
		return;
	}
	Measure_PrintHeader(ALL);
	list_record_from_buffer(ALL);

}

void Record_TypeRecord(char * filename)
{
	if(strcasecmp (filename, "MEM") == 0)
	{
		HEX_Dump(RecordBuffer, 4096, 0);
		return;
	}
	else
	{
		HEX_DumpFile(filename);
	}
}
