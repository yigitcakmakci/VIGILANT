#pragma once
#include <windows.h>
#include <string>

namespace ResourceLoader {

// Load an RCDATA resource as a UTF-8 std::string.
// Returns an empty string on failure.
inline std::string LoadTextResource(int resourceId) {
    HRSRC hRes = FindResourceW(NULL, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!hRes) return {};

    HGLOBAL hData = LoadResource(NULL, hRes);
    if (!hData) return {};

    DWORD size = SizeofResource(NULL, hRes);
    const char* data = static_cast<const char*>(LockResource(hData));
    if (!data || size == 0) return {};

    return std::string(data, size);
}

} // namespace ResourceLoader
