/*
  Copyright 2008 Google Inc.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#include <algorithm>
#include <climits>
#include <cstring>
#include <ctype.h>
#include "gadget_consts.h"
#include "string_utils.h"
#include "common.h"

namespace ggadget {

static const char kSlash      = '/';
static const char kBackSlash = '\\';

int GadgetStrCmp(const char *s1, const char *s2) {
#ifdef GADGET_CASE_SENSITIVE
  return strcmp(s1, s2);
#else
  return strcasecmp(s1, s2);
#endif
}

int GadgetStrNCmp(const char *s1, const char *s2, size_t n) {
#ifdef GADGET_CASE_SENSITIVE
  return strncmp(s1, s2, n);
#else
  return strncasecmp(s1, s2, n);
#endif
}

int GadgetCharCmp(char c1, char c2) {
#ifdef GADGET_CASE_SENSITIVE
  return static_cast<int>(c1) - c2;
#else
  return toupper(c1) - toupper(c2);
#endif
}

bool AssignIfDiffer(
    const char *source, std::string *dest,
    int (*comparator)(const char *, const char *)) {
  ASSERT(dest);
  bool changed = false;
  if (source && source[0]) {
    if (comparator(source, dest->c_str()) != 0) {
      changed = true;
      *dest = source;
    }
  } else if (!dest->empty()){
    changed = true;
    dest->clear();
  }
  return changed;
}

std::string TrimString(const std::string &s) {
  std::string::size_type start = s.find_first_not_of(" \t\r\n");
  std::string::size_type end = s.find_last_not_of(" \t\r\n");
  if (start == std::string::npos)
    return std::string("");

  ASSERT(end != std::string::npos);
  return std::string(s, start, end - start + 1);
}

std::string ToLower(const std::string &s) {
  std::string result(s);
  std::transform(result.begin(), result.end(), result.begin(), ::tolower);
  return result;
}

std::string ToUpper(const std::string &s) {
  std::string result(s);
  std::transform(result.begin(), result.end(), result.begin(), ::toupper);
  return result;
}

void StringAppendVPrintf(std::string *dst, const char* format, va_list ap) {
  // First try with a small fixed size buffer
  char space[1024];

  // It's possible for methods that use a va_list to invalidate
  // the data in it upon use.  The fix is to make a copy
  // of the structure before using it and use that copy instead.
  va_list backup_ap;
#ifdef va_copy
  va_copy(backup_ap, ap);
#else
  backup_ap = ap;
#endif

  int result = vsnprintf(space, sizeof(space), format, backup_ap);
  va_end(backup_ap);

  if (result >= 0 && result < static_cast<int>(sizeof(space))) {
    // It fits.
    dst->append(space);
  } else {
    // Repeatedly increase buffer size until it fits
    int length = sizeof(space);
    while (true) {
      if (result < 0) {
        // Older behavior: just try doubling the buffer size.
        length *= 2;
      } else {
        // We need exactly result + 1 characters.
        length = result + 1;
      }
      char* buf = new char[length];

      // Restore the va_list before we use it again
#ifdef va_copy
      va_copy(backup_ap, ap);
#else
      backup_ap = ap;
#endif
      result = vsnprintf(buf, length, format, backup_ap);
      va_end(backup_ap);

      if ((result >= 0) && (result < length)) {
        // It fits.
        dst->append(buf);
        delete[] buf;
        break;
      }
      delete[] buf;
    }
  }
}

std::string StringPrintf(const char* format, ...) {
  std::string dst;
  va_list ap;
  va_start(ap, format);
  StringAppendVPrintf(&dst, format, ap);
  va_end(ap);
  return dst;
}

std::string StringVPrintf(const char* format, va_list ap) {
  std::string dst;
  StringAppendVPrintf(&dst, format, ap);
  return dst;
}

void StringAppendPrintf(std::string *string, const char* format, ...) {
  va_list ap;
  va_start(ap, format);
  StringAppendVPrintf(string, format, ap);
  va_end(ap);
}

bool IsValidURLChar(unsigned char c) {
  // See RFC 2396 for information: http://www.ietf.org/rfc/rfc2396.txt
  // check for INVALID character (in US-ASCII: 0-127) and consider all
  // others valid
  return !(c <= ' ' || '<'==c || '>'==c || '\"'==c || '{'==c || '}'==c ||
          // '|'==c ||     // Technically | is unadvised, but it is valid, and some URLs use it
          // '^'==c ||     // Also technically invalid but Yahoo news use it... others too
          // '`'==c ||     // Yahoo uses this
          kBackSlash==c || '['==c || ']'==c || '\n' == c || '\r' == c
          // Comparison below is always false for char:
          || c >= 128);  // Enable converting non-ascii chars
}

std::string EncodeURL(const std::string &source) {
  std::string dest;
  for (size_t c = 0; c < source.length(); c++) {
    unsigned char src = source[c];

    if (src == kBackSlash) {
      dest.append(1, kSlash);
      continue;
    }

    // %-encode disallowed URL chars (b/w 0-127)
    // (chars >=128 are considered valid... although technically they're not)
    if (!IsValidURLChar(src)) {
      // output the percent, followed by the hex value of the character
      // Note: we know it's a char in US-ASCII (0-127)
      //
      dest.append(1, '%');

      static const char kHexChars[] = "0123456789abcdef";
      dest.append(1, kHexChars[src >> 4]);
      dest.append(1, kHexChars[src & 0xF]);
    } else {
      // not a special char: just copy
      dest.append(1, src);
    }
  }
  return dest;
}

bool IsValidRSSURL(const char* url) {
  if (!url) {
    return false;
  }

  if (strncasecmp(url, kHttpUrlPrefix, arraysize(kHttpUrlPrefix) - 1) &&
      strncasecmp(url, kHttpsUrlPrefix, arraysize(kHttpsUrlPrefix) - 1) &&
      strncasecmp(url, kFeedUrlPrefix, arraysize(kFeedUrlPrefix) - 1)) {
    return false;
  }

  for (int i = 0; url[i]; i++) {
    if (!IsValidURLChar(url[i])) {
      return false;
    }
  }

  return true;
}

bool IsValidURL(const char* url) {
  if (!url) {
    return false;
  }

  if (strncasecmp(url, kHttpUrlPrefix, arraysize(kHttpUrlPrefix) - 1) &&
      strncasecmp(url, kHttpsUrlPrefix, arraysize(kHttpsUrlPrefix) - 1)) {
    // Don't allow ftp://.
    return false;
  }

  for (int i = 0; url[i]; i++) {
    if (!IsValidURLChar(url[i])) {
      return false;
    }
  }

  return true;
}

std::string GetHostFromURL(const char *url) {
  if (!url || !*url)
    return std::string();

  const char *start = strstr(url, "://");
  if (!start)
    return std::string();

  start += 3;
  const char *end = strchr(start, '/');
  // Get the part between :// and the first '/'.
  std::string result(end ? std::string(start, end - start) :
                           std::string(start));
  // Remove the user:passwd@ part.
  size_t pos = result.find('@');
  if (pos != result.npos)
    result.erase(0, pos + 1);
  // Remove the parameter part when it directly follows the host name like
  // this: http://a.com?xyz.
  pos = result.find('?');
  if (pos != result.npos)
    result.erase(pos);
  // Remove the port part.
  pos = result.find(':');
  if (pos != result.npos)
    result.erase(pos);
  return result;
}

std::string EncodeJavaScriptString(const UTF16Char *source) {
  ASSERT(source);

  std::string dest;
  for (const UTF16Char *p = source; *p; p++) {
    switch (*p) {
      // The following special chars are not so complete, but also works.
      case '"': dest += "\\\""; break;
      case '\\': dest += "\\\\"; break;
      case '\n': dest += "\\n"; break;
      case '\r': dest += "\\r"; break;
      default:
        if (*p >= 0x7f || *p < 0x20) {
          char buf[10];
          snprintf(buf, sizeof(buf), "\\u%04X", *p);
          dest += buf;
        } else {
          dest += static_cast<char>(*p);
        }
        break;
    }
  }
  return dest;
}

bool SplitString(const std::string &source, const std::string &separator,
                 std::string *result_left, std::string *result_right) {
  std::string::size_type pos = source.find(separator);
  if (pos == source.npos) {
    if (result_left && result_left != &source)
      *result_left = source;
    if (result_right)
      result_right->clear();
    return false;
  }

  // Make a copy to allow results overwrite source.
  std::string source_copy(source);
  if (result_left)
    *result_left = source_copy.substr(0, pos);
  if (result_right)
    *result_right = source_copy.substr(pos + separator.length());
  return true;
}

std::string CompressWhiteSpaces(const char *source) {
  ASSERT(source);
  std::string result;
  bool in_space = false;
  while (*source) {
    if (isspace(*source)) {
      in_space = true;
    } else {
      if (in_space) {
        if (!result.empty())
          result += ' ';
        in_space = false;
      }
      result += *source;
    }
    source++;
  }
  return result;
}

struct StringPair {
  const char *source;
  size_t source_size;
  const char *target;
  size_t target_size;
};

static const StringPair kTagsToRemove[] = {
  { "<script", 7, "</script>",  9 },
  { "<style", 6, "</style>", 8 },
  { "<!--", 4, "-->", 3 },
};

// Only well-known and widely-used entities are supported.
static const StringPair kEntities[] = {
  { "&lt", 3, "<", 1 },
  { "&gt", 3, ">", 1 },
  { "&amp", 4, "&", 1 },
  { "&reg", 4, "\xC2\xAE", 2 },
  { "&quot", 5, "\"", 1 },
  { "&apos", 5, "\'", 1 },
  { "&nbsp", 5, " ", 1 },
  { "&copy", 5, "\xC2\xA9", 2 },
};

// This function is rather simple and doesn't handle all cases.
std::string ExtractTextFromHTML(const char *source) {
  ASSERT(source);
  std::string result;
  bool in_space = false;
  bool in_tag = false;
  char in_quote = 0;
  const char *end_tag_to_remove = NULL;
  size_t end_tag_size = 0;

  while (*source) {
    char c = *source;
    const char *to_append = source;
    size_t append_size = 0;
    char utf8_buf[6]; // Used to parse numeric entities.

    if (in_quote) {
      if (c == in_quote)
        in_quote = 0;
    } else if (end_tag_to_remove) {
      if (strncasecmp(source, end_tag_to_remove, end_tag_size) == 0) {
        source += end_tag_size - 1;
        end_tag_to_remove = NULL;
      }
    } else {
      switch (c) {
        case '<':
          for (size_t i = 0; i < arraysize(kTagsToRemove); i++) {
            if (strncasecmp(source, kTagsToRemove[i].source,
                            kTagsToRemove[i].source_size) == 0) {
              source += kTagsToRemove[i].source_size + 1;
              end_tag_to_remove = kTagsToRemove[i].target;
              end_tag_size = kTagsToRemove[i].target_size;
              break;
            }
          }
          if (!end_tag_to_remove)
            in_tag = true;
          break;
        case '>':
          if (in_tag) {
            // Remove the tag and treat it as a space.
            in_space = true;
            in_tag = false;
          } else {
            append_size = 1;
          }
          break;
        case '"':
        case '\'':
          // Quotes outside of tags are treated as normal text.
          if (in_tag)
            in_quote = c;
          break;
        case '&':
          for (size_t i = 0; i < arraysize(kEntities); i++) {
            if (strncmp(source, kEntities[i].source,
                        kEntities[i].source_size) == 0 &&
                !isalnum(source[kEntities[i].source_size])) {
              source += kEntities[i].source_size;
              if (*source != ';') source--;
              to_append = kEntities[i].target;
              append_size = kEntities[i].target_size;
            }
          }
          if (!append_size) {
            if (source[1] == '#') {
              // This is a numeric entity.
              source++;
              int base = 10;
              if (source[1] == 'x' || source[1] == 'X') {
                source++;
                base = 16;
              }
              char *endptr;
              UTF32Char utf32_char =
                  static_cast<UTF32Char>(strtol(source + 1, &endptr, base));
              if (utf32_char) {
                source = endptr;
                if (*source != ';') source--;
                append_size = ConvertCharUTF32ToUTF8(utf32_char, utf8_buf,
                                                     sizeof(utf8_buf));
                to_append = utf8_buf;
              }
            } else {
              // Unsupported entity, just leave it in the result.
              append_size = 1;
            }
          }
          break;
        default:
          if (!in_tag)
            append_size = 1;
          break;
      }
    }

    if (append_size) {
      ASSERT(to_append);
      if (append_size == 1 && isspace(*to_append)) {
        in_space = true;
      } else {
        if (in_space) {
          if (!result.empty())
            result += ' ';
          in_space = false;
        }
        result.append(to_append, append_size);
      }
    }
    source++;
  }
  return result;
}

bool ContainsHTML(const char *s) {
  if (!s || !*s)
    return false;

  const int kMaxSearch = 50000;
  int i = 0;
  while (s[i] && s[i] != '<' && i < kMaxSearch)  // must contain <
    i++;

  if (s[i] != '<')
    return false;

  i = 0;
  while (s[i] && i < kMaxSearch) {
    if (s[i] != '/' && s[i] != '<') {
      i++;
      continue;
    }

    if (s[i] == '/' && s[i + 1] == '>')  // />
      return true;

    if (s[i] == '<') {
      int first = tolower(s[i + 1]);
      if (first) {
        if (first == '/' || first == '!')
          return true;
        int second = tolower(s[i + 2]);
        if (second) {
          if (first == 'p' && second == '>')  // <p>
            return true;
          if ((first == 'b' || first == 'h') && (s[i + 3] == '>'))
            return true;  // <b?> <h?> (<br>, <hr>, <h1>, <h2>, etc.)
        }
      }
    }
    i++;
  }
  return false;
}

bool SimpleMatchXPath(const char *xpath, const char *pattern) {
  ASSERT(xpath && pattern);
  while (*xpath && *pattern) {
    // In the case of xpath[0] == '[', return false because it's invalid.
    if (GadgetCharCmp(*xpath, *pattern) != 0)
      return false;
    xpath++;
    pattern++;
    if (*xpath == '[') {
      while (*xpath && *xpath != ']')
        xpath++;
      if (*xpath == ']')
        xpath++;
    }
  }

  return !*xpath && !*pattern;
}

static const int kNumVersionParts = 4;

static bool ParseVersion(const char *version,
                         int parsed_version[kNumVersionParts]) {
  char *end_ptr;
  for (int i = 0; i < kNumVersionParts; i++) {
    if (!isdigit(version[0]))
      return false;
    long v = strtol(version, &end_ptr, 10);
    if (v < 0 || v > SHRT_MAX)
      return false;
    parsed_version[i] = static_cast<int>(v);

    if (i < kNumVersionParts - 1) {
      if (*end_ptr != '.')
        return false;
      version = end_ptr + 1;
    }
  }
  return *end_ptr == '\0';
}

bool CompareVersion(const char *version1, const char *version2, int *result) {
  ASSERT(result);
  int parsed_version1[kNumVersionParts], parsed_version2[kNumVersionParts];
  if (ParseVersion(version1, parsed_version1) &&
      ParseVersion(version2, parsed_version2)) {
    for (int i = 0; i < kNumVersionParts; i++) {
      if (parsed_version1[i] < parsed_version2[i]) {
        *result = -1;
        return true;
      }
      if (parsed_version1[i] > parsed_version2[i]) {
        *result = 1;
        return true;
      }
    }

    *result = 0;
    return true;
  }
  return false;
}

}  // namespace ggadget