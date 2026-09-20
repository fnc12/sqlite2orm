-- Seed rows for tests/corpus/northwind.sql, written for this corpus: upstream's own INSERTs are
-- some three thousand rows, and the corpus only needs values it can name in an expectation.
--
-- Ids and names are the ones upstream uses for the same rows, so a query written against the real
-- Northwind reads the same here. NULLs are deliberate: a nullable column that never holds one
-- tells the runtime check nothing about how the generated code carries a NULL.

INSERT INTO [Categories] ([CategoryID], [CategoryName], [Description], [Picture]) VALUES
    (1, 'Beverages', 'Soft drinks, coffees, teas, beers, and ales', NULL),
    (2, 'Condiments', 'Sweet and savory sauces, relishes, spreads, and seasonings', NULL),
    (3, 'Confections', 'Desserts, candies, and sweet breads', NULL);

INSERT INTO [Suppliers] ([SupplierID], [CompanyName], [ContactName], [ContactTitle], [Address], [City], [Region], [PostalCode], [Country], [Phone], [Fax], [HomePage]) VALUES
    (1, 'Exotic Liquids', 'Charlotte Cooper', 'Purchasing Manager', '49 Gilbert St.', 'London', NULL, 'EC1 4SD', 'UK', '(171) 555-2222', NULL, NULL),
    (2, 'New Orleans Cajun Delights', 'Shelley Burke', 'Order Administrator', 'P.O. Box 78934', 'New Orleans', 'LA', '70117', 'USA', '(100) 555-4822', NULL, NULL);

INSERT INTO [Products] ([ProductID], [ProductName], [SupplierID], [CategoryID], [QuantityPerUnit], [UnitPrice], [UnitsInStock], [UnitsOnOrder], [ReorderLevel], [Discontinued]) VALUES
    (1, 'Chai', 1, 1, '10 boxes x 20 bags', 18, 39, 0, 10, '0'),
    (2, 'Chang', 1, 1, '24 - 12 oz bottles', 19, 17, 40, 25, '0'),
    (3, 'Aniseed Syrup', 1, 2, '12 - 550 ml bottles', 10, 13, 70, 25, '0'),
    (4, 'Chef Antons Cajun Seasoning', 2, 2, '48 - 6 oz jars', 22, 53, 0, 0, '0'),
    (5, 'Chef Antons Gumbo Mix', 2, 2, '36 boxes', 21.35, 0, 0, 0, '1');

INSERT INTO [Regions] ([RegionID], [RegionDescription]) VALUES
    (1, 'Eastern'),
    (2, 'Western');

INSERT INTO [Territories] ([TerritoryID], [TerritoryDescription], [RegionID]) VALUES
    ('01581', 'Westboro', 1),
    ('01730', 'Bedford', 1),
    ('98104', 'Seattle', 2);

INSERT INTO [Employees] ([EmployeeID], [LastName], [FirstName], [Title], [TitleOfCourtesy], [BirthDate], [HireDate], [Address], [City], [Region], [PostalCode], [Country], [HomePhone], [Extension], [Photo], [Notes], [ReportsTo], [PhotoPath]) VALUES
    (1, 'Davolio', 'Nancy', 'Sales Representative', 'Ms.', '1948-12-08', '1992-05-01', '507 - 20th Ave. E.', 'Seattle', 'WA', '98122', 'USA', '(206) 555-9857', '5467', NULL, NULL, 2, NULL),
    (2, 'Fuller', 'Andrew', 'Vice President, Sales', 'Dr.', '1952-02-19', '1992-08-14', '908 W. Capital Way', 'Tacoma', 'WA', '98401', 'USA', '(206) 555-9482', '3457', NULL, NULL, NULL, NULL),
    (3, 'Leverling', 'Janet', 'Sales Representative', 'Ms.', '1963-08-30', '1992-04-01', '722 Moss Bay Blvd.', 'Kirkland', 'WA', '98033', 'USA', '(206) 555-3412', '3355', NULL, NULL, 2, NULL);

INSERT INTO [EmployeeTerritories] ([EmployeeID], [TerritoryID]) VALUES
    (1, '01581'),
    (1, '01730'),
    (3, '98104');

INSERT INTO [CustomerDemographics] ([CustomerTypeID], [CustomerDesc]) VALUES
    ('WHOLESALE', 'Buys in bulk'),
    ('RETAIL', NULL);

INSERT INTO [Customers] ([CustomerID], [CompanyName], [ContactName], [ContactTitle], [Address], [City], [Region], [PostalCode], [Country], [Phone], [Fax]) VALUES
    ('ALFKI', 'Alfreds Futterkiste', 'Maria Anders', 'Sales Representative', 'Obere Str. 57', 'Berlin', NULL, '12209', 'Germany', '030-0074321', '030-0076545'),
    ('ANATR', 'Ana Trujillo Emparedados y helados', 'Ana Trujillo', 'Owner', 'Avda. de la Constitucion 2222', 'Mexico D.F.', NULL, '05021', 'Mexico', '(5) 555-4729', '(5) 555-3745'),
    ('AROUT', 'Around the Horn', 'Thomas Hardy', 'Sales Representative', '120 Hanover Sq.', 'London', NULL, 'WA1 1DP', 'UK', '(171) 555-7788', '(171) 555-6750');

INSERT INTO [CustomerCustomerDemo] ([CustomerID], [CustomerTypeID]) VALUES
    ('ALFKI', 'WHOLESALE'),
    ('AROUT', 'RETAIL');

INSERT INTO [Shippers] ([ShipperID], [CompanyName], [Phone]) VALUES
    (1, 'Speedy Express', '(503) 555-9831'),
    (2, 'United Package', '(503) 555-3199');

INSERT INTO [Orders] ([OrderID], [CustomerID], [EmployeeID], [OrderDate], [RequiredDate], [ShippedDate], [ShipVia], [Freight], [ShipName], [ShipAddress], [ShipCity], [ShipRegion], [ShipPostalCode], [ShipCountry]) VALUES
    (10248, 'ALFKI', 1, '1996-07-04', '1996-08-01', '1996-07-16', 1, 32.38, 'Alfreds Futterkiste', 'Obere Str. 57', 'Berlin', NULL, '12209', 'Germany'),
    (10249, 'ANATR', 3, '1996-07-05', '1996-08-16', '1996-07-10', 2, 11.61, 'Ana Trujillo Emparedados y helados', 'Avda. de la Constitucion 2222', 'Mexico D.F.', NULL, '05021', 'Mexico'),
    (10250, 'AROUT', 3, '1996-07-08', '1996-08-05', NULL, 2, 65.83, 'Around the Horn', '120 Hanover Sq.', 'London', NULL, 'WA1 1DP', 'UK');

INSERT INTO [Order Details] ([OrderID], [ProductID], [UnitPrice], [Quantity], [Discount]) VALUES
    (10248, 1, 18, 12, 0),
    (10248, 2, 19, 10, 0),
    (10249, 3, 10, 5, 0),
    (10250, 4, 22, 35, 0.15),
    (10250, 5, 21.35, 15, 0);
