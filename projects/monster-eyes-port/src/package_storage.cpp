#include "package_storage.h"
#include <ArduinoJson.h>
#include <FFat.h>

namespace {

constexpr size_t MAX_FILE = 1024 * 1024, MAX_PACKAGE = 3 * 1024 * 1024;

uint16_t le16(const uint8_t *p) {
    return p[0] | uint16_t(p[1]) << 8;
}

uint32_t le32(const uint8_t *p) {
    return p[0] | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 |
           uint32_t(p[3]) << 24;
}

// FFat.exists() answers false for a directory on this core, so asking it
// whether a package is there says no however plainly the package is there.
// Opening the path and asking the handle is what the package scanner does,
// and that works.
bool dirExists(const String &path) {
    File f = FFat.open(path);
    bool ok = f && f.isDirectory();
    if (f) f.close();
    return ok;
}

}  // namespace

bool PackageStorage::validId(const String &s) {
    if (!s.length() || s.length() > 32 || s[0] == '.') return false;
    for (char c : s)
        if (!isalnum((unsigned char)c) && c != '_' && c != '-') return false;
    return true;
}

bool PackageStorage::validRelativePath(const String &s) {
    if (!s.length() || s.length() > 63 || s[0] == '/' || s.indexOf("..") >= 0 ||
        s.indexOf('\\') >= 0 || s.indexOf("//") >= 0 || s[0] == '.')
        return false;
    for (size_t i = 0; i < s.length(); ++i)
        if ((uint8_t)s[i] < 32) return false;
    return true;
}

bool PackageStorage::removeTree(const String &p) {
    File d = FFat.open(p);
    if (!d) return true;
    if (!d.isDirectory()) {
        d.close();
        return FFat.remove(p);
    }
    for (File f = d.openNextFile(); f; f = d.openNextFile()) {
        String n = f.path();
        bool dir = f.isDirectory();
        f.close();
        if (dir) {
            if (!removeTree(n)) return false;
        } else if (!FFat.remove(n))
            return false;
    }
    d.close();
    return FFat.rmdir(p);
}

// Staging directories are siblings of the live packages rather than children
// of one staging directory, which keeps publishing a rename within a single
// directory -- the narrowest thing to ask of FatFs.
//
// Names beginning with a dot are ours; refreshPackages() skips them, so a
// half-uploaded package never shows up as a style, and anything left behind by
// an upload that a reset interrupted is cleared out here at boot.
void PackageStorage::begin() {
    // Leftovers from an upload that was interrupted by a reset.
    removeTree("/eyes/.staging");  // Where staging used to live
    File root = FFat.open("/eyes");
    if (!root || !root.isDirectory()) return;
    for (File entry = root.openNextFile(); entry; entry = root.openNextFile()) {
        String path = entry.path();
        String name = path.substring(path.lastIndexOf('/') + 1);
        bool mine = name.startsWith(".stage-") || name.startsWith(".backup-");
        entry.close();
        if (mine) removeTree(path);
    }
}

bool PackageStorage::startUpload(const String &id, String &token,
                                 String &error) {
    cancel();
    if (!validId(id)) {
        error = "invalid package id";
        return false;
    }
    uint32_t r = esp_random();
    char t[9];
    snprintf(t, sizeof(t), "%08lX", (unsigned long)r);
    _id = id;
    _token = t;
    _stage = "/eyes/.stage-" + _token;
    if (!FFat.mkdir(_stage)) {
        error = "cannot create staging directory";
        cancel();
        return false;
    }
    token = _token;
    return true;
}

bool PackageStorage::beginFile(const String &token, const String &path,
                               String &error) {
    if (token != _token || !_stage.length()) {
        error = "invalid upload token";
        return false;
    }
    if (!validRelativePath(path) || path.indexOf('/') >= 0) {
        error = "invalid filename";
        return false;
    }
    String lower = path;
    lower.toLowerCase();
    if (path != "config.eye" && !lower.endsWith(".bmp") &&
        !lower.endsWith(".wav")) {
        error = "unsupported file type";
        return false;
    }
    _file = FFat.open(_stage + "/" + path, FILE_WRITE);
    if (!_file) {
        error = "cannot create staged file";
        return false;
    }
    _fileBytes = 0;
    return true;
}

bool PackageStorage::writeFile(const uint8_t *d, size_t n, String &error) {
    if (!_file) {
        error = "upload file is not open";
        return false;
    }
    // Count the bytes rather than asking the file how big it is. The VFS only
    // re-stats a file once something has been written to it, so a file just
    // created for writing reports whatever uninitialised stat data its handle
    // was built with -- hundreds of megabytes, in practice, which failed every
    // first upload of a session against MAX_FILE and made the second succeed.
    if (_fileBytes + n > MAX_FILE || _total + n > MAX_PACKAGE) {
        error = "package size limit exceeded";
        _file.close();
        return false;
    }
    if (_file.write(d, n) != n) {
        error = "filesystem write failed";
        _file.close();
        return false;
    }
    _fileBytes += n;
    _total += n;
    return true;
}

bool PackageStorage::endFile(String &error) {
    if (!_file) {
        error = "upload file is not open";
        return false;
    }
    _file.close();
    return true;
}

String PackageStorage::resolveAsset(const String &root, const String &v) const {
    if (!validRelativePath(v)) return String();
    String name = v;
    int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    return root + "/" + name;
}

bool PackageStorage::validateBmp(const String &p, uint8_t bits,
                                 String &e) const {
    File f = FFat.open(p);
    uint8_t h[54];
    if (!f || f.read(h, sizeof(h)) != sizeof(h) || h[0] != 'B' || h[1] != 'M') {
        e = "invalid BMP: " + p;
        return false;
    }
    uint32_t off = le32(h + 10), dib = le32(h + 14), w = le32(h + 18),
             ht = le32(h + 22), compression = le32(h + 30);
    uint16_t bpp = le16(h + 28);
    uint64_t row = ((uint64_t(w) * bpp + 31) / 32) * 4;
    if (dib < 40 || !w || !ht || w > 2048 || ht > 2048 || bpp != bits ||
        compression || uint64_t(off) + row * ht > f.size()) {
        e = "unsupported or truncated BMP: " + p;
        return false;
    }
    return true;
}

bool PackageStorage::validateWav(const String &p, String &e) const {
    File f = FFat.open(p);
    uint8_t h[12];
    if (!f || f.read(h, 12) != 12 || memcmp(h, "RIFF", 4) ||
        memcmp(h + 8, "WAVE", 4)) {
        e = "invalid WAV: " + p;
        return false;
    }
    bool fmt = false, data = false;
    while (f.available() >= 8) {
        uint8_t c[8];
        if (f.read(c, 8) != 8) break;
        uint32_t n = le32(c + 4), at = f.position();
        if (!memcmp(c, "fmt ", 4)) {
            uint8_t x[16];
            if (n < 16 || f.read(x, 16) != 16 || le16(x) != 1 ||
                le16(x + 2) < 1 || le16(x + 2) > 2 ||
                (le16(x + 14) != 8 && le16(x + 14) != 16) || !le32(x + 4)) {
                e = "unsupported WAV: " + p;
                return false;
            }
            fmt = true;
        } else if (!memcmp(c, "data", 4)) {
            if (!n || uint64_t(at) + n > f.size()) {
                e = "truncated WAV: " + p;
                return false;
            }
            data = true;
        }
        f.seek(at + n + (n & 1));
    }
    if (!fmt || !data) {
        e = "incomplete WAV: " + p;
        return false;
    }
    return true;
}

bool PackageStorage::validatePackage(const String &root, String &e) const {
    File f = FFat.open(root + "/config.eye");
    JsonDocument d;
    if (!f || deserializeJson(d, f) || !d.is<JsonObject>()) {
        e = "invalid config.eye";
        return false;
    }
    const char *tex[] = {"irisTexture", "scleraTexture"};
    for (auto k : tex) {
        if (!d[k].is<const char *>()) continue;
        String p = resolveAsset(root, d[k].as<String>());
        if (!p.length() || !validateBmp(p, 24, e)) return false;
    }
    const char *lids[] = {"upperEyelid", "lowerEyelid"};
    for (auto k : lids) {
        if (!d[k].is<const char *>()) continue;
        String p = resolveAsset(root, d[k].as<String>());
        if (!p.length() || !validateBmp(p, 1, e)) return false;
    }
    JsonArray sounds = d["extensions"]["audio"]["sounds"].as<JsonArray>();
    for (JsonVariant v : sounds) {
        String p = resolveAsset(root, v.as<String>());
        if (!p.length() || !validateWav(p, e)) return false;
    }
    return true;
}

bool PackageStorage::commit(const String &token, String &e) {
    if (token != _token || !_stage.length()) {
        e = "invalid upload token";
        return false;
    }
    if (_file) _file.close();
    if (!validatePackage(_stage, e)) {
        cancel();
        return false;
    }
    // Publishing is a rename, and a rename onto a name that already exists
    // fails, so the package being replaced has to be moved aside first -- and
    // put back if the publish does not land.
    String live = livePath(_id), backup = "/eyes/.backup-" + _token;
    bool had = dirExists(live);
    if (had && !FFat.rename(live, backup)) {
        e = "cannot back up existing package";
        return false;
    }
    if (!FFat.rename(_stage, live)) {
        if (had) FFat.rename(backup, live);
        e = "cannot publish package";
        return false;
    }
    if (had) removeTree(backup);
    _id = "";
    _token = "";
    _stage = "";
    _total = _fileBytes = 0;
    return true;
}

void PackageStorage::cancel() {
    if (_file) _file.close();
    if (_stage.length()) removeTree(_stage);
    _id = "";
    _token = "";
    _stage = "";
    _total = _fileBytes = 0;
}

bool PackageStorage::renamePackage(const String &id, const String &n,
                                   String &e) {
    if (!validId(id) || !validId(n)) {
        e = "invalid package id";
        return false;
    }
    String a = livePath(id), b = livePath(n);
    if (!dirExists(a)) {
        e = "package not found";
        return false;
    }
    if (dirExists(b)) {
        e = "target already exists";
        return false;
    }
    if (!FFat.rename(a, b)) {
        e = "rename failed";
        return false;
    }
    return true;
}

bool PackageStorage::deletePackage(const String &id, String &e) {
    if (!validId(id)) {
        e = "invalid package id";
        return false;
    }
    String p = livePath(id);
    if (!dirExists(p)) {
        e = "package not found";
        return false;
    }
    if (!removeTree(p)) {
        e = "delete failed";
        return false;
    }
    return true;
}
