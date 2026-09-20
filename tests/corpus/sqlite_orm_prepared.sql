-- The schema sqlite_orm's own prepared-statement tests build a storage from.
--
-- Source:  https://github.com/fnc12/sqlite_orm
--          tests/prepared_statement_tests/select.cpp and tests/prepared_statement_tests/prepared_common.h
--
-- Those tests declare the storage in C++, so the statements below are a transcription written for
-- this corpus: the SQL a user would hand sqlite2orm to get that storage back, down to the column
-- names, the default and the composite primary key. sqlite_orm is AGPL-3.0-or-commercial and has
-- the same owner as this repository. tests/corpus/sqlite_orm_prepared_data.sql seeds these tables
-- with the rows those tests insert.

CREATE TABLE users (
    id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL
);

CREATE TABLE visits (
    id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL,
    time INTEGER NOT NULL DEFAULT 50,
    FOREIGN KEY (user_id) REFERENCES users(id)
);

CREATE TABLE users_and_visits (
    user_id INTEGER NOT NULL,
    visit_id INTEGER NOT NULL,
    description TEXT NOT NULL,
    PRIMARY KEY (user_id, visit_id)
);

CREATE INDEX user_id_index ON users (id);
