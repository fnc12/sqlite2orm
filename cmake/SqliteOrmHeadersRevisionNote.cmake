# Say, at configure time, when nothing can check the revision of the sqlite_orm headers the runtime
# tests are about to compile against.
#
# The guard in tests/sqlite_orm_revision_tests.cpp can only compare revisions it was handed, and it
# is handed none when the headers are not a checkout -- a dependency tree copied out of another
# build, this project's standing workaround when FetchContent cannot reach GitHub, or any other
# directory named by FETCHCONTENT_SOURCE_DIR_SQLITE_ORM_HEADERS -- and none when the machine has no
# git at all. The guard then skips, and a skip inside an 85-second run is not a signal anybody
# reads, so the headers can be months old while the only sign of it is two runtime asserts failing
# in whatever change happens to be under test. That misattribution is what the pin exists to stop,
# which leaves the cases the pin cannot cover worth a line where they cannot be missed.
#
# The cause decides the advice, so the causes are kept apart. Without git nothing about the
# directory is known, including whether it is a checkout, so that one is answered first and on its
# own terms. A source override is never moved by cmake, however many times it runs, so its reader
# is sent to the checkout instead.
#
# What restores a populated dependency was measured on cmake 3.28, not assumed: removing the
# headers alone leaves the sub-build stamped as done, so the next configure repopulates nothing and
# fails in the update step, or -- with FETCHCONTENT_FULLY_DISCONNECTED on -- reports success with
# the source directory pointing at what is no longer there. Removing the sub-build directory is
# what puts the clone step back, and that setting skips the clone step as well, so it has to come
# off for the advice to do anything. It is also the only setting under which a populated tree that
# is not a checkout reaches this note at all: the update step otherwise fails the configure first.
#
# A revision that is known and simply wrong is not this note's business: the guard fails on it, by
# name, and says what to do about it.
function(sqlite2orm_headers_revision_note directory subbuild_directory source_override
                                          git_executable revision head out_variable)
    set(${out_variable} "" PARENT_SCOPE)
    if(NOT head STREQUAL "")
        return()
    endif()

    # A directory that is not there answers every query exactly as one that is not a checkout, and
    # it is a state this note's own earlier advice produced, so the note says which of the two it
    # met rather than the first that fits.
    set(state "is not a git checkout")
    if(NOT IS_DIRECTORY "${directory}")
        set(state "is not there")
    endif()

    if(NOT git_executable)
        string(CONCAT note
               "No git executable was found, so nothing here can check whether the sqlite_orm "
               "headers under test, in ${directory}, hold ${revision}: the revision guard is off. "
               "Put git on PATH and re-run cmake.")
    elseif(NOT source_override STREQUAL "")
        string(CONCAT note
               "The sqlite_orm headers under test come from ${source_override}, where "
               "FETCHCONTENT_SOURCE_DIR_SQLITE_ORM_HEADERS points, and that directory ${state}: "
               "nothing here can tell whether it holds ${revision}, so the revision guard is off. "
               "Check it out at ${revision}, or configure without the override.")
    else()
        string(CONCAT note
               "The sqlite_orm headers under test are in ${directory}, which ${state}: nothing "
               "here can tell whether it holds ${revision}, so the revision guard is off. Remove "
               "${subbuild_directory} and configure again with FETCHCONTENT_FULLY_DISCONNECTED "
               "off, which clones the headers afresh; removing the headers themselves does not, "
               "and leaves the tests aimed at a directory that is not there.")
    endif()

    set(${out_variable} "${note}" PARENT_SCOPE)
endfunction()
