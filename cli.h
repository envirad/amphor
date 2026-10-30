#ifndef CLI_H
#define CLI_H

#include <stdint.h>
#include <stdbool.h>


// Уровни сообщений
typedef enum {
    MSG_INFO,       // Информационное сообщение
    MSG_WARNING,    // Предупреждение
    MSG_ERROR,      // Ошибка
    MSG_FATAL,      // Фатальная ошибка
    MSG_UNEXPECTED  // Непредвиденная ошибка
} MessageLevel;

// Коды ошибок
typedef enum {
    // Общие ошибки
    ERR_CMD_NOT_FOUND,
    ERR_AMBIGUOUS_CMD,
    ERR_INVALID_PARAM,
    ERR_MISSING_ARG,
    ERR_FILE_REQUIRED,
    ERR_INVALID_MODE,
    ERR_TIME_FORMAT,
    ERR_OUT_OF_RANGE,

    // Ошибки SET
    ERR_SET_MISSING_VALUE,
    ERR_SET_INVALID_MODE,
    ERR_SET_UNKNOWN_PARAM,
	ERR_SET_INVALID_DATE,
	ERR_SET_INVALID_TIME,
	ERR_SET_INVALID_VALUE,

    // Ошибки SHOW
    ERR_SHOW_UNKNOWN_PARAM,
	// Ошибки REC
	ERR_REC_BUFFER_FULL,
	ERR_REC_BREAK,
	ERR_REC_FILE_NOT_FOUND,
	ERR_REC_TO_MANY_FILES,
	ERR_REC_NO_ROOM,
	ERR_REC_POWER_FAULT,
	// Ошибки MEASURE
	ERR_MEAS_INVALID_CHAN,

    // Максимальное количество ошибок (для внутреннего использования)
    MAX_ERROR_CODE
} ErrorCode;

// Структура для системных параметров
typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    bool quiet_mode;
    uint32_t exposure; 	// в миллисекундах
    uint32_t gain; 		// 0 -- выкл, 1 -- 2%, 2 -- 20%, 3 -- 100%.
    uint32_t period;   	// в миллисекундах
    bool echo;
} SystemParams;

// Функции обработки команд
void measure_function(uint8_t mask);
void record_function(uint8_t mask);
void type_function(const char *filename, bool hex_mode);
void list_function(const char *filename);
void create_function(const char *filename, uint32_t blocks, bool input);
void delete_function(const char *filename);
void init_function(void);
void directory_function(bool full);
void squeeze_function(void);
void execute_script(const char *filename);
void save_settings(const char *filename);
void load_settings(const char *filename);

// Основные функции CLI
void CLI_Init(void);
void CLI_ProcessCommand(char *cmd);
uint8_t parse_measure_list(const char *list);
bool parse_time(const char *str, uint32_t *ms);

// Функции обработки ошибок
int8_t CLI_PrintMessage(const char *process, MessageLevel level, ErrorCode code);
const char *get_error_message(ErrorCode code);

// Глобальная структура параметров
extern SystemParams sys_params;

#endif // CLI_H
