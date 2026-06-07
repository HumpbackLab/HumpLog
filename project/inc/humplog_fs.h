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

#ifndef HUMPLOG_FS_H
#define HUMPLOG_FS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

#define HUMPLOG_FS_NAME_LENGTH 12U
#define HUMPLOG_FS_MAX_NODES 64U
#define HUMPLOG_FS_MAX_FILE_SLOTS 8U
#define HUMPLOG_FS_FILE_CAPACITY 768U
#define HUMPLOG_FS_PATH_LENGTH 32U

typedef struct
{
  uint8_t used;
  uint8_t is_dir;
  uint8_t parent;
  uint8_t slot;
  uint32_t size;
  char name[HUMPLOG_FS_NAME_LENGTH + 1U];
} humplog_fs_node_t;

typedef void (*humplog_fs_iterate_callback_t)(uint8_t node_id,
                                              const humplog_fs_node_t *node,
                                              void *context);

void humplog_fs_init(void);
uint8_t humplog_fs_root(void);
uint8_t humplog_fs_node_valid(uint8_t node_id);
const humplog_fs_node_t *humplog_fs_node_get(uint8_t node_id);
void humplog_fs_refresh_dir(uint8_t parent_id);
void humplog_fs_iterate_dir(uint8_t parent_id,
                            humplog_fs_iterate_callback_t callback,
                            void *context);
int8_t humplog_fs_find_child(uint8_t parent_id, const char *name);
int8_t humplog_fs_create_file(uint8_t parent_id, const char *name, uint8_t fail_if_exists);
int8_t humplog_fs_create_dir(uint8_t parent_id, const char *name);
int8_t humplog_fs_delete(uint8_t node_id, uint8_t recursive);
int32_t humplog_fs_read(uint8_t node_id, uint32_t offset, uint8_t *data, uint16_t length);
int8_t humplog_fs_write(uint8_t node_id, uint32_t offset, const uint8_t *data, uint16_t length);
int8_t humplog_fs_append(uint8_t node_id, const uint8_t *data, uint16_t length);
int8_t humplog_fs_truncate(uint8_t node_id, uint32_t size);
int8_t humplog_fs_stream_begin(uint8_t node_id, uint32_t offset);
int8_t humplog_fs_stream_write(const uint8_t *data, uint16_t length);
int8_t humplog_fs_stream_sync(void);
int8_t humplog_fs_stream_end(void);
const uint8_t *humplog_fs_data(uint8_t node_id);
uint32_t humplog_fs_capacity(uint8_t node_id);
uint32_t humplog_fs_used_bytes(void);
uint32_t humplog_fs_total_bytes(void);
uint8_t humplog_fs_name_valid(const char *name);

#ifdef __cplusplus
}
#endif

#endif
