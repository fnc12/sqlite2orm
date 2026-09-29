# Find the clang-format the formatting tests run: version 19 exactly, since other versions format
# this tree differently and an older one in PATH would only produce noise.
#
# The GitHub runners have no clang-format 19, and without it the formatting tests are never
# registered, so a green CI said nothing about formatting at all. Where none is installed, the
# pinned release wheel from PyPI is downloaded into the build tree instead: a wheel is a zip that
# carries a static binary, so it needs neither python nor root. That is on by default wherever the
# `CI` environment variable is set -- GitHub Actions sets it -- and off elsewhere, where configuring
# should not reach the network behind a developer's back; -DSQLITE2ORM_FETCH_CLANG_FORMAT=ON asks for
# it on any machine.
#
# A download that was asked for and cannot happen stops the configure: skipping the tests quietly
# is exactly the hole this closes.
#
# Bumping the version means replacing every wheel URL and hash below, and the `19` in the checks.
set(SQLITE2ORM_CLANG_FORMAT_WHEEL_BASE "https://files.pythonhosted.org/packages/py2.py3/c/clang-format")
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
    set(sqlite2orm_clang_format_wheel "clang_format-19.1.7-py2.py3-none-manylinux_2_17_x86_64.manylinux2014_x86_64.whl")
    set(sqlite2orm_clang_format_wheel_sha256 "f4906fb463dd2033032978f56962caab268c9428a384126b9400543eb667f11c")
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    set(sqlite2orm_clang_format_wheel "clang_format-19.1.7-py2.py3-none-manylinux_2_17_aarch64.manylinux2014_aarch64.whl")
    set(sqlite2orm_clang_format_wheel_sha256 "dac394c83a9233ab6707f66e1cdbd950f8b014b58604142a5b6f7998bf0bcc8c")
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64")
    set(sqlite2orm_clang_format_wheel "clang_format-19.1.7-py2.py3-none-macosx_11_0_arm64.whl")
    set(sqlite2orm_clang_format_wheel_sha256 "776f89c7b056c498c0e256485bc031cbf514aaebe71e929ed54e50c478524b65")
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "x86_64")
    set(sqlite2orm_clang_format_wheel "clang_format-19.1.7-py2.py3-none-macosx_10_9_x86_64.whl")
    set(sqlite2orm_clang_format_wheel_sha256 "a09f34d2c89d176581858ff718c327eebc14eb6415c176dab4af5bfd8582a999")
else()
    set(sqlite2orm_clang_format_wheel "")
    set(sqlite2orm_clang_format_wheel_sha256 "")
endif()

# The URL and the hash can be pointed elsewhere -- a mirror, or the test of this file -- but the
# hash is never skipped.
if(NOT DEFINED SQLITE2ORM_CLANG_FORMAT_WHEEL_URL AND sqlite2orm_clang_format_wheel)
    set(SQLITE2ORM_CLANG_FORMAT_WHEEL_URL "${SQLITE2ORM_CLANG_FORMAT_WHEEL_BASE}/${sqlite2orm_clang_format_wheel}")
endif()
if(NOT DEFINED SQLITE2ORM_CLANG_FORMAT_WHEEL_SHA256)
    set(SQLITE2ORM_CLANG_FORMAT_WHEEL_SHA256 "${sqlite2orm_clang_format_wheel_sha256}")
endif()

if(DEFINED ENV{CI})
    set(sqlite2orm_fetch_clang_format_default ON)
else()
    set(sqlite2orm_fetch_clang_format_default OFF)
endif()
option(SQLITE2ORM_FETCH_CLANG_FORMAT "Download clang-format 19 for the formatting tests when it is not installed"
       ${sqlite2orm_fetch_clang_format_default})

function(sqlite2orm_is_clang_format_19 executable out_variable)
    execute_process(
        COMMAND "${executable}" --version
        OUTPUT_VARIABLE version
        ERROR_QUIET)
    if(version MATCHES "clang-format version 19\\.")
        set(${out_variable} TRUE PARENT_SCOPE)
    else()
        set(${out_variable} FALSE PARENT_SCOPE)
    endif()
endfunction()

find_program(SQLITE2ORM_CLANG_FORMAT_EXECUTABLE NAMES clang-format-19 clang-format)
set(sqlite2orm_clang_format_is_19 FALSE)
if(SQLITE2ORM_CLANG_FORMAT_EXECUTABLE)
    sqlite2orm_is_clang_format_19("${SQLITE2ORM_CLANG_FORMAT_EXECUTABLE}" sqlite2orm_clang_format_is_19)
endif()

if(NOT sqlite2orm_clang_format_is_19 AND SQLITE2ORM_FETCH_CLANG_FORMAT)
    if(NOT SQLITE2ORM_CLANG_FORMAT_WHEEL_URL OR NOT SQLITE2ORM_CLANG_FORMAT_WHEEL_SHA256)
        message(FATAL_ERROR "[sqlite2orm] No clang-format 19 wheel is pinned for "
                            "${CMAKE_HOST_SYSTEM_NAME} ${CMAKE_HOST_SYSTEM_PROCESSOR}. Install clang-format 19, "
                            "or configure with -DSQLITE2ORM_FETCH_CLANG_FORMAT=OFF to skip the formatting tests.")
    endif()
    set(sqlite2orm_clang_format_dir "${CMAKE_BINARY_DIR}/clang-format-19")
    # An existing wheel with the right hash is not downloaded again.
    file(DOWNLOAD "${SQLITE2ORM_CLANG_FORMAT_WHEEL_URL}" "${sqlite2orm_clang_format_dir}/clang-format.whl"
         EXPECTED_HASH SHA256=${SQLITE2ORM_CLANG_FORMAT_WHEEL_SHA256}
         STATUS sqlite2orm_clang_format_download)
    list(GET sqlite2orm_clang_format_download 0 sqlite2orm_clang_format_download_code)
    if(NOT sqlite2orm_clang_format_download_code EQUAL 0)
        list(GET sqlite2orm_clang_format_download 1 sqlite2orm_clang_format_download_message)
        message(FATAL_ERROR "[sqlite2orm] Could not download clang-format 19 from "
                            "${SQLITE2ORM_CLANG_FORMAT_WHEEL_URL}: ${sqlite2orm_clang_format_download_message}. "
                            "Install clang-format 19, or configure with -DSQLITE2ORM_FETCH_CLANG_FORMAT=OFF "
                            "to skip the formatting tests.")
    endif()
    file(REMOVE_RECURSE "${sqlite2orm_clang_format_dir}/wheel")
    file(MAKE_DIRECTORY "${sqlite2orm_clang_format_dir}/wheel")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E tar xf "${sqlite2orm_clang_format_dir}/clang-format.whl"
        WORKING_DIRECTORY "${sqlite2orm_clang_format_dir}/wheel"
        RESULT_VARIABLE sqlite2orm_clang_format_extract)
    set(sqlite2orm_clang_format_binary "${sqlite2orm_clang_format_dir}/wheel/clang_format/data/bin/clang-format")
    if(NOT sqlite2orm_clang_format_extract EQUAL 0 OR NOT EXISTS "${sqlite2orm_clang_format_binary}")
        message(FATAL_ERROR "[sqlite2orm] ${SQLITE2ORM_CLANG_FORMAT_WHEEL_URL} does not unpack to "
                            "clang_format/data/bin/clang-format.")
    endif()
    # A zip does not reliably keep the executable bit, so the binary is copied out with one.
    file(COPY "${sqlite2orm_clang_format_binary}" DESTINATION "${sqlite2orm_clang_format_dir}"
         FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
    set(SQLITE2ORM_CLANG_FORMAT_EXECUTABLE "${sqlite2orm_clang_format_dir}/clang-format")
    sqlite2orm_is_clang_format_19("${SQLITE2ORM_CLANG_FORMAT_EXECUTABLE}" sqlite2orm_clang_format_is_19)
    if(NOT sqlite2orm_clang_format_is_19)
        message(FATAL_ERROR "[sqlite2orm] The clang-format downloaded from ${SQLITE2ORM_CLANG_FORMAT_WHEEL_URL} "
                            "does not report version 19.")
    endif()
    message(STATUS "[sqlite2orm] Using the downloaded clang-format 19: ${SQLITE2ORM_CLANG_FORMAT_EXECUTABLE}")
endif()

if(NOT sqlite2orm_clang_format_is_19)
    message(STATUS "[sqlite2orm] clang-format 19 not found, skipping the formatting tests.")
    set(SQLITE2ORM_CLANG_FORMAT_EXECUTABLE "")
endif()
