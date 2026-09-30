#pragma once

inline constexpr int RTLD_NOW = 2;
inline constexpr int RTLD_LOCAL = 0;

extern "C"
{
    void* dlopen(const char* name, int flags);
    void* dlsym(void* library, const char* name);
    const char* dlerror();
    int dlclose(void* library);
}
