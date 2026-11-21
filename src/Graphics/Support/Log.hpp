#pragma once

namespace Aporia::graphics_internal {

// Engine-side file log (aporia_engine.log), one flushed line per call.
// Use for diagnostics that must survive a crash; not for per-frame output.
void engineLog(const char* format, ...);

} // namespace Aporia::graphics_internal
