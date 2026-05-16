cmake_minimum_required(VERSION 3.16)
project(ptolemy
    VERSION     1.0.0
    DESCRIPTION "H_hat_RB Field Engine — C Monad (Native Space)"
    LANGUAGES   C
)

# ── Standard install paths (PREFIX / bin / lib / share / include) ─────────────
include(GNUInstallDirs)

# ── Generate compile_commands.json for IDE / clangd ──────────────────────────
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Compiler flags ────────────────────────────────────────────────────────────
add_compile_options(
    -O2
    -Wall
    -Wextra
    -std=c11
    -D_POSIX_C_SOURCE=200809L
)

# ── Required: libm ────────────────────────────────────────────────────────────
find_library(MATH_LIB m REQUIRED)

# ── Optional: libxml2 (Phase 3 — HTML/XML extraction) ────────────────────────
find_package(LibXml2 QUIET)
if(LibXml2_FOUND)
    message(STATUS "libxml2 found — HTML/XML extraction enabled (Phase 3)")
    add_compile_definitions(HAVE_LIBXML2)
else()
    message(STATUS "libxml2 not found — HTML/XML ingested as plain text")
endif()

# ── Optional: poppler-glib (Phase 3 — PDF extraction) ────────────────────────
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(POPPLER QUIET poppler-glib)
    if(POPPLER_FOUND)
        message(STATUS "poppler-glib found — PDF extraction enabled (Phase 3)")
        add_compile_definitions(HAVE_POPPLER)
    else()
        message(STATUS "poppler-glib not found — PDF files skipped")
    endif()
endif()

# ── Main binary ───────────────────────────────────────────────────────────────
add_executable(ptolemy
    main.c
    monad.c
    checkpoint.c
    tokenizer.c
    filter.c
    ingest.c
    log.c
    daemon.c
)

target_link_libraries(ptolemy PRIVATE ${MATH_LIB})

if(LibXml2_FOUND)
    target_include_directories(ptolemy PRIVATE ${LIBXML2_INCLUDE_DIRS})
    target_link_libraries(ptolemy PRIVATE ${LIBXML2_LIBRARIES})
endif()

if(POPPLER_FOUND)
    target_include_directories(ptolemy PRIVATE ${POPPLER_INCLUDE_DIRS})
    target_link_libraries(ptolemy PRIVATE ${POPPLER_LIBRARIES})
endif()

# ── Tools: checkpoint_expand ──────────────────────────────────────────────────
add_executable(checkpoint_expand tools/checkpoint_expand.c)
target_link_libraries(checkpoint_expand PRIVATE ${MATH_LIB})

# ── Install targets ───────────────────────────────────────────────────────────
install(TARGETS ptolemy
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(TARGETS checkpoint_expand
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# ── Man pages ─────────────────────────────────────────────────────────────────
find_program(GZIP_EXECUTABLE gzip)

if(GZIP_EXECUTABLE)
    add_custom_command(
        OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/ptolemy.1.gz
        COMMAND ${GZIP_EXECUTABLE} -cn ${CMAKE_CURRENT_SOURCE_DIR}/man/ptolemy.1
                > ${CMAKE_CURRENT_BINARY_DIR}/ptolemy.1.gz
        DEPENDS man/ptolemy.1
    )
    add_custom_command(
        OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/ptolemyignore.5.gz
        COMMAND ${GZIP_EXECUTABLE} -cn ${CMAKE_CURRENT_SOURCE_DIR}/man/ptolemyignore.5
                > ${CMAKE_CURRENT_BINARY_DIR}/ptolemyignore.5.gz
        DEPENDS man/ptolemyignore.5
    )
    add_custom_target(man ALL
        DEPENDS
            ${CMAKE_CURRENT_BINARY_DIR}/ptolemy.1.gz
            ${CMAKE_CURRENT_BINARY_DIR}/ptolemyignore.5.gz
    )
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/ptolemy.1.gz
        DESTINATION ${CMAKE_INSTALL_MANDIR}/man1
    )
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/ptolemyignore.5.gz
        DESTINATION ${CMAKE_INSTALL_MANDIR}/man5
    )
else()
    install(FILES man/ptolemy.1
        DESTINATION ${CMAKE_INSTALL_MANDIR}/man1
    )
    install(FILES man/ptolemyignore.5
        DESTINATION ${CMAKE_INSTALL_MANDIR}/man5
    )
endif()

# ── systemd user units (optional — skipped if systemd not present) ────────────
set(SYSTEMD_USER_UNIT_DIR "" CACHE PATH "systemd user unit directory (empty=auto)")
if(SYSTEMD_USER_UNIT_DIR STREQUAL "")
    find_program(SYSTEMCTL systemctl)
    if(SYSTEMCTL)
        set(SYSTEMD_USER_UNIT_DIR "${CMAKE_INSTALL_PREFIX}/lib/systemd/user")
    endif()
endif()
if(NOT SYSTEMD_USER_UNIT_DIR STREQUAL "")
    install(FILES
        systemd/ptolemy.socket
        systemd/ptolemy.service
        DESTINATION ${SYSTEMD_USER_UNIT_DIR}
    )
    message(STATUS "systemd user units → ${SYSTEMD_USER_UNIT_DIR}")
    message(STATUS "  Enable with: systemctl --user enable --now ptolemy.socket")
endif()

# ── Data: create ~/.ptolemy on install ───────────────────────────────────────
# The runtime init_ptolemy_dir() handles this at first run; the install step
# merely documents the canonical home directory in the build system.
message(STATUS "Canonical monad home: ~/.ptolemy/monad_wordnet.bin")
message(STATUS "Override with: PTOLEMY_HOME or PTOLEMY_CHECKPOINT env vars")

# ── Summary ───────────────────────────────────────────────────────────────────
message(STATUS "")
message(STATUS "ptolemy ${PROJECT_VERSION} — H_hat_RB Field Engine")
message(STATUS "  N = 25000 Riemann zeros")
message(STATUS "  Native Space: radial complex spherical polar (σ₀–σ₄)")
message(STATUS "  σ = ½ forced by Noether balance")
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
