#include "cli.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdarg.h>
#include <stddef.h>
#include "tmp100.h"
#include "measure.h"
#include "record.h"
#include "files.h"
#include "disk.h"
#include "dump.h"
#include "rtc.h"
#include "pd.h"


// Глобальная структура системных параметров
SystemParams sys_params = {0};

// Вспомогательные функции ----------------------------------------------------

// Приведение строки к нижнему регистру
static void strtolower(char *str) {
    if (!str) return;
    for (; *str; str++) *str = tolower(*str);
}

// Удаление пробелов и управляющих символов в конце строки
static void rtrim_control(char *str) {
    if (!str) return;
    size_t len = strlen(str);
    while (len > 0 && (isspace((unsigned char)str[len-1]) ||
                      str[len-1] == '\r' ||
                      str[len-1] == '\n')) {
        str[--len] = '\0';
    }
}

// Сообщения об ошибках
static const char *error_messages[MAX_ERROR_CODE] = {
    // Общие ошибки
    [ERR_CMD_NOT_FOUND]    = "Invalid command",
    [ERR_AMBIGUOUS_CMD]    = "Ambiguous command",
    [ERR_INVALID_PARAM]    = "Invalid parameter",
    [ERR_MISSING_ARG]      = "Missing argument",
    [ERR_FILE_REQUIRED]    = "Filename required",
    [ERR_INVALID_MODE]     = "Invalid mode",
    [ERR_TIME_FORMAT]      = "Invalid time format",
    [ERR_OUT_OF_RANGE]     = "Value out of range",

    // Ошибки SET
    [ERR_SET_MISSING_VALUE] = "Missing value for parameter",
    [ERR_SET_INVALID_MODE]  = "Invalid mode for parameter",
    [ERR_SET_UNKNOWN_PARAM] = "Unknown parameter",
	[ERR_SET_INVALID_DATE] = "Invalid date",
	[ERR_SET_INVALID_TIME] = "Invalid time",
	[ERR_SET_INVALID_VALUE] = "Invalid value",

    // Ошибки SHOW
    [ERR_SHOW_UNKNOWN_PARAM] = "Unknown parameter",
	// Ошибки REC
	[ERR_REC_BUFFER_FULL] = "Buffer is full",
	[ERR_REC_BREAK] = "User break",
	[ERR_REC_FILE_NOT_FOUND] = "File not found",
	[ERR_REC_TO_MANY_FILES] = "To many files",
	[ERR_REC_NO_ROOM] = "No room on drive",
	[ERR_REC_POWER_FAULT] = "Power fault",
	// Ошибки MEASURE
	[ERR_MEAS_INVALID_CHAN] = "Invalid channel",
};

// Получение сообщения об ошибке по коду
const char *get_error_message(ErrorCode code) {
    if (code < 0 || code >= MAX_ERROR_CODE)
        return "Unknown error";
    return error_messages[code];
}

// Вывод сообщения в формате RT11
int8_t CLI_PrintMessage(const char *process, MessageLevel level, ErrorCode code) {
    const char *level_str = "";
    switch (level) {
        case MSG_INFO:       level_str = "I"; break;
        case MSG_WARNING:    level_str = "W"; break;
        case MSG_ERROR:      level_str = "E"; break;
        case MSG_FATAL:      level_str = "F"; break;
        case MSG_UNEXPECTED: level_str = "U"; break;
        default:             level_str = "?";
    }

    const char *msg = get_error_message(code);
    printf("?%s-%s-%s\n", process, level_str, msg);
    return -1;
}

// Структура для хранения команд
typedef struct {
    const char *name;
    void (*handler)(char *args);
} Command;

// Прототипы обработчиков команд
static void handle_measure(char *args);
static void handle_record(char *args);
static void handle_type(char *args);
static void handle_list(char *args);
static void handle_create(char *args);
static void handle_delete(char *args);
static void handle_init(char *args);
static void handle_directory(char *args);
static void handle_squeeze(char *args);
static void handle_set(char *args);
static void handle_show(char *args);
static void handle_save(char *args);
static void handle_load(char *args);

// Таблица команд
static const Command commands[] = {
    {"measure", handle_measure},
    {"record", handle_record},
    {"type", handle_type},
    {"list", handle_list},
    {"create", handle_create},
    {"delete", handle_delete},
    {"init", handle_init},
    {"directory", handle_directory},
    {"squeeze", handle_squeeze},
    {"set", handle_set},
    {"show", handle_show},
    {"save", handle_save},
    {"load", handle_load}
};

// Количество команд
static const int command_count = sizeof(commands) / sizeof(Command);

// Функция поиска по префиксу (универсальная)
static const void *find_by_prefix(const void *items, size_t item_size, size_t count,
                                 const char *input, const char *(*get_name)(const void *)) {
    const char *matches[count];
    int match_count = 0;
    size_t input_len = strlen(input);

    // Первый проход: поиск элементов, начинающихся с первой буквы
    for (size_t i = 0; i < count; i++) {
        const char *name = get_name((const char *)items + i * item_size);
        if (tolower(input[0]) == tolower(name[0])) {
            matches[match_count++] = name;
        }
    }

    // Если нет совпадений
    if (match_count == 0) return NULL;

    // Последующие символы сужают выбор
    for (size_t pos = 1; pos < input_len; pos++) {
        int new_count = 0;

        for (int i = 0; i < match_count; i++) {
            // Проверяем, что элемент имеет достаточно символов
            if (pos < strlen(matches[i])) {
                if (tolower(input[pos]) == tolower(matches[i][pos])) {
                    matches[new_count++] = matches[i];
                }
            }
        }

        // Если не осталось совпадений
        if (new_count == 0) break;
        match_count = new_count;
    }

    // Проверяем, что введенная строка является префиксом элемента
    const char *final_match = NULL;
    int final_count = 0;

    for (int i = 0; i < match_count; i++) {
        // Проверяем, что элемент начинается с введенной строки
        if (strncasecmp(input, matches[i], input_len) == 0) {
            final_count++;
            final_match = matches[i];
        }
    }

    if (final_count == 1) {
        // Находим оригинальный элемент по имени
        for (size_t i = 0; i < count; i++) {
            const char *name = get_name((const char *)items + i * item_size);
            if (strcmp(final_match, name) == 0) {
                return (const char *)items + i * item_size;
            }
        }
    }

    // Проверка на точное совпадение
    for (size_t i = 0; i < count; i++) {
        const char *name = get_name((const char *)items + i * item_size);
        if (strcasecmp(input, name) == 0) {
            return (const char *)items + i * item_size;
        }
    }

    return NULL; // Неоднозначный или не найден
}

// Вспомогательные функции для поиска
static const char *get_command_name(const void *item) {
    return ((const Command *)item)->name;
}

// Поиск команды по префиксу
static const Command *find_command(const char *input) {
    return find_by_prefix(commands, sizeof(Command), command_count,
                         input, get_command_name);
}

// Структура для параметров SET
typedef struct {
    const char *name;
    void (*handler)(char *value);
} SetParam;

// Обработчики параметров SET
static void handle_set_date(char *value);
static void handle_set_time(char *value);
static void handle_set_tt(char *value);
static void handle_set_exposure(char *value);
static void handle_set_gain(char *value);
static void handle_set_period(char *value);
static void handle_set_echo(char *value);

// Таблица параметров SET
static const SetParam set_params[] = {
    {"date", handle_set_date},
    {"time", handle_set_time},
    {"tt", handle_set_tt},
    {"exposure", handle_set_exposure},
	{"gain", handle_set_gain},
    {"period", handle_set_period},
    {"echo", handle_set_echo}
};

// Количество параметров SET
static const int set_param_count = sizeof(set_params) / sizeof(SetParam);

// Вспомогательная функция для поиска параметров SET
static const char *get_set_param_name(const void *item) {
    return ((const SetParam *)item)->name;
}

// Поиск параметра SET по префиксу
static const SetParam *find_set_param(const char *input) {
    return find_by_prefix(set_params, sizeof(SetParam), set_param_count,
                         input, get_set_param_name);
}

// Структура для параметров SHOW
typedef struct {
    const char *name;
    void (*handler)(void);
} ShowParam;

// Обработчики параметров SHOW
static void handle_show_date(void);
static void handle_show_time(void);
static void handle_show_tt(void);
static void handle_show_exposure(void);
static void handle_show_gain(void);
static void handle_show_period(void);
static void handle_show_echo(void);
static void handle_show_depth(void);
static void handle_show_temp(void);
static void handle_show_battery(void);
static void handle_show_all(void);

// Таблица параметров SHOW
static const ShowParam show_params[] = {
    {"date", handle_show_date},
    {"time", handle_show_time},
    {"tt", handle_show_tt},
    {"exposure", handle_show_exposure},
	{"gain", handle_show_gain},
    {"period", handle_show_period},
    {"echo", handle_show_echo},
    {"depth", handle_show_depth},
    {"temp", handle_show_temp},
    {"battery", handle_show_battery},
    {"all", handle_show_all}
};

// Количество параметров SHOW
static const int show_param_count = sizeof(show_params) / sizeof(ShowParam);

// Вспомогательная функция для поиска параметров SHOW
static const char *get_show_param_name(const void *item) {
    return ((const ShowParam *)item)->name;
}

// Поиск параметра SHOW по префиксу
static const ShowParam *find_show_param(const char *input) {
    return find_by_prefix(show_params, sizeof(ShowParam), show_param_count,
                         input, get_show_param_name);
}

// Парсинг списка измерений
uint8_t parse_measure_list(const char *list) {
    uint8_t mask = 0;
    char buffer[128];
    char *token;

    if (!list || !*list) return 0;

    // Копируем для безопасной работы
    strncpy(buffer, list, sizeof(buffer)-1);
    buffer[sizeof(buffer)-1] = '\0';

    token = strtok(buffer, ",");
    while (token) {
        // Удаляем пробелы по краям
        while (*token == ' ') token++;
        size_t len = strlen(token);
        while (len > 0 && token[len-1] == ' ') token[--len] = '\0';

        strtolower(token);

        if (strcmp(token, "time") == 0) mask |= TIME_BIT;
        else if (strcmp(token, "depth") == 0) mask |= DEPTH_BIT;
        else if (strcmp(token, "temp") == 0) mask |= TEMP_BIT;
        else if (strcmp(token, "clear") == 0) mask |= CLR_BIT;
        else if (strcmp(token, "red") == 0) mask |= RED_BIT;
        else if (strcmp(token, "green") == 0) mask |= GREEN_BIT;
        else if (strcmp(token, "blue") == 0) mask |= BLUE_BIT;
        else if (strcmp(token, "all") == 0) mask = ALL_MEASUREMENTS;
        else
        {
        	CLI_PrintMessage("MEASURE", MSG_ERROR, ERR_MEAS_INVALID_CHAN);
        }

        token = strtok(NULL, ",");
    }
    return mask;
}

// Парсинг временных значений
bool parse_time(const char *str, uint32_t *ms) {
    if (!str || !*str) return false;

    char *endptr;
    float val = strtof(str, &endptr);

    if (endptr == str) return false; // Ошибка преобразования

    if (strcasecmp(endptr, "ms") == 0) {
        *ms = (uint32_t)val;
    } else if (*endptr == 's' || *endptr == '\0') {
        *ms = (uint32_t)(val * 1000);
    } else {
        return false;
    }
    return true;
}

// Обработчики команд ---------------------------------------------------------

static void handle_measure(char *args) {
    if (!args || !*args) {
        CLI_PrintMessage("MEASURE", MSG_ERROR, ERR_MISSING_ARG);
        return;
    }

    rtrim_control(args); // Удаляем пробелы и управляющие символы в конце

    uint8_t mask;
    if (strcasecmp(args, "all") == 0) {
        mask = ALL_MEASUREMENTS;
    } else {
        mask = parse_measure_list(args);
    }

    measure_function(mask);
}

static void handle_record(char *args) {
    if (!args || !*args) {
        CLI_PrintMessage("RECORD", MSG_ERROR, ERR_MISSING_ARG);
        return;
    }

    rtrim_control(args); // Удаляем пробелы и управляющие символы в конце

    uint8_t mask;
    if (strcasecmp(args, "all") == 0) {
        mask = ALL_MEASUREMENTS;
    } else {
        mask = parse_measure_list(args);
    }

    record_function(mask);
}

static void handle_type(char *args) {
    bool hex_mode = true;
    char *filename = args;

    if (!args || !*args) {
        CLI_PrintMessage("TYPE", MSG_ERROR, ERR_FILE_REQUIRED);
        return;
    }

    // Проверяем наличие режима вывода
    char *space = strchr(args, ' ');
    if (space) {
        *space = '\0';
        rtrim_control(args); // Обрезаем пробелы и управляющие символы в режиме

        if (strcasecmp(args, "text") == 0) {
            hex_mode = false;
        } else if (strcasecmp(args, "hex") != 0) {
            CLI_PrintMessage("TYPE", MSG_ERROR, ERR_INVALID_MODE);
            return;
        }

        filename = space + 1;
        while (*filename == ' ') filename++; // Пропуск пробелов
    }

    rtrim_control(filename); // Обрезаем пробелы и управляющие символы в конце имени файла

    type_function(filename, hex_mode);
}

static void handle_list(char *args) {
    if (!args || !*args) {
        CLI_PrintMessage("LIST", MSG_ERROR, ERR_FILE_REQUIRED);
        return;
    }

    rtrim_control(args); // Удаляем пробелы и управляющие символы в конце
    list_function(args);
}

static void handle_create(char *args) {
    if (!args || !*args) {
        CLI_PrintMessage("CREATE", MSG_ERROR, ERR_FILE_REQUIRED);
        return;
    }

    uint32_t blocks = 1;
    bool input = false;

    // Разбиваем аргументы
    char buffer[128];
    strncpy(buffer, args, sizeof(buffer)-1);
    buffer[sizeof(buffer)-1] = '\0';

    char *token = strtok(buffer, " ");
    char *filename = token;

    while ((token = strtok(NULL, " ")) != NULL) {
        if (strncasecmp(token, "n=", 2) == 0) {
            blocks = atoi(token + 2);
        }
        else if (strncasecmp(token, "inp", 3) == 0) {
            input = true;
        }
    }

    rtrim_control(filename); // Удаляем пробелы и управляющие символы в конце имени файла
    create_function(filename, blocks, input);
}

static void handle_delete(char *args) {
    if (!args || !*args) {
        CLI_PrintMessage("DELETE", MSG_ERROR, ERR_FILE_REQUIRED);
        return;
    }

    rtrim_control(args); // Удаляем пробелы и управляющие символы в конце
    delete_function(args);
}

static void handle_init(char *args) {
    (void)args; // Неиспользуемый параметр
    init_function();
}

static void handle_directory(char *args) {
    bool full = false;
    if (args && *args) {
        rtrim_control(args); // Удаляем пробелы и управляющие символы в конце

        if (strcasecmp(args, "full") == 0 ||
            strcasecmp(args, "f") == 0) {
            full = true;
        }
    }
    directory_function(full);
}

static void handle_squeeze(char *args) {
    (void)args; // Неиспользуемый параметр
    squeeze_function();
}

// Обработчики параметров SET -------------------------------------------------

static void handle_set_date(char *value) {
	DateTime dt;
	if (!value || !*value) {
        CLI_PrintMessage("SET", MSG_ERROR, ERR_SET_MISSING_VALUE);
        return;
    }
	RTC_GetDateTime(&dt);
	if(!RTC_ParseDate(value, &dt))
	{
		CLI_PrintMessage("SET", MSG_ERROR, ERR_SET_INVALID_DATE);
		return;
	}
	RTC_SetDateTime(&dt);
    printf("Date set to %s\n", value);
}

static void handle_set_time(char *value) {
    DateTime dt;
	if (!value || !*value) {
        CLI_PrintMessage("SET", MSG_ERROR, ERR_SET_MISSING_VALUE);
        return;
    }
	RTC_GetDateTime(&dt);
	if(!RTC_ParseTime(value, &dt))
	{
		CLI_PrintMessage("SET", MSG_ERROR, ERR_SET_INVALID_TIME);
		return;
	}
	RTC_SetDateTime(&dt);

    if (strcasecmp(value, "sync") == 0) {
        printf("Time synchronized\n");
    } else {
        printf("Time set to %s\n", value);
    }
}

static void handle_set_tt(char *value) {
    if (!value || !*value) {
        CLI_PrintMessage("SET", MSG_ERROR, ERR_SET_MISSING_VALUE);
        return;
    }

    if (strcasecmp(value, "quiet") == 0 || strcasecmp(value, "q") == 0) {
        sys_params.quiet_mode = true;
        printf("TT set to QUIET\n");
    }
    else if (strcasecmp(value, "noquiet") == 0 ||
             strcasecmp(value, "noq") == 0) {
        sys_params.quiet_mode = false;
        printf("TT set to NOQUIET\n");
    }
    else {
        CLI_PrintMessage("SET", MSG_ERROR, ERR_SET_INVALID_MODE);
    }
}

static void handle_set_exposure(char *value) {
    if (!value || !*value) {
        CLI_PrintMessage("SET", MSG_ERROR, ERR_SET_MISSING_VALUE);
        return;
    }

    uint32_t ms;
    if (!parse_time(value, &ms)) {
        CLI_PrintMessage("SET", MSG_ERROR, ERR_TIME_FORMAT);
        return;
    }

    // Проверка диапазона
    if (ms < 10 || ms > 1000000) {
        CLI_PrintMessage("SET", MSG_ERROR, ERR_OUT_OF_RANGE);
        return;
    }

    sys_params.exposure = ms;
    printf("Exposure set to %lums\n", ms);
}

static void handle_set_gain(char *value) {
	if (!value || !*value) {
	        CLI_PrintMessage("SET", MSG_ERROR, ERR_SET_MISSING_VALUE);
	        return;
	}
	if((strcmp(value, "0") == 0) || (strcasecmp(value, "OFF") == 0))
	{
		sys_params.gain = 0;
		printf("Photo detector switched off\n");
		return;
	} else
	if(strcmp(value, "2") == 0)
	{
		sys_params.gain = 2;
		printf("Photo detector gain is 2%%\n");
		return;
	}else
		if(strcmp(value, "20") == 0)
	{
		sys_params.gain = 1;
		printf("Photo detector gain is 20%%\n");
		return;
	}else
	if((strcmp(value, "100") == 0) || (strcasecmp(value, "FULL")))
	{
		sys_params.gain = 3;
		printf("Photo detector gain is 100%%\n");
		return;
	}
	else
	{
		CLI_PrintMessage("SET", MSG_ERROR, ERR_SET_INVALID_VALUE);
		printf("Note: possible gain values are 0 or OFF, 2, 20 and 100 or FULL\n");
		return;
	}



}
static void handle_set_period(char *value) {
    if (!value || !*value) {
        CLI_PrintMessage("SET", MSG_ERROR, ERR_SET_MISSING_VALUE);
        return;
    }

    uint32_t ms;
    if (!parse_time(value, &ms)) {
        CLI_PrintMessage("SET", MSG_ERROR, ERR_TIME_FORMAT);
        return;
    }

    sys_params.period = ms;
    printf("Period set to %lums\n", ms);
}

static void handle_set_echo(char *value) {
    if (!value || !*value) {
        CLI_PrintMessage("SET", MSG_ERROR, ERR_SET_MISSING_VALUE);
        return;
    }

    if (strcasecmp(value, "on") == 0) {
        sys_params.echo = true;
        printf("Echo ON\n");
    }
    else if (strcasecmp(value, "off") == 0) {
        sys_params.echo = false;
        printf("Echo OFF\n");
    }
    else {
        CLI_PrintMessage("SET", MSG_ERROR, ERR_SET_INVALID_MODE);
    }
}

// Обработчики параметров SHOW ------------------------------------------------

static void handle_show_date(void) {
    DateTime dt;
    RTC_GetDateTime(&dt);
	printf("Date: %04u-%02u-%02u\n",
           dt.year, dt.month, dt.day);
}

static void handle_show_time(void) {
	DateTime dt;
	RTC_GetDateTime(&dt);
    printf("Time: %02u:%02u:%02u\n",
           dt.hours, dt.minutes, dt.seconds);
}

static void handle_show_tt(void) {
    printf("TT Quiet: %s\n", sys_params.quiet_mode ? "YES" : "NO");
}

static void handle_show_exposure(void) {
    printf("Exposure: %lums\n", sys_params.exposure);
}

static void handle_show_gain(void) {
	printf("Photo detector gain: ");
	switch(sys_params.gain)
	{
	case 0:
		return printf("0 (switched off\n");
	case 2:
		return printf("2%%\n");
	case 1:
		return printf("20%%\n");
	case 3:
		return printf("100%%\n");
	}
}

static void handle_show_period(void) {
    printf("Period: %lums\n", sys_params.period);
}

static void handle_show_echo(void) {
    printf("Echo: %s\n", sys_params.echo ? "ON" : "OFF");
}

static void handle_show_depth(void) {
    printf("Depth: %1.4f V\n", ADS1115_ReadVoltage(0x4a));
}

static void handle_show_temp(void) {
    printf("Temperature: %2.1f deg.C\n", TMP100_GetTemperature(0x48));
}

static void handle_show_battery(void) {
    printf("Battery: 100%%\n");
}

static void handle_show_all(void) {
    printf("System settings:\n");
    printf("  Date: %04u-%02u-%02u\n", sys_params.year, sys_params.month, sys_params.day);
    printf("  Time: %02u:%02u:%02u\n", sys_params.hour, sys_params.minute, sys_params.second);
    printf("  TT Quiet: %s\n", sys_params.quiet_mode ? "YES" : "NO");
    printf("  Exposure: %lums\n", sys_params.exposure);
    printf("  Period: %lums\n", sys_params.period);
    printf("  Echo: %s\n", sys_params.echo ? "ON" : "OFF");
}

// Обработчики команд SET и SHOW ----------------------------------------------

static void handle_set(char *args) {
    if (!args || !*args) {
        CLI_PrintMessage("SET", MSG_ERROR, ERR_MISSING_ARG);
        return;
    }

    char buffer[128];
    strncpy(buffer, args, sizeof(buffer)-1);
    buffer[sizeof(buffer)-1] = '\0';

    char *param_str = strtok(buffer, " ");
    if (!param_str) {
        CLI_PrintMessage("SET", MSG_ERROR, ERR_MISSING_ARG);
        return;
    }

    char *value = strtok(NULL, "");
    if (value) {
        while (*value == ' ') value++; // Пропуск пробелов
        rtrim_control(value); // Удаляем пробелы и управляющие символы в конце
    }

    // Поиск параметра по префиксу
    const SetParam *param = find_set_param(param_str);
    if (param) {
        param->handler(value);
    } else {
        CLI_PrintMessage("SET", MSG_ERROR, ERR_SET_UNKNOWN_PARAM);
    }
}

static void handle_show(char *args) {
    if (!args || !*args) {
        CLI_PrintMessage("SHOW", MSG_ERROR, ERR_MISSING_ARG);
        return;
    }

    rtrim_control(args); // Удаляем пробелы и управляющие символы в конце

    // Поиск параметра по префиксу
    const ShowParam *param = find_show_param(args);
    if (param) {
        param->handler();
    } else {
        CLI_PrintMessage("SHOW", MSG_ERROR, ERR_SHOW_UNKNOWN_PARAM);
    }
}

static void handle_save(char *args) {
    if (!args || !*args) {
        CLI_PrintMessage("SAVE", MSG_ERROR, ERR_FILE_REQUIRED);
        return;
    }

    rtrim_control(args); // Удаляем пробелы и управляющие символы в конце
    save_settings(args);
}

static void handle_load(char *args) {
    if (!args || !*args) {
        CLI_PrintMessage("LOAD", MSG_ERROR, ERR_FILE_REQUIRED);
        return;
    }

    rtrim_control(args); // Удаляем пробелы и управляющие символы в конце
    load_settings(args);
}

// Основной обработчик команд -------------------------------------------------

void CLI_ProcessCommand(char *line) {
    // Удаление начальных пробельных символов
    while (*line && (isspace((unsigned char)*line) || *line == '\r' || *line == '\n')) {
        line++;
    }

    size_t len = strlen(line);
    while (len > 0 && (isspace((unsigned char)line[len-1]) ||
                      line[len-1] == '\r' ||
                      line[len-1] == '\n')) {
        line[--len] = '\0';
    }

    if (len == 0) return;

    // Эхо-вывод при необходимости
    if (sys_params.echo) printf("> %s\n", line);

    // Команды, начинающиеся с точки
    if (line[0] == '.' && len > 1) {
        execute_script(line + 1);
        return;
    }

    // Разделение команды и аргументов
    char *cmd_str = line;
    char *args = strchr(line, ' ');
    if (args) {
        *args++ = '\0';
        while (*args == ' ') args++; // Пропуск пробелов после команды
    }

    // Поиск команды методом DEC RT11
    const Command *cmd = find_command(cmd_str);

    if (cmd) {
        cmd->handler(args);
    } else {
        // Проверка на неоднозначность
        int count = 0;
        const Command *last_match = NULL;

        for (int i = 0; i < command_count; i++) {
            if (strncasecmp(cmd_str, commands[i].name, strlen(cmd_str)) == 0) {
                count++;
                last_match = &commands[i];
            }
        }

        if (count == 0) {
            CLI_PrintMessage("CLI", MSG_ERROR, ERR_CMD_NOT_FOUND);
        } else if (count == 1) {
            last_match->handler(args);
        } else {
            CLI_PrintMessage("CLI", MSG_ERROR, ERR_AMBIGUOUS_CMD);
        }
    }
}

// Заготовки функций ----------------------------------------------------------

void measure_function(uint8_t set) {
    Measure_PrintHeader(set);
    measure((float)sys_params.exposure/1000.f, set); //
    MeasResult result = measure((float)sys_params.exposure/1000.f, set);
    Measure_PrintValueStr(result, set);
}

void record_function(uint8_t set) {
    printf("RECORD started\n");
    Record_NewRecord(set);
}

void type_function(const char *filename, bool hex_mode) {
    //printf("TYPE: %s [%s]\n", filename, hex_mode ? "HEX" : "TEXT");
	Record_TypeRecord(filename);
}

void list_function(const char *filename) {
    Record_ListRecord(filename);
}

void create_function(const char *filename, uint32_t blocks, bool input) {
    printf("CREATE: %s, blocks=%lu, input=%d\n", filename, blocks, input);
    if(input)
    	HEX_CreateFile(filename);
}

void delete_function(const char *filename) {
    printf("DELETE: %s\n", filename);
}

void init_function(void) {
    printf("FS is initializing \n");
	FS_Format();
}

void directory_function(bool full) {
	Disk_PrintFileList();
}

void squeeze_function(void) {
    printf("SQUEEZE filesystem\n");
}

void execute_script(const char *filename) {
    printf("EXECUTE script: %s\n", filename);
}

void save_settings(const char *filename) {
    printf("SAVE settings: %s\n", filename);
}

void load_settings(const char *filename) {
    printf("LOAD settings: %s\n", filename);
}

// Инициализация CLI ----------------------------------------------------------

void CLI_Init(void) {
    // Инициализация параметров по умолчанию
    sys_params.year = 2023;
    sys_params.month = 1;
    sys_params.day = 1;
    sys_params.hour = 0;
    sys_params.minute = 0;
    sys_params.second = 0;
    sys_params.quiet_mode = false;
    sys_params.exposure = 100;   // 100 мс
    sys_params.gain = (uint32_t) S100;
    sys_params.period = 1000;    // 1 секунда
    sys_params.echo = true;
}
