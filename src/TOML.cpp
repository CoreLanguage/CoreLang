#include "TOML.h"
#include "Common.h"
#include <cctype>

namespace core {

namespace {
struct TOMLParser {
  const std::string &text;
  size_t pos = 0;
  std::string err;
  TOMLValue root;
  TOMLValue *cur = &root;

  TOMLParser(const std::string &t) : text(t) {}

  bool fail(const std::string &m, unsigned line) {
    err = strfmt("line %u: %s", line, m.c_str());
    return false;
  }

  void skipWs() {
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t')) pos++;
  }
  void skipToEol() {
    while (pos < text.size() && text[pos] != '\n') pos++;
  }

  std::string parseString() {
    // assumes text[pos] == '"'
    pos++;
    std::string out;
    while (pos < text.size() && text[pos] != '"') {
      if (text[pos] == '\\' && pos + 1 < text.size()) {
        pos++;
        switch (text[pos]) {
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': out += '\r'; break;
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        default: out += text[pos];
        }
        pos++;
      } else {
        out += text[pos++];
      }
    }
    if (pos < text.size()) pos++; // closing quote
    return out;
  }

  bool parseKeyValue(TOMLValue &table) {
    std::string key;
    while (pos < text.size() && (isalnum(text[pos]) || text[pos] == '_' || text[pos] == '-')) {
      key += text[pos++];
    }
    skipWs();
    if (key.empty()) return false;
    if (pos >= text.size() || text[pos] != '=') return fail("expected '=' after key '" + key + "'", 0);
    pos++;
    skipWs();
    TOMLValue v;
    if (pos < text.size() && text[pos] == '"') {
      v.kind = TOMLValue::String;
      v.s = parseString();
    } else if (pos + 1 < text.size() && text[pos] == '[' ) {
      v.kind = TOMLValue::Array;
      pos++;
      skipWs();
      while (pos < text.size() && text[pos] != ']') {
        skipWs();
        if (pos < text.size() && text[pos] == '"') {
          v.arr.push_back(parseString());
          skipWs();
          if (pos < text.size() && text[pos] == ',') { pos++; skipWs(); continue; }
        } else if (pos < text.size() && text[pos] != ']') {
          pos++; // skip unexpected
          continue;
        }
        break;
      }
      if (pos < text.size()) pos++; // ]
    } else if (pos < text.size() && text[pos] == '{') {
      v.kind = TOMLValue::Table;
      pos++;
      skipWs();
      while (pos < text.size() && text[pos] != '}') {
        std::string subkey;
        while (pos < text.size() && (isalnum(text[pos]) || text[pos] == '_' || text[pos] == '-'))
          subkey += text[pos++];
        skipWs();
        if (pos < text.size() && text[pos] == '=') {
          pos++;
          skipWs();
          TOMLValue sv;
          if (pos < text.size() && text[pos] == '"') {
            sv.kind = TOMLValue::String;
            sv.s = parseString();
          } else {
            // number/bool
            std::string tok;
            while (pos < text.size() && text[pos] != ',' && text[pos] != '}') tok += text[pos++];
            while (!tok.empty() && isspace(tok.back())) tok.pop_back();
            if (tok == "true") { sv.kind = TOMLValue::Bool; sv.b = true; }
            else if (tok == "false") { sv.kind = TOMLValue::Bool; sv.b = false; }
            else if (!tok.empty()) { sv.kind = TOMLValue::Int; sv.i = strtoll(tok.c_str(), nullptr, 10); }
          }
          v.table[subkey] = sv;
        }
        skipWs();
        if (pos < text.size() && text[pos] == ',') { pos++; skipWs(); }
      }
      if (pos < text.size()) pos++; // }
    } else if (pos + 3 < text.size() && (text.substr(pos, 4) == "true" || text.substr(pos, 5) == "false")) {
      v.kind = TOMLValue::Bool;
      v.b = text[pos] == 't';
      pos += v.b ? 4 : 5;
    } else {
      // number
      std::string tok;
      while (pos < text.size() && text[pos] != '\n' && text[pos] != ',' &&
             text[pos] != ']' && text[pos] != '#' && text[pos] != ' ') tok += text[pos++];
      if (!tok.empty()) { v.kind = TOMLValue::Int; v.i = strtoll(tok.c_str(), nullptr, 10); }
    }
    table.table[key] = v;
    return true;
  }
};
} // namespace

bool TOML::parse(const std::string &text, TOMLValue &outRoot, std::string &err) {
  TOMLParser p(text);
  unsigned line = 1;
  while (p.pos < text.size()) {
    if (text[p.pos] == '\n') { line++; p.pos++; continue; }
    if (text[p.pos] == ' ' || text[p.pos] == '\t') { p.pos++; continue; }
    if (text[p.pos] == '#') { p.skipToEol(); continue; }
    if (text[p.pos] == '\r') { p.pos++; continue; }
    if (text[p.pos] == '[') {
      p.pos++;
      // [table] or [[array of tables]]
      bool arrayTable = p.pos < text.size() && text[p.pos] == '[';
      if (arrayTable) p.pos++;
      std::vector<std::string> parts;
      std::string curPart;
      while (p.pos < text.size() && text[p.pos] != ']' && text[p.pos] != '\n') {
        if (text[p.pos] == '.') {
          parts.push_back(curPart);
          curPart.clear();
          p.pos++;
          continue;
        }
        curPart += text[p.pos++];
      }
      if (!curPart.empty()) parts.push_back(curPart);
      if (p.pos < text.size()) p.pos++;           // ]
      if (arrayTable && p.pos < text.size()) p.pos++; // ]]
      // navigate/create table
      TOMLValue *t = &p.root;
      for (auto &part : parts) {
        if (arrayTable) {
          // append to array-of-tables: store as table with numbered keys (simplified)
          if (!t->table.count(part)) {
            TOMLValue arrV;
            arrV.kind = TOMLValue::Table;
            t->table[part] = arrV;
          }
          t = &t->table[part];
        } else {
          if (!t->table.count(part)) {
            TOMLValue tv;
            tv.kind = TOMLValue::Table;
            t->table[part] = tv;
          }
          t = &t->table[part];
        }
      }
      if (arrayTable) {
        // [[name]]: append a fresh table to the root-level named array
        TOMLValue tv;
        tv.kind = TOMLValue::Table;
        p.root.tableArrays[parts[0]].push_back(tv);
        p.cur = &p.root.tableArrays[parts[0]].back();
      } else {
        p.cur = t;
      }
      continue;
    }
    // key = value (possibly dotted key: a.b = v)
    size_t keyStart = p.pos;
    if (!p.parseKeyValue(*p.cur)) {
      err = strfmt("line %u: invalid TOML near '%s'", line, text.substr(keyStart, 20).c_str());
      return false;
    }
  }
  outRoot = p.root;
  err.clear();
  return true;
}

std::string TOML::escape(const std::string &s) {
  std::string out = "\"";
  for (char c : s) {
    switch (c) {
    case '"': out += "\\\""; break;
    case '\\': out += "\\\\"; break;
    case '\n': out += "\\n"; break;
    case '\t': out += "\\t"; break;
    default: out += c;
    }
  }
  out += "\"";
  return out;
}

} // namespace core
