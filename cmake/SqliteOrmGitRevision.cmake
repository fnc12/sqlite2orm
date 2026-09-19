# Report the revision a directory is checked out at, or nothing when the answer would not be about
# that directory.
#
# `git -C <dir> rev-parse HEAD` walks up into the enclosing repository, and the dependency trees
# this is asked about live under `build/_deps`, inside this repository: a copied header tree, a
# stub directory or a source override pointed anywhere under the build tree would answer with
# sqlite2orm's own commit, and a guard built on that reports a sqlite2orm sha as the sqlite_orm
# one. Take the top of the checkout first and only read HEAD when it is the directory itself.
function(sqlite2orm_git_revision_of directory git_executable out_variable)
    set(${out_variable} "" PARENT_SCOPE)
    if(NOT git_executable OR NOT IS_DIRECTORY "${directory}")
        return()
    endif()

    execute_process(
        COMMAND "${git_executable}" -C "${directory}" rev-parse --show-toplevel
        OUTPUT_VARIABLE toplevel
        RESULT_VARIABLE status
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(NOT status EQUAL 0 OR toplevel STREQUAL "")
        return()
    endif()

    get_filename_component(toplevel_path "${toplevel}" REALPATH)
    get_filename_component(directory_path "${directory}" REALPATH)
    if(NOT toplevel_path STREQUAL "${directory_path}")
        return()
    endif()

    execute_process(
        COMMAND "${git_executable}" -C "${directory}" rev-parse HEAD
        OUTPUT_VARIABLE head
        RESULT_VARIABLE status
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(NOT status EQUAL 0)
        return()
    endif()

    set(${out_variable} "${head}" PARENT_SCOPE)
endfunction()
