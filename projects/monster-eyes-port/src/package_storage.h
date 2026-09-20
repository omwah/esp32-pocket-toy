#pragma once

#include <Arduino.h>
#include <FS.h>

class PackageStorage {
public:
    void begin();
    bool startUpload(const String &id, String &token, String &error);
    bool beginFile(const String &token, const String &path, String &error);
    bool writeFile(const uint8_t *data, size_t length, String &error);
    bool endFile(String &error);
    bool commit(const String &token, String &error);
    void cancel();
    bool validatePackage(const String &root, String &error) const;
    bool renamePackage(const String &id, const String &newId, String &error);
    bool deletePackage(const String &id, String &error);
    static bool validId(const String &id);
    static bool validRelativePath(const String &path);
    String livePath(const String &id) const { return "/eyes/" + id; }
    const String &uploadId() const { return _id; }
    const String &token() const { return _token; }

private:
    bool removeTree(const String &path);
    bool validateBmp(const String &path, uint8_t bits, String &error) const;
    bool validateWav(const String &path, String &error) const;
    String resolveAsset(const String &root, const String &value) const;
    String _id, _token, _stage;
    File _file;
    size_t _total = 0, _fileBytes = 0;
};
