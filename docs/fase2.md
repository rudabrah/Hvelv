# Fase 2 – personlig og felles database i SQL Server

Status: **2a, 2b og 2c ferdige**, neste er 2d. Sist oppdatert 2026-10-05.

## Krav

- Hver bruker har sin egen **personlige database** som bare vedkommende har tilgang til.
- Alle kobler seg til én **felles database**. Hver person har sin egen bruker der, men **alle ser alt**
  (ingen samlinger eller roller per kunde).
- Appen kan være **koblet til flere databaser samtidig**.
- Begge typer ligger i **SQL Server** og kan inspiseres i SSMS.
- Påminnelser om utløp: per oppføring velges **varsel i appen** eller **e-post**.

## Beslutninger

| Spørsmål | Valg |
|---|---|
| Tilkobling | Appen kobler **direkte** til SQL Server (QtSql + ODBC). Ingen egen API-server. |
| Klartekst i SQL Server | **Metadata** i vanlige kolonner; hemmeligheter krypteres i appen. |
| Personlig database | **SQL Server lokalt** (LocalDB for kolleger; utvikling bruker lokal instans). |
| Felles database – drift | Ikke bestemt. Krever SQL Server Standard/Developer eller høyere (eller Azure SQL Managed Instance) for e-post via SQL Agent. |

## Arkitektur

```
Hvelv-appen (Qt, QtSql/QODBC → "ODBC Driver 17/18 for SQL Server")
 ├─ Personlig:  SQL Server (LocalDB) på egen maskin   → bare din Windows-bruker
 ├─ Felles:     SQL Server sentralt                   → egen pålogging per person, alle ser alt
 └─ Flere ved behov (annen felles database, gammel .hvelv-fil til import)
```

Appen har en liste over **tilkoblinger** (navn, type, connection string) i innstillingene. Hver
tilkobling låses opp for seg. Grensesnittet viser databasene i et sidepanel med «Alle» øverst; søk,
hurtigsøk og Auto-Type leter i alle åpne databaser. Redigeringsvinduet får «Lagre i …», og
oppføringer kan flyttes eller kopieres mellom databaser.

Internt erstattes «ett hvelv» av en **lagringsabstraksjon** (`VaultStore`) med implementasjonene
`FileStore` (dagens `.hvelv`, beholdes for import/eksport/sikkerhetskopi) og `SqlStore`.

## Hva som lagres hvordan

| Klartekst (synlig i SSMS) | Kryptert i appen (XChaCha20-Poly1305) |
|---|---|
| id, type, tittel, kunde, system, miljø, intern ansvarlig, utløpsdato, varslingsvalg og -mottakere, kontaktpersoner (navn, firma, rolle, e-post, telefon), endret av/når | brukernavn, passord, TOTP-hemmelighet, URL*, notater, egne felt, passordhistorikk, fornyelsesrutine, vedlegg (inkl. filnavn), kontaktnotater |

\* Se åpent spørsmål 1.

**Metadata er bundet til det krypterte innholdet.** De autentiserte dataene (AD) for en oppføring er
`"hvelv-entry-v1" | EntryId | KeyGeneration | kanonisk JSON av klartekstkolonnene`. Endres tittel,
kunde e.l. direkte i SSMS, avviser appen oppføringen som «endret utenom Hvelv» i stedet for å vise
feil passord for feil system. Konsekvens: metadata endres bare via appen.

## Nøkler

**Personlig database:** som dagens fil. Hovedpassord → Argon2id (64 MiB, 3 pass) → KEK → pakker opp
en tilfeldig 256-bits databasenøkkel. KDF-parametre, salt og innpakket nøkkel ligger i `HvelvMeta`.

**Felles database:** én tilfeldig 256-bits databasenøkkel, pakket inn til hver brukers offentlige
X25519-nøkkel (Monocypher: X25519 → nøkkelavledning → XChaCha20-Poly1305) i tabellen `Users`.
Brukerens private nøkkel for en felles database lagres **kryptert i den personlige databasen**
(tabell `Identities`). Å låse opp den personlige databasen gir dermed tilgang til de felles.

**Ny bruker:** registrerer seg selv (lager nøkkelpar, legger offentlig nøkkel i `Users` uten
innpakket databasenøkkel). En eksisterende bruker godkjenner etter å ha sammenlignet et kort
**fingeravtrykk** av den offentlige nøkkelen (f.eks. på Teams), og appen pakker inn databasenøkkelen
til den nye brukeren. Dette hindrer at noen med skrivetilgang til databasen smugler inn sin egen nøkkel.

**Avgang:** påloggingen stenges, ny databasenøkkel (`KeyGeneration + 1`) lages, alle oppføringer
krypteres på nytt av en gjenværende brukers app, og nøkkelen pakkes inn til de aktive brukerne.
Revisjonsloggen viser hvilke hemmeligheter personen faktisk hentet, slik at de fornyes først
(med fornyelsesrutine og kontaktperson fra oppføringen).

## Databaseskjema (utkast)

Alle objekter i skjemaet `hv`. Samme skjema i personlig og felles database; tabeller som bare
gjelder felles (`Users`, `AuditLog`) står tomme lokalt.

| Tabell | Innhold |
|---|---|
| `HvelvMeta` | skjemaversjon, databasetype (personlig/felles), KDF-parametre, salt, innpakket nøkkel (personlig), gjeldende `KeyGeneration` |
| `Users` | `Login` (= `SUSER_SNAME()`), visningsnavn, e-post, offentlig nøkkel, innpakket databasenøkkel (NULL = venter på godkjenning), `KeyGeneration`, godkjent av/når, aktiv |
| `Entries` | `EntryId` (uniqueidentifier), metadata-kolonnene over, `Payload` (nonce‖mac‖chiffertekst), `KeyGeneration`, `RowVer` (rowversion), opprettet/endret av/når, `IsDeleted` |
| `Contacts`, `EntryContacts` | kontaktpersoner og koblingen til oppføringer |
| `Attachments` | `EntryId`, kryptert `Payload` (filnavn + innhold) |
| `Identities` (bare personlig) | per felles tilkobling: offentlig nøkkel og kryptert privat nøkkel |
| `AuditLog` (bare felles) | tidspunkt, `Login`, handling (hent/endre/slett/kopier/vis), `EntryId` |
| `ReminderQueue` | anonyme e-postpåminnelser for personlige oppføringer, og logg over sendte varsler |

### Tilgang i felles database

Brukere legges i databaserollen **`hvelv_user`**, som bare har `EXECUTE` på skjemaet `hv` – ingen
direkte `SELECT`/`UPDATE` på tabellene. Prosedyrene (utkast):

| Prosedyre | Gjør |
|---|---|
| `hv.ListEntries @Since` | metadata for rader endret etter `@Since` (inkl. slettede) |
| `hv.GetEntry @Id` | henter `Payload` og **logger** i `AuditLog` |
| `hv.SaveEntry … @ExpectedRowVer` | lagrer; feil 50001 hvis raden er endret av andre (konflikt) |
| `hv.DeleteEntry`, `hv.SaveContact`, `hv.DeleteContact`, `hv.SaveAttachment`, … | |
| `hv.LogEvent @Id, @Action` | appen melder fra om «vist» / «kopiert» / «Auto-Type» |
| `hv.RegisterUser`, `hv.ApproveUser`, `hv.BeginKeyRotation`, `hv.CommitKeyRotation` | brukere og nøkkelbytte |

Pålogging: Windows-autentisering (domenekonto) eller SQL-pålogginger – avgjøres når driftsstedet er
valgt. **Forbehold:** en sysadmin på SQL Server kan slette data og omgå loggen, men kan ikke lese
hemmelighetene uten en brukers nøkkel.

### Samtidighet og synkronisering

`RowVer` gir optimistisk låsing: lagring sender med versjonen den bygger på, og ved konflikt viser
appen begge versjonene (ingenting forsvinner; passordhistorikken beholder det gamle passordet).
Appen spør etter endringer (`ListEntries @Since`) ved oppstart, når vinduet får fokus og ca. hvert
minutt.

## E-postvarsler

Utløpsdato og mottakere ligger i klartekst, så SQL Server sender selv: en daglig **SQL Agent-jobb**
med **Database Mail** finner oppføringer som nærmer seg utløp og sender en e-post med tittel,
kunde, dato og en `hvelv://`-lenke. LocalDB/Express har ikke SQL Agent, så e-post for personlige
oppføringer legges som en anonym rad i felles `ReminderQueue` (bare dato, mottaker og generell tekst).
Leveres som SQL-skript til drift.

## Rekkefølge

| Steg | Innhold | Ferdig når |
|---|---|---|
| **2a** ✅ | `VaultStore`-abstraksjon (`src/core/vaultstore.h`); dagens filhvelv blir `FileStore`. Kontrolleren lagrer via `ChangeSet` (alt eller ingenting). | Ingen synlig endring, alle tester grønne. |
| **2b** ✅ | Skjema + prosedyrer ([`sql/v1.sql`](../sql/v1.sql)), `SqlStore` (`src/core/sqlstore.h`), personlig database i SQL Server («SQL Server…» på opplåsingssiden), kopi fra `.hvelv` via «Eksporter». URL er foreløpig kryptert (åpent spørsmål 1). | Kan opprette, låse opp og bruke personlig database i SQL Server; tester mot lokal instans. |
| **2c** ✅ | Flere tilkoblinger («Databaser…»): passord lagret kryptert i den personlige databasen og automatisk opplåsing, filter på database + merke på hver rad, søk på tvers, «Lagre i …», flytt/kopier (kontakter følger med). | Personlig fil/SQL og en annen database åpne samtidig. |
| **2d** | Felles database: registrering, godkjenning med fingeravtrykk, nøkkelinnpakning, `hvelv_user`-rollen, revisjonslogg, konflikthåndtering, synk. | To testbrukere deler oppføringer; konflikt og tukling oppdages. |
| **2e** | E-post via SQL Agent + Database Mail; avgang med nøkkelbytte og fornyelsesliste. | Påminnelse sendes; tidligere bruker kan ikke lese nye data. |

## Testing

- Kjernelogikk (nøkkelinnpakning, AD-binding, konflikter) testes uten database som i dag.
- `SqlStore`-tester kjører mot en SQL Server-instans når `HVELV_SQL_SERVER` er satt
  (f.eks. `localhost`), og hoppes over ellers. Testene oppretter og sletter bare databaser med
  navn `HvelvTest_<tilfeldig>` – eksisterende databaser røres aldri.
- To simulerte brukere (to nøkkelpar, to tilkoblinger) for deling, godkjenning og avgang.

## Åpne spørsmål

1. **Revisjonslogg vs. søk og frakoblet bruk.** Skal `ListEntries` bare gi metadata, og hemmeligheter
   hentes (og logges) først når de vises/kopieres/Auto-Type-es? Da betyr loggen noe, men Auto-Type
   må matche vindu på klartekst-metadata – derfor foreslås **URL i klartekst** – og det blir ingen
   full lesing uten nett. Alternativet er å hente alt ved synk (rask, frakoblet), men da viser
   loggen bare at noen synket, ikke hva de så. **Anbefaling:** hente ved behov + URL i klartekst.
2. **Hvem kan godkjenne nye brukere og bytte nøkkel?** Alle aktive brukere (enklest, i tråd med
   «alle ser alt») eller en egen admin-rolle.
   *Foreløpig (2026-10-06):* admin oppretter påloggingen (se 3), men innpakningen av databasenøkkelen
   må fortsatt gjøres av en eksisterende Hvelv-bruker etter sjekk av fingeravtrykket. Forslag: alle
   aktive brukere kan godkjenne.
3. **Pålogging til felles database:** Windows-autentisering eller SQL-pålogginger.
   *Foreløpig (2026-10-06, må avklares med bedriften):* den samme brukeren som admin oppretter for
   pålogging til serverne, brukes også mot felles database. Admin oppretter den og legger den i
   `hvelv_user`; avgang = kontoen deaktiveres. Den personlige databasen oppretter brukeren selv.
   Gjenstår: er det en domenekonto, og er det den samme kontoen man logger på PC-en med? Ja → Windows-
   autentisering. Egen konto (f.eks. `adm-…`) eller SQL-pålogging → brukernavn/passord lagres
   kryptert i den personlige databasen, som for andre tilkoblinger i 2c.
4. **Driftssted og utgave** for felles database.
5. **LocalDB for kolleger:** må installeres på hver maskin (gratis, `SqlLocalDB.msi`). Skal appen
   lede brukeren gjennom det, eller skal IT rulle det ut?
   *Besvart (2026-10-06):* trengs ikke. Alle kolleger har SQL Server installert lokalt (samme
   oppsett). Velkomstsiden ved første oppstart finner lokale instanser i registeret og oppretter
   `HvelvPersonlig` der. Installeren tar bare med ODBC-driveren.

## Utviklingsmiljø (sjekket 2026-10-04)

| Komponent | Status |
|---|---|
| Qt ODBC-driver (`qsqlodbc.dll`) | ✅ installert med Qt 6.8.1 |
| ODBC-driver for SQL Server | ✅ «ODBC Driver 17 for SQL Server» (18 ikke installert – ikke nødvendig) |
| SQL Server | ✅ SQL Server 2022 **Developer** (16.0.1000.6, uten oppdateringer), standardinstans kjører, Windows-pålogging med sysadmin |
| SQL Server Agent | ⚠️ finnes, men er stoppet (trengs først i steg 2e) |
| SQL Server LocalDB | ❌ ikke installert – trengs ikke for utvikling (lokal instans brukes), men for kolleger |
| SSMS | ✅ versjon 20 |
| `sqlcmd` | ✅ (ODBC 17-verktøyene) |
| CDB (debugger for Qt Creator) | ✅ |
