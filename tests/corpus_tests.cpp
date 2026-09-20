#include <sqlite2orm/codegen_result.h>
#include <sqlite2orm/process.h>
#include <sqlite2orm/schema_header.h>
#include <sqlite2orm/schema_process.h>
#include <sqlite2orm/schema_reader.h>

#include "corpus_value_text.hpp"
#include "source_file_text.hpp"
#include "temp_build_dir.hpp"

#include <catch2/catch_all.hpp>
#include <sqlite3.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace sqlite2orm;

/*
 *  The corpus: real schemas, run through the whole product path — schema → generated header →
 *  generated query code → compilation → execution → the values a user reads back.
 *
 *  Every other test in this tree pins one construct, which is why a bug in the generator has so
 *  far been found one construct at a time, by someone going looking for it. These tests instead
 *  take the schemas people really hand the tool (tests/corpus/*.sql, each with its source and its
 *  licence) and check the whole of it at once, against SQLite itself.
 *
 *  Per schema the test builds a database from the schema and its seed rows, generates the storage
 *  header for it, generates the code for each query, compiles and links one program around the
 *  lot, runs it, and requires that every row equals the literal written here — and that SQLite,
 *  asked the same query on the same database, answers with that literal too. A query the
 *  generator gets wrong today is not left out: it is pinned as `knownBad` with the card that
 *  tracks it, so the corpus stays usable while the bug waits its turn and goes red the day the
 *  bug is fixed and the expectation has to move.
 *
 *  These cases are hidden (`[.corpus]`): they compile, link and run a program per schema, and the
 *  corpus grows with every schema worth watching. ctest runs them as `sqlite2orm_corpus`, in a CI
 *  job of their own.
 */

namespace {

    namespace fs = std::filesystem;

    /** How the generated code differs from SQLite today, for a query whose bug has a card. */
    struct KnownBad {
        /** Where the bug is tracked; a corpus entry may not carry a divergence without one. */
        std::string_view card;
        /** What the generated program prints today. Ignored when `compiles` is false. */
        std::vector<std::string> rows;
        /** false: the generated code does not compile at all, and the corpus checks it still does not. */
        bool compiles = true;
    };

    /** One query of a corpus schema, and the rows it is supposed to produce. */
    struct CorpusQuery {
        std::string_view sql;
        /**
         *  The rows SQLite returns, one string per row, columns `|`-separated, a NULL as `NULL`
         *  and a REAL as `corpus_test_helpers::realText` writes it. The test checks these against
         *  SQLite as well as against the generated code, so a literal here is not one side's word
         *  for it: it is what both sides have to say.
         */
        std::vector<std::string> rows;
        /** Set only while the generated code disagrees with the rows above. */
        KnownBad knownBad = {};
    };

    /** Rows the corpus expects the generated program to print for `query`. */
    const std::vector<std::string>& rowsFromGeneratedCode(const CorpusQuery& query) {
        return query.knownBad.card.empty() ? query.rows : query.knownBad.rows;
    }

    [[nodiscard]] fs::path makeTempDatabasePath() {
        static thread_local std::mt19937 gen{std::random_device{}()};
        std::uniform_int_distribution<std::uint64_t> dist{};
        return fs::temp_directory_path() / ("sqlite2orm_corpus_" + std::to_string(dist(gen)) + ".db");
    }

    /** A database file built from a corpus schema, removed with the object. */
    struct CorpusDatabase {
        fs::path path;
        sqlite3* handle = nullptr;

        /** Runs `tests/corpus/<name>.sql` and `tests/corpus/<name>_data.sql` into a fresh file. */
        explicit CorpusDatabase(std::string_view name) : path(makeTempDatabasePath()) {
            REQUIRE(sqlite3_open(path.string().c_str(), &handle) == SQLITE_OK);
            const std::string prefix = "tests/corpus/" + std::string(name);
            exec(source_file_test_helpers::readSourceFile(prefix + ".sql"));
            exec(source_file_test_helpers::readSourceFile(prefix + "_data.sql"));
        }

        ~CorpusDatabase() {
            sqlite3_close(handle);
            std::error_code ec;
            fs::remove(path, ec);
        }

        CorpusDatabase(const CorpusDatabase&) = delete;
        CorpusDatabase& operator=(const CorpusDatabase&) = delete;

        void exec(const std::string& sql) const {
            char* message = nullptr;
            const int result = sqlite3_exec(handle, sql.c_str(), nullptr, nullptr, &message);
            const std::string reported = message ? message : "";
            sqlite3_free(message);
            INFO("SQLite refused a corpus file: " << reported);
            REQUIRE(result == SQLITE_OK);
        }

        /** The rows SQLite itself returns for `sql`, written the way a corpus expectation is. */
        [[nodiscard]] std::vector<std::string> rows(std::string_view sql) const {
            sqlite3_stmt* statement = nullptr;
            const int prepared =
                sqlite3_prepare_v2(handle, sql.data(), static_cast<int>(sql.size()), &statement, nullptr);
            INFO("SQLite could not prepare `" << sql << "`: " << sqlite3_errmsg(handle));
            REQUIRE(prepared == SQLITE_OK);

            std::vector<std::string> result;
            for (int step = sqlite3_step(statement); step == SQLITE_ROW; step = sqlite3_step(statement)) {
                std::string row;
                for (int column = 0, columns = sqlite3_column_count(statement); column < columns; ++column) {
                    if (column != 0) {
                        row += '|';
                    }
                    switch (sqlite3_column_type(statement, column)) {
                        case SQLITE_NULL:
                            row += "NULL";
                            break;
                        case SQLITE_FLOAT:
                            row += corpus_test_helpers::realText(sqlite3_column_double(statement, column));
                            break;
                        default:
                            row += reinterpret_cast<const char*>(sqlite3_column_text(statement, column));
                            break;
                    }
                }
                result.push_back(row);
            }
            sqlite3_finalize(statement);
            return result;
        }
    };

    /** The storage header sqlite2orm generates for `databasePath`, i.e. what `--db` prints. */
    [[nodiscard]] std::string generatedHeader(const fs::path& databasePath) {
        const SqliteSchemaReader reader{databasePath.string()};
        const CodeGenResult header = generateSqliteSchemaHeader(processSqliteSchema(reader));
        std::string reported;
        for (const std::string& error: header.errors) {
            reported += "\nheader generation reported: " + error;
        }
        INFO(reported);
        REQUIRE(header.errors.empty());
        REQUIRE_FALSE(header.code.empty());
        return header.code;
    }

    /** The sqlite_orm code sqlite2orm generates for `sql`, i.e. what `-e` prints. */
    [[nodiscard]] std::string generatedQuery(std::string_view sql) {
        const ProcessSqlResult pipeline = processSql(sql);
        std::string reported;
        for (const ParseError& error: pipeline.parseResult.errors) {
            reported += "\nparse error: " + error.message;
        }
        for (const ValidationError& error: pipeline.validationErrors) {
            reported += "\nvalidation error: " + error.message;
        }
        for (const std::string& error: pipeline.codegen.errors) {
            reported += "\ncodegen error: " + error;
        }
        INFO("`" << sql << '`' << reported);
        REQUIRE(pipeline.ok());
        REQUIRE_FALSE(pipeline.codegen.code.empty());
        return pipeline.codegen.code;
    }

    /**
     *  A program around `generatedQueries`, to be compiled against the generated header written
     *  next to it. Each query announces how many rows it produced before printing them, so a
     *  value that happens to read like a separator cannot be mistaken for one; a value holding a
     *  newline still could, and the corpus has none.
     */
    [[nodiscard]] std::string programSource(const std::vector<std::string>& generatedQueries,
                                            const fs::path& databasePath) {
        std::ostringstream program;
        program << "#include \"schema.hpp\"\n"
                   "\n"
                   "#include <corpus_value_text.hpp>\n"
                   "\n"
                   "#include <exception>\n"
                   "#include <iostream>\n"
                   "\n"
                   "int main() {\n"
                   "    using namespace sqlite_orm;\n"
                   "    auto storage = make_sqlite_schema_storage(\""
                << databasePath.string() << "\");\n";
        for (const std::string& query: generatedQueries) {
            program << "    try {\n        " << query
                    << "\n"
                       "        std::cout << \"ROWS \" << rows.size() << '\\n';\n"
                       "        for (const auto& row: rows) {\n"
                       "            std::cout << corpus_test_helpers::cell(row) << '\\n';\n"
                       "        }\n"
                       "    } catch (const std::exception& e) {\n"
                       "        std::cout << \"THREW \" << e.what() << '\\n';\n"
                       "    }\n";
        }
        program << "    return 0;\n"
                   "}\n";
        return program.str();
    }

    /** `compilerCommand()` plus the include the generated program needs for the shared rendering. */
    [[nodiscard]] std::string corpusCompilerCommand() {
        return codegen_test_helpers::TempBuildDir::compilerCommand() + " -I" + SQLITE2ORM_TEST_SOURCE_DIR + "/tests";
    }

    [[nodiscard]] std::string fileText(const fs::path& path) {
        std::ifstream stream{path, std::ios::binary};
        return std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
    }

    /**
     *  Compiles and runs `source` next to `header`, and hands back what each query printed. A
     *  compilation failure is the test's failure, with the compiler's own words: generated code
     *  that does not build is exactly the bug the corpus is here to catch, and a message saying
     *  only that an exit code was not zero would leave the reader to reproduce it by hand.
     */
    [[nodiscard]] std::vector<std::vector<std::string>>
    runProgram(const std::string& header, const std::string& source, std::size_t queryCount) {
        const codegen_test_helpers::TempBuildDir dir;
        dir.write("schema.hpp", header);
        const fs::path sourcePath = dir.write("queries.cpp", source);
        const fs::path binaryPath = dir.file("queries");
        const fs::path compilerLogPath = dir.file("compiler.log");
        const fs::path outputPath = dir.file("queries.out");

        std::ostringstream compile;
        compile << corpusCompilerCommand() << ' ' << sourcePath.string() << ' '
                << codegen_test_helpers::TempBuildDir::sqlite3LinkFlags() << " -o " << binaryPath.string() << " > "
                << compilerLogPath.string() << " 2>&1";
        if (codegen_test_helpers::TempBuildDir::run(compile.str()) != 0) {
            FAIL("the generated code does not compile:\n" << fileText(compilerLogPath));
        }

        const std::string run = binaryPath.string() + " > " + outputPath.string() + " 2>&1";
        const int exitCode = codegen_test_helpers::TempBuildDir::run(run);
        const std::string output = fileText(outputPath);
        INFO("the generated program printed:\n" << output);
        REQUIRE(exitCode == 0);

        std::istringstream lines{output};
        std::vector<std::vector<std::string>> perQuery;
        for (std::string line; std::getline(lines, line);) {
            INFO("the generated program printed:\n" << output);
            REQUIRE(line.rfind("ROWS ", 0) == 0);
            std::vector<std::string> rows(static_cast<std::size_t>(std::stoul(line.substr(5))));
            for (std::string& row: rows) {
                REQUIRE(static_cast<bool>(std::getline(lines, row)));
            }
            perQuery.push_back(std::move(rows));
        }
        INFO("the generated program printed:\n" << output);
        REQUIRE(perQuery.size() == queryCount);
        return perQuery;
    }

    /** Requires that the code generated for `sql` still does not compile, as its card says. */
    void requireStillDoesNotCompile(const std::string& header, const CorpusQuery& query, const fs::path& databasePath) {
        const codegen_test_helpers::TempBuildDir dir;
        dir.write("schema.hpp", header);
        const fs::path sourcePath = dir.write("query.cpp", programSource({generatedQuery(query.sql)}, databasePath));

        std::ostringstream compile;
        compile << corpusCompilerCommand() << " -fsyntax-only " << sourcePath.string() << " > /dev/null 2>&1";
        INFO("`" << query.sql << "` compiles now; " << query.knownBad.card
                 << " is fixed, so move it out of knownBad and give it the rows SQLite returns");
        REQUIRE(codegen_test_helpers::TempBuildDir::run(compile.str()) != 0);
    }

    /**
     *  Runs the whole path on one corpus schema and checks every query of it, both against SQLite
     *  and against the literals of `queries`.
     */
    void checkCorpusSchema(std::string_view name, const std::vector<CorpusQuery>& queries) {
        const CorpusDatabase database{name};
        const std::string header = generatedHeader(database.path);

        std::vector<std::string> generated;
        std::vector<const CorpusQuery*> compiled;
        for (const CorpusQuery& query: queries) {
            INFO("query: " << query.sql);
            const std::vector<std::string> fromSqlite = database.rows(query.sql);
            CHECK(fromSqlite == query.rows);
            if (!query.knownBad.card.empty() && query.knownBad.compiles) {
                INFO(query.knownBad.card);
                CHECK(query.knownBad.rows != query.rows);
            }
            if (query.knownBad.card.empty() || query.knownBad.compiles) {
                generated.push_back(generatedQuery(query.sql));
                compiled.push_back(&query);
            }
        }

        const std::vector<std::vector<std::string>> printed =
            runProgram(header, programSource(generated, database.path), generated.size());
        for (std::size_t index = 0; index < compiled.size(); ++index) {
            const CorpusQuery& query = *compiled[index];
            INFO("query: " << query.sql);
            INFO("generated: " << generated[index]);
            if (!query.knownBad.card.empty()) {
                INFO("this query is pinned as known-bad; if it now matches SQLite, "
                     << query.knownBad.card << " is fixed and the expectation moves to `rows`");
            }
            CHECK(printed[index] == rowsFromGeneratedCode(query));
        }

        for (const CorpusQuery& query: queries) {
            if (!query.knownBad.card.empty() && !query.knownBad.compiles) {
                requireStillDoesNotCompile(header, query, database.path);
            }
        }
    }

    constexpr std::string_view subqueryHoistsTableCard =
        "card 1868205864612005383: a subquery in WHERE drags its table into the outer FROM";
    constexpr std::string_view aliasedColumnCard =
        "card 1868205961584313865: an unqualified result column is generated without the FROM alias";
    constexpr std::string_view typeofNameCard = "card 1868206036830127627: TYPEOF generates `typeof`, not `typeof_`";

}  // namespace

TEST_CASE("corpus: Chinook", "[.corpus]") {
    checkCorpusSchema(
        "chinook",
        {
            {.sql = "SELECT COUNT(*) FROM Track;", .rows = {"7"}},
            {.sql = "SELECT Name FROM Artist ORDER BY ArtistId;",
             .rows = {"AC/DC", "Accept", "Aerosmith", "Alanis Morissette", "NULL"}},
            {.sql = "SELECT Composer FROM Track ORDER BY TrackId;",
             .rows = {"Angus Young, Malcolm Young, Brian Johnson",
                      "NULL",
                      "F. Baltes, S. Kaufman, U. Dirkscneider & W. Hoffman",
                      "F. Baltes, R.A. Smith-Diesel, S. Kaufman",
                      "Deaffy & R.A. Smith-Diesel",
                      "Angus Young, Malcolm Young, Brian Johnson",
                      "Steven Tyler"}},
            {.sql = "SELECT SUM(Milliseconds) FROM Track;", .rows = {"2045711"}},
            {.sql = "SELECT TrackId, Name FROM Track WHERE Milliseconds > 300000 ORDER BY TrackId;",
             .rows = {"1|For Those About To Rock (We Salute You)", "2|Balls to the Wall", "5|Princess of the Dawn"}},
            {.sql = "SELECT UnitPrice FROM Track ORDER BY TrackId;",
             .rows = {"0.99", "0.99", "0.99", "1.99", "1.99", "0.99", "0.99"}},
            {.sql = "SELECT MAX(Total) FROM Invoice;", .rows = {"5.94"}},
            {.sql = "SELECT Name FROM Track WHERE AlbumId IS NULL;", .rows = {"Walk On Water"}},
            {.sql = "SELECT AVG(UnitPrice) FROM Track;", .rows = {"1.27571428571429"}},
            {.sql = "SELECT Bytes FROM Track ORDER BY TrackId;",
             .rows = {"11170334", "5510424", "3990994", "4331779", "NULL", "6713451", "9719579"}},
            {.sql = "SELECT GenreId, COUNT(*) FROM Track GROUP BY GenreId ORDER BY GenreId;",
             .rows = {"NULL|1", "1|5", "3|1"}},
            {.sql = "SELECT AlbumId, COUNT(*) FROM Track GROUP BY AlbumId HAVING COUNT(*) > 1 ORDER BY AlbumId;",
             .rows = {"1|2", "3|3"}},
            {.sql = "SELECT t.Name, a.Title FROM Track t JOIN Album a ON t.AlbumId = a.AlbumId ORDER BY t.TrackId "
                    "LIMIT 3;",
             .rows = {"For Those About To Rock (We Salute You)|For Those About To Rock We Salute You",
                      "Balls to the Wall|Balls to the Wall",
                      "Fast As a Shark|Restless and Wild"}},
            {.sql = "SELECT IFNULL(Composer, 'unknown') FROM Track ORDER BY TrackId LIMIT 3;",
             .rows = {"Angus Young, Malcolm Young, Brian Johnson",
                      "unknown",
                      "F. Baltes, S. Kaufman, U. Dirkscneider & W. Hoffman"}},
            {.sql = "SELECT FirstName || ' ' || LastName FROM Employee ORDER BY EmployeeId;",
             .rows = {"Andrew Adams", "Nancy Edwards", "Jane Peacock"}},
            {.sql = "SELECT InvoiceId, SUM(UnitPrice * Quantity) FROM InvoiceLine GROUP BY InvoiceId ORDER BY "
                    "InvoiceId;",
             .rows = {"1|1.98", "2|3.97", "3|5.96"}},
            {.sql = "SELECT CASE WHEN Milliseconds > 300000 THEN 'long' ELSE 'short' END FROM Track ORDER BY TrackId;",
             .rows = {"long", "long", "short", "short", "long", "short", "short"}},
            {.sql = "SELECT Name, ROW_NUMBER() OVER (ORDER BY TrackId) FROM Track ORDER BY TrackId LIMIT 3;",
             .rows = {"For Those About To Rock (We Salute You)|1", "Balls to the Wall|2", "Fast As a Shark|3"}},
            {.sql = "WITH long_tracks AS (SELECT TrackId FROM Track WHERE Milliseconds > 300000) SELECT COUNT(*) FROM "
                    "long_tracks;",
             .rows = {"3"}},
            {.sql = "SELECT Name FROM Artist UNION SELECT Name FROM Genre ORDER BY Name;",
             .rows = {"NULL", "AC/DC", "Accept", "Aerosmith", "Alanis Morissette", "Jazz", "Metal", "Rock"}},
            {.sql = "SELECT Title FROM Album WHERE ArtistId IN (SELECT ArtistId FROM Artist WHERE Name = 'AC/DC') "
                    "ORDER BY AlbumId LIMIT 3;",
             .rows = {"For Those About To Rock We Salute You", "Let There Be Rock"},
             .knownBad = {.card = subqueryHoistsTableCard,
                          .rows = {"For Those About To Rock We Salute You",
                                   "For Those About To Rock We Salute You",
                                   "For Those About To Rock We Salute You"}}},
            {.sql = "SELECT Name FROM Track WHERE EXISTS (SELECT 1 FROM Album WHERE Album.AlbumId = Track.AlbumId) "
                    "ORDER BY TrackId LIMIT 3;",
             .rows = {"For Those About To Rock (We Salute You)", "Balls to the Wall", "Fast As a Shark"},
             .knownBad = {.card = subqueryHoistsTableCard,
                          .rows = {"For Those About To Rock (We Salute You)",
                                   "For Those About To Rock (We Salute You)",
                                   "For Those About To Rock (We Salute You)"}}},
            {.sql = "SELECT Title FROM Album a LEFT JOIN Track t ON a.AlbumId = t.AlbumId WHERE t.TrackId IS NULL "
                    "ORDER BY a.AlbumId LIMIT 3;",
             .rows = {"Let There Be Rock", "Big Ones"},
             .knownBad = {.card = aliasedColumnCard,
                          .rows = {"For Those About To Rock We Salute You", "Balls to the Wall", "Restless and Wild"}}},
            {.sql = "SELECT TYPEOF(Bytes) FROM Track ORDER BY TrackId;",
             .rows = {"integer", "integer", "integer", "integer", "null", "integer", "integer"},
             .knownBad = {.card = typeofNameCard, .compiles = false}},
        });
}

TEST_CASE("corpus: Northwind", "[.corpus]") {
    checkCorpusSchema(
        "northwind",
        {
            {.sql = "SELECT COUNT(*) FROM Products;", .rows = {"5"}},
            {.sql = "SELECT ProductName FROM Products ORDER BY ProductID;",
             .rows = {"Chai", "Chang", "Aniseed Syrup", "Chef Antons Cajun Seasoning", "Chef Antons Gumbo Mix"}},
            {.sql = "SELECT Region FROM Customers ORDER BY CustomerID;", .rows = {"NULL", "NULL", "NULL"}},
            {.sql = "SELECT SUM(UnitsInStock) FROM Products;", .rows = {"122"}},
            {.sql = "SELECT OrderID FROM Orders WHERE ShippedDate IS NULL;", .rows = {"10250"}},
            {.sql = "SELECT COUNT(*) FROM [Order Details];", .rows = {"5"}},
            {.sql = "SELECT OrderID, SUM(UnitPrice * Quantity) FROM [Order Details] GROUP BY OrderID ORDER BY OrderID;",
             .rows = {"10248|406", "10249|50", "10250|1090.25"}},
            {.sql = "SELECT LastName, FirstName FROM Employees ORDER BY EmployeeID;",
             .rows = {"Davolio|Nancy", "Fuller|Andrew", "Leverling|Janet"}},
            {.sql = "SELECT Discount FROM [Order Details] ORDER BY OrderID, ProductID;",
             .rows = {"0", "0", "0", "0.15", "0"}},
            {.sql = "SELECT MAX(Freight) FROM Orders;", .rows = {"65.83"}},
            {.sql = "SELECT p.ProductName, c.CategoryName FROM Products p JOIN Categories c ON p.CategoryID = "
                    "c.CategoryID ORDER BY p.ProductID;",
             .rows = {"Chai|Beverages",
                      "Chang|Beverages",
                      "Aniseed Syrup|Condiments",
                      "Chef Antons Cajun Seasoning|Condiments",
                      "Chef Antons Gumbo Mix|Condiments"}},
            {.sql = "SELECT ProductName FROM Products WHERE Discontinued = '1';", .rows = {"Chef Antons Gumbo Mix"}},
            {.sql = "SELECT ProductName, UnitPrice FROM Products WHERE UnitPrice > 18 ORDER BY ProductID;",
             .rows = {"Chang|19", "Chef Antons Cajun Seasoning|22", "Chef Antons Gumbo Mix|21.35"}},
            {.sql = "SELECT EmployeeID, TerritoryID FROM EmployeeTerritories ORDER BY EmployeeID, TerritoryID;",
             .rows = {"1|01581", "1|01730", "3|98104"}},
            {.sql = "SELECT CustomerDesc FROM CustomerDemographics ORDER BY CustomerTypeID;",
             .rows = {"NULL", "Buys in bulk"}},
            {.sql = "SELECT COUNT(*) FROM Employees WHERE ReportsTo IS NOT NULL;", .rows = {"2"}},
        });
}

TEST_CASE("corpus: sqlite_orm prepared-statement tests", "[.corpus]") {
    checkCorpusSchema(
        "sqlite_orm_prepared",
        {
            {.sql = "SELECT name FROM users ORDER BY id;", .rows = {"Team BS", "Shy'm", "Maître Gims"}},
            {.sql = "SELECT COUNT(*) FROM visits;", .rows = {"5"}},
            {.sql = "SELECT user_id, COUNT(*) FROM visits GROUP BY user_id ORDER BY user_id;",
             .rows = {"1|2", "2|1", "3|2"}},
            {.sql = "SELECT description FROM users_and_visits ORDER BY user_id, visit_id;",
             .rows = {"Glad you came", "Shine", "Woohoo"}},
            {.sql = "SELECT name FROM users WHERE id = 3;", .rows = {"Maître Gims"}},
            {.sql = "SELECT MAX(time) FROM visits;", .rows = {"100004"}},
            {.sql = "SELECT u.name, v.time FROM users u JOIN visits v ON u.id = v.user_id ORDER BY v.id;",
             .rows = {"Team BS|100000", "Team BS|100001", "Shy'm|100002", "Maître Gims|100003", "Maître Gims|100004"}},
            {.sql = "SELECT name FROM users WHERE name LIKE 'S%';", .rows = {"Shy'm"}},
            {.sql = "SELECT LENGTH(name) FROM users ORDER BY id;", .rows = {"7", "5", "11"}},
        });
}

TEST_CASE("corpus: sqlite_orm trigger tests", "[.corpus]") {
    checkCorpusSchema("sqlite_orm_triggers",
                      {
                          {.sql = "SELECT sql_id, sql_text, sql_x, sql_y FROM test_insert ORDER BY sql_id;",
                           .rows = {"1|SQLite trigger|20|2", "123|HelloTrigger|12|13"}},
                          {.sql = "SELECT COUNT(*) FROM test_insert;", .rows = {"2"}},
                          {.sql = "SELECT x FROM test_update WHERE id = 4;", .rows = {"42"}},
                          {.sql = "SELECT text FROM test_delete ORDER BY id;", .rows = {"test"}},
                          {.sql = "SELECT COUNT(*) FROM test_delete;", .rows = {"1"}},
                      });
}
