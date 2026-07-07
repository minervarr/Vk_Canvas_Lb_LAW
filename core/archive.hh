#pragma once
#include <string>
#include <vector>

// Archive engine: small, dependency-free persistence primitives built on plain
// std::fstream over a caller-supplied directory path (no Android/JNI). Reusable
// across projects.
//
//   RecordStore   — an append-only list of text records plus one "current" blob
//                   and an enabled flag. (Powers the transcription history:
//                   archived sessions + the live session that must survive a
//                   process death.)
//   KeyValueStore — a flat key=value text file with typed get/set. (Powers app
//                   settings.)
namespace archive {

// ── RecordStore ─────────────────────────────────────────────────────────────
// Files under dir: <name>.records (records separated by 0x1E, which never
// appears in UTF-8 text), <name>.current (live blob), <name>.enabled ("0"/"1").
class RecordStore {
 public:
  // dir is e.g. Android's internalDataPath; name namespaces the files.
  void init(const char* dir, const char* name = "history");

  bool enabled() const { return enabled_; }
  void setEnabled(bool on);                 // persists the flag

  const std::vector<std::string>& entries() const { return entries_; }
  void archive(const std::string& text);    // append one record (no-op if disabled/empty)
  void clearAll();                          // wipe records + entries_

  void saveCurrent(const std::string& text) const;
  std::string loadCurrent() const;

 private:
  std::string dir_, name_;
  std::vector<std::string> entries_;
  bool enabled_ = true;

  std::string recordsPath() const { return path("records"); }
  std::string currentPath() const { return path("current"); }
  std::string flagPath()    const { return path("enabled"); }
  std::string path(const char* ext) const { return dir_ + "/" + name_ + "." + ext; }
  void loadEntries();
};

// ── KeyValueStore ───────────────────────────────────────────────────────────
// One flat "key=value" text file (one entry per line). Unknown keys fall back to
// the supplied default. Call save() to flush after a batch of set()s.
class KeyValueStore {
 public:
  void init(const char* dir, const char* name = "settings");  // loads file if present

  int         getInt   (const std::string& key, int def) const;
  float       getFloat (const std::string& key, float def) const;
  bool        getBool  (const std::string& key, bool def) const;
  std::string getString(const std::string& key, const std::string& def) const;

  void setInt   (const std::string& key, int v);
  void setFloat (const std::string& key, float v);
  void setBool  (const std::string& key, bool v);
  void setString(const std::string& key, const std::string& v);

  void save() const;   // write all pairs back to disk

 private:
  std::string filePath_;
  std::vector<std::pair<std::string, std::string>> pairs_;  // preserves order

  const std::string* find(const std::string& key) const;
  void set(const std::string& key, std::string value);
};

}  // namespace archive
