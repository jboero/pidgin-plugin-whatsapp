cmake_minimum_required(VERSION 3.16)
project(pidgin-whatsapp C)

# ── Find dependencies ──────────────────────────────────────────────
find_package(PkgConfig REQUIRED)
pkg_check_modules(PURPLE REQUIRED purple)
pkg_check_modules(GLIB REQUIRED glib-2.0)

# Optional: GDK pixbuf for image handling
pkg_check_modules(PIXBUF gdk-pixbuf-2.0)
if(PIXBUF_FOUND)
    add_definitions(-DHAVE_GDK_PIXBUF)
endif()

# ── Determine install paths ───────────────────────────────────────
if(NOT DEFINED PURPLE_PLUGIN_DIR)
    execute_process(
        COMMAND ${PKG_CONFIG_EXECUTABLE} --variable=plugindir purple
        OUTPUT_VARIABLE PURPLE_PLUGIN_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endif()

if(NOT DEFINED PURPLE_DATA_DIR)
    execute_process(
        COMMAND ${PKG_CONFIG_EXECUTABLE} --variable=datarootdir purple
        OUTPUT_VARIABLE PURPLE_DATA_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endif()

message(STATUS "Purple plugin dir: ${PURPLE_PLUGIN_DIR}")
message(STATUS "Purple data dir:   ${PURPLE_DATA_DIR}")
message(STATUS "Purple includes:   ${PURPLE_INCLUDE_DIRS}")

# ── Go static library build ───────────────────────────────────────
set(GO_SRC_DIR "${CMAKE_SOURCE_DIR}/src/go")
set(GO_BUILD_DIR "${CMAKE_BINARY_DIR}/go-build")
set(GO_ARCHIVE "${GO_BUILD_DIR}/libwhatsmeow-bridge.a")
set(GO_HEADER "${GO_BUILD_DIR}/libwhatsmeow-bridge.h")

file(MAKE_DIRECTORY ${GO_BUILD_DIR})

# Copy bridge.h so Go can find it
configure_file(${GO_SRC_DIR}/bridge.h ${GO_BUILD_DIR}/bridge.h COPYONLY)

# Build Go code as a C static archive
add_custom_command(
    OUTPUT ${GO_ARCHIVE} ${GO_HEADER}
    COMMAND ${CMAKE_COMMAND} -E env
        CGO_ENABLED=1
        CGO_CFLAGS=-I${GO_BUILD_DIR}
        GOPATH=${GO_BUILD_DIR}/gopath
        GOMODCACHE=${GO_BUILD_DIR}/gomod
        go build
            -buildmode=c-archive
            -o ${GO_ARCHIVE}
            .
    WORKING_DIRECTORY ${GO_SRC_DIR}
    COMMENT "Building Go whatsmeow bridge (this downloads modules on first run)..."
    DEPENDS
        ${GO_SRC_DIR}/whatsmeow_bridge.go
        ${GO_SRC_DIR}/bridge.h
        ${GO_SRC_DIR}/go.mod
)

add_custom_target(go-bridge DEPENDS ${GO_ARCHIVE})

# ── C shared library (the actual .so plugin) ──────────────────────
add_library(whatsmeow-lite SHARED
    src/c/plugin.c
)

add_dependencies(whatsmeow-lite go-bridge)

target_include_directories(whatsmeow-lite PRIVATE
    ${PURPLE_INCLUDE_DIRS}
    ${GLIB_INCLUDE_DIRS}
    ${GO_BUILD_DIR}          # for bridge.h and generated Go header
    ${GO_SRC_DIR}            # for bridge.h
)

target_link_directories(whatsmeow-lite PRIVATE
    ${PURPLE_LIBRARY_DIRS}
    ${GLIB_LIBRARY_DIRS}
)

target_link_libraries(whatsmeow-lite
    ${GO_ARCHIVE}
    ${PURPLE_LIBRARIES}
    ${GLIB_LIBRARIES}
    pthread
    m
    resolv
)

# Strip the "lib" prefix — purple wants "whatsmeow-lite.so" not "libwhatsmeow-lite.so"
# Actually, purple is fine with either. Let's keep the lib prefix for clarity.
set_target_properties(whatsmeow-lite PROPERTIES
    OUTPUT_NAME "whatsmeow-lite"
    PREFIX "lib"
)

if(PIXBUF_FOUND)
    target_include_directories(whatsmeow-lite PRIVATE ${PIXBUF_INCLUDE_DIRS})
    target_link_libraries(whatsmeow-lite ${PIXBUF_LIBRARIES})
endif()

# ── Install ───────────────────────────────────────────────────────
install(TARGETS whatsmeow-lite DESTINATION ${PURPLE_PLUGIN_DIR})

# Install protocol icons (optional — create your own or borrow from purple-gowhatsapp)
# install(DIRECTORY assets/pixmaps/ DESTINATION ${PURPLE_DATA_DIR}/pixmaps/)
