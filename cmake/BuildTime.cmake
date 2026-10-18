# Writes the build time to OUT (cmake -DOUT=<file> -P BuildTime.cmake), only when it changed, so a rebuild within the
# same minute recompiles nothing.
string(TIMESTAMP now "%Y-%m-%d %H:%M")
set(content "#define PLUGIN_BUILD_TIME \"${now}\"\n")

if(EXISTS "${OUT}")
    file(READ "${OUT}" old)
endif()
if(NOT old STREQUAL content)
    file(WRITE "${OUT}" "${content}")
endif()
