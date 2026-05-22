#include "openlog_fs.h"

#include <string.h>

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

    if(ch == '.' )
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

int8_t openlog_fs_truncate(uint8_t node_id, uint16_t size)
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

int8_t openlog_fs_write(uint8_t node_id, uint16_t offset, const uint8_t *data, uint16_t length)
{
  uint16_t end_offset;
  uint8_t slot;

  if(!openlog_fs_node_valid(node_id) || g_nodes[node_id].is_dir != 0U || data == NULL)
  {
    return -1;
  }

  end_offset = (uint16_t)(offset + length);
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

  memcpy(&g_file_storage[slot][offset], data, length);
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

uint16_t openlog_fs_capacity(uint8_t node_id)
{
  if(!openlog_fs_node_valid(node_id) || g_nodes[node_id].is_dir != 0U)
  {
    return 0U;
  }

  return OPENLOG_FS_FILE_CAPACITY;
}

uint16_t openlog_fs_used_bytes(void)
{
  uint8_t index;
  uint16_t total;

  total = 0U;
  for(index = 0U; index < OPENLOG_FS_MAX_NODES; ++index)
  {
    if(g_nodes[index].used != 0U && g_nodes[index].is_dir == 0U)
    {
      total = (uint16_t)(total + g_nodes[index].size);
    }
  }

  return total;
}

uint16_t openlog_fs_total_bytes(void)
{
  return OPENLOG_FS_MAX_FILE_SLOTS * OPENLOG_FS_FILE_CAPACITY;
}
