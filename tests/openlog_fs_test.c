#include "openlog_fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *message)
{
  fprintf(stderr, "FAIL: %s\n", message);
  exit(1);
}

static void expect_true(int condition, const char *message)
{
  if(!condition)
  {
    fail(message);
  }
}

static void expect_int(int actual, int expected, const char *message)
{
  if(actual != expected)
  {
    fprintf(stderr, "FAIL: %s (actual=%d expected=%d)\n", message, actual, expected);
    exit(1);
  }
}

static void expect_mem(const void *actual, const void *expected, size_t length, const char *message)
{
  if(memcmp(actual, expected, length) != 0)
  {
    fail(message);
  }
}

static void test_init_and_root(void)
{
  const openlog_fs_node_t *root;

  openlog_fs_init();
  expect_int(openlog_fs_root(), 0, "root id should be zero");
  expect_true(openlog_fs_node_valid(openlog_fs_root()) != 0, "root should be valid");

  root = openlog_fs_node_get(openlog_fs_root());
  expect_true(root != NULL, "root node should exist");
  expect_true(root->is_dir != 0, "root should be directory");
  expect_int(root->parent, openlog_fs_root(), "root parent should be itself");
  expect_int(openlog_fs_used_bytes(), 0, "used bytes after init");
}

static void test_name_validation(void)
{
  expect_true(openlog_fs_name_valid("LOG00001.TXT") != 0, "8.3-ish file name should pass");
  expect_true(openlog_fs_name_valid("dir_01") != 0, "simple directory name should pass");
  expect_true(openlog_fs_name_valid("") == 0, "empty name should fail");
  expect_true(openlog_fs_name_valid("TOO.LONG.EXT") == 0, "multiple dots should fail");
  expect_true(openlog_fs_name_valid("bad/name") == 0, "slash should fail");
}

static void test_create_and_find(void)
{
  int8_t dir_id;
  int8_t file_id;

  openlog_fs_init();
  dir_id = openlog_fs_create_dir(openlog_fs_root(), "LOGS");
  expect_true(dir_id >= 0, "directory creation should succeed");

  file_id = openlog_fs_create_file(openlog_fs_root(), "TEST.TXT", 1U);
  expect_true(file_id >= 0, "file creation should succeed");
  expect_int(openlog_fs_find_child(openlog_fs_root(), "logs"), dir_id, "find child should ignore case");
  expect_int(openlog_fs_find_child(openlog_fs_root(), "test.txt"), file_id, "file lookup should ignore case");
  expect_int(openlog_fs_create_file(openlog_fs_root(), "TEST.TXT", 1U), -1, "duplicate create should fail");
}

static void test_write_append_and_sparse(void)
{
  static const uint8_t hello[] = "HELLO";
  static const uint8_t world[] = " WORLD";
  static const uint8_t expected[] = {'H', 'E', 'L', 'L', 'O', 0x00, 0x00, ' ', 'W', 'O', 'R', 'L', 'D'};
  const openlog_fs_node_t *node;
  const uint8_t *data;
  int8_t file_id;
  uint16_t previous_size;

  openlog_fs_init();
  file_id = openlog_fs_create_file(openlog_fs_root(), "A.TXT", 1U);
  expect_true(file_id >= 0, "test file should be created");

  expect_int(openlog_fs_write((uint8_t)file_id, 0U, hello, sizeof(hello) - 1U), 0, "initial write should pass");
  expect_int(openlog_fs_write((uint8_t)file_id, 7U, world, sizeof(world) - 1U), 0, "sparse write should pass");
  node = openlog_fs_node_get((uint8_t)file_id);
  expect_true(node != NULL, "node should exist after writes");
  expect_int(node->size, (int)sizeof(expected), "sparse write should extend size");
  data = openlog_fs_data((uint8_t)file_id);
  expect_mem(data, expected, sizeof(expected), "file contents should match expected sparse layout");

  previous_size = node->size;
  expect_int(openlog_fs_append((uint8_t)file_id, (const uint8_t *)"!", 1U), 0, "append should pass");
  data = openlog_fs_data((uint8_t)file_id);
  expect_int(data[previous_size], '!', "append should write at end");
}

static void test_truncate_and_delete(void)
{
  int8_t dir_id;
  int8_t child_id;
  int8_t file_id;
  const openlog_fs_node_t *node;

  openlog_fs_init();
  file_id = openlog_fs_create_file(openlog_fs_root(), "TRUNC.TXT", 1U);
  expect_true(file_id >= 0, "truncate file should be created");
  expect_int(openlog_fs_append((uint8_t)file_id, (const uint8_t *)"abcdef", 6U), 0, "append before truncate");
  expect_int(openlog_fs_truncate((uint8_t)file_id, 3U), 0, "truncate should succeed");
  node = openlog_fs_node_get((uint8_t)file_id);
  expect_int(node->size, 3, "truncate should shrink file");
  expect_mem(openlog_fs_data((uint8_t)file_id), "abc", 3U, "truncated prefix should stay intact");

  dir_id = openlog_fs_create_dir(openlog_fs_root(), "DIR1");
  expect_true(dir_id >= 0, "directory for delete test should be created");
  child_id = openlog_fs_create_file((uint8_t)dir_id, "CHILD.TXT", 1U);
  expect_true(child_id >= 0, "child file should be created");
  expect_int(openlog_fs_delete((uint8_t)dir_id, 0U), -1, "non-recursive delete of non-empty dir should fail");
  expect_int(openlog_fs_delete((uint8_t)dir_id, 1U), 0, "recursive delete should succeed");
  expect_true(openlog_fs_node_valid((uint8_t)dir_id) == 0, "deleted dir should be invalid");
  expect_true(openlog_fs_node_valid((uint8_t)child_id) == 0, "deleted child should be invalid");
}

static void test_limits(void)
{
  int8_t file_id;

  openlog_fs_init();
  file_id = openlog_fs_create_file(openlog_fs_root(), "LIMIT.TXT", 1U);
  expect_true(file_id >= 0, "limit file should be created");
  expect_int(openlog_fs_capacity((uint8_t)file_id), OPENLOG_FS_FILE_CAPACITY, "reported file capacity");
  expect_int(openlog_fs_write((uint8_t)file_id, OPENLOG_FS_FILE_CAPACITY, NULL, 0U), 0, "zero-length write at end should pass");
  expect_int(openlog_fs_write((uint8_t)file_id, OPENLOG_FS_FILE_CAPACITY, (const uint8_t *)"X", 1U), -1, "write past capacity should fail");
}

static void test_stream_write(void)
{
  int8_t file_id;
  uint8_t buffer[6];

  openlog_fs_init();
  file_id = openlog_fs_create_file(openlog_fs_root(), "STREAM.TXT", 1U);
  expect_true(file_id >= 0, "stream file should be created");
  expect_int(openlog_fs_stream_begin((uint8_t)file_id, 0U), 0, "stream begin should pass");
  expect_int(openlog_fs_stream_write((const uint8_t *)"abc", 3U), 0, "stream first write should pass");
  expect_int(openlog_fs_stream_write((const uint8_t *)"def", 3U), 0, "stream second write should pass");
  expect_int(openlog_fs_stream_sync(), 0, "stream sync should pass");
  expect_int(openlog_fs_stream_end(), 0, "stream end should pass");
  expect_int(openlog_fs_read((uint8_t)file_id, 0U, buffer, sizeof(buffer)), (int)sizeof(buffer), "stream readback should pass");
  expect_mem(buffer, "abcdef", sizeof(buffer), "stream contents should match");
}

typedef struct
{
  uint8_t count;
  uint8_t saw_file;
  uint8_t saw_dir;
} iterate_result_t;

static void iterate_collect(uint8_t node_id, const openlog_fs_node_t *node, void *context)
{
  iterate_result_t *result = (iterate_result_t *)context;

  (void)node_id;
  ++result->count;
  if(node != NULL)
  {
    if(node->is_dir != 0U && strcmp(node->name, "DIR2") == 0)
    {
      result->saw_dir = 1U;
    }
    if(node->is_dir == 0U && strcmp(node->name, "FILE2.TXT") == 0)
    {
      result->saw_file = 1U;
    }
  }
}

static void test_iterate_dir(void)
{
  iterate_result_t result;

  openlog_fs_init();
  expect_true(openlog_fs_create_dir(openlog_fs_root(), "DIR2") >= 0, "iterate dir should create directory");
  expect_true(openlog_fs_create_file(openlog_fs_root(), "FILE2.TXT", 1U) >= 0, "iterate dir should create file");

  memset(&result, 0, sizeof(result));
  openlog_fs_iterate_dir(openlog_fs_root(), iterate_collect, &result);
  expect_int(result.count, 2, "iterate dir should visit each child once");
  expect_true(result.saw_dir != 0U, "iterate dir should see directory");
  expect_true(result.saw_file != 0U, "iterate dir should see file");
}

int main(void)
{
  test_init_and_root();
  test_name_validation();
  test_create_and_find();
  test_write_append_and_sparse();
  test_truncate_and_delete();
  test_limits();
  test_stream_write();
  test_iterate_dir();

  puts("openlog_fs tests passed");
  return 0;
}
