cmake_minimum_required(VERSION 3.20)

# tcxNodeInspector draws through Dear ImGui, so it needs the tcxImGui addon's
# headers at compile time. Auto-collection links only TrussC, so we provide a
# custom build that also links the sibling tcxImGui target (loaded first via
# addons.make / the addon.json dependency) — PUBLIC so its include dirs reach
# both this addon's sources and the host app.

set(ADDON_NAME tcxNodeInspector)

file(GLOB ADDON_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
file(GLOB ADDON_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)

add_library(${ADDON_NAME} STATIC ${ADDON_SOURCES} ${ADDON_HEADERS})
target_include_directories(${ADDON_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(${ADDON_NAME} PUBLIC TrussC tcxImGui)

add_library(tc::tcxNodeInspector ALIAS tcxNodeInspector)
