#include "Log.hpp"

#include <cstdarg>
#include <cstdio>

namespace Aporia::graphics_internal {

void engineLog(const char* format, ...) {
    FILE* logFile = nullptr;
    if (fopen_s(&logFile, "aporia_engine.log", "a") != 0 || logFile == nullptr) {
        return;
    }
    va_list arguments;
    va_start(arguments, format);
    std::vfprintf(logFile, format, arguments);
    va_end(arguments);
    std::fputc('\n', logFile);
    std::fflush(logFile);
    std::fclose(logFile);
}

} // namespace Aporia::graphics_internal
