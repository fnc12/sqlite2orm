-- Seed rows for tests/corpus/sqlite_orm_triggers.sql, written for this corpus. Every statement
-- here is one the triggers of that schema react to, so what the queries read back is what the
-- triggers left behind rather than what was written.

INSERT INTO test_insert (sql_id, sql_text, sql_x, sql_y) VALUES (1, 'SQLite trigger', 8, 2);
UPDATE test_insert SET sql_x = 20 WHERE sql_id = 1;

INSERT INTO test_update (id, text, x, y) VALUES (4, 'update', 1, 2);

INSERT INTO test_delete (id, text, x, y) VALUES (4, 'test', 1, 2);
INSERT INTO test_delete (id, text, x, y) VALUES (5, 'not test', 3, 4);
