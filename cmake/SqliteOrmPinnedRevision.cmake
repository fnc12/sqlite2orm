# The revision of fnc12/sqlite_orm the runtime tests compile against.
#
# The pin is a plain variable with a cache entry only where someone asked for one. A `CACHE STRING`
# default would be written on the first configure and win over this file from then on, so the day
# the pin is bumped CI would move and every tree that had already configured would keep the old
# headers -- with the guard in tests/sqlite_orm_revision_tests.cpp skipping, because the revision it
# is handed no longer equals the pinned one. That is the failure this pin exists to stop, so the
# default follows the file, and only `-DSQLITE2ORM_SQLITE_ORM_REVISION=...` makes a tree keep a
# revision of its own (`cmake -U SQLITE2ORM_SQLITE_ORM_REVISION <tree>` gives the pin back).
#
# Bumping it means editing this file, the table row in README.md and the literals in
# tests/sqlite_orm_revision_tests.cpp, which check that the three agree.
set(SQLITE2ORM_SQLITE_ORM_PINNED_REVISION "eb77998ef5e27350b25977b061e46e202742ecc8")
if(NOT DEFINED SQLITE2ORM_SQLITE_ORM_REVISION)
    set(SQLITE2ORM_SQLITE_ORM_REVISION "${SQLITE2ORM_SQLITE_ORM_PINNED_REVISION}")
elseif(NOT SQLITE2ORM_SQLITE_ORM_REVISION STREQUAL SQLITE2ORM_SQLITE_ORM_PINNED_REVISION)
    # A tree that chose its own revision keeps it across bumps, and its guard can then only skip.
    # Say so at configure time, or it stays green while checking nothing.
    message(STATUS "[sqlite2orm] This tree is configured against sqlite_orm "
                   "${SQLITE2ORM_SQLITE_ORM_REVISION}, not the pinned "
                   "${SQLITE2ORM_SQLITE_ORM_PINNED_REVISION}: the revision guard is off here. "
                   "Run cmake -U SQLITE2ORM_SQLITE_ORM_REVISION to follow the pin again.")
endif()
