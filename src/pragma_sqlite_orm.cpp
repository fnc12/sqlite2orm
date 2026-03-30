#include <sqlite2orm/pragma_sqlite_orm.h>
#include <sqlite2orm/utils.h>

namespace sqlite2orm {

    namespace {

        bool isIntPragma(std::string_view name) {
            return name == "busy_timeout" || name == "application_id" || name == "synchronous" ||
                   name == "user_version" || name == "auto_vacuum" || name == "max_page_count";
        }

    }  // namespace

    std::optional<std::string> validatePragmaForSqliteOrm(const PragmaNode& node) {
        if(node.schemaName) {
            return std::string{
                "schema-qualified PRAGMA is not represented in sqlite_orm::storage::pragma "
                "(use the main database connection only)"};
        }
        const std::string name = toLowerAscii(node.pragmaName);
        if(name == "module_list" || name == "quick_check") {
            if(node.value) {
                return "PRAGMA " + name + " does not take an argument in this form";
            }
            return std::nullopt;
        }
        if(name == "table_info" || name == "table_xinfo") {
            if(!node.value) {
                return "PRAGMA " + name + " requires a table name argument";
            }
            return std::nullopt;
        }
        if(name == "integrity_check") {
            return std::nullopt;
        }
        if(isIntPragma(name)) {
            if(node.value) {
                return std::nullopt;
            }
            return std::nullopt;
        }
        if(name == "recursive_triggers" || name == "journal_mode" || name == "locking_mode") {
            return std::nullopt;
        }
        return std::string("PRAGMA ") + name +
               " is not wrapped by sqlite_orm::storage::pragma "
               "(see sqlite_orm dev/pragma.h for supported pragmas)";
    }

}  // namespace sqlite2orm
