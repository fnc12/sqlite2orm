#include "codegen_tests_common.hpp"

TEST_CASE("codegen: CREATE VIRTUAL TABLE fts5") {
    REQUIRE(
        generate("CREATE VIRTUAL TABLE IF NOT EXISTS posts_fts USING fts5(title, body)") ==
        "struct PostsFts {\n    std::string title;\n    std::string body;\n};\n\nauto vtab = "
        "make_virtual_table<PostsFts>(\"posts_fts\", using_fts5(make_column(\"title\", &PostsFts::title), "
        "make_column(\"body\", &PostsFts::body)));\n");
}

TEST_CASE("codegen: CREATE VIRTUAL TABLE without IF NOT EXISTS warns") {
    const CodeGenResult expected{
        "struct X {\n    std::string a;\n};\n\nauto vtab = make_virtual_table<X>(\"x\", using_fts5(make_column(\"a\", "
        "&X::a)));\n",
        {columnRefStyleDp(1, "&X::a")},
        {"sqlite_orm serializes virtual tables as CREATE VIRTUAL TABLE IF NOT EXISTS; SQL without IF NOT EXISTS differs "
         "from serialized output"}};
    REQUIRE(generateFull("CREATE VIRTUAL TABLE x USING fts5(a)") == expected);
}

TEST_CASE("codegen: CREATE VIRTUAL TABLE generate_series") {
    REQUIRE(generate("CREATE VIRTUAL TABLE IF NOT EXISTS nums USING generate_series") ==
            "auto vtab = make_virtual_table<generate_series>(\"nums\", internal::using_generate_series());\n");
}

TEST_CASE("codegen: CREATE VIRTUAL TABLE dbstat") {
    REQUIRE(generate("CREATE VIRTUAL TABLE IF NOT EXISTS st USING dbstat") ==
            "auto vtab = make_virtual_table<dbstat>(\"st\", using_dbstat());\n");
    REQUIRE(generate("CREATE VIRTUAL TABLE IF NOT EXISTS st2 USING dbstat('temp')") ==
            "auto vtab = make_virtual_table<dbstat>(\"st2\", using_dbstat(\"temp\"));\n");
}

TEST_CASE("codegen: CREATE VIRTUAL TABLE rtree five columns") {
    REQUIRE(
        generate("CREATE VIRTUAL TABLE IF NOT EXISTS geo USING rtree(id, minX, maxX, minY, maxY)") ==
        "struct Geo {\n    int64_t id = 0;\n    float minX = 0.0;\n    float maxX = 0.0;\n    float minY = 0.0;\n    "
        "float maxY = 0.0;\n};\n\nauto vtab = make_virtual_table<Geo>(\"geo\", using_rtree(make_column(\"id\", "
        "&Geo::id), make_column(\"minX\", &Geo::minX), make_column(\"maxX\", &Geo::maxX), make_column(\"minY\", "
        "&Geo::minY), make_column(\"maxY\", &Geo::maxY)));\n");
}

TEST_CASE("codegen: CREATE VIRTUAL TABLE rtree_i32") {
    REQUIRE(generate("CREATE VIRTUAL TABLE IF NOT EXISTS g2 USING rtree_i32(i, a, b, c, d)") ==
            "struct G2 {\n    int64_t i = 0;\n    int32_t a = 0;\n    int32_t b = 0;\n    int32_t c = 0;\n    int32_t d "
            "= 0;\n};\n\nauto vtab = make_virtual_table<G2>(\"g2\", using_rtree_i32(make_column(\"i\", &G2::i), "
            "make_column(\"a\", &G2::a), make_column(\"b\", &G2::b), make_column(\"c\", &G2::c), "
            "make_column(\"d\", &G2::d)));\n");
}

TEST_CASE("codegen: CREATE VIRTUAL TABLE schema and temp warn") {
    const CodeGenResult expected{
        "struct PostsFts {\n    std::string body;\n};\n\nauto vtab = make_virtual_table<PostsFts>(\"posts_fts\", "
        "using_fts5(make_column(\"body\", &PostsFts::body)));\n",
        {columnRefStyleDp(1, "&PostsFts::body")},
        {"schema-qualified VIRTUAL TABLE name is not represented in sqlite_orm; generated code uses unqualified table "
         "name only",
         "TEMP/TEMPORARY VIRTUAL TABLE is not represented in sqlite_orm virtual table mapping; generated code does "
         "not mark the table as temporary"}};
    REQUIRE(generateFull(
                "CREATE TEMP VIRTUAL TABLE IF NOT EXISTS main.posts_fts USING fts5(body)") == expected);
}

TEST_CASE("codegen: CREATE VIRTUAL TABLE fts5 non-column args stub") {
    const CodeGenResult expected{
        "/* CREATE VIRTUAL TABLE: fts5 (unmapped arguments) */",
        {},
        {"FTS5 module arguments that are not plain column names cannot be mapped to sqlite_orm::using_fts5()"}};
    REQUIRE(generateFull("CREATE VIRTUAL TABLE IF NOT EXISTS f USING fts5(lower(title))") == expected);
}

TEST_CASE("codegen: unknown virtual table module") {
    const CodeGenResult expected{
        "/* CREATE VIRTUAL TABLE: unknown module */",
        {},
        {"virtual table module \"noop\" has no sqlite_orm mapping in sqlite2orm codegen"}};
    REQUIRE(generateFull("CREATE VIRTUAL TABLE IF NOT EXISTS z USING noop") == expected);
}
