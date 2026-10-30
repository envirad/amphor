/*
 * disk.h
 *
 *  Created on: 2 июн. 2025 г.
 *      Author: jar
 */

#ifndef DISK_H_
#define DISK_H_

#include <stdio.h>
#include <string.h>
#include "files.h"
#include "record.h"
#include "cli.h"

int8_t Disk_NewFile(void);
void Disk_PrintFileList(void);


#endif /* DISK_H_ */
