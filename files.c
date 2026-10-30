#include <rtc.h>
#include "files.h"
#include "w25.h"
#include <string.h>
#include <stdio.h>

// Адреса для ротации метаданных
static const uint32_t superblock_addrs[4] = {0x000000, 0x010000, 0x020000, 0x030000};
static const uint32_t fat_addrs[4] = {0x001000, 0x011000, 0x021000, 0x031000};

static uint8_t current_superblock_idx = 0;
static uint8_t current_fat_idx = 0;

typedef struct {
    char signature[4];
    uint32_t version;
    uint32_t num_files;
    uint32_t data_start;
} SuperBlock;

static SuperBlock superblock;
static FileEntry file_table[MAX_FILES];

// Зарезервированные блоки (системные области)
static const uint32_t reserved_blocks[] = {
    0x000000 / BLOCK_SIZE, 0x010000 / BLOCK_SIZE,
    0x020000 / BLOCK_SIZE, 0x030000 / BLOCK_SIZE,
    0x001000 / BLOCK_SIZE, 0x011000 / BLOCK_SIZE,
    0x021000 / BLOCK_SIZE, 0x031000 / BLOCK_SIZE
};

static const uint8_t reserved_blocks_count = 8;

// Сохранение суперблока
static void FS_SaveSuperblock() {
    uint8_t buffer[BLOCK_SIZE];
    memcpy(buffer, &superblock, sizeof(SuperBlock));

    W25_SectorErase(superblock_addrs[current_superblock_idx]);
    W25_SectorProgram(superblock_addrs[current_superblock_idx], buffer);

    current_superblock_idx = (current_superblock_idx + 1) % 4;
}

// Сохранение FAT
static void FS_SaveFAT() {
    uint8_t buffer[BLOCK_SIZE];
    memcpy(buffer, file_table, sizeof(FileEntry) * MAX_FILES);

    W25_SectorErase(fat_addrs[current_fat_idx]);
    W25_SectorProgram(fat_addrs[current_fat_idx], buffer);

    current_fat_idx = (current_fat_idx + 1) % 4;
}

// Поиск последнего валидного суперблока
static int8_t FS_FindLatestSuperblock() {
    uint8_t buffer[sizeof(SuperBlock)];

    for(int8_t i = 3; i >= 0; i--) {
        W25_ReadData(superblock_addrs[i], buffer, sizeof(SuperBlock));
        SuperBlock *sb = (SuperBlock*)buffer;

        if(strncmp(sb->signature, "FLAT", 4) == 0) {
            current_superblock_idx = (i + 1) % 4;
            memcpy(&superblock, sb, sizeof(SuperBlock));
            return i;
        }
    }
    return -1;
}

// Поиск последней валидной FAT
static int8_t FS_FindLatestFAT() {
    uint8_t buffer[BLOCK_SIZE];

    for(int8_t i = 3; i >= 0; i--) {
        W25_ReadData(fat_addrs[i], buffer, sizeof(FileEntry) * MAX_FILES);
        FileEntry *fe = (FileEntry*)buffer;

        if(fe[0].name[0] != 0xFF) {
            current_fat_idx = (i + 1) % 4;
            memcpy(file_table, buffer, sizeof(FileEntry) * MAX_FILES);
            return i;
        }
    }
    return -1;
}

void FS_Init() {
    if(FS_FindLatestSuperblock() < 0 || FS_FindLatestFAT() < 0) {
        FS_Format();
    }
}

void FS_Format() {
	W25_ChipErase();
    memset(&superblock, 0, sizeof(SuperBlock));
    strncpy(superblock.signature, "FLAT", 4);
    superblock.version = 1;
    superblock.num_files = 0;
    superblock.data_start = 51;  // После системных областей

    memset(file_table, 0, sizeof(FileEntry) * MAX_FILES);

    FS_SaveSuperblock();
    FS_SaveFAT();
}

uint8_t FS_IsBlockFree(uint32_t block) {
    // Проверка резервированных блоков
    for(uint8_t i = 0; i < reserved_blocks_count; i++) {
        if(block == reserved_blocks[i]) return 0;
    }

    // Проверка файлов
    for(uint8_t i = 0; i < MAX_FILES; i++) {
        if(file_table[i].name[0] && file_table[i].name[0] != 0xFF) {
            uint32_t start = file_table[i].start_block;
            uint32_t end = start + file_table[i].size_blocks - 1;
            if(block >= start && block <= end) return 0;
        }
    }

    return 1;
}

static int32_t FS_AllocateBlocks(uint32_t size_blocks) {
    uint32_t start_block = superblock.data_start;
    uint32_t free_blocks = 0;

    while(1) {
        if(FS_IsBlockFree(start_block + free_blocks)) {
            free_blocks++;
            if(free_blocks == size_blocks) {
                return start_block;
            }
        } else {
            free_blocks = 0;
            start_block++;
        }
    }

    return -1;
}

int8_t FS_CreateFile(const char *name, uint32_t size_blocks) {
    // Проверка имени
    if(strlen(name) > FILENAME_LEN - 1) return -1;

    // Поиск свободного слота
    int8_t slot = -1;
    for(uint8_t i = 0; i < MAX_FILES; i++) {
        if(file_table[i].name[0] == 0 || file_table[i].name[0] == 0xFF) {
            slot = i;
            break;
        }
    }
    if(slot < 0) return -1;

    // Поиск свободных блоков
    int32_t start_block = FS_AllocateBlocks(size_blocks);
    if(start_block < 0) return -1;

    // Заполнение записи
    memset(&file_table[slot], 0, sizeof(FileEntry));
    strncpy(file_table[slot].name, name, FILENAME_LEN);
    file_table[slot].created_time = RTC_GetUnixTime();
    file_table[slot].size_blocks = size_blocks;
    file_table[slot].used_blocks = size_blocks; // Для обычных файлов используем все блоки
    file_table[slot].start_block = start_block;

    // Обновление суперблока
    superblock.num_files++;
    superblock.data_start = start_block + size_blocks;

    // Сохранение
    FS_SaveFAT();
    FS_SaveSuperblock();

    return slot;
}

int8_t FS_CreateExtendableFile(const char *name) {
    // Проверка длины имени
    if(strlen(name) > FILENAME_LEN) return -1;

    // Проверка, не существует ли уже файл с таким именем
    for(uint8_t i = 0; i < MAX_FILES; i++) {
        if(file_table[i].name[0] && file_table[i].name[0] != 0xFF) {
            if(strncmp(file_table[i].name, name, FILENAME_LEN) == 0) {
                return -2; // Файл с таким именем уже существует
            }
        }
    }

    // Создаем файл с минимальным размером (1 блок)
    int8_t file_id = FS_CreateFile(name, 1);
    if(file_id >= 0) {
        file_table[file_id].flags |= FILE_EXTENDABLE;
        file_table[file_id].used_blocks = 0; // Начинаем с 0 использованных блоков

        // Сохраняем изменения в FAT
        FS_SaveFAT();
    }
    return file_id;
}

int8_t FS_DeleteFile(uint8_t file_id) {
    if(file_id >= MAX_FILES || !file_table[file_id].name[0]) return -1;

    file_table[file_id].name[0] = 0xFF; // Мягкое удаление

    FS_SaveFAT();
    return 0;
}

int8_t FS_RecoverFile(uint8_t file_id) {
    if(file_id >= MAX_FILES || file_table[file_id].name[0] != 0xFF) return -1;

    file_table[file_id].name[0] = '?'; // Восстановление

    FS_SaveFAT();
    return 0;
}

uint8_t FS_ListFiles(FileEntry *files, uint8_t max_count) {
    uint8_t count = 0;
    for(uint8_t i = 0; i < MAX_FILES && count < max_count; i++) {
        if(file_table[i].name[0] && file_table[i].name[0] != 0xFF) {
            memcpy(&files[count], &file_table[i], sizeof(FileEntry));
            count++;
        }
    }
    return count;
}

void FS_ReadFile(uint8_t file_id, uint32_t block_offset, uint8_t *buf) {
    if(file_id >= MAX_FILES || !file_table[file_id].name[0]) return;

    uint32_t addr = (file_table[file_id].start_block + block_offset) * BLOCK_SIZE;
    W25_ReadData(addr, buf, BLOCK_SIZE);
}

int8_t FS_WriteFile(uint8_t file_id, uint32_t block_offset, uint8_t *buf) {
    if(file_id >= MAX_FILES || !file_table[file_id].name[0]) return -1;

    // Проверка выхода за пределы файла
    if(block_offset >= file_table[file_id].size_blocks) {
        return -2; // Выход за пределы
    }

    uint32_t block = file_table[file_id].start_block + block_offset;

    // Защита метаданных
    for(uint8_t i = 0; i < reserved_blocks_count; i++) {
        if(block == reserved_blocks[i]) return -3;
    }

    uint32_t addr = block * BLOCK_SIZE;
    W25_SectorProgram(addr, buf);

    // Обновляем счетчик использованных блоков
    if(block_offset >= file_table[file_id].used_blocks) {
        file_table[file_id].used_blocks = block_offset + 1;
    }

    return 0;
}

int8_t FS_WriteFileExtendable(uint8_t file_id, uint32_t block_offset, uint8_t *buf) {
    if(file_id >= MAX_FILES || !file_table[file_id].name[0])
        return -1; // Неверный ID

    // Проверяем, является ли файл расширяемым
    if(!(file_table[file_id].flags & FILE_EXTENDABLE)) {
        // Если не расширяемый - используем обычную запись
        return FS_WriteFile(file_id, block_offset, buf);
    }

    // Проверяем, нужно ли расширение
    if(block_offset >= file_table[file_id].size_blocks) {
        // Рассчитываем необходимое расширение
        uint32_t needed_blocks = block_offset - file_table[file_id].size_blocks + 1;

        // Находим свободные блоки после текущего конца файла
        uint32_t next_block = file_table[file_id].start_block + file_table[file_id].size_blocks;
        //uint32_t free_blocks = 0;

        // Проверяем доступность блоков
        for(uint32_t i = 0; i < needed_blocks; i++) {
            if(!FS_IsBlockFree(next_block + i)) {
                // Если блоки не свободны - делаем дефрагментацию
                FS_Squeeze();
                break;
            }
        }

        // Повторная проверка после дефрагментации
        for(uint32_t i = 0; i < needed_blocks; i++) {
            if(!FS_IsBlockFree(next_block + i)) {
                return -4; // Недостаточно свободного места
            }
        }

        // Расширяем файл
        file_table[file_id].size_blocks += needed_blocks;
        superblock.data_start = next_block + needed_blocks;

        // Обновляем метаданные
        FS_SaveFAT();
        FS_SaveSuperblock();
    }

    // Выполняем запись
    uint32_t block = file_table[file_id].start_block + block_offset;

    // Защита метаданных
    for(uint8_t i = 0; i < reserved_blocks_count; i++) {
        if(block == reserved_blocks[i])
            return -5; // Попытка записи в системную область
    }

    uint32_t addr = block * BLOCK_SIZE;
    W25_SectorProgram(addr, buf);

    // Обновляем счетчик использованных блоков
    if(block_offset >= file_table[file_id].used_blocks) {
        file_table[file_id].used_blocks = block_offset + 1;
        FS_SaveFAT(); // Сохраняем изменение метаданных
    }

    return 0; // Успех
}

int8_t FS_ExtendFile(uint8_t file_id, uint32_t additional_blocks) {
    if(file_id >= MAX_FILES || !file_table[file_id].name[0])
        return -1; // Неверный ID

    // Находим свободные блоки после текущего конца файла
    uint32_t next_block = file_table[file_id].start_block + file_table[file_id].size_blocks;
    //uint32_t free_blocks = 0;

    // Проверяем доступность блоков
    for(uint32_t i = 0; i < additional_blocks; i++) {
        if(!FS_IsBlockFree(next_block + i)) {
            return -2; // Недостаточно свободного места
        }
    }

    // Расширяем файл
    file_table[file_id].size_blocks += additional_blocks;
    superblock.data_start = next_block + additional_blocks;

    // Обновляем метаданные
    FS_SaveFAT();
    FS_SaveSuperblock();

    return 0;
}

int8_t FS_FinalizeFileSize(uint8_t file_id) {
    if(file_id >= MAX_FILES || !file_table[file_id].name[0])
        return -1; // Неверный ID

    // Фиксируем фактический размер
    if(file_table[file_id].flags & FILE_EXTENDABLE) {
        file_table[file_id].size_blocks = file_table[file_id].used_blocks;
        FS_SaveFAT();
    }
    return 0;
}

void FS_Squeeze() {
    uint32_t new_start = 2; // После системных областей
    uint8_t buffer[BLOCK_SIZE];
    FileEntry temp_table[MAX_FILES];

    // Временная копия FAT
    memcpy(temp_table, file_table, sizeof(FileEntry) * MAX_FILES);

    // Перемещение файлов
    for(uint8_t i = 0; i < MAX_FILES; i++) {
        if(temp_table[i].name[0] && temp_table[i].name[0] != 0xFF) {
            uint32_t old_start = temp_table[i].start_block;
            temp_table[i].start_block = new_start;

            // Копирование данных
            for(uint32_t j = 0; j < temp_table[i].size_blocks; j++) {
                uint32_t src_addr = (old_start + j) * BLOCK_SIZE;
                uint32_t dst_addr = (new_start + j) * BLOCK_SIZE;

                W25_ReadData(src_addr, buffer, BLOCK_SIZE);
                W25_SectorErase(dst_addr);
                W25_SectorProgram(dst_addr, buffer);
            }

            new_start += temp_table[i].size_blocks;

            // Пропуск резервированных блоков
            while(1) {
                uint8_t reserved = 0;
                for(uint8_t k = 0; k < reserved_blocks_count; k++) {
                    if(new_start == reserved_blocks[k]) {
                        reserved = 1;
                        break;
                    }
                }
                if(!reserved) break;
                new_start++;
            }
        }
    }

    // Обновление структур
    superblock.data_start = new_start;
    memcpy(file_table, temp_table, sizeof(FileEntry) * MAX_FILES);

    // Очистка удаленных файлов
    for(uint8_t i = 0; i < MAX_FILES; i++) {
        if(file_table[i].name[0] == 0xFF) {
            memset(&file_table[i], 0, sizeof(FileEntry));
        }
    }

    // Сохранение
    FS_SaveFAT();
    FS_SaveSuperblock();
}

uint32_t FS_GetFreeSpace() {
    uint32_t total_blocks = 16 * 1024 * 1024 / BLOCK_SIZE; // 16MB flash
    uint32_t used_blocks = 0;

    for(uint8_t i = 0; i < MAX_FILES; i++) {
        if(file_table[i].name[0] && file_table[i].name[0] != 0xFF) {
            used_blocks += file_table[i].size_blocks;
        }
    }

    return (total_blocks - used_blocks - reserved_blocks_count) * BLOCK_SIZE;
}

uint32_t FS_GetTotalSpace() {
    return 16 * 1024 * 1024; // 16MB
}

// Внутренняя функция для создания файла с заданным номером
static int8_t create_file_with_number(uint16_t number, uint8_t file_type) {
    char sys_name[12];

    // Формируем имя в формате 8.3
    memset(sys_name, ' ', 11);  // Заполняем пробелами
    sys_name[11] = '\0';

    // Префикс (2 символа)
    sys_name[0] = 'R';
    sys_name[1] = '_';

    // Номер (3 цифры)
    sys_name[2] = '0' + (number / 100) % 10;
    sys_name[3] = '0' + (number / 10) % 10;
    sys_name[4] = '0' + number % 10;

    // Расширение (3 символа)
    sys_name[8] = 'D';
    sys_name[9] = 'A';
    sys_name[10] = 'T';

    // Проверяем, не существует ли уже файл с таким именем
    for(uint8_t i = 0; i < MAX_FILES; i++) {
        if(file_table[i].name[0] && file_table[i].name[0] != 0xFF) {
            if(memcmp(file_table[i].name, sys_name, 11) == 0) {
                return -1; // Файл уже существует
            }
        }
    }

    // Создаем файл
    if(file_type == 0) {
        return FS_CreateFile(sys_name, 1);
    } else {
        return FS_CreateExtendableFile(sys_name);
    }
}

int8_t FS_CreateAutomaticFile(uint8_t file_type) {
    // Перебираем возможные номера от 1 до 999
    for(uint16_t num = 1; num <= 999; num++) {
        int8_t file_id = create_file_with_number(num, file_type);
        if(file_id >= 0) {
            return file_id;
        }
    }
    return -1; // Нет свободных номеров
}

void FS_ConvertToDisplayName(const char* sys_name, char* buffer) {
    // Копируем имя (первые 8 символов без пробелов в конце)
    char name_part[9];
    strncpy(name_part, sys_name, 8);
    name_part[8] = '\0';

    // Удаляем пробелы в конце имени
    for(int i = 7; i >= 0; i--) {
        if(name_part[i] == ' ') name_part[i] = '\0';
        else break;
    }

    // Копируем расширение
    char ext_part[4];
    strncpy(ext_part, sys_name + 8, 3);
    ext_part[3] = '\0';

    // Формируем итоговое имя
    snprintf(buffer, 13, "%s.%s", name_part, ext_part);
}

void FS_ConvertToSystemName(const char * filename, char * sys_name)
{
	memset(sys_name, ' ', 12);
	uint8_t idx = 0;
	while((filename[++ idx] != '.') && (filename[idx] != '\0') && idx < 12); // idx -- положение точки
	strncpy(sys_name, filename, idx);
	strncpy(sys_name + 8, filename + idx+1, 3);
	sys_name[11] = 0;
}

int8_t FS_FindFile(char * filename)
{
	char sys_name[13];
	FS_ConvertToSystemName(filename, sys_name);
	for(int i = 0; i < MAX_FILES; i++)
	{
		if(strcmp(sys_name, file_table[i].name) == 0)
		{
			printf("%s\n", file_table[i].name);
			return(i);
		}
	}
	return -1; // файл с таким именем не найден
}

int32_t FS_GetFileUsedBlocks(int8_t file_id)
{
	return file_table[file_id].used_blocks;
}
