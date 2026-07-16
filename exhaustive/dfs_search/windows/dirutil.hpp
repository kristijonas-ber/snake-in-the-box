#pragma once

#include <string>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#else
#include <cstdio>
#include <sys/stat.h>
#include <sys/types.h>
#endif

static inline bool ensureDirRecursive(const char *path)
{
    if (!path || !*path) return false;

    std::string cur(path);
    for (char &c : cur)
        if (c == '\\') c = '/';

    std::string part;
    for (size_t i = 0; i < cur.size(); i++)
    {
        char c = cur[i];
        part.push_back(c);
        if (c != '/')
            continue;

        while (!part.empty() && part.back() == '/')
            part.pop_back();
        if (part.empty() || part == ".")
            continue;

#ifdef _WIN32
        if (_access(part.c_str(), 0) == 0)
            continue;
        if (_mkdir(part.c_str()) != 0 && _access(part.c_str(), 0) != 0)
            return false;
#else
        struct stat st;
        if (stat(part.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
            continue;
        if (mkdir(part.c_str(), 0777) != 0)
        {
            if (stat(part.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))
                return false;
        }
#endif
    }

    while (!part.empty() && part.back() == '/')
        part.pop_back();
    if (!part.empty() && part != ".")
    {
#ifdef _WIN32
        if (_access(part.c_str(), 0) != 0 && _mkdir(part.c_str()) != 0 && _access(part.c_str(), 0) != 0)
            return false;
#else
        struct stat st;
        if (stat(part.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))
        {
            if (mkdir(part.c_str(), 0777) != 0)
            {
                if (stat(part.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))
                    return false;
            }
        }
#endif
    }

    return true;
}
