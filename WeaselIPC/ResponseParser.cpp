#include "stdafx.h"
#include <StringAlgorithm.hpp>
#include <WeaselIPC.h>
#include "Deserializer.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace weasel;

ResponseParser::ResponseParser(std::wstring* commit,
                               Context* context,
                               Status* status,
                               Config* config,
                               UIStyle* style)
    : p_commit(commit),
      p_context(context),
      p_status(status),
      p_config(config),
      p_style(style) {
  Deserializer::Initialize(this);
}

bool ResponseParser::operator()(LPWSTR buffer, UINT length) {
  // TEMP DIAG (remove after root-causing): the reply is read out of a reusable
  // buffer. If the server's reply was truncated, or the buffer still held a
  // leftover request header, the loop below never sees the "." terminator and
  // returns false — and the caller then skips the whole UI refresh, which is
  // what freezes the candidate window on the previous keystroke. Dump the head
  // of what we actually got (hex, so binary garbage is recognisable).
  {
    char head[512];
    size_t n = 0;
    for (size_t i = 0; i < 120 && buffer && buffer[i]; ++i) {
      int w =
          snprintf(head + n, sizeof(head) - n, "%04x ", (unsigned)buffer[i]);
      if (w <= 0 || (size_t)w >= sizeof(head) - n)
        break;
      n += (size_t)w;
    }
    head[n] = '\0';
    const char* tmp = getenv("TEMP");
    std::string path = std::string(tmp ? tmp : "C:\\") + "\\weasel-diag.log";
    FILE* f = fopen(path.c_str(), "a");
    if (f) {
      fprintf(f, "[parse] cap=%u head=%s\n", length, head);
      fclose(f);
    }
  }

  wbufferstream bs(buffer, length);
  std::wstring line;
  while (bs.good()) {
    std::getline(bs, line);
    if (!bs.good())
      return false;

    // file ends
    if (line == L".")
      break;

    Feed(line);
  }
  return bs.good();
}

void ResponseParser::Feed(const std::wstring& line) {
  // ignore blank lines and comments
  if (line.empty() || line.find_first_of(L'#') == 0)
    return;

  Deserializer::KeyType key;
  std::wstring value;

  // extract key (split by L'.') and value
  std::wstring::size_type sep_pos = line.find_first_of(L'=');
  if (sep_pos == std::wstring::npos)
    return;
  split(key, line.substr(0, sep_pos), L".");
  if (key.empty())
    return;
  value = line.substr(sep_pos + 1);

  // first part of the key serve as action type
  std::wstring const& action = key[0];

  // get required action deserializer instance
  std::map<std::wstring, Deserializer::Ptr>::iterator i =
      deserializers.find(action);
  if (i == deserializers.end()) {
    // line ignored... since corresponding deserializer is not active
    return;
  }

  // dispatch
  Deserializer::Ptr p = i->second;
  p->Store(key, value);
}
