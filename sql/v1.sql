-- Hvelv databaseskjema, versjon 1 (personlig database).
--
-- Kjøres av appen når en database opprettes, men kan også kjøres manuelt i SSMS mot en tom
-- database. Alle objekter ligger i skjemaet hv. Hemmeligheter krypteres i appen før de lagres:
-- kolonnene som heter Payload inneholder nonce[24] | mac[16] | chiffertekst (XChaCha20-Poly1305).
-- Metadata i klartekst er bundet til Payload som autentiserte data, så endringer gjort direkte i
-- databasen oppdages av appen. Endre derfor bare data via Hvelv.
--
-- Batcher skilles med GO på egen linje.

CREATE SCHEMA hv;
GO

CREATE TABLE hv.Meta (
    Name  nvarchar(64)  NOT NULL CONSTRAINT PK_Meta PRIMARY KEY,
    Value nvarchar(max) NOT NULL
);

CREATE TABLE hv.Entries (
    EntryId         nvarchar(64)  NOT NULL CONSTRAINT PK_Entries PRIMARY KEY,
    EntryType       nvarchar(32)  NOT NULL,
    Title           nvarchar(max) NOT NULL,
    Customer        nvarchar(max) NOT NULL,
    SystemName      nvarchar(max) NOT NULL,
    Environment     nvarchar(max) NOT NULL,
    Owner           nvarchar(max) NOT NULL,
    ExpiresAt       date          NULL,
    ReminderDays    int           NOT NULL,
    ReminderChannel nvarchar(10)  NOT NULL,
    KeyGeneration   int           NOT NULL,
    Payload         varbinary(max) NULL,          -- NULL når oppføringen er slettet
    IsDeleted       bit           NOT NULL CONSTRAINT DF_Entries_IsDeleted DEFAULT 0,
    CreatedAt       datetime2(3)  NOT NULL,
    CreatedBy       nvarchar(128) NOT NULL CONSTRAINT DF_Entries_CreatedBy DEFAULT SUSER_SNAME(),
    UpdatedAt       datetime2(3)  NOT NULL,
    UpdatedBy       nvarchar(128) NOT NULL CONSTRAINT DF_Entries_UpdatedBy DEFAULT SUSER_SNAME(),
    RowVer          rowversion    NOT NULL
);

CREATE TABLE hv.Contacts (
    ContactId     nvarchar(64)  NOT NULL CONSTRAINT PK_Contacts PRIMARY KEY,
    Name          nvarchar(max) NOT NULL,
    Company       nvarchar(max) NOT NULL,
    Role          nvarchar(max) NOT NULL,
    Email         nvarchar(max) NOT NULL,
    Phone         nvarchar(max) NOT NULL,
    KeyGeneration int           NOT NULL,
    Payload       varbinary(max) NULL,            -- notater
    IsDeleted     bit           NOT NULL CONSTRAINT DF_Contacts_IsDeleted DEFAULT 0,
    UpdatedAt     datetime2(3)  NOT NULL CONSTRAINT DF_Contacts_UpdatedAt DEFAULT SYSUTCDATETIME(),
    UpdatedBy     nvarchar(128) NOT NULL CONSTRAINT DF_Contacts_UpdatedBy DEFAULT SUSER_SNAME(),
    RowVer        rowversion    NOT NULL
);

CREATE TABLE hv.EntryContacts (
    EntryId   nvarchar(64) NOT NULL CONSTRAINT FK_EntryContacts_Entry REFERENCES hv.Entries (EntryId),
    ContactId nvarchar(64) NOT NULL CONSTRAINT FK_EntryContacts_Contact REFERENCES hv.Contacts (ContactId),
    SortOrder int          NOT NULL,
    CONSTRAINT PK_EntryContacts PRIMARY KEY (EntryId, ContactId)
);

CREATE TABLE hv.Attachments (
    AttachmentId  nvarchar(64)  NOT NULL CONSTRAINT PK_Attachments PRIMARY KEY,
    EntryId       nvarchar(64)  NOT NULL CONSTRAINT FK_Attachments_Entry REFERENCES hv.Entries (EntryId),
    KeyGeneration int           NOT NULL,
    Payload       varbinary(max) NOT NULL,        -- filnavn, type og innhold
    CreatedAt     datetime2(3)  NOT NULL CONSTRAINT DF_Attachments_CreatedAt DEFAULT SYSUTCDATETIME()
);
GO

-- ---------------------------------------------------------------------------------------------
-- Prosedyrer. Appen bruker bare disse, så tilgang senere kan gis med EXECUTE på skjemaet hv.
-- Feil 50001 = konflikt: raden er endret eller slettet av noen andre siden den ble lest.
-- ---------------------------------------------------------------------------------------------

CREATE OR ALTER PROCEDURE hv.GetMeta
AS
    SET NOCOUNT ON;
    SELECT Name, Value FROM hv.Meta;
GO

CREATE OR ALTER PROCEDURE hv.SetMeta @Name nvarchar(64), @Value nvarchar(max)
AS
BEGIN
    SET NOCOUNT ON;
    UPDATE hv.Meta SET Value = @Value WHERE Name = @Name;
    IF @@ROWCOUNT = 0
        INSERT hv.Meta (Name, Value) VALUES (@Name, @Value);
END
GO

CREATE OR ALTER PROCEDURE hv.ListEntries
AS
    SET NOCOUNT ON;
    SELECT EntryId, EntryType, Title, Customer, SystemName, Environment, Owner, ExpiresAt,
           ReminderDays, ReminderChannel, KeyGeneration, Payload, CreatedAt, UpdatedAt, RowVer
    FROM hv.Entries
    WHERE IsDeleted = 0;
GO

CREATE OR ALTER PROCEDURE hv.SaveEntry
    @EntryId nvarchar(64), @EntryType nvarchar(32), @Title nvarchar(max), @Customer nvarchar(max),
    @SystemName nvarchar(max), @Environment nvarchar(max), @Owner nvarchar(max), @ExpiresAt date,
    @ReminderDays int, @ReminderChannel nvarchar(10), @KeyGeneration int, @Payload varbinary(max),
    @CreatedAt datetime2(3), @UpdatedAt datetime2(3), @ExpectedRowVer binary(8)
AS
BEGIN
    SET NOCOUNT ON;
    SET XACT_ABORT ON;
    DECLARE @Current binary(8) =
        (SELECT RowVer FROM hv.Entries WITH (UPDLOCK, HOLDLOCK) WHERE EntryId = @EntryId);

    IF @Current IS NULL
    BEGIN
        IF @ExpectedRowVer IS NOT NULL
            THROW 50001, N'Oppføringen er slettet et annet sted.', 1;
        INSERT hv.Entries (EntryId, EntryType, Title, Customer, SystemName, Environment, Owner, ExpiresAt,
                           ReminderDays, ReminderChannel, KeyGeneration, Payload, CreatedAt, UpdatedAt)
        VALUES (@EntryId, @EntryType, @Title, @Customer, @SystemName, @Environment, @Owner, @ExpiresAt,
                @ReminderDays, @ReminderChannel, @KeyGeneration, @Payload, @CreatedAt, @UpdatedAt);
    END
    ELSE
    BEGIN
        IF @ExpectedRowVer IS NULL OR @ExpectedRowVer <> @Current
            THROW 50001, N'Oppføringen er endret et annet sted.', 1;
        UPDATE hv.Entries
        SET EntryType = @EntryType, Title = @Title, Customer = @Customer, SystemName = @SystemName,
            Environment = @Environment, Owner = @Owner, ExpiresAt = @ExpiresAt, ReminderDays = @ReminderDays,
            ReminderChannel = @ReminderChannel, KeyGeneration = @KeyGeneration, Payload = @Payload,
            UpdatedAt = @UpdatedAt, UpdatedBy = SUSER_SNAME()
        WHERE EntryId = @EntryId AND IsDeleted = 0;
        IF @@ROWCOUNT = 0
            THROW 50001, N'Oppføringen er slettet et annet sted.', 1;
    END

    SELECT RowVer FROM hv.Entries WHERE EntryId = @EntryId;
END
GO

-- Myk sletting: raden beholdes (for synkronisering senere), men innholdet fjernes.
CREATE OR ALTER PROCEDURE hv.DeleteEntry @EntryId nvarchar(64), @ExpectedRowVer binary(8)
AS
BEGIN
    SET NOCOUNT ON;
    SET XACT_ABORT ON;
    UPDATE hv.Entries
    SET IsDeleted = 1, Payload = NULL, Title = N'', Customer = N'', SystemName = N'', Environment = N'',
        Owner = N'', ExpiresAt = NULL, UpdatedAt = SYSUTCDATETIME(), UpdatedBy = SUSER_SNAME()
    WHERE EntryId = @EntryId AND IsDeleted = 0 AND RowVer = @ExpectedRowVer;
    IF @@ROWCOUNT = 0
        THROW 50001, N'Oppføringen er endret eller slettet et annet sted.', 1;
    DELETE hv.EntryContacts WHERE EntryId = @EntryId;
    DELETE hv.Attachments WHERE EntryId = @EntryId;
END
GO

CREATE OR ALTER PROCEDURE hv.ListContacts
AS
    SET NOCOUNT ON;
    SELECT ContactId, Name, Company, Role, Email, Phone, KeyGeneration, Payload, RowVer
    FROM hv.Contacts
    WHERE IsDeleted = 0;
GO

CREATE OR ALTER PROCEDURE hv.SaveContact
    @ContactId nvarchar(64), @Name nvarchar(max), @Company nvarchar(max), @Role nvarchar(max),
    @Email nvarchar(max), @Phone nvarchar(max), @KeyGeneration int, @Payload varbinary(max),
    @ExpectedRowVer binary(8)
AS
BEGIN
    SET NOCOUNT ON;
    SET XACT_ABORT ON;
    DECLARE @Current binary(8) =
        (SELECT RowVer FROM hv.Contacts WITH (UPDLOCK, HOLDLOCK) WHERE ContactId = @ContactId);

    IF @Current IS NULL
    BEGIN
        IF @ExpectedRowVer IS NOT NULL
            THROW 50001, N'Kontakten er slettet et annet sted.', 1;
        INSERT hv.Contacts (ContactId, Name, Company, Role, Email, Phone, KeyGeneration, Payload)
        VALUES (@ContactId, @Name, @Company, @Role, @Email, @Phone, @KeyGeneration, @Payload);
    END
    ELSE
    BEGIN
        IF @ExpectedRowVer IS NULL OR @ExpectedRowVer <> @Current
            THROW 50001, N'Kontakten er endret et annet sted.', 1;
        UPDATE hv.Contacts
        SET Name = @Name, Company = @Company, Role = @Role, Email = @Email, Phone = @Phone,
            KeyGeneration = @KeyGeneration, Payload = @Payload,
            UpdatedAt = SYSUTCDATETIME(), UpdatedBy = SUSER_SNAME()
        WHERE ContactId = @ContactId AND IsDeleted = 0;
        IF @@ROWCOUNT = 0
            THROW 50001, N'Kontakten er slettet et annet sted.', 1;
    END

    SELECT RowVer FROM hv.Contacts WHERE ContactId = @ContactId;
END
GO

CREATE OR ALTER PROCEDURE hv.DeleteContact @ContactId nvarchar(64), @ExpectedRowVer binary(8)
AS
BEGIN
    SET NOCOUNT ON;
    SET XACT_ABORT ON;
    UPDATE hv.Contacts
    SET IsDeleted = 1, Payload = NULL, Name = N'', Company = N'', Role = N'', Email = N'', Phone = N'',
        UpdatedAt = SYSUTCDATETIME(), UpdatedBy = SUSER_SNAME()
    WHERE ContactId = @ContactId AND IsDeleted = 0 AND RowVer = @ExpectedRowVer;
    IF @@ROWCOUNT = 0
        THROW 50001, N'Kontakten er endret eller slettet et annet sted.', 1;
    DELETE hv.EntryContacts WHERE ContactId = @ContactId;
END
GO

CREATE OR ALTER PROCEDURE hv.ListEntryContacts
AS
    SET NOCOUNT ON;
    SELECT ec.EntryId, ec.ContactId
    FROM hv.EntryContacts ec
    JOIN hv.Entries e ON e.EntryId = ec.EntryId AND e.IsDeleted = 0
    ORDER BY ec.EntryId, ec.SortOrder;
GO

-- @ContactIds er en JSON-liste med kontakt-id-er, i visningsrekkefølge.
CREATE OR ALTER PROCEDURE hv.SetEntryContacts @EntryId nvarchar(64), @ContactIds nvarchar(max)
AS
BEGIN
    SET NOCOUNT ON;
    SET XACT_ABORT ON;
    DELETE hv.EntryContacts WHERE EntryId = @EntryId;
    INSERT hv.EntryContacts (EntryId, ContactId, SortOrder)
    SELECT @EntryId, j.[value], CAST(j.[key] AS int)
    FROM OPENJSON(@ContactIds) j;
END
GO

CREATE OR ALTER PROCEDURE hv.ListAttachments
AS
    SET NOCOUNT ON;
    SELECT a.AttachmentId, a.EntryId, a.KeyGeneration, a.Payload
    FROM hv.Attachments a
    JOIN hv.Entries e ON e.EntryId = a.EntryId AND e.IsDeleted = 0
    ORDER BY a.CreatedAt, a.AttachmentId;
GO

-- Vedlegg endres aldri, de legges bare til eller fjernes.
CREATE OR ALTER PROCEDURE hv.AddAttachment
    @AttachmentId nvarchar(64), @EntryId nvarchar(64), @KeyGeneration int, @Payload varbinary(max)
AS
    SET NOCOUNT ON;
    INSERT hv.Attachments (AttachmentId, EntryId, KeyGeneration, Payload)
    VALUES (@AttachmentId, @EntryId, @KeyGeneration, @Payload);
GO

CREATE OR ALTER PROCEDURE hv.DeleteAttachment @AttachmentId nvarchar(64)
AS
    SET NOCOUNT ON;
    DELETE hv.Attachments WHERE AttachmentId = @AttachmentId;
GO
