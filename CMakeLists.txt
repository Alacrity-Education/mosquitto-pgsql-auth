cmake_minimum_required(VERSION 3.16)
project(mosquitto_pgsql_auth VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include(ExternalProject)
find_program(MAKE_EXECUTABLE make REQUIRED)
find_program(PERL_EXECUTABLE perl REQUIRED)

# ---- Mosquitto headers (from host system) ----
find_path(MOSQUITTO_INCLUDE_DIR
    NAMES mosquitto/broker_plugin.h
    PATHS /usr/include /usr/local/include
    REQUIRED
)

# ---- Build libpq from source (static, no external runtime deps) ----
set(PG_VERSION  "17.2")
set(PG_INST_DIR "${CMAKE_BINARY_DIR}/libpq-install")

ExternalProject_Add(libpq_ext
    URL                       "https://ftp.postgresql.org/pub/source/v${PG_VERSION}/postgresql-${PG_VERSION}.tar.gz"
    TLS_VERIFY                ON
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE

    INSTALL_DIR "${PG_INST_DIR}"

    CONFIGURE_COMMAND <SOURCE_DIR>/configure
        --prefix=<INSTALL_DIR>
        --enable-static
        --without-readline
        --without-icu
        --without-gssapi
        --without-ldap
        --without-pam
        --without-openssl
        --without-zlib
        --disable-nls
        CC=${CMAKE_C_COMPILER}
        CFLAGS=-std=gnu17

    # libpq's Makefile auto-pulls pgcommon_shlib and pgport_shlib via SHLIB_PREREQS
    BUILD_COMMAND ${MAKE_EXECUTABLE} -C src/interfaces/libpq

    # Install libpq headers+lib, then copy the PIC-compiled pgcommon/pgport
    # archives from the build tree into the install prefix
    INSTALL_COMMAND
        ${MAKE_EXECUTABLE} -C src/interfaces/libpq install
        COMMAND ${CMAKE_COMMAND} -E copy
                <SOURCE_DIR>/src/include/postgres_ext.h
                <INSTALL_DIR>/include/postgres_ext.h
        COMMAND ${CMAKE_COMMAND} -E copy
                src/common/libpgcommon_shlib.a
                <INSTALL_DIR>/lib/libpgcommon.a
        COMMAND ${CMAKE_COMMAND} -E copy
                src/port/libpgport_shlib.a
                <INSTALL_DIR>/lib/libpgport.a

    BUILD_BYPRODUCTS
        "${PG_INST_DIR}/lib/libpq.a"
        "${PG_INST_DIR}/lib/libpgcommon.a"
        "${PG_INST_DIR}/lib/libpgport.a"
        "${PG_INST_DIR}/include/libpq-fe.h"
)

ExternalProject_Get_Property(libpq_ext BINARY_DIR)

# Run PostgreSQL's own generated-headers target before building libpq.
# This generates errcodes.h, catalog headers, node headers, lwlocknames.h, etc.
# and symlinks them all into src/include/ where CPPFLAGS points (-I src/include).
ExternalProject_Add_Step(libpq_ext generate_headers
    COMMAND ${MAKE_EXECUTABLE} -C src/backend generated-headers
    WORKING_DIRECTORY "${BINARY_DIR}"
    DEPENDEES configure
    DEPENDERS build
)

# ---- Plugin target ----
add_library(mosquitto_pgsql_auth MODULE src/plugin.c)
add_dependencies(mosquitto_pgsql_auth libpq_ext)

set_target_properties(mosquitto_pgsql_auth PROPERTIES
    PREFIX ""
    SUFFIX ".so"
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET default
)

target_include_directories(mosquitto_pgsql_auth PRIVATE
    ${MOSQUITTO_INCLUDE_DIR}
    "${PG_INST_DIR}/include"
)

target_compile_options(mosquitto_pgsql_auth PRIVATE -Wall -Wextra)

# libpq/pgcommon/pgport are fully self-contained when built without openssl/gssapi/ldap:
# no additional dynamic libraries needed in the container at runtime.
target_link_libraries(mosquitto_pgsql_auth PRIVATE
    "-Wl,-Bstatic"
    "${PG_INST_DIR}/lib/libpq.a"
    "${PG_INST_DIR}/lib/libpgcommon.a"
    "${PG_INST_DIR}/lib/libpgport.a"
    "-Wl,-Bdynamic"
)

# ---- Install ----
include(GNUInstallDirs)
install(TARGETS mosquitto_pgsql_auth
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/mosquitto"
)
