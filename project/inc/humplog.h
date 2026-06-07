/**
 * HumpLog - OpenLog-compatible serial logger firmware for AT32F421
 * Copyright (C) 2025  HumpbackLab
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef HUMPLOG_H
#define HUMPLOG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void humplog_init(void);
void humplog_process(void);
uint8_t humplog_record_active(void);

#ifdef __cplusplus
}
#endif

#endif
