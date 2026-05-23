#include "openlog.h"

#include "openlog_fs.h"
#include "wk_system.h"
#include "wk_usart.h"

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define OPENLOG_ESCAPE_CHAR 0x1AU
#define OPENLOG_ESCAPE_COUNT 3U
#define OPENLOG_VERSION_BANNER "12"
#define OPENLOG_PROMPT_RECORD "<"
#define OPENLOG_PROMPT_COMMAND ">"
#define OPENLOG_LINE_BUFFER_SIZE 80U
#define OPENLOG_WRITE_LINE_BUFFER_SIZE 80U

typedef enum
{
  OPENLOG_MODE_NEWLOG = 0,
  OPENLOG_MODE_COMMAND,
  OPENLOG_MODE_APPEND,
  OPENLOG_MODE_WRITE
} openlog_mode_t;

typedef struct
{
  openlog_mode_t mode;
  uint8_t current_dir;
  uint8_t active_file;
  uint8_t escape_count;
  uint8_t line_length;
  uint8_t write_line_length;
  uint8_t ignore_lf;
  uint8_t write_ignore_lf;
  uint32_t write_offset;
  uint32_t log_sequence;
  char line_buffer[OPENLOG_LINE_BUFFER_SIZE + 1U];
  char write_line_buffer[OPENLOG_WRITE_LINE_BUFFER_SIZE + 1U];
} openlog_context_t;

static openlog_context_t g_openlog;

static uint8_t openlog_char_equal(char left, char right)
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

static uint8_t openlog_text_equal(const char *left, const char *right)
{
  while(*left != '\0' && *right != '\0')
  {
    if(!openlog_char_equal(*left, *right))
    {
      return 0U;
    }
    ++left;
    ++right;
  }

  return (*left == '\0' && *right == '\0') ? 1U : 0U;
}

static void openlog_write_text(const char *text)
{
  wk_usart1_write_string(text);
}

static void openlog_write_bytes(const uint8_t *data, uint16_t length)
{
  wk_usart1_write_buffer(data, length);
}

static void openlog_write_crlf(void)
{
  openlog_write_text("\r\n");
}

static void openlog_write_prompt(const char *suffix)
{
  openlog_write_text(OPENLOG_VERSION_BANNER);
  openlog_write_text(suffix);
}

static void openlog_write_prompt_line(void)
{
  openlog_write_crlf();
  openlog_write_prompt(OPENLOG_PROMPT_COMMAND);
}

static char *openlog_next_token(char **cursor)
{
  char *start;

  while(**cursor == ' ')
  {
    ++(*cursor);
  }

  if(**cursor == '\0')
  {
    return NULL;
  }

  start = *cursor;
  while(**cursor != '\0' && **cursor != ' ')
  {
    ++(*cursor);
  }

  if(**cursor != '\0')
  {
    **cursor = '\0';
    ++(*cursor);
  }

  return start;
}

static void openlog_write_decimal(uint32_t value)
{
  char buffer[16];
  snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)value);
  openlog_write_text(buffer);
}

static void openlog_write_hex_byte(uint8_t value)
{
  static const char hex[] = "0123456789ABCDEF";
  char buffer[3];

  buffer[0] = hex[(value >> 4) & 0x0F];
  buffer[1] = hex[value & 0x0F];
  buffer[2] = '\0';
  openlog_write_text(buffer);
}

static int8_t openlog_find_node(const char *name)
{
  if(name == NULL)
  {
    return -1;
  }

  if(openlog_text_equal(name, "/"))
  {
    return (int8_t)openlog_fs_root();
  }

  if(openlog_text_equal(name, ".."))
  {
    const openlog_fs_node_t *node = openlog_fs_node_get(g_openlog.current_dir);
    return node != NULL ? (int8_t)node->parent : -1;
  }

  return openlog_fs_find_child(g_openlog.current_dir, name);
}

static uint8_t openlog_create_newlog_file(void)
{
  char name[13];
  uint32_t attempts;
  int8_t node_id;

  for(attempts = 0U; attempts < 100000UL; ++attempts)
  {
    snprintf(name, sizeof(name), "LOG%05lu.TXT",
             (unsigned long)g_openlog.log_sequence++);
    node_id = openlog_fs_create_file(openlog_fs_root(), name, 1U);
    if(node_id >= 0)
    {
      g_openlog.active_file = (uint8_t)node_id;
      return 1U;
    }
  }

  return 0U;
}

static void openlog_enter_command_mode(void)
{
  g_openlog.mode = OPENLOG_MODE_COMMAND;
  g_openlog.escape_count = 0U;
  g_openlog.line_length = 0U;
  openlog_write_prompt_line();
}

static void openlog_handle_stream_byte(uint8_t byte)
{
  if(byte == OPENLOG_ESCAPE_CHAR)
  {
    ++g_openlog.escape_count;
    if(g_openlog.escape_count >= OPENLOG_ESCAPE_COUNT)
    {
      openlog_enter_command_mode();
    }
    return;
  }

  while(g_openlog.escape_count > 0U)
  {
    if(openlog_fs_append(g_openlog.active_file, &((uint8_t){OPENLOG_ESCAPE_CHAR}), 1U) != 0)
    {
      openlog_write_text("\r\nerror: storage full");
      openlog_enter_command_mode();
      return;
    }
    --g_openlog.escape_count;
  }

  if(openlog_fs_append(g_openlog.active_file, &byte, 1U) != 0)
  {
    openlog_write_text("\r\nerror: storage full");
    openlog_enter_command_mode();
  }
}

static void openlog_print_help(void)
{
  openlog_write_text("\r\nnew append write rm size read cat ls md cd sync reset ?");
  openlog_write_text("\r\ndisk available, baud/init unsupported");
}

static void openlog_print_path(void)
{
  uint8_t chain[OPENLOG_FS_MAX_NODES];
  uint8_t count;
  uint8_t cursor;
  const openlog_fs_node_t *node;

  if(g_openlog.current_dir == openlog_fs_root())
  {
    openlog_write_text("\\");
    return;
  }

  count = 0U;
  cursor = g_openlog.current_dir;
  node = openlog_fs_node_get(cursor);
  while(node != NULL && cursor != openlog_fs_root() && count < OPENLOG_FS_MAX_NODES)
  {
    chain[count++] = cursor;
    cursor = node->parent;
    node = openlog_fs_node_get(cursor);
  }

  openlog_write_text("\\");
  while(count > 0U)
  {
    node = openlog_fs_node_get(chain[--count]);
    if(node != NULL)
    {
      openlog_write_text(node->name);
      if(count > 0U)
      {
        openlog_write_text("\\");
      }
    }
  }
}

static void openlog_command_ls(void)
{
  uint8_t index;
  uint8_t found;
  const openlog_fs_node_t *node;

  openlog_fs_refresh_dir(g_openlog.current_dir);
  found = 0U;
  for(index = 0U; index < OPENLOG_FS_MAX_NODES; ++index)
  {
    node = openlog_fs_node_get(index);
    if(node != NULL &&
       index != openlog_fs_root() &&
       node->parent == g_openlog.current_dir)
    {
      openlog_write_crlf();
      if(node->is_dir != 0U)
      {
        openlog_write_text("\\");
      }
      openlog_write_text(node->name);
      found = 1U;
    }
  }

  if(found == 0U)
  {
    openlog_write_text("\r\n<empty>");
  }
}

static void openlog_command_read(uint8_t node_id, uint32_t start, uint32_t length, uint32_t type)
{
  const openlog_fs_node_t *node;
  uint8_t byte_buffer[16];
  int32_t bytes_read;
  uint32_t index;
  uint32_t end;
  uint32_t remaining;

  node = openlog_fs_node_get(node_id);
  if(node == NULL || node->is_dir != 0U)
  {
    openlog_write_text("\r\nerror: not a file");
    return;
  }

  if(start > node->size)
  {
    start = node->size;
  }

  end = node->size;
  if(length != 0U && (start + length) < end)
  {
    end = start + length;
  }

  openlog_write_crlf();
  if(type == 2U)
  {
    remaining = end - start;
    while(remaining > 0U)
    {
      bytes_read = openlog_fs_read(node_id,
                                   start + (end - start - remaining),
                                   byte_buffer,
                                   remaining > sizeof(byte_buffer) ? sizeof(byte_buffer) : (uint16_t)remaining);
      if(bytes_read <= 0)
      {
        return;
      }
      for(index = 0U; index < (uint32_t)bytes_read; ++index)
      {
        if((start + (end - start - remaining) + index) > start)
        {
          openlog_write_text(" ");
        }
        openlog_write_hex_byte(byte_buffer[index]);
      }
      remaining -= (uint32_t)bytes_read;
    }
    return;
  }

  remaining = end - start;
  while(remaining > 0U)
  {
    bytes_read = openlog_fs_read(node_id,
                                 start + (end - start - remaining),
                                 byte_buffer,
                                 remaining > sizeof(byte_buffer) ? sizeof(byte_buffer) : (uint16_t)remaining);
    if(bytes_read <= 0)
    {
      return;
    }
    openlog_write_bytes(byte_buffer, (uint16_t)bytes_read);
    remaining -= (uint32_t)bytes_read;
  }
}

static void openlog_command_cat(uint8_t node_id)
{
  const openlog_fs_node_t *node;
  uint8_t data_byte;
  int32_t bytes_read;
  uint16_t index;

  node = openlog_fs_node_get(node_id);
  if(node == NULL || node->is_dir != 0U)
  {
    openlog_write_text("\r\nerror: not a file");
    return;
  }

  for(index = 0U; index < node->size; ++index)
  {
    if((index % 8U) == 0U)
    {
      openlog_write_crlf();
      openlog_write_hex_byte((uint8_t)(index >> 8));
      openlog_write_hex_byte((uint8_t)(index & 0xFFU));
      openlog_write_text(": ");
    }
    else
    {
      openlog_write_text(" ");
    }
    bytes_read = openlog_fs_read(node_id, index, &data_byte, 1U);
    if(bytes_read != 1)
    {
      return;
    }
    openlog_write_hex_byte(data_byte);
  }
}

static void openlog_start_append(const char *name)
{
  int8_t node_id;

  node_id = openlog_fs_create_file(g_openlog.current_dir, name, 0U);
  if(node_id < 0)
  {
    openlog_write_text("\r\nerror: cannot open file");
    return;
  }

  g_openlog.active_file = (uint8_t)node_id;
  g_openlog.mode = OPENLOG_MODE_APPEND;
  g_openlog.escape_count = 0U;
}

static void openlog_start_write(const char *name, uint32_t offset)
{
  int8_t node_id;

  node_id = openlog_fs_create_file(g_openlog.current_dir, name, 0U);
  if(node_id < 0)
  {
    openlog_write_text("\r\nerror: cannot open file");
    return;
  }

  g_openlog.active_file = (uint8_t)node_id;
  g_openlog.write_offset = offset;
  g_openlog.write_line_length = 0U;
  g_openlog.write_ignore_lf = 0U;
  g_openlog.mode = OPENLOG_MODE_WRITE;
  openlog_write_text("\r\nwrite mode, empty line exits");
}

static void openlog_process_command(char *line)
{
  char *cursor;
  char *command;
  char *arg1;
  char *arg2;
  char *arg3;
  char *arg4;
  int8_t node_id;

  cursor = line;
  command = openlog_next_token(&cursor);
  if(command == NULL)
  {
    openlog_write_prompt_line();
    return;
  }

  arg1 = openlog_next_token(&cursor);
  arg2 = openlog_next_token(&cursor);
  arg3 = openlog_next_token(&cursor);
  arg4 = openlog_next_token(&cursor);

  if(openlog_text_equal(command, "?"))
  {
    openlog_print_help();
  }
  else if(openlog_text_equal(command, "new"))
  {
    if(arg1 == NULL)
    {
      openlog_write_text("\r\nerror: missing file");
    }
    else if(openlog_fs_create_file(g_openlog.current_dir, arg1, 1U) < 0)
    {
      openlog_write_text("\r\nerror: cannot create file");
    }
  }
  else if(openlog_text_equal(command, "append"))
  {
    if(arg1 == NULL)
    {
      openlog_write_text("\r\nerror: missing file");
    }
    else
    {
      openlog_start_append(arg1);
      return;
    }
  }
  else if(openlog_text_equal(command, "write"))
  {
    uint32_t offset = 0U;

    if(arg1 == NULL)
    {
      openlog_write_text("\r\nerror: missing file");
    }
    else
    {
      if(arg2 != NULL)
      {
        offset = (uint16_t)strtoul(arg2, NULL, 10);
      }
      openlog_start_write(arg1, offset);
      return;
    }
  }
  else if(openlog_text_equal(command, "rm"))
  {
    uint8_t recursive = 0U;

    if(arg1 != NULL && openlog_text_equal(arg1, "-rf"))
    {
      recursive = 1U;
      arg1 = arg2;
    }

    if(arg1 == NULL)
    {
      openlog_write_text("\r\nerror: missing target");
    }
    else
    {
      node_id = openlog_find_node(arg1);
      if(node_id < 0 || openlog_fs_delete((uint8_t)node_id, recursive) != 0)
      {
        openlog_write_text("\r\nerror: delete failed");
      }
    }
  }
  else if(openlog_text_equal(command, "size"))
  {
    if(arg1 == NULL)
    {
      openlog_write_text("\r\nerror: missing file");
    }
    else
    {
      const openlog_fs_node_t *node;
      node_id = openlog_find_node(arg1);
      node = node_id >= 0 ? openlog_fs_node_get((uint8_t)node_id) : NULL;
      if(node == NULL || node->is_dir != 0U)
      {
        openlog_write_text("\r\nerror: not a file");
      }
      else
      {
        openlog_write_crlf();
        openlog_write_decimal(node->size);
      }
    }
  }
  else if(openlog_text_equal(command, "read"))
  {
    uint32_t start = 0U;
    uint32_t length = 0U;
    uint32_t type = 1U;

    if(arg1 == NULL)
    {
      openlog_write_text("\r\nerror: missing file");
    }
    else
    {
      node_id = openlog_find_node(arg1);
      if(node_id < 0)
      {
        openlog_write_text("\r\nerror: file not found");
      }
      else
      {
        if(arg2 != NULL)
        {
          start = strtoul(arg2, NULL, 10);
        }
        if(arg3 != NULL)
        {
          length = strtoul(arg3, NULL, 10);
          if(arg4 != NULL)
          {
            type = strtoul(arg4, NULL, 10);
          }
        }
        openlog_command_read((uint8_t)node_id, start, length, type);
      }
    }
  }
  else if(openlog_text_equal(command, "cat"))
  {
    if(arg1 == NULL)
    {
      openlog_write_text("\r\nerror: missing file");
    }
    else
    {
      node_id = openlog_find_node(arg1);
      if(node_id < 0)
      {
        openlog_write_text("\r\nerror: file not found");
      }
      else
      {
        openlog_command_cat((uint8_t)node_id);
      }
    }
  }
  else if(openlog_text_equal(command, "ls"))
  {
    openlog_command_ls();
  }
  else if(openlog_text_equal(command, "md"))
  {
    if(arg1 == NULL || openlog_fs_create_dir(g_openlog.current_dir, arg1) < 0)
    {
      openlog_write_text("\r\nerror: cannot create directory");
    }
  }
  else if(openlog_text_equal(command, "cd"))
  {
    if(arg1 == NULL)
    {
      openlog_write_crlf();
      openlog_print_path();
    }
    else
    {
      node_id = openlog_find_node(arg1);
      if(node_id < 0)
      {
        openlog_write_text("\r\nerror: directory not found");
      }
      else if(openlog_fs_node_get((uint8_t)node_id)->is_dir == 0U)
      {
        openlog_write_text("\r\nerror: not a directory");
      }
      else
      {
        g_openlog.current_dir = (uint8_t)node_id;
      }
    }
  }
  else if(openlog_text_equal(command, "sync"))
  {
    openlog_write_text("\r\nsynced");
  }
  else if(openlog_text_equal(command, "reset"))
  {
    openlog_write_text("\r\nresetting");
    wk_delay_ms(10U);
    NVIC_SystemReset();
  }
  else if(openlog_text_equal(command, "disk"))
  {
    openlog_write_text("\r\nSD ");
    openlog_write_decimal(openlog_fs_used_bytes());
    openlog_write_text("/");
    openlog_write_decimal(openlog_fs_total_bytes());
    openlog_write_text(" bytes");
  }
  else if(openlog_text_equal(command, "baud") || openlog_text_equal(command, "init"))
  {
    openlog_write_text("\r\nunsupported in this firmware");
  }
  else
  {
    openlog_write_text("\r\nerror: unknown command");
  }

  openlog_write_prompt_line();
}

static void openlog_handle_command_byte(uint8_t byte)
{
  if(byte == '\r')
  {
    g_openlog.ignore_lf = 1U;
    wk_usart1_write_byte('\r');
    wk_usart1_write_byte('\n');
    g_openlog.line_buffer[g_openlog.line_length] = '\0';
    openlog_process_command(g_openlog.line_buffer);
    g_openlog.line_length = 0U;
    return;
  }

  if(byte == '\n')
  {
    if(g_openlog.ignore_lf != 0U)
    {
      g_openlog.ignore_lf = 0U;
      return;
    }
    wk_usart1_write_byte('\r');
    wk_usart1_write_byte('\n');
    g_openlog.line_buffer[g_openlog.line_length] = '\0';
    openlog_process_command(g_openlog.line_buffer);
    g_openlog.line_length = 0U;
    return;
  }

  g_openlog.ignore_lf = 0U;
  if(byte == '\b' || byte == 0x7FU)
  {
    if(g_openlog.line_length > 0U)
    {
      --g_openlog.line_length;
      openlog_write_text("\b \b");
    }
    return;
  }

  if(g_openlog.line_length < OPENLOG_LINE_BUFFER_SIZE)
  {
    g_openlog.line_buffer[g_openlog.line_length++] = (char)byte;
    wk_usart1_write_byte(byte);
  }
}

static void openlog_handle_write_byte(uint8_t byte)
{
  if(byte == '\r')
  {
    g_openlog.write_ignore_lf = 1U;
    wk_usart1_write_byte('\r');
    wk_usart1_write_byte('\n');
    if(g_openlog.write_line_length == 0U)
    {
      g_openlog.mode = OPENLOG_MODE_COMMAND;
      openlog_write_prompt_line();
      return;
    }
    if(openlog_fs_write(g_openlog.active_file, g_openlog.write_offset,
                        (const uint8_t *)g_openlog.write_line_buffer,
                        g_openlog.write_line_length) != 0 ||
       openlog_fs_write(g_openlog.active_file,
                        (uint16_t)(g_openlog.write_offset + g_openlog.write_line_length),
                        (const uint8_t *)"\n", 1U) != 0)
    {
      openlog_write_text("\r\nerror: storage full");
      g_openlog.mode = OPENLOG_MODE_COMMAND;
      openlog_write_prompt_line();
      return;
    }
    g_openlog.write_offset = (uint16_t)(g_openlog.write_offset + g_openlog.write_line_length + 1U);
    g_openlog.write_line_length = 0U;
    return;
  }

  if(byte == '\n')
  {
    if(g_openlog.write_ignore_lf != 0U)
    {
      g_openlog.write_ignore_lf = 0U;
      return;
    }
    wk_usart1_write_byte('\r');
    wk_usart1_write_byte('\n');
    if(g_openlog.write_line_length == 0U)
    {
      g_openlog.mode = OPENLOG_MODE_COMMAND;
      openlog_write_prompt_line();
    }
    return;
  }

  g_openlog.write_ignore_lf = 0U;
  if(byte == '\b' || byte == 0x7FU)
  {
    if(g_openlog.write_line_length > 0U)
    {
      --g_openlog.write_line_length;
      openlog_write_text("\b \b");
    }
    return;
  }

  if(g_openlog.write_line_length < OPENLOG_WRITE_LINE_BUFFER_SIZE)
  {
    g_openlog.write_line_buffer[g_openlog.write_line_length++] = (char)byte;
    wk_usart1_write_byte(byte);
  }
}

void openlog_init(void)
{
  memset(&g_openlog, 0, sizeof(g_openlog));
  openlog_fs_init();
  g_openlog.current_dir = openlog_fs_root();
  g_openlog.mode = OPENLOG_MODE_NEWLOG;

  if(openlog_create_newlog_file() == 0U)
  {
    g_openlog.mode = OPENLOG_MODE_COMMAND;
    openlog_write_prompt(OPENLOG_PROMPT_COMMAND);
    openlog_write_text("\r\nerror: cannot create log file");
    openlog_write_prompt_line();
    return;
  }

  openlog_write_prompt(OPENLOG_PROMPT_RECORD);
}

void openlog_process(void)
{
  while(wk_usart1_readable() != 0U)
  {
    uint8_t byte = wk_usart1_read_byte();

    switch(g_openlog.mode)
    {
      case OPENLOG_MODE_NEWLOG:
      case OPENLOG_MODE_APPEND:
        openlog_handle_stream_byte(byte);
        break;

      case OPENLOG_MODE_COMMAND:
        openlog_handle_command_byte(byte);
        break;

      case OPENLOG_MODE_WRITE:
        openlog_handle_write_byte(byte);
        break;

      default:
        g_openlog.mode = OPENLOG_MODE_COMMAND;
        openlog_write_prompt_line();
        break;
    }
  }
}
