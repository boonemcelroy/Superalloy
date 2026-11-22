/*
  ==============================================================================
    Superalloy — ConsoleUtf8.h
    The tools print UTF-8 (metal names such as "René 41", "Copper — Grade 1",
    "0 Ω"). A Windows console defaults to the OEM code page (437 in the US), which
    shows those bytes as "Ren├⌐ 41". Switching the console's output code page to
    UTF-8 at start-up makes the text read correctly. Elsewhere this does nothing.

    Declared directly rather than through <windows.h>, so no Windows macros
    (min, max, near, far) leak into the tools. The declaration is identical to
    the one in <windows.h>, so both can coexist. kernel32 is linked by default.
  ==============================================================================
*/
#pragma once

#if defined (_WIN32)
extern "C" __declspec (dllimport) int __stdcall SetConsoleOutputCP (unsigned int codePage);
#endif

namespace superalloy::tools
{
    inline void useUtf8Console() noexcept
    {
       #if defined (_WIN32)
        SetConsoleOutputCP (65001u);    // CP_UTF8
       #endif
    }
}
