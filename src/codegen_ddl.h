#pragma once

#include <sqlite2orm/ast.h>
#include <sqlite2orm/codegen_result.h>

namespace sqlite2orm {

    class CodeGenerator;
    class CodeGeneratorContext;

    class DdlCodeGenerator {
      public:
        DdlCodeGenerator(CodeGenerator& coordinator, CodeGeneratorContext& context);

        CodeGenResult generateCreateTable(const CreateTableNode& createTable);
        CodeGenResult generateCreateTrigger(const CreateTriggerNode& createTrigger);
        CodeGenResult generateCreateIndex(const CreateIndexNode& createIndex);
        CodeGenResult generateTransactionControl(const TransactionControlNode& node);
        CodeGenResult generateVacuum(const VacuumStatementNode& node);
        CodeGenResult generateDrop(const DropStatementNode& node);
        CodeGenResult generateCreateVirtualTable(const CreateVirtualTableNode& node);
        CodeGenResult generateCreateView(const CreateViewNode& node);

        CreateTableParts createTableParts(const CreateTableNode& createTable);

      private:
        CodeGenerator& coordinator;
        CodeGeneratorContext& context;
    };

}  // namespace sqlite2orm
