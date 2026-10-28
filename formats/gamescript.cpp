#include "gamescript.h"

#include <cctype>
#include <cstdint>
#include <cstdlib>

using namespace rc;

namespace {

bool sameText(std::string_view a, std::string_view b) {
  if(a.size()!=b.size())
    return false;
  for(size_t i=0; i<a.size(); ++i)
    if(std::tolower(uint8_t(a[i]))!=std::tolower(uint8_t(b[i])))
      return false;
  return true;
  }

}

const GameScript::Line* GameScript::find(std::string_view command) const {
  for(auto& l:lines)
    if(sameText(l.command,command))
      return &l;
  return nullptr;
  }

GameScript GameScript::parse(std::string_view text) {
  GameScript ret;
  while(!text.empty()) {
    const size_t     eol  = text.find('\n');
    std::string_view line = text.substr(0,eol);
    text = eol==std::string_view::npos ? std::string_view() : text.substr(eol+1);

    std::vector<std::string> words;
    size_t i = 0;
    while(i<line.size()) {
      const char c = line[i];
      if(std::isspace(uint8_t(c)) || c=='=') {
        ++i;
        continue;
        }
      if(c=='/' && i+1<line.size() && line[i+1]=='/')
        break;
      if(c=='"' || c=='\'') {
        const size_t end = line.find(c,i+1);
        words.emplace_back(line.substr(i+1,end==std::string_view::npos ? std::string_view::npos : end-i-1));
        i = end==std::string_view::npos ? line.size() : end+1;
        continue;
        }
      size_t end = i;
      while(end<line.size() && !std::isspace(uint8_t(line[end])) && line[end]!='=')
        ++end;
      words.emplace_back(line.substr(i,end-i));
      i = end;
      }
    if(words.size()<2)
      continue;
    char* end = nullptr;
    const float t = std::strtof(words[0].c_str(),&end);
    if(end==words[0].c_str())
      continue;
    Line l;
    l.time    = t;
    l.command = std::move(words[1]);
    l.args.assign(std::make_move_iterator(words.begin()+2),std::make_move_iterator(words.end()));
    ret.lines.push_back(std::move(l));
    }
  return ret;
  }
