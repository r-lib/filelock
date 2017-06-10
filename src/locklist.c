
#include <stdlib.h>
#include <string.h>

#include "filelock.h"

filelock__list_t lock_list_head = { 0, 0 };
filelock__list_t *lock_list = &lock_list_head;

#ifdef _WIN32
int filelock__list_add(const char *path, HANDLE file) {
#else
int filelock__list_add(const char *path, int file) {
#endif
  filelock__list_t *node = calloc(1, sizeof(filelock__list_t));
  if (!node) return 1;
  node->path = strdup(path);
  node->file = file;
  if (!node->path) { free(node); return 1; }
  node->next = lock_list->next;
  lock_list->next = node;
  return 0;
}

void filelock__list_remove(const char *path) {
  filelock__list_t *prev = lock_list, *ptr = lock_list->next;
  while (ptr) {
    if (!strcmp(ptr->path, path)) {
      prev->next = ptr->next;
      free(ptr->path);
      free(ptr);
      return;
    }
    prev = ptr;
    ptr = ptr->next;
  }
}

filelock__list_t *filelock__list_find(const char *path) {
  filelock__list_t *ptr = lock_list->next;
  while (ptr) {
    if (!strcmp(ptr->path, path)) return ptr;
    ptr = ptr->next;
  }
  return 0;
}
