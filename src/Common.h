// Core compiler - common utilities shared by every phase.
#ifndef CORE_COMMON_H
#define CORE_COMMON_H

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace core {

// ---------------------------------------------------------------- location --
struct SourceLoc {
  unsigned file = 0;   // index into SourceMgr
  unsigned line = 1;   // 1-based
  unsigned col = 1;    // 1-based
  unsigned offset = 0; // byte offset into file text
  bool valid = false;
};

// --------------------------------------------------------------- source mgr --
struct SourceFile {
  std::string path;
  std::string text;
  std::vector<unsigned> lineOffsets; // offset of each line start
};

class SourceMgr {
public:
  unsigned addFile(const std::string &path, const std::string &text) {
    SourceFile f;
    f.path = path;
    f.text = text;
    f.lineOffsets.push_back(0);
    for (unsigned i = 0; i < text.size(); i++)
      if (text[i] == '\n') f.lineOffsets.push_back(i + 1);
    files.push_back(std::move(f));
    return (unsigned)files.size() - 1;
  }
  SourceFile &file(unsigned id) { return files[id]; }
  const SourceFile &file(unsigned id) const { return files[id]; }
  size_t fileCount() const { return files.size(); }

  std::string fileName(unsigned id) const {
    if (id >= files.size()) return "<unknown>";
    return files[id].path;
  }
  // Extract one 1-based source line (without trailing newline).
  std::string getLineText(unsigned fileID, unsigned line) const {
    if (fileID >= files.size() || line == 0) return "";
    const SourceFile &f = files[fileID];
    if (line - 1 >= f.lineOffsets.size()) return "";
    unsigned start = f.lineOffsets[line - 1];
    unsigned end = (line < f.lineOffsets.size()) ? f.lineOffsets[line] : f.text.size();
    std::string s = f.text.substr(start, end - start);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
  }

private:
  std::vector<SourceFile> files;
};

// ------------------------------------------------------------------ strings --
std::string strfmt(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
std::string readFileOrEmpty(const std::string &path, bool &ok);
std::string joinPath(const std::string &a, const std::string &b);
std::string dirName(const std::string &path);
bool fileExists(const std::string &path);
bool dirExists(const std::string &path);
std::string escapeString(const std::string &s); // escape for display inside quotes

} // namespace core
#endif
