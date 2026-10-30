/*
 * disk.c
 *
 *  Created on: 2 июн. 2025 г.
 *      Author: jar
 */

#include "disk.h"


int8_t Disk_NewFile(void)
{
	return FS_CreateAutomaticFile(1);
}

void Disk_PrintFileList(void) {
    FileEntry files[MAX_FILES];
    uint8_t count = FS_ListFiles(files, MAX_FILES);

    // Заголовок таблицы
    printf("ID  Filename     Size (KB)  Created\n");
    printf("----------------------------------\n");

    for(uint8_t i = 0; i < count; i++) {
        // Преобразуем системное имя в отображаемое
        char display_name[13];
        FS_ConvertToDisplayName(files[i].name, display_name);

        // Рассчитываем размер в КБ
        uint32_t size_kb = (files[i].size_blocks * BLOCK_SIZE) / 1024;

        // Формируем строку с информацией о файле
        char file_info[64];
        char datime[20];
        time_t temp_time = (time_t)files[i].created_time;
        struct tm *tm_time = localtime(&temp_time);
        snprintf(datime, sizeof(datime), "%04d-%02d-%02d %02d:%02d:%02d", tm_time->tm_year + 1900, tm_time->tm_mon + 1, tm_time->tm_mday, tm_time->tm_hour, tm_time->tm_min, tm_time->tm_sec);
        snprintf(file_info, sizeof(file_info), "%-3d %-12s %-9lu %s\n",
                 i, display_name, size_kb, datime);

        puts(file_info);
    }

    // Итоговая информация
    char summary[50];
    snprintf(summary, sizeof(summary), "\r\nTotal files: %d, Free space: %lu KB\n",
             count, FS_GetFreeSpace() / 1024);
    puts(summary);
}


