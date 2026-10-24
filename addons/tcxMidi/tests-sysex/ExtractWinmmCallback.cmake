# Compile the pinned backend's actual callback on the host with mocked WinMM
# calls. Keep extraction identical for the normal target and mutation overlays.
file(READ "${SOURCE_FILE}" winmm)
string(FIND "${winmm}" "  static constexpr int bytes_for_message(" begin)
string(FIND "${winmm}" "  HMIDIIN inHandle;" end)
if(begin LESS 0 OR end LESS begin)
    message(FATAL_ERROR "Cannot extract the checked WinMM input callback")
endif()
math(EXPR length "${end} - ${begin}")
string(SUBSTRING "${winmm}" ${begin} ${length} callback)
file(CONFIGURE OUTPUT "${OUTPUT_FILE}" CONTENT "${callback}" @ONLY NEWLINE_STYLE UNIX)
