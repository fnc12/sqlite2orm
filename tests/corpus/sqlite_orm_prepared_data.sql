-- Seed rows for tests/corpus/sqlite_orm_prepared.sql: the rows sqlite_orm's own prepared-statement
-- tests insert, names and all. The names are there for a reason -- `Maître Gims` is the corpus'
-- only check that text outside ASCII survives the round trip through the generated code.

INSERT INTO users (id, name) VALUES
    (1, 'Team BS'),
    (2, 'Shy''m'),
    (3, 'Maître Gims');

INSERT INTO visits (id, user_id, time) VALUES
    (1, 1, 100000),
    (2, 1, 100001),
    (3, 2, 100002),
    (4, 3, 100003),
    (5, 3, 100004);

INSERT INTO users_and_visits (user_id, visit_id, description) VALUES
    (2, 1, 'Glad you came'),
    (3, 1, 'Shine'),
    (3, 2, 'Woohoo');
