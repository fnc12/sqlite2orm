#pragma once

namespace sqlite2orm {

    /**
     *  How a result column the caller reads back is widened past the type sqlite_orm gives its
     *  expression: `cast<int64_t>(...)` for a bitwise result sqlite_orm types `int`, then
     *  `as_optional(...)` for a value that can be NULL, in that order, the way the ordinary SELECT
     *  writes them. A compound SELECT settles this for all of its arms at once and hands it to
     *  each arm, one entry per result column position.
     */
    struct ResultColumnWidening {
        bool integerCast = false;
        bool asOptional = false;

        bool operator==(const ResultColumnWidening&) const = default;
    };

}  // namespace sqlite2orm
