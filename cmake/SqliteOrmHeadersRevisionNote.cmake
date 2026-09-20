# Say, at configure time, when nothing can check the revision of the sqlite_orm headers the runtime
# tests are about to compile against.
#
# The guard in tests/sqlite_orm_revision_tests.cpp can only compare revisions it was handed, and a
# directory that carries no .git has none to hand it: a dependency tree copied out of another
# checkout -- this project's standing workaround when FetchContent cannot reach GitHub -- or any
# other directory named by FETCHCONTENT_SOURCE_DIR_SQLITE_ORM_HEADERS. The guard then skips, and a
# skip inside an 85-second run is not a signal anybody reads, so the headers can be months old
# while the only sign of it is two runtime asserts failing in whatever change happens to be under
# test. That misattribution is what the pin exists to stop, which leaves the one case the pin
# cannot cover worth a line where it cannot be missed.
#
# A revision that is known and simply wrong is not this note's business: the guard fails on it, by
# name, and says what to do about it.
function(sqlite2orm_headers_revision_note directory source_override revision head out_variable)
    if(NOT head STREQUAL "")
        set(${out_variable} "" PARENT_SCOPE)
        return()
    endif()

    if(NOT source_override STREQUAL "")
        string(CONCAT note
               "The sqlite_orm headers under test come from ${source_override}, where "
               "FETCHCONTENT_SOURCE_DIR_SQLITE_ORM_HEADERS points, and that directory is not a git "
               "checkout: nothing here can tell whether it holds ${revision}, so the revision "
               "guard is off. Check it out at ${revision}, or configure without the override.")
    else()
        string(CONCAT note
               "The sqlite_orm headers under test are in ${directory}, which is not a git "
               "checkout: nothing here can tell whether it holds ${revision}, so the revision "
               "guard is off. Remove that directory and re-run cmake to populate it again.")
    endif()

    set(${out_variable} "${note}" PARENT_SCOPE)
endfunction()
