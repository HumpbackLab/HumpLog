#ifndef OPENLOG_FS_H
#define OPENLOG_FS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

#define OPENLOG_FS_NAME_LENGTH 12U
#define OPENLOG_FS_MAX_NODES 64U
#define OPENLOG_FS_MAX_FILE_SLOTS 8U
#define OPENLOG_FS_FILE_CAPACITY 768U
#define OPENLOG_FS_PATH_LENGTH 96U

typedef struct
{
  uint8_t used;
  uint8_t is_dir;
  uint8_t parent;
  uint8_t slot;
  uint32_t size;
  char name[OPENLOG_FS_NAME_LENGTH + 1U];
} openlog_fs_node_t;

typedef void (*openlog_fs_iterate_callback_t)(uint8_t node_id,
                                              const openlog_fs_node_t *node,
                                              void *context);

void openlog_fs_init(void);
uint8_t openlog_fs_root(void);
uint8_t openlog_fs_node_valid(uint8_t node_id);
const openlog_fs_node_t *openlog_fs_node_get(uint8_t node_id);
void openlog_fs_refresh_dir(uint8_t parent_id);
void openlog_fs_iterate_dir(uint8_t parent_id,
                            openlog_fs_iterate_callback_t callback,
                            void *context);
int8_t openlog_fs_find_child(uint8_t parent_id, const char *name);
int8_t openlog_fs_create_file(uint8_t parent_id, const char *name, uint8_t fail_if_exists);
int8_t openlog_fs_create_dir(uint8_t parent_id, const char *name);
int8_t openlog_fs_delete(uint8_t node_id, uint8_t recursive);
int32_t openlog_fs_read(uint8_t node_id, uint32_t offset, uint8_t *data, uint16_t length);
int8_t openlog_fs_write(uint8_t node_id, uint32_t offset, const uint8_t *data, uint16_t length);
int8_t openlog_fs_append(uint8_t node_id, const uint8_t *data, uint16_t length);
int8_t openlog_fs_truncate(uint8_t node_id, uint32_t size);
int8_t openlog_fs_stream_begin(uint8_t node_id, uint32_t offset);
int8_t openlog_fs_stream_write(const uint8_t *data, uint16_t length);
int8_t openlog_fs_stream_sync(void);
int8_t openlog_fs_stream_end(void);
const uint8_t *openlog_fs_data(uint8_t node_id);
uint32_t openlog_fs_capacity(uint8_t node_id);
uint32_t openlog_fs_used_bytes(void);
uint32_t openlog_fs_total_bytes(void);
uint8_t openlog_fs_name_valid(const char *name);

#ifdef __cplusplus
}
#endif

#endif
