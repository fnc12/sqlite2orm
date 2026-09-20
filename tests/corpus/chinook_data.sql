-- Seed rows for tests/corpus/chinook.sql, written for this corpus: upstream's own INSERTs are
-- fifteen thousand rows, and the corpus only needs values it can name in an expectation.
--
-- The names are the ones upstream uses for the same ids, so a query written against the real
-- Chinook reads the same here. NULLs are deliberate: a nullable column that never holds one
-- tells the runtime check nothing about how the generated code carries a NULL.

INSERT INTO [Artist] ([ArtistId], [Name]) VALUES
    (1, 'AC/DC'),
    (2, 'Accept'),
    (3, 'Aerosmith'),
    (4, 'Alanis Morissette'),
    (5, NULL);

INSERT INTO [Album] ([AlbumId], [Title], [ArtistId]) VALUES
    (1, 'For Those About To Rock We Salute You', 1),
    (2, 'Balls to the Wall', 2),
    (3, 'Restless and Wild', 2),
    (4, 'Let There Be Rock', 1),
    (5, 'Big Ones', 3);

INSERT INTO [Genre] ([GenreId], [Name]) VALUES
    (1, 'Rock'),
    (2, 'Jazz'),
    (3, 'Metal');

INSERT INTO [MediaType] ([MediaTypeId], [Name]) VALUES
    (1, 'MPEG audio file'),
    (2, 'Protected AAC audio file');

INSERT INTO [Track]
    ([TrackId], [Name], [AlbumId], [MediaTypeId], [GenreId], [Composer], [Milliseconds], [Bytes], [UnitPrice]) VALUES
    (1, 'For Those About To Rock (We Salute You)', 1, 1, 1, 'Angus Young, Malcolm Young, Brian Johnson', 343719, 11170334, 0.99),
    (2, 'Balls to the Wall', 2, 2, 1, NULL, 342562, 5510424, 0.99),
    (3, 'Fast As a Shark', 3, 2, 1, 'F. Baltes, S. Kaufman, U. Dirkscneider & W. Hoffman', 230619, 3990994, 0.99),
    (4, 'Restless and Wild', 3, 1, 3, 'F. Baltes, R.A. Smith-Diesel, S. Kaufman', 252051, 4331779, 1.99),
    (5, 'Princess of the Dawn', 3, 1, NULL, 'Deaffy & R.A. Smith-Diesel', 375418, NULL, 1.99),
    (6, 'Put The Finger On You', 1, 1, 1, 'Angus Young, Malcolm Young, Brian Johnson', 205662, 6713451, 0.99),
    (7, 'Walk On Water', NULL, 1, 1, 'Steven Tyler', 295680, 9719579, 0.99);

INSERT INTO [Playlist] ([PlaylistId], [Name]) VALUES
    (1, 'Music'),
    (2, 'Movies');

INSERT INTO [PlaylistTrack] ([PlaylistId], [TrackId]) VALUES
    (1, 1),
    (1, 2),
    (1, 3),
    (2, 4);

INSERT INTO [Employee]
    ([EmployeeId], [LastName], [FirstName], [Title], [ReportsTo], [BirthDate], [HireDate], [Address], [City], [State], [Country], [PostalCode], [Phone], [Fax], [Email]) VALUES
    (1, 'Adams', 'Andrew', 'General Manager', NULL, '1962-02-18 00:00:00', '2002-08-14 00:00:00', '11120 Jasper Ave NW', 'Edmonton', 'AB', 'Canada', 'T5K 2N1', '+1 (780) 428-9482', '+1 (780) 428-3457', 'andrew@chinookcorp.com'),
    (2, 'Edwards', 'Nancy', 'Sales Manager', 1, '1958-12-08 00:00:00', '2002-05-01 00:00:00', '825 8 Ave SW', 'Calgary', 'AB', 'Canada', 'T2P 2T3', '+1 (403) 262-3443', '+1 (403) 262-3322', 'nancy@chinookcorp.com'),
    (3, 'Peacock', 'Jane', 'Sales Support Agent', 2, '1973-08-29 00:00:00', '2002-04-01 00:00:00', '1111 6 Ave SW', 'Calgary', 'AB', 'Canada', 'T2P 5M5', '+1 (403) 262-3443', '+1 (403) 262-6712', 'jane@chinookcorp.com');

INSERT INTO [Customer]
    ([CustomerId], [FirstName], [LastName], [Company], [Address], [City], [State], [Country], [PostalCode], [Phone], [Fax], [Email], [SupportRepId]) VALUES
    (1, 'Luis', 'Goncalves', 'Embraer - Empresa Brasileira de Aeronautica S.A.', 'Av. Brigadeiro Faria Lima, 2170', 'Sao Jose dos Campos', 'SP', 'Brazil', '12227-000', '+55 (12) 3923-5555', '+55 (12) 3923-5566', 'luisg@embraer.com.br', 3),
    (2, 'Leonie', 'Kohler', NULL, 'Theodor-Heuss-Strasse 34', 'Stuttgart', NULL, 'Germany', '70174', '+49 0711 2842222', NULL, 'leonekohler@surfeu.de', 3),
    (3, 'Francois', 'Tremblay', NULL, '1498 rue Belanger', 'Montreal', 'QC', 'Canada', 'H2G 1A7', '+1 (514) 721-4711', NULL, 'ftremblay@gmail.com', NULL);

INSERT INTO [Invoice]
    ([InvoiceId], [CustomerId], [InvoiceDate], [BillingAddress], [BillingCity], [BillingState], [BillingCountry], [BillingPostalCode], [Total]) VALUES
    (1, 2, '2021-01-01 00:00:00', 'Theodor-Heuss-Strasse 34', 'Stuttgart', NULL, 'Germany', '70174', 1.98),
    (2, 3, '2021-01-02 00:00:00', '1498 rue Belanger', 'Montreal', 'QC', 'Canada', 'H2G 1A7', 3.96),
    (3, 1, '2021-01-03 00:00:00', 'Av. Brigadeiro Faria Lima, 2170', 'Sao Jose dos Campos', 'SP', 'Brazil', '12227-000', 5.94);

INSERT INTO [InvoiceLine] ([InvoiceLineId], [InvoiceId], [TrackId], [UnitPrice], [Quantity]) VALUES
    (1, 1, 1, 0.99, 1),
    (2, 1, 2, 0.99, 1),
    (3, 2, 3, 0.99, 2),
    (4, 2, 4, 1.99, 1),
    (5, 3, 5, 1.99, 2),
    (6, 3, 6, 0.99, 2);
