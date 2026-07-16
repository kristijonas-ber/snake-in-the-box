#pragma once

#include <string>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

// Create a single directory (no parents). Returns true if it exists afterward.
static inline bool mkdirOne(const std::string &dir)
{
    if (dir.empty() || dir == "." || dir == "/")
        return true;
#ifdef _WIN32
    if (_access(dir.c_str(), 0) == 0) return true;
    return _mkdir(dir.c_str()) == 0 || _access(dir.c_str(), 0) == 0;
#else
    struct stat st;
    if (stat(dir.c_str(), &st) == 0) return S_ISDIR(st.st_mode);
    if (mkdir(dir.c_str(), 0777) == 0) return true;
    return stat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

// Create `path` and every parent directory. Accepts '/' or '\' separators, and
// both relative and absolute paths. Each ancestor prefix is created in turn:
// "a/b/c" -> "a", "a/b", "a/b/c". Returns true on success.
static inline bool ensureDirRecursive(const char *path)
{
    if (!path || !*path) return false;

    std::string s(path);
    for (char &c : s)
        if (c == '\\') c = '/';

    // Create each prefix ending at a separator, then the whole path.
    for (size_t i = 1; i <= s.size(); i++)
    {
        if (i == s.size() || s[i] == '/')
        {
            std::string sub = s.substr(0, i);
            if (!mkdirOne(sub))
                return false;
        }
    }
    return true;
}
