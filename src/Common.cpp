#include "Common.h"
#include <cstdarg>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace core {

std::string strfmt(const char *fmt, ...) {
  char buf[4096];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  return std::string(buf);
}

std::string readFileOrEmpty(const std::string &path, bool &ok) {
  std::ifstream in(path, std::ios::binary);
  ok = in.good();
  if (!ok) return "";
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::string joinPath(const std::string &a, const std::string &b) {
  if (a.empty()) return b;
  if (!b.empty() && b[0] == '/') return b;
  if (a.back() == '/') return a + b;
  return a + "/" + b;
}

std::string dirName(const std::string &path) {
  size_t p = path.find_last_of('/');
  if (p == std::string::npos) return ".";
  if (p == 0) return "/";
  return path.substr(0, p);
}

bool fileExists(const std::string &path) {
  struct stat st;
  return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool dirExists(const std::string &path) {
  struct stat st;
  return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string escapeString(const std::string &s) {
  std::string out;
  for (char c : s) {
    switch (c) {
    case '\n': out += "\\n"; break;
    case '\t': out += "\\t"; break;
    case '\r': out += "\\r"; break;
    case '\\': out += "\\\\"; break;
    case '"': out += "\\\""; break;
    default:
      if ((unsigned char)c < 32) out += strfmt("\\x%02x", c);
      else out += c;
    }
  }
  return out;
}

} // namespace core
