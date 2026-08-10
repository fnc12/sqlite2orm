#pragma once

#include <sqlite2orm/process.h>

#include "codegen_context.h"

#include <map>
#include <string>
#include <vector>

namespace sqlite2orm {

    /**
     *  Same as processSql(), with a registry of known table columns injected into the code
     *  generator so CREATE VIEW statements can infer struct field types. Internal to the library;
     *  public callers get the registry built automatically (processMultiSql, processSqliteSchema).
     */
    ProcessSqlResult processSqlWithSourceTables(
        std::string_view sql,
        const CodeGenPolicy* policy,
        const std::map<std::string, std::vector<SourceTableColumn>>& sourceTables);

}  // namespace sqlite2orm
