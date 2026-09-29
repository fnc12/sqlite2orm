#pragma once

#include <cstddef>

namespace sqlite2orm {

    /**
     *  How much of a schema the generated header carries: N of M rows. A row SQLite owns — its own
     *  `sqlite_...` object or a table FTS5 keeps its index in — is left out of the storage by design
     *  and is not counted at all; every other row counts, and is generated only when its code is in
     *  the header. A row that parsed and generated on its own but was left out anyway (a view resting
     *  on a table that is not generated, a virtual table) is not generated.
     */
    struct SchemaCoverage {
        /** Rows the header is expected to carry. */
        size_t total = 0;
        /** Of those, the rows whose code is in the header. */
        size_t generated = 0;

        [[nodiscard]] bool complete() const {
            return this->generated == this->total;
        }

        bool operator==(const SchemaCoverage&) const = default;
    };

}  // namespace sqlite2orm
