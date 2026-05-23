#include "openlog_fs.h"

#include <stdio.h>
#include <string.h>

#ifdef OPENLOG_FS_USE_RAMDISK_TEST

#define OPENLOG_FS_ROOT_ID 0U
#define OPENLOG_FS_INVALID_SLOT 0xFFU

static openlog_fs_node_t g_nodes[OPENLOG_FS_MAX_NODES];
static uint8_t g_file_storage[OPENLOG_FS_MAX_FILE_SLOTS][OPENLOG_FS_FILE_CAPACITY];
static uint8_t g_slot_in_use[OPENLOG_FS_MAX_FILE_SLOTS];

static uint8_t openlog_fs_char_equal(char left, char right)
{
  if(left >= 'a' && left <= 'z')
  {
    left = (char)(left - ('a' - 'A'));
  }
  if(right >= 'a' && right <= 'z')
  {
    right = (char)(right - ('a' - 'A'));
  }
  return left == right ? 1U : 0U;
}

static uint8_t openlog_fs_name_equal(const char *left, const char *right)
{
  while(*left != '\0' && *right != '\0')
  {
    if(!openlog_fs_char_equal(*left, *right))
    {
      return 0U;
    }
    ++left;
    ++right;
  }

  return (*left == '\0' && *right == '\0') ? 1U : 0U;
}

static int8_t openlog_fs_allocate_node(void)
{
  uint8_t index;

  for(index = 1U; index < OPENLOG_FS_MAX_NODES; ++index)
  {
    if(g_nodes[index].used == 0U)
    {
      return (int8_t)index;
    }
  }

  return -1;
}

static int8_t openlog_fs_allocate_slot(void)
{
  uint8_t index;

  for(index = 0U; index < OPENLOG_FS_MAX_FILE_SLOTS; ++index)
  {
    if(g_slot_in_use[index] == 0U)
    {
      g_slot_in_use[index] = 1U;
      memset(g_file_storage[index], 0, OPENLOG_FS_FILE_CAPACITY);
      return (int8_t)index;
    }
  }

  return -1;
}

static void openlog_fs_free_slot(uint8_t slot)
{
  if(slot < OPENLOG_FS_MAX_FILE_SLOTS)
  {
    g_slot_in_use[slot] = 0U;
    memset(g_file_storage[slot], 0, OPENLOG_FS_FILE_CAPACITY);
  }
}

static uint8_t openlog_fs_dir_empty(uint8_t node_id)
{
  uint8_t index;

  for(index = 0U; index < OPENLOG_FS_MAX_NODES; ++index)
  {
    if(g_nodes[index].used != 0U && g_nodes[index].parent == node_id && index != node_id)
    {
      return 0U;
    }
  }

  return 1U;
}

void openlog_fs_init(void)
{
  memset(g_nodes, 0, sizeof(g_nodes));
  memset(g_file_storage, 0, sizeof(g_file_storage));
  memset(g_slot_in_use, 0, sizeof(g_slot_in_use));

  g_nodes[OPENLOG_FS_ROOT_ID].used = 1U;
  g_nodes[OPENLOG_FS_ROOT_ID].is_dir = 1U;
  g_nodes[OPENLOG_FS_ROOT_ID].parent = OPENLOG_FS_ROOT_ID;
  g_nodes[OPENLOG_FS_ROOT_ID].slot = OPENLOG_FS_INVALID_SLOT;
  strcpy(g_nodes[OPENLOG_FS_ROOT_ID].name, "/");
}

uint8_t openlog_fs_root(void)
{
  return OPENLOG_FS_ROOT_ID;
}

uint8_t openlog_fs_node_valid(uint8_t node_id)
{
  return (node_id < OPENLOG_FS_MAX_NODES && g_nodes[node_id].used != 0U) ? 1U : 0U;
}

const openlog_fs_node_t *openlog_fs_node_get(uint8_t node_id)
{
  if(!openlog_fs_node_valid(node_id))
  {
    return NULL;
  }

  return &g_nodes[node_id];
}

void openlog_fs_refresh_dir(uint8_t parent_id)
{
  (void)parent_id;
}

void openlog_fs_iterate_dir(uint8_t parent_id,
                            openlog_fs_iterate_callback_t callback,
                            void *context)
{
  uint8_t index;

  if(callback == NULL || !openlog_fs_node_valid(parent_id) || g_nodes[parent_id].is_dir == 0U)
  {
    return;
  }

  for(index = 0U; index < OPENLOG_FS_MAX_NODES; ++index)
  {
    if(g_nodes[index].used != 0U &&
       index != parent_id &&
       g_nodes[index].parent == parent_id)
    {
      callback(index, &g_nodes[index], context);
    }
  }
}

int8_t openlog_fs_find_child(uint8_t parent_id, const char *name)
{
  uint8_t index;

  if(!openlog_fs_node_valid(parent_id) || name == NULL)
  {
    return -1;
  }

  for(index = 0U; index < OPENLOG_FS_MAX_NODES; ++index)
  {
    if(g_nodes[index].used != 0U &&
       g_nodes[index].parent == parent_id &&
       index != parent_id &&
       openlog_fs_name_equal(g_nodes[index].name, name))
    {
      return (int8_t)index;
    }
  }

  return -1;
}

uint8_t openlog_fs_name_valid(const char *name)
{
  size_t length;
  size_t index;
  uint8_t dot_count;

  if(name == NULL || *name == '\0')
  {
    return 0U;
  }

  length = strlen(name);
  if(length > OPENLOG_FS_NAME_LENGTH)
  {
    return 0U;
  }

  dot_count = 0U;
  for(index = 0U; index < length; ++index)
  {
    char ch = name[index];

    if(ch == '.')
    {
      ++dot_count;
      if(dot_count > 1U)
      {
        return 0U;
      }
      continue;
    }

    if((ch >= '0' && ch <= '9') ||
       (ch >= 'A' && ch <= 'Z') ||
       (ch >= 'a' && ch <= 'z') ||
       ch == '_' ||
       ch == '-')
    {
      continue;
    }

    return 0U;
  }

  return 1U;
}

int8_t openlog_fs_create_file(uint8_t parent_id, const char *name, uint8_t fail_if_exists)
{
  int8_t existing;
  int8_t node_id;
  int8_t slot_id;

  if(!openlog_fs_node_valid(parent_id) || !openlog_fs_name_valid(name))
  {
    return -1;
  }

  existing = openlog_fs_find_child(parent_id, name);
  if(existing >= 0)
  {
    if(g_nodes[(uint8_t)existing].is_dir != 0U || fail_if_exists != 0U)
    {
      return -1;
    }
    return existing;
  }

  node_id = openlog_fs_allocate_node();
  slot_id = openlog_fs_allocate_slot();
  if(node_id < 0 || slot_id < 0)
  {
    if(slot_id >= 0)
    {
      openlog_fs_free_slot((uint8_t)slot_id);
    }
    return -1;
  }

  memset(&g_nodes[(uint8_t)node_id], 0, sizeof(openlog_fs_node_t));
  g_nodes[(uint8_t)node_id].used = 1U;
  g_nodes[(uint8_t)node_id].parent = parent_id;
  g_nodes[(uint8_t)node_id].slot = (uint8_t)slot_id;
  strncpy(g_nodes[(uint8_t)node_id].name, name, OPENLOG_FS_NAME_LENGTH);
  g_nodes[(uint8_t)node_id].name[OPENLOG_FS_NAME_LENGTH] = '\0';
  return node_id;
}

int8_t openlog_fs_create_dir(uint8_t parent_id, const char *name)
{
  int8_t existing;
  int8_t node_id;

  if(!openlog_fs_node_valid(parent_id) || !openlog_fs_name_valid(name))
  {
    return -1;
  }

  existing = openlog_fs_find_child(parent_id, name);
  if(existing >= 0)
  {
    return -1;
  }

  node_id = openlog_fs_allocate_node();
  if(node_id < 0)
  {
    return -1;
  }

  memset(&g_nodes[(uint8_t)node_id], 0, sizeof(openlog_fs_node_t));
  g_nodes[(uint8_t)node_id].used = 1U;
  g_nodes[(uint8_t)node_id].is_dir = 1U;
  g_nodes[(uint8_t)node_id].parent = parent_id;
  g_nodes[(uint8_t)node_id].slot = OPENLOG_FS_INVALID_SLOT;
  strncpy(g_nodes[(uint8_t)node_id].name, name, OPENLOG_FS_NAME_LENGTH);
  g_nodes[(uint8_t)node_id].name[OPENLOG_FS_NAME_LENGTH] = '\0';
  return node_id;
}

int8_t openlog_fs_delete(uint8_t node_id, uint8_t recursive)
{
  uint8_t index;

  if(node_id == OPENLOG_FS_ROOT_ID || !openlog_fs_node_valid(node_id))
  {
    return -1;
  }

  if(g_nodes[node_id].is_dir != 0U)
  {
    if(recursive == 0U && !openlog_fs_dir_empty(node_id))
    {
      return -1;
    }

    if(recursive != 0U)
    {
      for(index = 1U; index < OPENLOG_FS_MAX_NODES; ++index)
      {
        if(g_nodes[index].used != 0U && g_nodes[index].parent == node_id)
        {
          if(openlog_fs_delete(index, 1U) != 0)
          {
            return -1;
          }
        }
      }
    }
  }
  else
  {
    openlog_fs_free_slot(g_nodes[node_id].slot);
  }

  memset(&g_nodes[node_id], 0, sizeof(openlog_fs_node_t));
  return 0;
}

int32_t openlog_fs_read(uint8_t node_id, uint32_t offset, uint8_t *data, uint16_t length)
{
  const openlog_fs_node_t *node;

  node = openlog_fs_node_get(node_id);
  if(node == NULL || node->is_dir != 0U || data == NULL)
  {
    return -1;
  }

  if(offset >= node->size)
  {
    return 0;
  }

  if(length > (node->size - offset))
  {
    length = (uint16_t)(node->size - offset);
  }

  memcpy(data, &g_file_storage[node->slot][offset], length);
  return (int32_t)length;
}

int8_t openlog_fs_truncate(uint8_t node_id, uint32_t size)
{
  if(!openlog_fs_node_valid(node_id) || g_nodes[node_id].is_dir != 0U)
  {
    return -1;
  }

  if(size > OPENLOG_FS_FILE_CAPACITY)
  {
    return -1;
  }

  if(size < g_nodes[node_id].size)
  {
    memset(&g_file_storage[g_nodes[node_id].slot][size], 0,
           (size_t)(g_nodes[node_id].size - size));
  }
  g_nodes[node_id].size = size;
  return 0;
}

int8_t openlog_fs_write(uint8_t node_id, uint32_t offset, const uint8_t *data, uint16_t length)
{
  uint32_t end_offset;
  uint8_t slot;

  if(!openlog_fs_node_valid(node_id) || g_nodes[node_id].is_dir != 0U)
  {
    return -1;
  }

  if(length != 0U && data == NULL)
  {
    return -1;
  }

  end_offset = offset + length;
  if(end_offset < offset || end_offset > OPENLOG_FS_FILE_CAPACITY)
  {
    return -1;
  }

  slot = g_nodes[node_id].slot;
  if(offset > g_nodes[node_id].size)
  {
    memset(&g_file_storage[slot][g_nodes[node_id].size], 0,
           (size_t)(offset - g_nodes[node_id].size));
  }

  if(length != 0U)
  {
    memcpy(&g_file_storage[slot][offset], data, length);
  }
  if(end_offset > g_nodes[node_id].size)
  {
    g_nodes[node_id].size = end_offset;
  }

  return 0;
}

int8_t openlog_fs_append(uint8_t node_id, const uint8_t *data, uint16_t length)
{
  if(!openlog_fs_node_valid(node_id))
  {
    return -1;
  }

  return openlog_fs_write(node_id, g_nodes[node_id].size, data, length);
}

const uint8_t *openlog_fs_data(uint8_t node_id)
{
  if(!openlog_fs_node_valid(node_id) || g_nodes[node_id].is_dir != 0U)
  {
    return NULL;
  }

  return g_file_storage[g_nodes[node_id].slot];
}

uint32_t openlog_fs_capacity(uint8_t node_id)
{
  if(!openlog_fs_node_valid(node_id) || g_nodes[node_id].is_dir != 0U)
  {
    return 0U;
  }

  return OPENLOG_FS_FILE_CAPACITY;
}

uint32_t openlog_fs_used_bytes(void)
{
  uint8_t index;
  uint32_t total;

  total = 0U;
  for(index = 0U; index < OPENLOG_FS_MAX_NODES; ++index)
  {
    if(g_nodes[index].used != 0U && g_nodes[index].is_dir == 0U)
    {
      total += g_nodes[index].size;
    }
  }

  return total;
}

uint32_t openlog_fs_total_bytes(void)
{
  return OPENLOG_FS_MAX_FILE_SLOTS * OPENLOG_FS_FILE_CAPACITY;
}

#else

#include "ff.h"

#define OPENLOG_FS_ROOT_ID 0U
#define OPENLOG_FS_VOLUME_PATH "0:"
#define OPENLOG_FS_SECTOR_SIZE 512U

static FATFS g_openlog_fatfs;
static openlog_fs_node_t g_nodes[OPENLOG_FS_MAX_NODES];
static char g_paths[OPENLOG_FS_MAX_NODES][OPENLOG_FS_PATH_LENGTH + 1U];

static void openlog_fs_build_fatfs_path(const char *relative_path, char *fatfs_path, size_t length)
{
  if(relative_path == NULL || relative_path[0] == '\0')
  {
    (void)snprintf(fatfs_path, length, OPENLOG_FS_VOLUME_PATH "/");
  }
  else
  {
    (void)snprintf(fatfs_path, length, OPENLOG_FS_VOLUME_PATH "/%s", relative_path);
  }
}

static uint8_t openlog_fs_build_child_path(uint8_t parent_id, const char *name, char *relative_path, size_t length)
{
  if(!openlog_fs_node_valid(parent_id) || name == NULL || length == 0U)
  {
    return 0U;
  }

  if(g_paths[parent_id][0] == '\0')
  {
    if(strlen(name) >= length)
    {
      return 0U;
    }
    strcpy(relative_path, name);
    return 1U;
  }

  if((strlen(g_paths[parent_id]) + 1U + strlen(name)) >= length)
  {
    return 0U;
  }

  strcpy(relative_path, g_paths[parent_id]);
  strcat(relative_path, "/");
  strcat(relative_path, name);
  return 1U;
}

static void openlog_fs_invalidate_node(uint8_t node_id)
{
  if(node_id < OPENLOG_FS_MAX_NODES && node_id != OPENLOG_FS_ROOT_ID)
  {
    memset(&g_nodes[node_id], 0, sizeof(g_nodes[node_id]));
    memset(g_paths[node_id], 0, sizeof(g_paths[node_id]));
  }
}

static int8_t openlog_fs_find_cached_path(const char *relative_path)
{
  uint8_t index;

  for(index = 0U; index < OPENLOG_FS_MAX_NODES; ++index)
  {
    if(g_nodes[index].used != 0U && strcmp(g_paths[index], relative_path) == 0)
    {
      return (int8_t)index;
    }
  }

  return -1;
}

static int8_t openlog_fs_allocate_node(void)
{
  uint8_t index;

  for(index = 1U; index < OPENLOG_FS_MAX_NODES; ++index)
  {
    if(g_nodes[index].used == 0U)
    {
      return (int8_t)index;
    }
  }

  return -1;
}

static int8_t openlog_fs_register_node(uint8_t parent_id,
                                       const char *name,
                                       const char *relative_path,
                                       uint8_t is_dir,
                                       uint32_t size)
{
  int8_t node_id;

  node_id = openlog_fs_find_cached_path(relative_path);
  if(node_id < 0)
  {
    node_id = openlog_fs_allocate_node();
    if(node_id < 0)
    {
      return -1;
    }
  }

  memset(&g_nodes[(uint8_t)node_id], 0, sizeof(openlog_fs_node_t));
  g_nodes[(uint8_t)node_id].used = 1U;
  g_nodes[(uint8_t)node_id].is_dir = is_dir;
  g_nodes[(uint8_t)node_id].parent = parent_id;
  g_nodes[(uint8_t)node_id].size = size;
  strncpy(g_nodes[(uint8_t)node_id].name, name, OPENLOG_FS_NAME_LENGTH);
  g_nodes[(uint8_t)node_id].name[OPENLOG_FS_NAME_LENGTH] = '\0';
  strncpy(g_paths[(uint8_t)node_id], relative_path, OPENLOG_FS_PATH_LENGTH);
  g_paths[(uint8_t)node_id][OPENLOG_FS_PATH_LENGTH] = '\0';
  return node_id;
}

static int8_t openlog_fs_register_by_stat(uint8_t parent_id, const char *name, const char *relative_path)
{
  FILINFO file_info;
  char fatfs_path[OPENLOG_FS_PATH_LENGTH + 4U];

  openlog_fs_build_fatfs_path(relative_path, fatfs_path, sizeof(fatfs_path));
  if(f_stat(fatfs_path, &file_info) != FR_OK)
  {
    return -1;
  }

  return openlog_fs_register_node(parent_id,
                                  name,
                                  relative_path,
                                  (file_info.fattrib & AM_DIR) != 0U ? 1U : 0U,
                                  (uint32_t)file_info.fsize);
}

static int8_t openlog_fs_delete_path_recursive(const char *relative_path)
{
  DIR directory;
  FILINFO file_info;
  FRESULT result;
  char fatfs_path[OPENLOG_FS_PATH_LENGTH + 4U];
  char child_relative_path[OPENLOG_FS_PATH_LENGTH + 1U];

  openlog_fs_build_fatfs_path(relative_path, fatfs_path, sizeof(fatfs_path));
  result = f_opendir(&directory, fatfs_path);
  if(result != FR_OK)
  {
    return -1;
  }

  for(;;)
  {
    result = f_readdir(&directory, &file_info);
    if(result != FR_OK || file_info.fname[0] == '\0')
    {
      break;
    }

    if(strcmp(file_info.fname, ".") == 0 || strcmp(file_info.fname, "..") == 0)
    {
      continue;
    }

    if(relative_path[0] == '\0')
    {
      (void)snprintf(child_relative_path, sizeof(child_relative_path), "%s", file_info.fname);
    }
    else
    {
      (void)snprintf(child_relative_path, sizeof(child_relative_path), "%s/%s", relative_path, file_info.fname);
    }

    if((file_info.fattrib & AM_DIR) != 0U)
    {
      if(openlog_fs_delete_path_recursive(child_relative_path) != 0)
      {
        (void)f_closedir(&directory);
        return -1;
      }
    }
    else
    {
      openlog_fs_build_fatfs_path(child_relative_path, fatfs_path, sizeof(fatfs_path));
      if(f_unlink(fatfs_path) != FR_OK)
      {
        (void)f_closedir(&directory);
        return -1;
      }
    }
  }

  (void)f_closedir(&directory);
  openlog_fs_build_fatfs_path(relative_path, fatfs_path, sizeof(fatfs_path));
  return f_unlink(fatfs_path) == FR_OK ? 0 : -1;
}

static int8_t openlog_fs_sync_node_size(uint8_t node_id)
{
  FILINFO file_info;
  char fatfs_path[OPENLOG_FS_PATH_LENGTH + 4U];

  if(!openlog_fs_node_valid(node_id))
  {
    return -1;
  }

  openlog_fs_build_fatfs_path(g_paths[node_id], fatfs_path, sizeof(fatfs_path));
  if(f_stat(fatfs_path, &file_info) != FR_OK)
  {
    openlog_fs_invalidate_node(node_id);
    return -1;
  }

  g_nodes[node_id].is_dir = (file_info.fattrib & AM_DIR) != 0U ? 1U : 0U;
  g_nodes[node_id].size = (uint32_t)file_info.fsize;
  return 0;
}

static int8_t openlog_fs_fill_zero_gap(FIL *file, uint32_t from_offset, uint32_t to_offset)
{
  uint8_t zero_buffer[32];
  UINT bytes_written;
  uint32_t remaining;
  UINT chunk;

  memset(zero_buffer, 0, sizeof(zero_buffer));
  if(from_offset >= to_offset)
  {
    return 0;
  }

  if(f_lseek(file, from_offset) != FR_OK)
  {
    return -1;
  }

  remaining = to_offset - from_offset;
  while(remaining > 0U)
  {
    chunk = remaining > sizeof(zero_buffer) ? (UINT)sizeof(zero_buffer) : (UINT)remaining;
    if(f_write(file, zero_buffer, chunk, &bytes_written) != FR_OK || bytes_written != chunk)
    {
      return -1;
    }
    remaining -= chunk;
  }

  return 0;
}

void openlog_fs_init(void)
{
  memset(g_nodes, 0, sizeof(g_nodes));
  memset(g_paths, 0, sizeof(g_paths));

  g_nodes[OPENLOG_FS_ROOT_ID].used = 1U;
  g_nodes[OPENLOG_FS_ROOT_ID].is_dir = 1U;
  g_nodes[OPENLOG_FS_ROOT_ID].parent = OPENLOG_FS_ROOT_ID;
  strcpy(g_nodes[OPENLOG_FS_ROOT_ID].name, "/");

  (void)f_mount(&g_openlog_fatfs, OPENLOG_FS_VOLUME_PATH, 1U);
}

uint8_t openlog_fs_root(void)
{
  return OPENLOG_FS_ROOT_ID;
}

uint8_t openlog_fs_node_valid(uint8_t node_id)
{
  return (node_id < OPENLOG_FS_MAX_NODES && g_nodes[node_id].used != 0U) ? 1U : 0U;
}

const openlog_fs_node_t *openlog_fs_node_get(uint8_t node_id)
{
  if(!openlog_fs_node_valid(node_id))
  {
    return NULL;
  }

  return &g_nodes[node_id];
}

void openlog_fs_refresh_dir(uint8_t parent_id)
{
  DIR directory;
  FILINFO file_info;
  FRESULT result;
  char fatfs_path[OPENLOG_FS_PATH_LENGTH + 4U];
  char child_relative_path[OPENLOG_FS_PATH_LENGTH + 1U];
  uint8_t index;
  uint8_t seen[OPENLOG_FS_MAX_NODES];
  int8_t node_id;

  if(!openlog_fs_node_valid(parent_id) || g_nodes[parent_id].is_dir == 0U)
  {
    return;
  }

  memset(seen, 0, sizeof(seen));
  seen[parent_id] = 1U;

  openlog_fs_build_fatfs_path(g_paths[parent_id], fatfs_path, sizeof(fatfs_path));
  if(f_opendir(&directory, fatfs_path) != FR_OK)
  {
    return;
  }

  for(;;)
  {
    result = f_readdir(&directory, &file_info);
    if(result != FR_OK || file_info.fname[0] == '\0')
    {
      break;
    }

    if(strcmp(file_info.fname, ".") == 0 || strcmp(file_info.fname, "..") == 0)
    {
      continue;
    }

    if(!openlog_fs_build_child_path(parent_id, file_info.fname, child_relative_path, sizeof(child_relative_path)))
    {
      continue;
    }

    node_id = openlog_fs_register_node(parent_id,
                                       file_info.fname,
                                       child_relative_path,
                                       (file_info.fattrib & AM_DIR) != 0U ? 1U : 0U,
                                       (uint32_t)file_info.fsize);
    if(node_id >= 0)
    {
      seen[(uint8_t)node_id] = 1U;
    }
  }

  (void)f_closedir(&directory);

  for(index = 1U; index < OPENLOG_FS_MAX_NODES; ++index)
  {
    if(g_nodes[index].used != 0U && g_nodes[index].parent == parent_id && seen[index] == 0U)
    {
      openlog_fs_invalidate_node(index);
    }
  }
}

void openlog_fs_iterate_dir(uint8_t parent_id,
                            openlog_fs_iterate_callback_t callback,
                            void *context)
{
  DIR directory;
  FILINFO file_info;
  char fatfs_path[OPENLOG_FS_PATH_LENGTH + 4U];
  char child_relative_path[OPENLOG_FS_PATH_LENGTH + 1U];
  openlog_fs_node_t node;
  int8_t cached_id;
  uint8_t emitted_id;

  if(callback == NULL || !openlog_fs_node_valid(parent_id) || g_nodes[parent_id].is_dir == 0U)
  {
    return;
  }

  openlog_fs_build_fatfs_path(g_paths[parent_id], fatfs_path, sizeof(fatfs_path));
  if(f_opendir(&directory, fatfs_path) != FR_OK)
  {
    return;
  }

  for(;;)
  {
    if(f_readdir(&directory, &file_info) != FR_OK || file_info.fname[0] == '\0')
    {
      break;
    }

    if(strcmp(file_info.fname, ".") == 0 || strcmp(file_info.fname, "..") == 0)
    {
      continue;
    }

    if(!openlog_fs_build_child_path(parent_id, file_info.fname, child_relative_path, sizeof(child_relative_path)))
    {
      continue;
    }

    memset(&node, 0, sizeof(node));
    node.used = 1U;
    node.is_dir = (file_info.fattrib & AM_DIR) != 0U ? 1U : 0U;
    node.parent = parent_id;
    node.size = (uint32_t)file_info.fsize;
    strncpy(node.name, file_info.fname, OPENLOG_FS_NAME_LENGTH);
    node.name[OPENLOG_FS_NAME_LENGTH] = '\0';

    emitted_id = 0xFFU;
    cached_id = openlog_fs_find_cached_path(child_relative_path);
    if(cached_id >= 0)
    {
      emitted_id = (uint8_t)cached_id;
    }
    callback(emitted_id, &node, context);
  }

  (void)f_closedir(&directory);
}

uint8_t openlog_fs_name_valid(const char *name)
{
  size_t length;
  size_t index;
  uint8_t dot_count;

  if(name == NULL || *name == '\0')
  {
    return 0U;
  }

  length = strlen(name);
  if(length > OPENLOG_FS_NAME_LENGTH)
  {
    return 0U;
  }

  dot_count = 0U;
  for(index = 0U; index < length; ++index)
  {
    char ch = name[index];

    if(ch == '.')
    {
      ++dot_count;
      if(dot_count > 1U)
      {
        return 0U;
      }
      continue;
    }

    if((ch >= '0' && ch <= '9') ||
       (ch >= 'A' && ch <= 'Z') ||
       (ch >= 'a' && ch <= 'z') ||
       ch == '_' ||
       ch == '-')
    {
      continue;
    }

    return 0U;
  }

  return 1U;
}

int8_t openlog_fs_find_child(uint8_t parent_id, const char *name)
{
  char child_relative_path[OPENLOG_FS_PATH_LENGTH + 1U];
  int8_t cached_id;

  if(!openlog_fs_node_valid(parent_id) || !openlog_fs_name_valid(name))
  {
    return -1;
  }

  if(!openlog_fs_build_child_path(parent_id, name, child_relative_path, sizeof(child_relative_path)))
  {
    return -1;
  }

  cached_id = openlog_fs_find_cached_path(child_relative_path);
  if(cached_id >= 0 && openlog_fs_sync_node_size((uint8_t)cached_id) == 0)
  {
    return cached_id;
  }

  return openlog_fs_register_by_stat(parent_id, name, child_relative_path);
}

int8_t openlog_fs_create_file(uint8_t parent_id, const char *name, uint8_t fail_if_exists)
{
  FIL file;
  FRESULT result;
  char relative_path[OPENLOG_FS_PATH_LENGTH + 1U];
  char fatfs_path[OPENLOG_FS_PATH_LENGTH + 4U];
  BYTE mode;

  if(!openlog_fs_node_valid(parent_id) || g_nodes[parent_id].is_dir == 0U || !openlog_fs_name_valid(name))
  {
    return -1;
  }

  if(!openlog_fs_build_child_path(parent_id, name, relative_path, sizeof(relative_path)))
  {
    return -1;
  }

  openlog_fs_build_fatfs_path(relative_path, fatfs_path, sizeof(fatfs_path));
  mode = (BYTE)(FA_WRITE | (fail_if_exists != 0U ? FA_CREATE_NEW : FA_OPEN_ALWAYS));
  result = f_open(&file, fatfs_path, mode);
  if(result != FR_OK)
  {
    return -1;
  }
  (void)f_close(&file);

  return openlog_fs_register_by_stat(parent_id, name, relative_path);
}

int8_t openlog_fs_create_dir(uint8_t parent_id, const char *name)
{
  char relative_path[OPENLOG_FS_PATH_LENGTH + 1U];
  char fatfs_path[OPENLOG_FS_PATH_LENGTH + 4U];

  if(!openlog_fs_node_valid(parent_id) || g_nodes[parent_id].is_dir == 0U || !openlog_fs_name_valid(name))
  {
    return -1;
  }

  if(!openlog_fs_build_child_path(parent_id, name, relative_path, sizeof(relative_path)))
  {
    return -1;
  }

  openlog_fs_build_fatfs_path(relative_path, fatfs_path, sizeof(fatfs_path));
  if(f_mkdir(fatfs_path) != FR_OK)
  {
    return -1;
  }

  return openlog_fs_register_node(parent_id, name, relative_path, 1U, 0U);
}

int8_t openlog_fs_delete(uint8_t node_id, uint8_t recursive)
{
  DIR directory;
  FILINFO file_info;
  char fatfs_path[OPENLOG_FS_PATH_LENGTH + 4U];

  if(node_id == OPENLOG_FS_ROOT_ID || !openlog_fs_node_valid(node_id))
  {
    return -1;
  }

  openlog_fs_build_fatfs_path(g_paths[node_id], fatfs_path, sizeof(fatfs_path));
  if(g_nodes[node_id].is_dir != 0U)
  {
    if(recursive == 0U)
    {
      if(f_opendir(&directory, fatfs_path) != FR_OK)
      {
        return -1;
      }
      if(f_readdir(&directory, &file_info) != FR_OK)
      {
        (void)f_closedir(&directory);
        return -1;
      }
      (void)f_closedir(&directory);
      if(file_info.fname[0] != '\0')
      {
        return -1;
      }
      if(f_unlink(fatfs_path) != FR_OK)
      {
        return -1;
      }
    }
    else if(openlog_fs_delete_path_recursive(g_paths[node_id]) != 0)
    {
      return -1;
    }
  }
  else if(f_unlink(fatfs_path) != FR_OK)
  {
    return -1;
  }

  openlog_fs_invalidate_node(node_id);
  return 0;
}

int32_t openlog_fs_read(uint8_t node_id, uint32_t offset, uint8_t *data, uint16_t length)
{
  FIL file;
  UINT bytes_read;
  char fatfs_path[OPENLOG_FS_PATH_LENGTH + 4U];

  if(!openlog_fs_node_valid(node_id) || g_nodes[node_id].is_dir != 0U || data == NULL)
  {
    return -1;
  }

  openlog_fs_build_fatfs_path(g_paths[node_id], fatfs_path, sizeof(fatfs_path));
  if(f_open(&file, fatfs_path, FA_READ) != FR_OK)
  {
    return -1;
  }

  if(f_lseek(&file, offset) != FR_OK)
  {
    (void)f_close(&file);
    return -1;
  }

  if(f_read(&file, data, length, &bytes_read) != FR_OK)
  {
    (void)f_close(&file);
    return -1;
  }

  (void)f_close(&file);
  (void)openlog_fs_sync_node_size(node_id);
  return (int32_t)bytes_read;
}

int8_t openlog_fs_write(uint8_t node_id, uint32_t offset, const uint8_t *data, uint16_t length)
{
  FIL file;
  UINT bytes_written;
  FRESULT result;
  FSIZE_t current_size;
  char fatfs_path[OPENLOG_FS_PATH_LENGTH + 4U];

  if(!openlog_fs_node_valid(node_id) || g_nodes[node_id].is_dir != 0U || (length != 0U && data == NULL))
  {
    return -1;
  }

  openlog_fs_build_fatfs_path(g_paths[node_id], fatfs_path, sizeof(fatfs_path));
  result = f_open(&file, fatfs_path, FA_WRITE | FA_OPEN_EXISTING);
  if(result != FR_OK)
  {
    return -1;
  }

  current_size = f_size(&file);
  if(offset > current_size && openlog_fs_fill_zero_gap(&file, (uint32_t)current_size, offset) != 0)
  {
    (void)f_close(&file);
    return -1;
  }

  if(f_lseek(&file, offset) != FR_OK)
  {
    (void)f_close(&file);
    return -1;
  }

  if(length != 0U)
  {
    if(f_write(&file, data, length, &bytes_written) != FR_OK || bytes_written != length)
    {
      (void)f_close(&file);
      return -1;
    }
  }

  (void)f_close(&file);
  return openlog_fs_sync_node_size(node_id);
}

int8_t openlog_fs_append(uint8_t node_id, const uint8_t *data, uint16_t length)
{
  if(openlog_fs_sync_node_size(node_id) != 0)
  {
    return -1;
  }

  return openlog_fs_write(node_id, g_nodes[node_id].size, data, length);
}

int8_t openlog_fs_truncate(uint8_t node_id, uint32_t size)
{
  FIL file;
  char fatfs_path[OPENLOG_FS_PATH_LENGTH + 4U];

  if(!openlog_fs_node_valid(node_id) || g_nodes[node_id].is_dir != 0U)
  {
    return -1;
  }

  openlog_fs_build_fatfs_path(g_paths[node_id], fatfs_path, sizeof(fatfs_path));
  if(f_open(&file, fatfs_path, FA_WRITE | FA_OPEN_EXISTING) != FR_OK)
  {
    return -1;
  }

  if(f_lseek(&file, size) != FR_OK || f_truncate(&file) != FR_OK)
  {
    (void)f_close(&file);
    return -1;
  }

  (void)f_close(&file);
  return openlog_fs_sync_node_size(node_id);
}

const uint8_t *openlog_fs_data(uint8_t node_id)
{
  (void)node_id;
  return NULL;
}

uint32_t openlog_fs_capacity(uint8_t node_id)
{
  (void)node_id;
  return 0xFFFFFFFFUL;
}

uint32_t openlog_fs_used_bytes(void)
{
  DWORD free_clusters;
  FATFS *filesystem;
  uint32_t total_bytes;
  uint32_t free_bytes;

  if(f_getfree(OPENLOG_FS_VOLUME_PATH, &free_clusters, &filesystem) != FR_OK)
  {
    return 0U;
  }

  total_bytes = (uint32_t)((filesystem->n_fatent - 2U) * filesystem->csize * OPENLOG_FS_SECTOR_SIZE);
  free_bytes = (uint32_t)(free_clusters * filesystem->csize * OPENLOG_FS_SECTOR_SIZE);
  return total_bytes - free_bytes;
}

uint32_t openlog_fs_total_bytes(void)
{
  DWORD free_clusters;
  FATFS *filesystem;

  if(f_getfree(OPENLOG_FS_VOLUME_PATH, &free_clusters, &filesystem) != FR_OK)
  {
    return 0U;
  }

  return (uint32_t)((filesystem->n_fatent - 2U) * filesystem->csize * OPENLOG_FS_SECTOR_SIZE);
}

#endif
