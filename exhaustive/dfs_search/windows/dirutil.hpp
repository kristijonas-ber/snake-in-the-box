#pragma once

#include <cstdio>
#include <string>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

static inline bool ensureDirRecursive(const char *path)
{
    if (!path || !*path) return false;

    std::string cur(path);
    for (char &c : cur)
        if (c == '\\') c = '/';

    for (size_t i = 1; i <= cur.size(); i++)
    {
        if (i < cur.size() && cur[i] != '/')
            continue;

        std::string part = cur.substr(0, i);
        while (!part.empty() && part.back() == '/')
            part.pop_back();
        if (part.empty() || part == ".")
            continue;

#ifdef _WIN32
        if (_mkdir(part.c_str()) != 0)
#else
        if (mkdir(part.c_str(), 0777) != 0)
#endif
        {
            FILE *probe = fopen(part.c_str(), "rb");
            if (!probe) return false;
            fclose(probe);
        }
    }

    if (cur.back() != '/')
    {
        std::string part = cur;
        while (!part.empty() && part.back() == '/')
            part.pop_back();
        if (!part.empty() && part != ".")
        {
#ifdef _WIN32
            if (_mkdir(part.c_str()) != 0)
#else
            if (mkdir(part.c_str(), 0777) != 0)
#endif
            {
                FILE *probe = fopen(part.c_str(), "rb");
                if (!probe) return false;
                fclose(probe);
            }
        }
    }

    return true;
}
