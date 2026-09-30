# fontSfntCheck: compile stb_truetype for this build with TC_STBTT_TEST_LIMITS
# (core/include/impl/stb_impl.cpp). The CFF vertex-count limit and a cap on
# STBTT_malloc sizes then become variables the test sets, so it reaches both
# limits with small fonts. Only stb_impl.cpp is compiled differently.
if(TARGET TrussC)
    get_filename_component(_tc_stb_impl "${TRUSSC_DIR}/include/impl/stb_impl.cpp" ABSOLUTE)
    set_property(SOURCE "${_tc_stb_impl}" TARGET_DIRECTORY TrussC
                 APPEND PROPERTY COMPILE_DEFINITIONS TC_STBTT_TEST_LIMITS)
endif()
