// Minimal TOML reader/writer for core.toml and core.lock.
// Supports: comments, [tables], [dotted.tables], key = value, strings,
// integers, booleans, arrays of strings, inline tables.
#ifndef CORE_TOML_H
#define CORE_TOML_H

#include <map>
#include <string>
#include <vector>

namespace core {

struct TOMLValue {
  enum Kind { String, Int, Bool, Array, Table, Empty } kind = Empty;
  std::string s;
  long long i = 0;
  bool b = false;
  std::vector<std::string> arr;
  std::map<std::string, TOMLValue> table;
  std::map<std::string, std::vector<TOMLValue>> tableArrays; // [[name]] sections

  bool isString() const { return kind == String; }
  bool isTable() const { return kind == Table; }
  std::string str(const std::string &def = "") const { return kind == String ? s : def; }
  long long integer(long long def = 0) const { return kind == Int ? i : def; }
  bool boolean(bool def = false) const { return kind == Bool ? b : def; }
};

class TOML {
public:
  static bool parse(const std::string &text, TOMLValue &outRoot, std::string &err);
  static std::string escape(const std::string &s);
};

} // namespace core
#endif
