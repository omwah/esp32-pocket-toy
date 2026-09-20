/**
 * @file FFat.cpp
 * @brief Resolving device paths against a host directory.
 */

#include "FFat.h"

#include <sys/stat.h>

HostFFatFS FFat;

void HostFFatFS::setRoot(const char *path) {
  if (!path || !path[0])
    return;
  snprintf(_root, sizeof(_root), "%s", path);
  // Trailing slashes would double up when paths are joined, and the device
  // paths all start with one of their own.
  size_t n = strlen(_root);
  while (n > 1 && _root[n - 1] == '/')
    _root[--n] = 0;
}

bool HostFFatFS::begin(bool formatOnFail) {
  (void)formatOnFail;
  struct stat st;
  if (stat(_root, &st) != 0 || !S_ISDIR(st.st_mode)) {
    Serial.printf("Asset root '%s' is not a directory.\n", _root);
    return false;
  }
  return true;
}

// A config file naming "../../etc/passwd" as its iris texture should fail to
// open rather than succeed, so the join walks the path and refuses to let the
// depth go negative. Cheaper and clearer than realpath(), and it does not
// require the file to exist.
File HostFFatFS::open(const char *path, const char *mode) {
  (void)mode;
  if (!path)
    return File();

  const char *p = path;
  while (*p == '/')
    ++p;

  int depth = 0;
  for (const char *seg = p; *seg;) {
    const char *end = strchr(seg, '/');
    const size_t len = end ? (size_t)(end - seg) : strlen(seg);
    if (len == 2 && !strncmp(seg, "..", 2)) {
      if (--depth < 0)
        return File();
    } else if (len && !(len == 1 && seg[0] == '.')) {
      ++depth;
    }
    seg = end ? end + 1 : seg + len;
  }

  char full[2048];
  snprintf(full, sizeof(full), "%s/%s", _root, p);
  FILE *f = fopen(full, "rb");
  return File(f);
}
