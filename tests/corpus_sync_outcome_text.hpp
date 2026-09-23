#pragma once

#include <sqlite_orm/sqlite_orm.h>

#include <string>

/**
 *  How the corpus writes down what `sync_schema()` did to one database object.
 *
 *  Only the program the corpus generates includes this -- it is the side that has sqlite_orm on
 *  its include path -- and it names the enumerator rather than printing its number or the sentence
 *  sqlite_orm prints for it, so an expected line says what the run proves and stays readable when
 *  that wording changes.
 */
namespace corpus_test_helpers {

    inline std::string syncOutcomeText(sqlite_orm::sync_schema_result outcome) {
        switch (outcome) {
            case sqlite_orm::sync_schema_result::new_table_created:
                return "new_table_created";
            case sqlite_orm::sync_schema_result::already_in_sync:
                return "already_in_sync";
            case sqlite_orm::sync_schema_result::old_columns_removed:
                return "old_columns_removed";
            case sqlite_orm::sync_schema_result::new_columns_added:
                return "new_columns_added";
            case sqlite_orm::sync_schema_result::new_columns_added_and_old_columns_removed:
                return "new_columns_added_and_old_columns_removed";
            case sqlite_orm::sync_schema_result::dropped_and_recreated:
                return "dropped_and_recreated";
            case sqlite_orm::sync_schema_result::dropped_and_recreated_with_data_loss:
                return "dropped_and_recreated_with_data_loss";
        }
        return "unknown";
    }

}  // namespace corpus_test_helpers
