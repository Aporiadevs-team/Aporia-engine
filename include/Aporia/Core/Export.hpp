#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(APORIA_BUILDING_LIBRARY)
#    define APORIA_API __declspec(dllexport)
#  elif defined(APORIA_USING_LIBRARY)
#    define APORIA_API __declspec(dllimport)
#  else
#    define APORIA_API
#  endif
#else
#  if defined(APORIA_BUILDING_LIBRARY)
#    define APORIA_API __attribute__((visibility("default")))
#  else
#    define APORIA_API
#  endif
#endif
