#include "archive.hh"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace archive {

namespace {
constexpr char kRecordSep = '\x1e';  // never appears in UTF-8 text

std::string readFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return {};
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

void writeFile(const std::string& path, const std::string& data) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (f) f.write(data.data(), (std::streamsize)data.size());
}
}  // namespace

// ── RecordStore ─────────────────────────────────────────────────────────────

void RecordStore::init(const char* dir, const char* name) {
  dir_  = dir  ? dir  : "";
  name_ = name ? name : "history";
  std::string flag = readFile(flagPath());
  enabled_ = !(flag.size() >= 1 && flag[0] == '0');
  loadEntries();
}

void RecordStore::loadEntries() {
  entries_.clear();
  std::string blob = readFile(recordsPath());
  if (blob.empty()) return;
  size_t start = 0;
  while (start < blob.size()) {
    size_t sep = blob.find(kRecordSep, start);
    if (sep == std::string::npos) {
      if (start < blob.size()) entries_.emplace_back(blob.substr(start));
      break;
    }
    entries_.emplace_back(blob.substr(start, sep - start));
    start = sep + 1;
  }
}

void RecordStore::setEnabled(bool on) {
  enabled_ = on;
  writeFile(flagPath(), on ? "1" : "0");
}

void RecordStore::archive(const std::string& text) {
  if (!enabled_ || text.empty()) return;
  entries_.push_back(text);
  // Append rather than rewrite. Each record is terminated by the separator, so
  // the parser never sees a spurious leading empty entry.
  std::ofstream f(recordsPath(), std::ios::binary | std::ios::app);
  if (f) { f.write(text.data(), (std::streamsize)text.size()); f.put(kRecordSep); }
}

void RecordStore::clearAll() {
  entries_.clear();
  writeFile(recordsPath(), "");
}

void RecordStore::saveCurrent(const std::string& text) const {
  writeFile(currentPath(), text);
}

std::string RecordStore::loadCurrent() const {
  return readFile(currentPath());
}

// ── KeyValueStore ───────────────────────────────────────────────────────────

void KeyValueStore::init(const char* dir, const char* name) {
  filePath_ = std::string(dir ? dir : "") + "/" + (name ? name : "settings") + ".kv";
  pairs_.clear();
  std::istringstream ss(readFile(filePath_));
  std::string line;
  while (std::getline(ss, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    pairs_.emplace_back(line.substr(0, eq), line.substr(eq + 1));
  }
}

const std::string* KeyValueStore::find(const std::string& key) const {
  for (const auto& p : pairs_) if (p.first == key) return &p.second;
  return nullptr;
}

void KeyValueStore::set(const std::string& key, std::string value) {
  for (auto& p : pairs_) {
    if (p.first == key) { p.second = std::move(value); return; }
  }
  pairs_.emplace_back(key, std::move(value));
}

int KeyValueStore::getInt(const std::string& key, int def) const {
  const std::string* v = find(key);
  return v ? std::atoi(v->c_str()) : def;
}
float KeyValueStore::getFloat(const std::string& key, float def) const {
  const std::string* v = find(key);
  return v ? (float)std::atof(v->c_str()) : def;
}
bool KeyValueStore::getBool(const std::string& key, bool def) const {
  const std::string* v = find(key);
  return v ? (*v == "1" || *v == "true") : def;
}
std::string KeyValueStore::getString(const std::string& key, const std::string& def) const {
  const std::string* v = find(key);
  return v ? *v : def;
}

void KeyValueStore::setInt(const std::string& key, int v)    { set(key, std::to_string(v)); }
void KeyValueStore::setFloat(const std::string& key, float v){ set(key, std::to_string(v)); }
void KeyValueStore::setBool(const std::string& key, bool v)  { set(key, v ? "1" : "0"); }
void KeyValueStore::setString(const std::string& key, const std::string& v) { set(key, v); }

void KeyValueStore::save() const {
  std::ostringstream ss;
  for (const auto& p : pairs_) ss << p.first << '=' << p.second << '\n';
  writeFile(filePath_, ss.str());
}

}  // namespace archive
