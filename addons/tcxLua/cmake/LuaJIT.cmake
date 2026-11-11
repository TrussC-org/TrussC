# Owned integration for the unchanged zhaozg/luajit-cmake sources.
# The vendor's CMake 3.10 policy scope must honor the parent's source path.
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
get_filename_component(LUAJIT_DIR "${CMAKE_CURRENT_LIST_DIR}/../LuaJIT" ABSOLUTE)

# TrussC does not enable GDB JIT registration. Keep the upstream GDB source
# out of ordinary builds (its disabled body contains nonportable macro calls).
if(LUAJIT_ENABLE_GDBJIT OR LUAJIT_USE_GDBJIT)
    message(FATAL_ERROR "TrussC builds do not support GDB JIT registration")
endif()

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../luajit-cmake" luajit-cmake)
get_target_property(tcx_luajit_sources libluajit SOURCES)
list(REMOVE_ITEM tcx_luajit_sources "${LUAJIT_DIR}/src/lj_gdbjit.c")
set_property(TARGET libluajit PROPERTY SOURCES "${tcx_luajit_sources}")

# Use the engine's generated rolling header instead of the stale beta3 copy.
get_target_property(tcx_luajit_binary_dir libluajit BINARY_DIR)
target_include_directories(luajit-header INTERFACE "${tcx_luajit_binary_dir}")
add_dependencies(luajit-header buildvm_arch_h)
