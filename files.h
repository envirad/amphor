#ifndef FILES_H
#define FILES_H

#include <stdint.h>

#define MAX_FILES        64
#define FILENAME_LEN     12  // 8.3 формат
#define BLOCK_SIZE       4096

#define FILE_PREFIX "R_"
#define FILE_EXTENSION "DAT"

// Флаги файла
#define FILE_EXTENDABLE  0x01  // Файл может расширяться динамически

typedef struct {
    char name[FILENAME_LEN];
    uint32_t created_time;
    uint32_t size_blocks;      // Выделенные блоки
    uint32_t used_blocks;      // Фактически используемые блоки
    uint32_t start_block;
    uint8_t flags;             // Флаги файла
} FileEntry;

// Инициализация ФС
void FS_Init(void);
void FS_Format(void);

// Работа с файлами
int8_t FS_CreateFile(const char *name, uint32_t size_blocks);
int8_t FS_CreateExtendableFile(const char *name);
int8_t FS_DeleteFile(uint8_t file_id);
int8_t FS_RecoverFile(uint8_t file_id);
uint8_t FS_ListFiles(FileEntry *files, uint8_t max_count);
int8_t FS_FindFile(char * filename);
/**
 * @brief Создает файл с автоматически сгенерированным именем
 * @param file_type 0 = обычный файл, 1 = расширяемый
 * @return ID созданного файла или -1 при ошибке
 */
int8_t FS_CreateAutomaticFile(uint8_t file_type);

/**
 * @brief Преобразует системное имя файла в отображаемое
 * @param sys_name Системное имя (12 байт)
 * @param buffer Буфер для результата (мин. 13 байт)
 */
void FS_ConvertToDisplayName(const char* sys_name, char* buffer);

// Операции с данными
void FS_ReadFile(uint8_t file_id, uint32_t block_offset, uint8_t *buf);
int8_t FS_WriteFile(uint8_t file_id, uint32_t block_offset, uint8_t *buf);
int8_t FS_WriteFileExtendable(uint8_t file_id, uint32_t block_offset, uint8_t *buf);

// Управление размером файла
int8_t FS_ExtendFile(uint8_t file_id, uint32_t additional_blocks);
int8_t FS_FinalizeFileSize(uint8_t file_id);

// Оптимизация
void FS_Squeeze(void);

// Утилиты
uint32_t FS_GetFreeSpace(void);
uint32_t FS_GetTotalSpace(void);
uint8_t FS_IsBlockFree(uint32_t);  // Теперь публичная для расширенного доступа
int32_t FS_GetFileUsedBlocks(int8_t);

#endif // FILES_H
