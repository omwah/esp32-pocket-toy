/**
 * @file package_io.h
 * @brief Copying a package on disk, for Save As.
 *
 * Live edits do not come through here at all: they are served straight out of
 * memory by the FFat shim's overlay, so nothing is written anywhere until a
 * save is asked for. This is only the deliberate, user-driven write.
 */

#ifndef _SIM_PACKAGE_IO_H_
#define _SIM_PACKAGE_IO_H_

#include <string>

/**
 * @brief Copy a package's bitmaps into a new directory.
 *
 * Used by Save As, so the result is a complete package that loads like any
 * other rather than half a reference to its parent. config.eye is skipped,
 * since the caller writes its own edited version.
 *
 * @param fromDir Source directory.
 * @param toDir   Destination; created, and must not already exist.
 * @param error   Receives a message on failure.
 * @return true on success.
 */
bool copyPackageAssets(const std::string &fromDir, const std::string &toDir,
                       std::string *error);

#endif // _SIM_PACKAGE_IO_H_
