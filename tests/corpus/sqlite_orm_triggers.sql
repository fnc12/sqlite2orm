-- The schema sqlite_orm's own trigger tests build a storage from.
--
-- Source:  https://github.com/fnc12/sqlite_orm, tests/trigger_tests.cpp ("triggers_basics")
--
-- Those tests declare the storage in C++, so the statements below are a transcription written for
-- this corpus: the SQL a user would hand sqlite2orm to get that storage back, with the same table
-- and column names and the same trigger bodies. sqlite_orm is AGPL-3.0-or-commercial and has the
-- same owner as this repository. A CREATE TRIGGER is the one statement whose body is a program
-- rather than a declaration, which is why the corpus carries a schema built around three of them.

CREATE TABLE test_insert (
    sql_id INTEGER NOT NULL PRIMARY KEY,
    sql_text TEXT NOT NULL,
    sql_x INTEGER NOT NULL,
    sql_y INTEGER NOT NULL
);

CREATE TABLE test_update (
    id INTEGER NOT NULL PRIMARY KEY,
    text TEXT NOT NULL,
    x INTEGER NOT NULL,
    y INTEGER NOT NULL
);

CREATE TABLE test_delete (
    id INTEGER NOT NULL PRIMARY KEY,
    text TEXT NOT NULL,
    x INTEGER NOT NULL,
    y INTEGER NOT NULL
);

CREATE TRIGGER trigger_insert AFTER UPDATE OF sql_x ON test_insert
BEGIN
    INSERT INTO test_insert (sql_id, sql_text, sql_x, sql_y) VALUES (123, 'HelloTrigger', 12, 13);
END;

CREATE TRIGGER trigger_update AFTER INSERT ON test_update
BEGIN
    UPDATE test_update SET x = 42 WHERE text = 'update';
END;

CREATE TRIGGER trigger_delete AFTER INSERT ON test_delete
BEGIN
    DELETE FROM test_delete WHERE text <> 'test';
END;
