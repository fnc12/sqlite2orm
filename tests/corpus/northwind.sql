-- Northwind, the sample database of a specialty foods importer.
--
-- Source:  https://github.com/jpwhite3/northwind-SQLite3, src/create.sql
-- License: MIT, Copyright (c) 2016 JP White
--          https://github.com/jpwhite3/northwind-SQLite3/blob/main/LICENSE
--
-- The CREATE TABLE and CREATE INDEX statements are upstream's, statement for statement. The
-- INSERTs that stand between them there are not, and tests/corpus/northwind_data.sql seeds these
-- tables instead. Upstream's sixteen CREATE VIEWs are left out as well: sqlite_orm maps a view
-- through C++26 reflection, so the header generator says so in a warning and emits
-- `struct [[= "..."_orm_name]] ...`, which no compiler this corpus runs on accepts. Put them back
-- when the corpus can compile a schema under C++26.

CREATE TABLE [Categories]
(      [CategoryID] INTEGER PRIMARY KEY AUTOINCREMENT,
       [CategoryName] TEXT,
       [Description] TEXT,
       [Picture] BLOB
);

CREATE TABLE [CustomerCustomerDemo](
   [CustomerID]TEXT NOT NULL,
   [CustomerTypeID]TEXT NOT NULL,
   PRIMARY KEY ("CustomerID","CustomerTypeID"),
   FOREIGN KEY ([CustomerID]) REFERENCES [Customers] ([CustomerID])
		ON DELETE NO ACTION ON UPDATE NO ACTION,
	FOREIGN KEY ([CustomerTypeID]) REFERENCES [CustomerDemographics] ([CustomerTypeID])
		ON DELETE NO ACTION ON UPDATE NO ACTION
);

CREATE TABLE [CustomerDemographics](
   [CustomerTypeID]TEXT NOT NULL,
   [CustomerDesc]TEXT,
    PRIMARY KEY ("CustomerTypeID")
);

CREATE TABLE [Customers]
(      [CustomerID] TEXT,
       [CompanyName] TEXT,
       [ContactName] TEXT,
       [ContactTitle] TEXT,
       [Address] TEXT,
       [City] TEXT,
       [Region] TEXT,
       [PostalCode] TEXT,
       [Country] TEXT,
       [Phone] TEXT,
       [Fax] TEXT,
       PRIMARY KEY (`CustomerID`)
);

CREATE TABLE [Employees]
(      [EmployeeID] INTEGER PRIMARY KEY AUTOINCREMENT,
       [LastName] TEXT,
       [FirstName] TEXT,
       [Title] TEXT,
       [TitleOfCourtesy] TEXT,
       [BirthDate] DATE,
       [HireDate] DATE,
       [Address] TEXT,
       [City] TEXT,
       [Region] TEXT,
       [PostalCode] TEXT,
       [Country] TEXT,
       [HomePhone] TEXT,
       [Extension] TEXT,
       [Photo] BLOB,
       [Notes] TEXT,
       [ReportsTo] INTEGER,
       [PhotoPath] TEXT,
	   FOREIGN KEY ([ReportsTo]) REFERENCES [Employees] ([EmployeeID])
		ON DELETE NO ACTION ON UPDATE NO ACTION
);

CREATE TABLE [EmployeeTerritories](
   [EmployeeID]INTEGER NOT NULL,
   [TerritoryID]TEXT NOT NULL,
    PRIMARY KEY ("EmployeeID","TerritoryID"),
	FOREIGN KEY ([EmployeeID]) REFERENCES [Employees] ([EmployeeID])
		ON DELETE NO ACTION ON UPDATE NO ACTION,
	FOREIGN KEY ([TerritoryID]) REFERENCES [Territories] ([TerritoryID])
		ON DELETE NO ACTION ON UPDATE NO ACTION
);

CREATE TABLE [Order Details](
   [OrderID]INTEGER NOT NULL,
   [ProductID]INTEGER NOT NULL,
   [UnitPrice]NUMERIC NOT NULL DEFAULT 0,
   [Quantity]INTEGER NOT NULL DEFAULT 1,
   [Discount]REAL NOT NULL DEFAULT 0,
    PRIMARY KEY ("OrderID","ProductID"),
    CHECK ([Discount]>=(0) AND [Discount]<=(1)),
    CHECK ([Quantity]>(0)),
    CHECK ([UnitPrice]>=(0)),
	FOREIGN KEY ([OrderID]) REFERENCES [Orders] ([OrderID])
		ON DELETE NO ACTION ON UPDATE NO ACTION,
	FOREIGN KEY ([ProductID]) REFERENCES [Products] ([ProductID])
		ON DELETE NO ACTION ON UPDATE NO ACTION
);

CREATE TABLE [Orders](
   [OrderID]INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
   [CustomerID]TEXT,
   [EmployeeID]INTEGER,
   [OrderDate]DATETIME,
   [RequiredDate]DATETIME,
   [ShippedDate]DATETIME,
   [ShipVia]INTEGER,
   [Freight]NUMERIC DEFAULT 0,
   [ShipName]TEXT,
   [ShipAddress]TEXT,
   [ShipCity]TEXT,
   [ShipRegion]TEXT,
   [ShipPostalCode]TEXT,
   [ShipCountry]TEXT,
   FOREIGN KEY ([EmployeeID]) REFERENCES [Employees] ([EmployeeID])
		ON DELETE NO ACTION ON UPDATE NO ACTION,
	FOREIGN KEY ([CustomerID]) REFERENCES [Customers] ([CustomerID])
		ON DELETE NO ACTION ON UPDATE NO ACTION,
	FOREIGN KEY ([ShipVia]) REFERENCES [Shippers] ([ShipperID])
		ON DELETE NO ACTION ON UPDATE NO ACTION
);

CREATE TABLE [Products](
   [ProductID]INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
   [ProductName]TEXT NOT NULL,
   [SupplierID]INTEGER,
   [CategoryID]INTEGER,
   [QuantityPerUnit]TEXT,
   [UnitPrice]NUMERIC DEFAULT 0,
   [UnitsInStock]INTEGER DEFAULT 0,
   [UnitsOnOrder]INTEGER DEFAULT 0,
   [ReorderLevel]INTEGER DEFAULT 0,
   [Discontinued]TEXT NOT NULL DEFAULT '0',
    CHECK ([UnitPrice]>=(0)),
    CHECK ([ReorderLevel]>=(0)),
    CHECK ([UnitsInStock]>=(0)),
    CHECK ([UnitsOnOrder]>=(0)),
	FOREIGN KEY ([CategoryID]) REFERENCES [Categories] ([CategoryID])
		ON DELETE NO ACTION ON UPDATE NO ACTION,
	FOREIGN KEY ([SupplierID]) REFERENCES [Suppliers] ([SupplierID])
		ON DELETE NO ACTION ON UPDATE NO ACTION
);

CREATE TABLE [Regions](
   [RegionID]INTEGER NOT NULL PRIMARY KEY,
   [RegionDescription]TEXT NOT NULL
);

CREATE TABLE [Shippers](
   [ShipperID]INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
   [CompanyName]TEXT NOT NULL,
   [Phone]TEXT
);

CREATE TABLE [Suppliers](
   [SupplierID]INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
   [CompanyName]TEXT NOT NULL,
   [ContactName]TEXT,
   [ContactTitle]TEXT,
   [Address]TEXT,
   [City]TEXT,
   [Region]TEXT,
   [PostalCode]TEXT,
   [Country]TEXT,
   [Phone]TEXT,
   [Fax]TEXT,
   [HomePage]TEXT
);

CREATE TABLE [Territories](
   [TerritoryID]TEXT NOT NULL,
   [TerritoryDescription]TEXT NOT NULL,
   [RegionID]INTEGER NOT NULL,
    PRIMARY KEY ("TerritoryID"),
	FOREIGN KEY ([RegionID]) REFERENCES [Regions] ([RegionID])
		ON DELETE NO ACTION ON UPDATE NO ACTION
);
