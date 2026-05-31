#include "humplog.h"

#include "board_led.h"
#include "humplog_fs.h"
#include "sd_spi.h"
#include "wk_system.h"
#include "wk_usart.h"

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define HUMPLOG_ESCAPE_CHAR 0x1AU
#define HUMPLOG_ESCAPE_COUNT 0U    //default to disable escape mode. enable in config.txt by setting to 3 or higher
#define HUMPLOG_VERSION_BANNER "12"
#define HUMPLOG_PROMPT_RECORD "<"
#define HUMPLOG_PROMPT_COMMAND ">"
#define HUMPLOG_LINE_BUFFER_SIZE 80U
#define HUMPLOG_WRITE_LINE_BUFFER_SIZE 80U
#define HUMPLOG_STREAM_BUFFER_SIZE 2048U
#define HUMPLOG_STREAM_BUFFER_COUNT 3U
#define HUMPLOG_STREAM_FLUSH_IDLE_US 20000U
#define HUMPLOG_CONFIG_FILE "config.txt"

typedef enum
{
  HUMPLOG_BOOT_MODE_NEWLOG = 0,
  HUMPLOG_BOOT_MODE_SEQLOG = 1,
  HUMPLOG_BOOT_MODE_COMMAND = 2
} humplog_boot_mode_t;

typedef enum
{
  HUMPLOG_MODE_NEWLOG = 0,
  HUMPLOG_MODE_COMMAND,
  HUMPLOG_MODE_APPEND,
  HUMPLOG_MODE_WRITE,
  HUMPLOG_MODE_BAUD_MENU,
  HUMPLOG_MODE_SET_MENU
} humplog_mode_t;

typedef struct
{
  uint32_t baud_rate;
  uint8_t escape_char;
  uint8_t escape_count;
  uint8_t boot_mode;
  uint8_t verbose_errors;
  uint8_t echo_enabled;
  uint8_t ignore_rx_on_boot;
} humplog_config_t;

typedef struct
{
  humplog_mode_t mode;
  uint8_t current_dir;
  uint8_t active_file;
  uint8_t escape_count;
  uint8_t stream_active_index;
  uint8_t stream_drain_index;
  uint8_t stream_overrun_latched;
  uint8_t stream_drain;
  uint8_t line_length;
  uint8_t write_line_length;
  uint16_t stream_length[HUMPLOG_STREAM_BUFFER_COUNT];
  uint8_t ignore_lf;
  uint8_t write_ignore_lf;
  uint32_t write_offset;
  uint32_t stream_offset;
  uint32_t stream_last_tick;
  uint32_t log_sequence;
  char line_buffer[HUMPLOG_LINE_BUFFER_SIZE + 1U];
  char write_line_buffer[HUMPLOG_WRITE_LINE_BUFFER_SIZE + 1U];
  uint8_t stream_buffer[HUMPLOG_STREAM_BUFFER_COUNT][HUMPLOG_STREAM_BUFFER_SIZE];
} humplog_context_t;

typedef struct
{
  uint32_t flush_calls;
  uint32_t flush_bytes;
  uint32_t flush_total_us;
  uint32_t flush_max_us;
  uint32_t write_calls;
  uint32_t write_bytes;
  uint32_t write_total_us;
  uint32_t write_max_us;
  uint32_t sync_calls;
  uint32_t sync_total_us;
  uint32_t sync_max_us;
} humplog_stream_stats_t;

static humplog_context_t g_humplog;
static humplog_config_t g_humplog_config;
static humplog_stream_stats_t g_humplog_stream_stats;

static void humplog_config_set_defaults(void);
static uint8_t humplog_config_baud_valid(uint32_t baud_rate);
static uint8_t humplog_config_load(void);
static uint8_t humplog_config_save(void);
static uint8_t humplog_boot_mode_enter(uint8_t write_prompt);
static uint8_t humplog_reinitialize(uint8_t write_prompt, uint8_t force_command_mode);
static uint8_t humplog_start_sequential_log(void);
static uint8_t humplog_rx_line_is_low(void);
static void humplog_update_log_sequence(void);
static uint8_t humplog_stream_flush(void);
static uint8_t humplog_stream_queue_byte(uint8_t byte);
static void humplog_stream_start(uint8_t node_id, humplog_mode_t mode);
static void humplog_stream_stop(void);
static uint8_t humplog_stream_flush_pending(void);
static uint8_t humplog_stream_commit_active(void);
static void humplog_reset_runtime_state(void);
static void humplog_write_prompt_for_mode(void);
static void humplog_report_error(const char *message);
static uint8_t humplog_wildcard_match(const char *pattern, const char *text);
static void humplog_handle_menu_line(char *line);
static void humplog_stream_stats_reset(void);
static void humplog_command_stats(const char *arg1);

static uint8_t humplog_char_equal(char left, char right)
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

static uint8_t humplog_text_equal(const char *left, const char *right)
{
  while(*left != '\0' && *right != '\0')
  {
    if(!humplog_char_equal(*left, *right))
    {
      return 0U;
    }
    ++left;
    ++right;
  }

  return (*left == '\0' && *right == '\0') ? 1U : 0U;
}

static void humplog_write_text(const char *text)
{
  wk_usart1_write_string(text);
}

static void humplog_write_bytes(const uint8_t *data, uint16_t length)
{
  wk_usart1_write_buffer(data, length);
}

static void humplog_write_crlf(void)
{
  humplog_write_text("\r\n");
}

static void humplog_write_prompt(const char *suffix)
{
  humplog_write_text(HUMPLOG_VERSION_BANNER);
  humplog_write_text(suffix);
}

static void humplog_write_prompt_line(void)
{
  humplog_write_crlf();
  humplog_write_prompt_for_mode();
}

static void humplog_write_prompt_for_mode(void)
{
  switch(g_humplog.mode)
  {
    case HUMPLOG_MODE_NEWLOG:
    case HUMPLOG_MODE_APPEND:
      humplog_write_prompt(HUMPLOG_PROMPT_RECORD);
      break;

    default:
      humplog_write_prompt(HUMPLOG_PROMPT_COMMAND);
      break;
  }
}

static void humplog_config_set_defaults(void)
{
  g_humplog_config.baud_rate = 115200U;
  g_humplog_config.escape_char = HUMPLOG_ESCAPE_CHAR;
  g_humplog_config.escape_count = HUMPLOG_ESCAPE_COUNT;
  g_humplog_config.boot_mode = HUMPLOG_BOOT_MODE_NEWLOG;
  g_humplog_config.verbose_errors = 1U;
  g_humplog_config.echo_enabled = 1U;
  g_humplog_config.ignore_rx_on_boot = 1U;
}

static uint8_t humplog_config_baud_valid(uint32_t baud_rate)
{
  return (baud_rate >= 300U && baud_rate <= 1000000U) ? 1U : 0U;
}

static uint8_t humplog_rx_line_is_low(void)
{
  return gpio_input_data_bit_read(GPIOA, GPIO_PINS_10) == RESET ? 1U : 0U;
}

static char *humplog_next_token(char **cursor)
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

static void humplog_write_decimal(uint32_t value)
{
  char buffer[16];
  snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)value);
  humplog_write_text(buffer);
}

static void humplog_write_hex_byte(uint8_t value)
{
  static const char hex[] = "0123456789ABCDEF";
  char buffer[3];

  buffer[0] = hex[(value >> 4) & 0x0F];
  buffer[1] = hex[value & 0x0F];
  buffer[2] = '\0';
  humplog_write_text(buffer);
}

static void humplog_report_error(const char *message)
{
  if(g_humplog_config.verbose_errors != 0U)
  {
    humplog_write_crlf();
    humplog_write_text(message);
  }
  else
  {
    humplog_write_text("\r\n!");
  }
}

static int8_t humplog_find_node(const char *name)
{
  if(name == NULL)
  {
    return -1;
  }

  if(humplog_text_equal(name, "/"))
  {
    return (int8_t)humplog_fs_root();
  }

  if(humplog_text_equal(name, ".."))
  {
    const humplog_fs_node_t *node = humplog_fs_node_get(g_humplog.current_dir);
    return node != NULL ? (int8_t)node->parent : -1;
  }

  return humplog_fs_find_child(g_humplog.current_dir, name);
}

static uint8_t humplog_wildcard_match(const char *pattern, const char *text)
{
  if(pattern == NULL || text == NULL)
  {
    return 0U;
  }

  if(*pattern == '\0')
  {
    return *text == '\0' ? 1U : 0U;
  }

  if(*pattern == '*')
  {
    do
    {
      if(humplog_wildcard_match(pattern + 1, text) != 0U)
      {
        return 1U;
      }
    }
    while(*text++ != '\0');
    return 0U;
  }

  if(*text == '\0')
  {
    return 0U;
  }

  if(*pattern == '?' || humplog_char_equal(*pattern, *text) != 0U)
  {
    return humplog_wildcard_match(pattern + 1, text + 1);
  }

  return 0U;
}

typedef struct
{
  uint32_t next_sequence;
} humplog_log_scan_context_t;

static void humplog_log_scan_callback(uint8_t node_id,
                                      const humplog_fs_node_t *node,
                                      void *context)
{
  humplog_log_scan_context_t *scan_context;
  char *end_ptr;
  unsigned long value;

  (void)node_id;
  scan_context = (humplog_log_scan_context_t *)context;
  if(node == NULL || scan_context == NULL || node->is_dir != 0U)
  {
    return;
  }

  if(strncmp(node->name, "LOG", 3U) != 0 || strlen(node->name) != 12U ||
     strcmp(&node->name[8], ".TXT") != 0)
  {
    return;
  }

  value = strtoul(&node->name[3], &end_ptr, 10);
  if(end_ptr != &node->name[8])
  {
    return;
  }

  if((uint32_t)(value + 1UL) > scan_context->next_sequence)
  {
    scan_context->next_sequence = (uint32_t)(value + 1UL);
  }
}

static void humplog_update_log_sequence(void)
{
  humplog_log_scan_context_t context;

  context.next_sequence = 0U;
  humplog_fs_iterate_dir(humplog_fs_root(), humplog_log_scan_callback, &context);
  g_humplog.log_sequence = context.next_sequence;
}

static uint8_t humplog_config_load(void)
{
  int8_t node_id;
  char buffer[96];
  int32_t bytes_read;
  char *line_end;
  char *field;
  uint32_t parsed[7];
  uint8_t field_count;
  uint8_t needs_save;
  uint8_t index;

  humplog_config_set_defaults();
  node_id = humplog_fs_find_child(humplog_fs_root(), HUMPLOG_CONFIG_FILE);
  if(node_id < 0)
  {
    return humplog_config_save();
  }

  bytes_read = humplog_fs_read((uint8_t)node_id, 0U, (uint8_t *)buffer, sizeof(buffer) - 1U);
  if(bytes_read <= 0)
  {
    return 0U;
  }

  buffer[bytes_read] = '\0';
  line_end = strpbrk(buffer, "\r\n");
  if(line_end != NULL)
  {
    *line_end = '\0';
  }

  field_count = 0U;
  needs_save = 0U;
  field = strtok(buffer, ",");
  while(field != NULL && field_count < 7U)
  {
    parsed[field_count++] = strtoul(field, NULL, 10);
    field = strtok(NULL, ",");
  }

  if(field_count < 4U)
  {
    return 0U;
  }

  if(!humplog_config_baud_valid(parsed[0]))
  {
    needs_save = 1U;
  }
  else
  {
    g_humplog_config.baud_rate = parsed[0];
  }

  if(parsed[1] > 255U)
  {
    needs_save = 1U;
  }
  else
  {
    g_humplog_config.escape_char = (uint8_t)parsed[1];
  }

  if(parsed[2] > 254U)
  {
    needs_save = 1U;
  }
  else
  {
    g_humplog_config.escape_count = (uint8_t)parsed[2];
  }

  if(parsed[3] > 2U)
  {
    needs_save = 1U;
  }
  else
  {
    g_humplog_config.boot_mode = (uint8_t)parsed[3];
  }

  for(index = field_count; index < 7U; ++index)
  {
    needs_save = 1U;
  }

  if(field_count >= 5U)
  {
    if(parsed[4] > 1U)
    {
      needs_save = 1U;
    }
    else
    {
      g_humplog_config.verbose_errors = (uint8_t)parsed[4];
    }
  }
  if(field_count >= 6U)
  {
    if(parsed[5] > 1U)
    {
      needs_save = 1U;
    }
    else
    {
      g_humplog_config.echo_enabled = (uint8_t)parsed[5];
    }
  }
  if(field_count >= 7U)
  {
    if(parsed[6] > 1U)
    {
      needs_save = 1U;
    }
    else
    {
      g_humplog_config.ignore_rx_on_boot = (uint8_t)parsed[6];
    }
  }

  if(needs_save != 0U)
  {
    (void)humplog_config_save();
  }
  return 1U;
}

static uint8_t humplog_config_save(void)
{
  char buffer[96];
  int8_t node_id;
  int written;

  node_id = humplog_fs_create_file(humplog_fs_root(), HUMPLOG_CONFIG_FILE, 0U);
  if(node_id < 0)
  {
    return 0U;
  }

  written = snprintf(buffer, sizeof(buffer),
                     "%lu,%u,%u,%u,%u,%u,%u\r\n"
                     "baud,escape,esc#,mode,verb,echo,ignoreRX\r\n",
                     (unsigned long)g_humplog_config.baud_rate,
                     g_humplog_config.escape_char,
                     g_humplog_config.escape_count,
                     g_humplog_config.boot_mode,
                     g_humplog_config.verbose_errors,
                     g_humplog_config.echo_enabled,
                     g_humplog_config.ignore_rx_on_boot);
  if(written <= 0)
  {
    return 0U;
  }

  if(humplog_fs_truncate((uint8_t)node_id, 0U) != 0)
  {
    return 0U;
  }

  return humplog_fs_write((uint8_t)node_id, 0U, (const uint8_t *)buffer, (uint16_t)written) == 0 ? 1U : 0U;
}

static uint8_t humplog_create_newlog_file(void)
{
  char name[13];
  uint32_t attempts;
  int8_t node_id;

  for(attempts = 0U; attempts < 100000UL; ++attempts)
  {
    snprintf(name, sizeof(name), "LOG%05lu.TXT",
             (unsigned long)g_humplog.log_sequence++);
    node_id = humplog_fs_create_file(humplog_fs_root(), name, 1U);
    if(node_id >= 0)
    {
      g_humplog.active_file = (uint8_t)node_id;
      return 1U;
    }
  }

  return 0U;
}

static uint8_t humplog_start_sequential_log(void)
{
  int8_t node_id;

  node_id = humplog_fs_create_file(humplog_fs_root(), "SEQLOG.TXT", 0U);
  if(node_id < 0)
  {
    return 0U;
  }

  humplog_stream_start((uint8_t)node_id, HUMPLOG_MODE_APPEND);
  return 1U;
}

static uint8_t humplog_stream_flush(void)
{
  uint32_t start_tick;
  uint32_t elapsed_us;
  uint32_t pending_bytes;
  uint32_t sync_start_tick;
  uint32_t sync_elapsed_us;

  if((g_humplog.mode != HUMPLOG_MODE_NEWLOG && g_humplog.mode != HUMPLOG_MODE_APPEND) ||
     (g_humplog.stream_length[0] == 0U &&
      g_humplog.stream_length[1] == 0U &&
      g_humplog.stream_length[2] == 0U))
  {
    return 1U;
  }

  pending_bytes = (uint32_t)g_humplog.stream_length[0] +
                  (uint32_t)g_humplog.stream_length[1] +
                  (uint32_t)g_humplog.stream_length[2];
  start_tick = wk_timebase_raw_tick();
  if(humplog_stream_commit_active() == 0U)
  {
    return 0U;
  }

  /* Drain every full buffer in the ring. */
  while(g_humplog.stream_drain_index != g_humplog.stream_active_index)
  {
    if(humplog_stream_flush_pending() == 0U)
    {
      return 0U;
    }
  }

  sync_start_tick = wk_timebase_raw_tick();
  if(humplog_fs_stream_sync() != 0)
  {
    return 0U;
  }
  sync_elapsed_us = wk_timebase_elapsed_us(sync_start_tick);
  g_humplog_stream_stats.sync_calls += 1U;
  g_humplog_stream_stats.sync_total_us += sync_elapsed_us;
  if(sync_elapsed_us > g_humplog_stream_stats.sync_max_us)
  {
    g_humplog_stream_stats.sync_max_us = sync_elapsed_us;
  }

  elapsed_us = wk_timebase_elapsed_us(start_tick);
  g_humplog_stream_stats.flush_calls += 1U;
  g_humplog_stream_stats.flush_bytes += pending_bytes;
  g_humplog_stream_stats.flush_total_us += elapsed_us;
  if(elapsed_us > g_humplog_stream_stats.flush_max_us)
  {
    g_humplog_stream_stats.flush_max_us = elapsed_us;
  }
  return 1U;
}

static uint8_t humplog_stream_flush_pending(void)
{
  uint8_t drain_index;
  uint16_t pending_length;
  uint32_t start_tick;
  uint32_t elapsed_us;

  if(g_humplog.stream_drain_index == g_humplog.stream_active_index)
  {
    return 1U;  /* ring empty — nothing to drain */
  }

  drain_index = g_humplog.stream_drain_index;
  pending_length = g_humplog.stream_length[drain_index];
  if(pending_length == 0U)
  {
    /* Stale slot — advance drain pointer and let caller retry. */
    g_humplog.stream_drain_index = (uint8_t)((drain_index + 1U) % HUMPLOG_STREAM_BUFFER_COUNT);
    return 1U;
  }

  start_tick = wk_timebase_raw_tick();
  if(humplog_fs_stream_write(g_humplog.stream_buffer[drain_index], pending_length) != 0)
  {
    return 0U;
  }

  elapsed_us = wk_timebase_elapsed_us(start_tick);
  g_humplog_stream_stats.write_calls += 1U;
  g_humplog_stream_stats.write_bytes += pending_length;
  g_humplog_stream_stats.write_total_us += elapsed_us;
  if(elapsed_us > g_humplog_stream_stats.write_max_us)
  {
    g_humplog_stream_stats.write_max_us = elapsed_us;
  }
  g_humplog.stream_offset += pending_length;
  g_humplog.stream_length[drain_index] = 0U;
  g_humplog.stream_drain_index = (uint8_t)((drain_index + 1U) % HUMPLOG_STREAM_BUFFER_COUNT);
  return 1U;
}

static uint8_t humplog_stream_commit_active(void)
{
  uint8_t next_write;

  if(g_humplog.stream_length[g_humplog.stream_active_index] == 0U)
  {
    return 1U;  /* nothing to commit */
  }

  next_write = (uint8_t)((g_humplog.stream_active_index + 1U) % HUMPLOG_STREAM_BUFFER_COUNT);

  /* Ring full — drain the oldest buffer to free a slot. */
  if(next_write == g_humplog.stream_drain_index)
  {
    if(humplog_stream_flush_pending() == 0U)
    {
      return 0U;
    }
  }

  g_humplog.stream_active_index = next_write;
  return 1U;
}

static void humplog_stream_start(uint8_t node_id, humplog_mode_t mode)
{
  const humplog_fs_node_t *node;

  g_humplog.active_file = node_id;
  g_humplog.mode = mode;
  g_humplog.escape_count = 0U;
  g_humplog.stream_active_index = 0U;
  g_humplog.stream_drain_index = 0U;
  g_humplog.stream_length[0] = 0U;
  g_humplog.stream_length[1] = 0U;
  g_humplog.stream_length[2] = 0U;
  g_humplog.stream_overrun_latched = 0U;
  node = humplog_fs_node_get(node_id);
  g_humplog.stream_offset = node != NULL ? node->size : 0U;
  g_humplog.stream_last_tick = wk_timebase_raw_tick();
  wk_usart1_rx_overrun_clear();
  if(humplog_fs_stream_begin(node_id, g_humplog.stream_offset) != 0)
  {
    g_humplog.mode = HUMPLOG_MODE_COMMAND;
  }

  if(g_humplog.mode == mode)
  {
    board_led_on();  /* record mode — solid LED, blink takes over when data flows */
  }
}

static void humplog_stream_stop(void)
{
  uint8_t flush_ok;

  flush_ok = humplog_stream_flush();
  if(humplog_fs_stream_end() != 0)
  {
    flush_ok = 0U;
  }

  if(flush_ok == 0U)
  {
    humplog_report_error("error: storage full");
  }
  if(wk_usart1_rx_overrun_bytes() != 0U)
  {
    g_humplog.stream_overrun_latched = 1U;
  }
  g_humplog.stream_length[0] = 0U;
  g_humplog.stream_length[1] = 0U;
  g_humplog.stream_length[2] = 0U;
}

static uint8_t humplog_stream_queue_byte(uint8_t byte)
{
  uint8_t active_index;

  active_index = g_humplog.stream_active_index;
  if(g_humplog.stream_length[active_index] >= HUMPLOG_STREAM_BUFFER_SIZE)
  {
    if(humplog_stream_commit_active() == 0U)
    {
      return 0U;
    }
    active_index = g_humplog.stream_active_index;
  }

  g_humplog.stream_buffer[active_index][g_humplog.stream_length[active_index]++] = byte;
  g_humplog.stream_last_tick = wk_timebase_raw_tick();
  board_led_fast_blink();  /* data is flowing — fast blink indicator */
  return 1U;
}

static void humplog_enter_command_mode(void)
{
  if(g_humplog.mode == HUMPLOG_MODE_NEWLOG || g_humplog.mode == HUMPLOG_MODE_APPEND)
  {
    /* Commit the active buffer so that in-flight data is preserved
       in the pending buffer.  Do NOT block on SD-card write or sync
       here — the main loop will drain the buffers asynchronously
       so the USART RX path stays responsive. */
    if(g_humplog.stream_length[g_humplog.stream_active_index] != 0U)
    {
      (void)humplog_stream_commit_active();
    }
    g_humplog.stream_drain = 1U;
  }
  g_humplog.mode = HUMPLOG_MODE_COMMAND;
  g_humplog.escape_count = 0U;
  g_humplog.line_length = 0U;
  if(g_humplog.stream_overrun_latched != 0U)
  {
    g_humplog.stream_overrun_latched = 0U;
    humplog_report_error("error: rx overrun");
  }
  board_led_off();
  humplog_write_prompt_line();
}

static void humplog_stream_drain_complete(void)
{
  if(g_humplog.stream_drain == 0U)
  {
    return;
  }

  /* Drain the ring: commit the current write buffer (if non-empty),
     then flush every pending buffer to the file system. */
  (void)humplog_stream_commit_active();
  while(g_humplog.stream_drain_index != g_humplog.stream_active_index)
  {
    if(humplog_stream_flush_pending() == 0U)
    {
      break;
    }
  }

  (void)humplog_fs_stream_sync();
  (void)humplog_fs_stream_end();
  g_humplog.stream_drain = 0U;
}

static void humplog_reset_runtime_state(void)
{
  g_humplog.current_dir = humplog_fs_root();
  g_humplog.active_file = humplog_fs_root();
  g_humplog.escape_count = 0U;
  g_humplog.stream_active_index = 0U;
  g_humplog.stream_drain_index = 0U;
  g_humplog.stream_overrun_latched = 0U;
  g_humplog.stream_drain = 0U;
  g_humplog.line_length = 0U;
  g_humplog.write_line_length = 0U;
  g_humplog.stream_length[0] = 0U;
  g_humplog.stream_length[1] = 0U;
  g_humplog.stream_length[2] = 0U;
  g_humplog.ignore_lf = 0U;
  g_humplog.write_ignore_lf = 0U;
  g_humplog.write_offset = 0U;
  g_humplog.stream_offset = 0U;
  g_humplog.stream_last_tick = wk_timebase_raw_tick();
  memset(g_humplog.line_buffer, 0, sizeof(g_humplog.line_buffer));
  memset(g_humplog.write_line_buffer, 0, sizeof(g_humplog.write_line_buffer));
}

static void humplog_stream_stats_reset(void)
{
  memset(&g_humplog_stream_stats, 0, sizeof(g_humplog_stream_stats));
}

static void humplog_command_stats(const char *arg1)
{
  uint32_t avg_us;

  if(arg1 != NULL && humplog_text_equal(arg1, "reset"))
  {
    humplog_stream_stats_reset();
    humplog_write_text("\r\nstats reset");
    return;
  }

  humplog_write_text("\r\nbuf=");
  humplog_write_decimal(HUMPLOG_STREAM_BUFFER_SIZE);
  humplog_write_text(" rxovr=");
  humplog_write_decimal(wk_usart1_rx_overrun_bytes());

  humplog_write_text("\r\nflush calls=");
  humplog_write_decimal(g_humplog_stream_stats.flush_calls);
  humplog_write_text(" bytes=");
  humplog_write_decimal(g_humplog_stream_stats.flush_bytes);
  humplog_write_text(" avg_us=");
  avg_us = g_humplog_stream_stats.flush_calls != 0U ?
           (g_humplog_stream_stats.flush_total_us / g_humplog_stream_stats.flush_calls) : 0U;
  humplog_write_decimal(avg_us);
  humplog_write_text(" max_us=");
  humplog_write_decimal(g_humplog_stream_stats.flush_max_us);

  humplog_write_text("\r\nwrite calls=");
  humplog_write_decimal(g_humplog_stream_stats.write_calls);
  humplog_write_text(" bytes=");
  humplog_write_decimal(g_humplog_stream_stats.write_bytes);
  humplog_write_text(" avg_us=");
  avg_us = g_humplog_stream_stats.write_calls != 0U ?
           (g_humplog_stream_stats.write_total_us / g_humplog_stream_stats.write_calls) : 0U;
  humplog_write_decimal(avg_us);
  humplog_write_text(" max_us=");
  humplog_write_decimal(g_humplog_stream_stats.write_max_us);

  humplog_write_text("\r\nsync calls=");
  humplog_write_decimal(g_humplog_stream_stats.sync_calls);
  humplog_write_text(" avg_us=");
  avg_us = g_humplog_stream_stats.sync_calls != 0U ?
           (g_humplog_stream_stats.sync_total_us / g_humplog_stream_stats.sync_calls) : 0U;
  humplog_write_decimal(avg_us);
  humplog_write_text(" max_us=");
  humplog_write_decimal(g_humplog_stream_stats.sync_max_us);
}

static uint8_t humplog_boot_mode_enter(uint8_t write_prompt)
{
  humplog_update_log_sequence();
  humplog_reset_runtime_state();

  switch(g_humplog_config.boot_mode)
  {
    case HUMPLOG_BOOT_MODE_NEWLOG:
      if(humplog_create_newlog_file() == 0U)
      {
        g_humplog.mode = HUMPLOG_MODE_COMMAND;
        if(write_prompt != 0U)
        {
          g_humplog.mode = HUMPLOG_MODE_COMMAND;
          humplog_write_prompt_for_mode();
          humplog_report_error("error: cannot create log file");
          humplog_write_prompt_line();
        }
        return 0U;
      }
      humplog_stream_start(g_humplog.active_file, HUMPLOG_MODE_NEWLOG);
      if(g_humplog.mode == HUMPLOG_MODE_COMMAND)
      {
        if(write_prompt != 0U)
        {
          humplog_write_prompt_for_mode();
          humplog_report_error("error: cannot open log stream");
          humplog_write_prompt_line();
        }
        return 0U;
      }
      if(write_prompt != 0U)
      {
        humplog_write_prompt(HUMPLOG_PROMPT_RECORD);
      }
      return 1U;

    case HUMPLOG_BOOT_MODE_SEQLOG:
      if(humplog_start_sequential_log() == 0U)
      {
        g_humplog.mode = HUMPLOG_MODE_COMMAND;
        if(write_prompt != 0U)
        {
          g_humplog.mode = HUMPLOG_MODE_COMMAND;
          humplog_write_prompt_for_mode();
          humplog_report_error("error: cannot open SEQLOG.TXT");
          humplog_write_prompt_line();
        }
        return 0U;
      }
      if(write_prompt != 0U)
      {
        humplog_write_prompt(HUMPLOG_PROMPT_RECORD);
      }
      return 1U;

    case HUMPLOG_BOOT_MODE_COMMAND:
    default:
      g_humplog.mode = HUMPLOG_MODE_COMMAND;
      if(write_prompt != 0U)
      {
        humplog_write_prompt(HUMPLOG_PROMPT_COMMAND);
      }
      return 1U;
  }
}

static uint8_t humplog_reinitialize(uint8_t write_prompt, uint8_t force_command_mode)
{
  humplog_fs_init();
  if(humplog_config_load() == 0U)
  {
    humplog_config_set_defaults();
    (void)humplog_config_save();
  }
  if(g_humplog_config.baud_rate != 115200U)
  {
    wk_usart1_set_baud(g_humplog_config.baud_rate);
  }
  else
  {
    wk_usart1_set_baud(115200U);
  }
  wk_usart1_discard_rx();
  humplog_update_log_sequence();
  if(force_command_mode != 0U)
  {
    g_humplog.mode = HUMPLOG_MODE_COMMAND;
    humplog_reset_runtime_state();
    if(write_prompt != 0U)
    {
      humplog_write_prompt(HUMPLOG_PROMPT_COMMAND);
    }
    return 1U;
  }
  return humplog_boot_mode_enter(write_prompt);
}

static void humplog_handle_stream_byte(uint8_t byte)
{
  if(g_humplog_config.escape_count != 0U && byte == g_humplog_config.escape_char)
  {
    ++g_humplog.escape_count;
    if(g_humplog.escape_count >= g_humplog_config.escape_count)
    {
      humplog_enter_command_mode();
    }
    return;
  }

  while(g_humplog.escape_count > 0U)
  {
    uint8_t escaped_byte;

    escaped_byte = g_humplog_config.escape_char;
    if(humplog_stream_queue_byte(escaped_byte) == 0U)
    {
      humplog_report_error("error: storage full");
      humplog_enter_command_mode();
      return;
    }
    --g_humplog.escape_count;
  }

  if(humplog_stream_queue_byte(byte) == 0U)
  {
    humplog_report_error("error: storage full");
    humplog_enter_command_mode();
  }
}

static void humplog_print_help(void)
{
  humplog_write_text("\r\nnew append write rm size read cat ls md cd sync stats reset init disk baud set verbose ?");
  humplog_write_text("\r\necho on|off, verbose on|off, rm/ls support * and ?, set 3=resetlog, stats reset");
}

static void humplog_print_path(void)
{
  uint8_t chain[HUMPLOG_FS_MAX_NODES];
  uint8_t count;
  uint8_t cursor;
  const humplog_fs_node_t *node;

  if(g_humplog.current_dir == humplog_fs_root())
  {
    humplog_write_text("\\");
    return;
  }

  count = 0U;
  cursor = g_humplog.current_dir;
  node = humplog_fs_node_get(cursor);
  while(node != NULL && cursor != humplog_fs_root() && count < HUMPLOG_FS_MAX_NODES)
  {
    chain[count++] = cursor;
    cursor = node->parent;
    node = humplog_fs_node_get(cursor);
  }

  humplog_write_text("\\");
  while(count > 0U)
  {
    node = humplog_fs_node_get(chain[--count]);
    if(node != NULL)
    {
      humplog_write_text(node->name);
      if(count > 0U)
      {
        humplog_write_text("\\");
      }
    }
  }
}

typedef struct
{
  const char *pattern;
  uint8_t found;
} humplog_ls_context_t;

typedef struct
{
  const char *pattern;
  uint8_t recursive;
  uint8_t deleted_any;
  uint8_t failed;
} humplog_rm_context_t;

static void humplog_ls_iterate_callback(uint8_t node_id,
                                        const humplog_fs_node_t *node,
                                        void *context)
{
  humplog_ls_context_t *ls_context;

  (void)node_id;
  ls_context = (humplog_ls_context_t *)context;
  if(node == NULL)
  {
    return;
  }

  if(ls_context->pattern != NULL &&
     humplog_wildcard_match(ls_context->pattern, node->name) == 0U)
  {
    return;
  }

  humplog_write_crlf();
  if(node->is_dir != 0U)
  {
    humplog_write_text("\\");
  }
  humplog_write_text(node->name);
  ls_context->found = 1U;
}

static void humplog_rm_iterate_callback(uint8_t node_id,
                                        const humplog_fs_node_t *node,
                                        void *context)
{
  humplog_rm_context_t *rm_context;
  int8_t target_id;

  rm_context = (humplog_rm_context_t *)context;
  if(node == NULL || rm_context == NULL)
  {
    return;
  }

  if(humplog_wildcard_match(rm_context->pattern, node->name) == 0U)
  {
    return;
  }

  target_id = node_id != 0xFFU ? (int8_t)node_id : humplog_find_node(node->name);
  if(target_id < 0 || humplog_fs_delete((uint8_t)target_id, rm_context->recursive) != 0)
  {
    rm_context->failed = 1U;
    return;
  }
  rm_context->deleted_any = 1U;
}

static void humplog_command_ls(const char *pattern)
{
  humplog_ls_context_t context;

  context.pattern = pattern;
  context.found = 0U;
  humplog_fs_iterate_dir(g_humplog.current_dir, humplog_ls_iterate_callback, &context);
  if(context.found == 0U)
  {
    humplog_write_text("\r\n<empty>");
  }
}

static void humplog_command_read(uint8_t node_id, uint32_t start, uint32_t length, uint32_t type)
{
  const humplog_fs_node_t *node;
  uint8_t byte_buffer[16];
  int32_t bytes_read;
  uint32_t index;
  uint32_t end;
  uint32_t remaining;

  node = humplog_fs_node_get(node_id);
  if(node == NULL || node->is_dir != 0U)
  {
    humplog_report_error("error: not a file");
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

  humplog_write_crlf();
  if(type == 2U)
  {
    remaining = end - start;
    while(remaining > 0U)
    {
      bytes_read = humplog_fs_read(node_id,
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
          humplog_write_text(" ");
        }
        humplog_write_hex_byte(byte_buffer[index]);
      }
      remaining -= (uint32_t)bytes_read;
    }
    return;
  }

  remaining = end - start;
  while(remaining > 0U)
  {
    bytes_read = humplog_fs_read(node_id,
                                 start + (end - start - remaining),
                                 byte_buffer,
                                 remaining > sizeof(byte_buffer) ? sizeof(byte_buffer) : (uint16_t)remaining);
    if(bytes_read <= 0)
    {
      return;
    }
    humplog_write_bytes(byte_buffer, (uint16_t)bytes_read);
    remaining -= (uint32_t)bytes_read;
  }
}

static void humplog_command_cat(uint8_t node_id)
{
  const humplog_fs_node_t *node;
  uint8_t data_byte;
  int32_t bytes_read;
  uint32_t index;

  node = humplog_fs_node_get(node_id);
  if(node == NULL || node->is_dir != 0U)
  {
    humplog_report_error("error: not a file");
    return;
  }

  for(index = 0U; index < node->size; ++index)
  {
    if((index % 8U) == 0U)
    {
      humplog_write_crlf();
      humplog_write_hex_byte((uint8_t)(index >> 8));
      humplog_write_hex_byte((uint8_t)(index & 0xFFU));
      humplog_write_text(": ");
    }
    else
    {
      humplog_write_text(" ");
    }
    bytes_read = humplog_fs_read(node_id, index, &data_byte, 1U);
    if(bytes_read != 1)
    {
      return;
    }
    humplog_write_hex_byte(data_byte);
  }
}

static void humplog_start_append(const char *name)
{
  int8_t node_id;

  node_id = humplog_fs_create_file(g_humplog.current_dir, name, 0U);
  if(node_id < 0)
  {
    humplog_report_error("error: cannot open file");
    return;
  }

  humplog_stream_start((uint8_t)node_id, HUMPLOG_MODE_APPEND);
}

static void humplog_start_write(const char *name, uint32_t offset)
{
  int8_t node_id;

  node_id = humplog_fs_create_file(g_humplog.current_dir, name, 0U);
  if(node_id < 0)
  {
    humplog_report_error("error: cannot open file");
    return;
  }

  g_humplog.active_file = (uint8_t)node_id;
  g_humplog.write_offset = offset;
  g_humplog.write_line_length = 0U;
  g_humplog.write_ignore_lf = 0U;
  g_humplog.mode = HUMPLOG_MODE_WRITE;
  humplog_write_text("\r\nwrite mode, empty line exits");
}

static int8_t humplog_parse_on_off(const char *value)
{
  if(value == NULL)
  {
    return -1;
  }
  if(humplog_text_equal(value, "on"))
  {
    return 1;
  }
  if(humplog_text_equal(value, "off"))
  {
    return 0;
  }
  return -1;
}

static void humplog_handle_menu_line(char *line)
{
  uint32_t baud_rate;

  if(g_humplog.mode == HUMPLOG_MODE_BAUD_MENU)
  {
    if(humplog_text_equal(line, "x") || humplog_text_equal(line, "q"))
    {
      g_humplog.mode = HUMPLOG_MODE_COMMAND;
      humplog_write_prompt_line();
      return;
    }

    baud_rate = strtoul(line, NULL, 10);
    if(!humplog_config_baud_valid(baud_rate))
    {
      humplog_report_error("error: invalid baud");
      g_humplog.mode = HUMPLOG_MODE_COMMAND;
      humplog_write_prompt_line();
      return;
    }

    g_humplog_config.baud_rate = baud_rate;
    if(humplog_config_save() == 0U)
    {
      humplog_report_error("error: cannot save config");
      g_humplog.mode = HUMPLOG_MODE_COMMAND;
      humplog_write_prompt_line();
      return;
    }

    humplog_write_text("\r\nswitching baud");
    wk_usart1_flush();
    wk_delay_ms(20U);
    wk_usart1_set_baud(baud_rate);
    wk_usart1_discard_rx();
    g_humplog.mode = HUMPLOG_MODE_COMMAND;
    humplog_write_prompt_line();
    return;
  }

  if(g_humplog.mode == HUMPLOG_MODE_SET_MENU)
  {
    if(humplog_text_equal(line, "x") || humplog_text_equal(line, "q"))
    {
      g_humplog.mode = HUMPLOG_MODE_COMMAND;
      humplog_write_prompt_line();
      return;
    }

    if(humplog_text_equal(line, "0") || humplog_text_equal(line, "newlog"))
    {
      g_humplog_config.boot_mode = HUMPLOG_BOOT_MODE_NEWLOG;
    }
    else if(humplog_text_equal(line, "1") || humplog_text_equal(line, "sequential") || humplog_text_equal(line, "seqlog"))
    {
      g_humplog_config.boot_mode = HUMPLOG_BOOT_MODE_SEQLOG;
    }
    else if(humplog_text_equal(line, "2") || humplog_text_equal(line, "command"))
    {
      g_humplog_config.boot_mode = HUMPLOG_BOOT_MODE_COMMAND;
    }
    else if(humplog_text_equal(line, "3") || humplog_text_equal(line, "resetlog"))
    {
      g_humplog.log_sequence = 0U;
      humplog_write_text("\r\nlog number reset");
      g_humplog.mode = HUMPLOG_MODE_COMMAND;
      humplog_write_prompt_line();
      return;
    }
    else
    {
      humplog_report_error("error: invalid mode");
      g_humplog.mode = HUMPLOG_MODE_COMMAND;
      humplog_write_prompt_line();
      return;
    }

    if(humplog_config_save() == 0U)
    {
      humplog_report_error("error: cannot save config");
    }
    else
    {
      humplog_write_text("\r\nmode saved");
    }
    g_humplog.mode = HUMPLOG_MODE_COMMAND;
    humplog_write_prompt_line();
  }
}

static void humplog_process_command(char *line)
{
  char *cursor;
  char *command;
  char *arg1;
  char *arg2;
  char *arg3;
  char *arg4;
  int8_t node_id;

  cursor = line;
  command = humplog_next_token(&cursor);
  if(command == NULL)
  {
    humplog_write_prompt_line();
    return;
  }

  arg1 = humplog_next_token(&cursor);
  arg2 = humplog_next_token(&cursor);
  arg3 = humplog_next_token(&cursor);
  arg4 = humplog_next_token(&cursor);

  /* Finish any pending async drain before serving the command so
     that size / read / cat see accurate file state and a new
     append / new / write starts from a clean fs stream. */
  humplog_stream_drain_complete();

  if(humplog_text_equal(command, "?"))
  {
    humplog_print_help();
  }
  else if(humplog_text_equal(command, "new"))
  {
    if(arg1 == NULL)
    {
      humplog_report_error("error: missing file");
    }
    else if(humplog_fs_create_file(g_humplog.current_dir, arg1, 1U) < 0)
    {
      humplog_report_error("error: cannot create file");
    }
  }
  else if(humplog_text_equal(command, "append"))
  {
    if(arg1 == NULL)
    {
      humplog_report_error("error: missing file");
    }
    else
    {
      humplog_start_append(arg1);
      if(g_humplog.mode == HUMPLOG_MODE_COMMAND)
      {
        humplog_write_prompt_line();
      }
      return;
    }
  }
  else if(humplog_text_equal(command, "write"))
  {
    uint32_t offset = 0U;

    if(arg1 == NULL)
    {
      humplog_report_error("error: missing file");
    }
    else
    {
      if(arg2 != NULL)
      {
        offset = strtoul(arg2, NULL, 10);
      }
      humplog_start_write(arg1, offset);
      return;
    }
  }
  else if(humplog_text_equal(command, "rm"))
  {
    uint8_t recursive = 0U;

    if(arg1 != NULL && humplog_text_equal(arg1, "-rf"))
    {
      recursive = 1U;
      arg1 = arg2;
    }

    if(arg1 == NULL)
    {
      humplog_report_error("error: missing target");
    }
    else if(strchr(arg1, '*') != NULL || strchr(arg1, '?') != NULL)
    {
      humplog_rm_context_t context;

      context.pattern = arg1;
      context.recursive = recursive;
      context.deleted_any = 0U;
      context.failed = 0U;
      humplog_fs_iterate_dir(g_humplog.current_dir, humplog_rm_iterate_callback, &context);
      if(context.deleted_any == 0U || context.failed != 0U)
      {
        humplog_report_error("error: delete failed");
      }
    }
    else
    {
      node_id = humplog_find_node(arg1);
      if(node_id < 0 || humplog_fs_delete((uint8_t)node_id, recursive) != 0)
      {
        humplog_report_error("error: delete failed");
      }
    }
  }
  else if(humplog_text_equal(command, "size"))
  {
    if(arg1 == NULL)
    {
      humplog_report_error("error: missing file");
    }
    else
    {
      const humplog_fs_node_t *node;
      node_id = humplog_find_node(arg1);
      node = node_id >= 0 ? humplog_fs_node_get((uint8_t)node_id) : NULL;
      if(node == NULL || node->is_dir != 0U)
      {
        humplog_report_error("error: not a file");
      }
      else
      {
        humplog_write_crlf();
        humplog_write_decimal(node->size);
      }
    }
  }
  else if(humplog_text_equal(command, "read"))
  {
    uint32_t start = 0U;
    uint32_t length = 0U;
    uint32_t type = 1U;

    if(arg1 == NULL)
    {
      humplog_report_error("error: missing file");
    }
    else
    {
      node_id = humplog_find_node(arg1);
      if(node_id < 0)
      {
        humplog_report_error("error: file not found");
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
        humplog_command_read((uint8_t)node_id, start, length, type);
      }
    }
  }
  else if(humplog_text_equal(command, "cat"))
  {
    if(arg1 == NULL)
    {
      humplog_report_error("error: missing file");
    }
    else
    {
      node_id = humplog_find_node(arg1);
      if(node_id < 0)
      {
        humplog_report_error("error: file not found");
      }
      else
      {
        humplog_command_cat((uint8_t)node_id);
      }
    }
  }
  else if(humplog_text_equal(command, "ls"))
  {
    humplog_command_ls(arg1);
  }
  else if(humplog_text_equal(command, "md"))
  {
    if(arg1 == NULL || humplog_fs_create_dir(g_humplog.current_dir, arg1) < 0)
    {
      humplog_report_error("error: cannot create directory");
    }
  }
  else if(humplog_text_equal(command, "cd"))
  {
    if(arg1 == NULL)
    {
      humplog_write_crlf();
      humplog_print_path();
    }
    else
    {
      node_id = humplog_find_node(arg1);
      if(node_id < 0)
      {
        humplog_report_error("error: directory not found");
      }
      else if(humplog_fs_node_get((uint8_t)node_id)->is_dir == 0U)
      {
        humplog_report_error("error: not a directory");
      }
      else
      {
        g_humplog.current_dir = (uint8_t)node_id;
      }
    }
  }
  else if(humplog_text_equal(command, "sync"))
  {
    if(humplog_stream_flush() == 0U)
    {
      humplog_report_error("error: storage full");
    }
    else
    {
      humplog_write_text("\r\nsynced");
    }
  }
  else if(humplog_text_equal(command, "stats"))
  {
    humplog_command_stats(arg1);
  }
  else if(humplog_text_equal(command, "baud"))
  {
    if(arg1 != NULL)
    {
      char baud_line[16];

      snprintf(baud_line, sizeof(baud_line), "%s", arg1);
      g_humplog.mode = HUMPLOG_MODE_BAUD_MENU;
      humplog_handle_menu_line(baud_line);
      return;
    }

    g_humplog.mode = HUMPLOG_MODE_BAUD_MENU;
    humplog_write_text("\r\nenter baud rate, x to exit");
    humplog_write_prompt_line();
    return;
  }
  else if(humplog_text_equal(command, "set"))
  {
    if(arg1 != NULL)
    {
      char set_line[16];

      snprintf(set_line, sizeof(set_line), "%s", arg1);
      g_humplog.mode = HUMPLOG_MODE_SET_MENU;
      humplog_handle_menu_line(set_line);
      return;
    }

    g_humplog.mode = HUMPLOG_MODE_SET_MENU;
    humplog_write_text("\r\n0 newlog, 1 seqlog, 2 command, 3 resetlog, x exit");
    humplog_write_prompt_line();
    return;
  }
  else if(humplog_text_equal(command, "verbose"))
  {
    int8_t state;

    state = humplog_parse_on_off(arg1);
    if(state < 0)
    {
      humplog_report_error("error: usage verbose on|off");
    }
    else
    {
      g_humplog_config.verbose_errors = (uint8_t)state;
      if(humplog_config_save() == 0U)
      {
        humplog_report_error("error: cannot save config");
      }
    }
  }
  else if(humplog_text_equal(command, "echo"))
  {
    int8_t state;

    state = humplog_parse_on_off(arg1);
    if(state < 0)
    {
      humplog_report_error("error: usage echo on|off");
    }
    else
    {
      g_humplog_config.echo_enabled = (uint8_t)state;
      if(humplog_config_save() == 0U)
      {
        humplog_report_error("error: cannot save config");
      }
    }
  }
  else if(humplog_text_equal(command, "init"))
  {
    humplog_stream_stop();
    wk_usart1_write_string("\r\nreinitializing\r\n");
    wk_usart1_flush();
    wk_delay_ms(20U);
    (void)humplog_reinitialize(1U, 1U);
    return;
  }
  else if(humplog_text_equal(command, "reset"))
  {
    humplog_stream_stop();
    humplog_write_text("\r\nresetting");
    wk_delay_ms(10U);
    NVIC_SystemReset();
  }
  else if(humplog_text_equal(command, "disk"))
  {
    sd_spi_card_info_t card_info;

    humplog_write_crlf();
    if(sd_spi_get_card_info(&card_info) == 0U)
    {
      humplog_write_text("disk info unavailable");
    }
    else
    {
      humplog_write_text("MID:");
      humplog_write_hex_byte(card_info.manufacturer_id);
      humplog_write_text(" OID:");
      humplog_write_text(card_info.oem_id);
      humplog_write_text(" PNM:");
      humplog_write_text(card_info.product_name);
      humplog_write_text(" PRV:");
      humplog_write_decimal(card_info.product_revision_major);
      humplog_write_text(".");
      humplog_write_decimal(card_info.product_revision_minor);
      humplog_write_text(" PSN:");
      humplog_write_decimal(card_info.serial_number);
      humplog_write_text(" MDT:");
      humplog_write_decimal(card_info.manufacture_year);
      humplog_write_text("/");
      humplog_write_decimal(card_info.manufacture_month);
      humplog_write_text(" SIZE:");
      humplog_write_decimal((card_info.sector_count / 2048U));
      humplog_write_text("MB");
    }
  }
  else
  {
    humplog_report_error("error: unknown command");
  }

  humplog_write_prompt_line();
}

static void humplog_handle_command_byte(uint8_t byte)
{
  if(byte == '\r')
  {
    g_humplog.ignore_lf = 1U;
    if(g_humplog_config.echo_enabled != 0U)
    {
      wk_usart1_write_byte('\r');
      wk_usart1_write_byte('\n');
    }
    g_humplog.line_buffer[g_humplog.line_length] = '\0';
    if(g_humplog.mode == HUMPLOG_MODE_COMMAND)
    {
      humplog_process_command(g_humplog.line_buffer);
    }
    else
    {
      humplog_handle_menu_line(g_humplog.line_buffer);
    }
    g_humplog.line_length = 0U;
    return;
  }

  if(byte == '\n')
  {
    if(g_humplog.ignore_lf != 0U)
    {
      g_humplog.ignore_lf = 0U;
      return;
    }
    if(g_humplog_config.echo_enabled != 0U)
    {
      wk_usart1_write_byte('\r');
      wk_usart1_write_byte('\n');
    }
    g_humplog.line_buffer[g_humplog.line_length] = '\0';
    if(g_humplog.mode == HUMPLOG_MODE_COMMAND)
    {
      humplog_process_command(g_humplog.line_buffer);
    }
    else
    {
      humplog_handle_menu_line(g_humplog.line_buffer);
    }
    g_humplog.line_length = 0U;
    return;
  }

  g_humplog.ignore_lf = 0U;
  if(byte == '\b' || byte == 0x7FU)
  {
    if(g_humplog.line_length > 0U)
    {
      --g_humplog.line_length;
      if(g_humplog_config.echo_enabled != 0U)
      {
        humplog_write_text("\b \b");
      }
    }
    return;
  }

  if(g_humplog.line_length < HUMPLOG_LINE_BUFFER_SIZE)
  {
    g_humplog.line_buffer[g_humplog.line_length++] = (char)byte;
    if(g_humplog_config.echo_enabled != 0U)
    {
      wk_usart1_write_byte(byte);
    }
  }
}

static void humplog_handle_write_byte(uint8_t byte)
{
  if(byte == '\r')
  {
    g_humplog.write_ignore_lf = 1U;
    wk_usart1_write_byte('\r');
    wk_usart1_write_byte('\n');
    if(g_humplog.write_line_length == 0U)
    {
      g_humplog.mode = HUMPLOG_MODE_COMMAND;
      humplog_write_prompt_line();
      return;
    }
    if(humplog_fs_write(g_humplog.active_file, g_humplog.write_offset,
                        (const uint8_t *)g_humplog.write_line_buffer,
                        g_humplog.write_line_length) != 0 ||
       humplog_fs_write(g_humplog.active_file,
                        g_humplog.write_offset + g_humplog.write_line_length,
                        (const uint8_t *)"\n", 1U) != 0)
    {
      humplog_report_error("error: storage full");
      g_humplog.mode = HUMPLOG_MODE_COMMAND;
      humplog_write_prompt_line();
      return;
    }
    g_humplog.write_offset = g_humplog.write_offset + g_humplog.write_line_length + 1U;
    g_humplog.write_line_length = 0U;
    return;
  }

  if(byte == '\n')
  {
    if(g_humplog.write_ignore_lf != 0U)
    {
      g_humplog.write_ignore_lf = 0U;
      return;
    }
    wk_usart1_write_byte('\r');
    wk_usart1_write_byte('\n');
    if(g_humplog.write_line_length == 0U)
    {
      g_humplog.mode = HUMPLOG_MODE_COMMAND;
      humplog_write_prompt_line();
    }
    return;
  }

  g_humplog.write_ignore_lf = 0U;
  if(byte == '\b' || byte == 0x7FU)
  {
    if(g_humplog.write_line_length > 0U)
    {
      --g_humplog.write_line_length;
      humplog_write_text("\b \b");
    }
    return;
  }

  if(g_humplog.write_line_length < HUMPLOG_WRITE_LINE_BUFFER_SIZE)
  {
    g_humplog.write_line_buffer[g_humplog.write_line_length++] = (char)byte;
    wk_usart1_write_byte(byte);
  }
}

void humplog_init(void)
{
  memset(&g_humplog, 0, sizeof(g_humplog));
  humplog_stream_stats_reset();
  humplog_config_set_defaults();
  humplog_fs_init();
  if(humplog_config_load() == 0U)
  {
    humplog_config_set_defaults();
    (void)humplog_config_save();
  }
  if(g_humplog_config.ignore_rx_on_boot == 0U && humplog_rx_line_is_low() != 0U)
  {
    humplog_config_set_defaults();
    (void)humplog_config_save();
  }
  wk_usart1_set_baud(g_humplog_config.baud_rate);
  (void)humplog_boot_mode_enter(1U);
}

void humplog_process(void)
{
  while(wk_usart1_readable() != 0U)
  {
    uint8_t byte = wk_usart1_read_byte();

    switch(g_humplog.mode)
    {
      case HUMPLOG_MODE_NEWLOG:
      case HUMPLOG_MODE_APPEND:
        humplog_handle_stream_byte(byte);
        break;

      case HUMPLOG_MODE_COMMAND:
      case HUMPLOG_MODE_BAUD_MENU:
      case HUMPLOG_MODE_SET_MENU:
        humplog_handle_command_byte(byte);
        break;

      case HUMPLOG_MODE_WRITE:
        humplog_handle_write_byte(byte);
        break;

      default:
        g_humplog.mode = HUMPLOG_MODE_COMMAND;
        humplog_write_prompt_line();
        break;
    }
  }

  if((g_humplog.mode == HUMPLOG_MODE_NEWLOG || g_humplog.mode == HUMPLOG_MODE_APPEND) &&
     wk_usart1_rx_overrun_bytes() != 0U)
  {
    g_humplog.stream_overrun_latched = 1U;
  }

  if((g_humplog.mode == HUMPLOG_MODE_NEWLOG || g_humplog.mode == HUMPLOG_MODE_APPEND) &&
     wk_usart1_readable() == 0U)
  {
    if(humplog_stream_flush_pending() == 0U)
    {
      humplog_report_error("error: storage full");
      humplog_enter_command_mode();
      return;
    }
  }

  if((g_humplog.mode == HUMPLOG_MODE_NEWLOG || g_humplog.mode == HUMPLOG_MODE_APPEND) &&
     (g_humplog.stream_length[0] != 0U || g_humplog.stream_length[1] != 0U || g_humplog.stream_length[2] != 0U) &&
     wk_timebase_elapsed_us(g_humplog.stream_last_tick) >= HUMPLOG_STREAM_FLUSH_IDLE_US)
  {
    if(humplog_stream_flush() == 0U)
    {
      humplog_report_error("error: storage full");
      humplog_enter_command_mode();
    }
    else
    {
      board_led_on();  /* idle → solid LED until data resumes */
    }
  }

  /* Async stream drain — keep flushing pending buffers after ESCAPE
     has already returned the command prompt.  The fs stream stays open
     until every byte has been written and synced. */
  if(g_humplog.stream_drain != 0U)
  {
    if(g_humplog.stream_length[0] != 0U || g_humplog.stream_length[1] != 0U || g_humplog.stream_length[2] != 0U)
    {
      if(humplog_stream_flush_pending() == 0U)
      {
        humplog_report_error("error: storage full");
        g_humplog.stream_drain = 0U;
        (void)humplog_fs_stream_end();
      }
    }
    else
    {
      (void)humplog_fs_stream_sync();
      (void)humplog_fs_stream_end();
      g_humplog.stream_drain = 0U;
    }
  }
}

uint8_t humplog_record_active(void)
{
  return (g_humplog.mode == HUMPLOG_MODE_NEWLOG || g_humplog.mode == HUMPLOG_MODE_APPEND) ? 1U : 0U;
}
