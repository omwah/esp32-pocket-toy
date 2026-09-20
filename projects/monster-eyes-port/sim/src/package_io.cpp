/**
 * @file package_io.cpp
 * @brief Copying a package on disk, for Save As.
 */

#include "package_io.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

/** @brief Copy one file's contents. @return true on success. */
bool copyFile(const std::string &from, const std::string &to) {
  FILE *in = fopen(from.c_str(), "rb");
  if (!in)
    return false;
  FILE *out = fopen(to.c_str(), "wb");
  if (!out) {
    fclose(in);
    return false;
  }
  char buf[64 * 1024];
  size_t n;
  bool ok = true;
  while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
    if (fwrite(buf, 1, n, out) != n) {
      ok = false;
      break;
    }
  }
  fclose(in);
  fclose(out);
  return ok;
}

} // namespace

bool copyPackageAssets(const std::string &fromDir, const std::string &toDir,
                       std::string *error) {
  struct stat st;
  if (stat(toDir.c_str(), &st) == 0) {
    if (error)
      *error = "A package by that name already exists.";
    return false;
  }
  if (mkdir(toDir.c_str(), 0755) != 0) {
    if (error)
      *error = std::string("Could not create the directory: ") +
               strerror(errno);
    return false;
  }

  DIR *dir = opendir(fromDir.c_str());
  if (!dir) {
    if (error)
      *error = "Could not read the source package.";
    return false;
  }
  bool ok = true;
  while (const struct dirent *entry = readdir(dir)) {
    if (entry->d_name[0] == '.')
      continue;
    // The caller writes its own, merged from the edits.
    if (!strcmp(entry->d_name, "config.eye"))
      continue;
    const std::string from = fromDir + "/" + entry->d_name;
    struct stat fst;
    if (stat(from.c_str(), &fst) != 0 || !S_ISREG(fst.st_mode))
      continue;
    if (!copyFile(from, toDir + "/" + entry->d_name)) {
      if (error)
        *error = std::string("Could not copy ") + entry->d_name;
      ok = false;
      break;
    }
  }
  closedir(dir);
  return ok;
}
