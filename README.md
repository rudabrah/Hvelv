# Hvelv

Passordhvelv for IT-folk som jobber mot mange tredjepartssystemer. I tillegg til brukernavn og passord
lagres kontaktperson hos tredjepart, intern ansvarlig, 2FA-plattform med innebygde TOTP-koder og
utløpsdato for brukeren, med påminnelser.

**Status:** fase 1.5 – lokal desktop-app (Qt 6.8 / QML / C++20). Fase 2 legger til felles server.

## Funksjoner

- **Oppføringstyper med maler:** pålogging, API/app-registrering, sertifikat, database, server og
  notat. Hver type har egne felt, og malene (f.eks. Entra ID-appregistrering) fyller inn en
  fornyelsesrutine.
- **Organisering:** kunde → system → miljø, med gruppering og filter på kunde og intern ansvarlig.
- **Kontaktpersoner:** felles kontaktliste (navn, firma, rolle, e-post, telefon) som kobles til oppføringer.
- **Per oppføring:** egendefinerte felt, passordhistorikk (de 20 siste), fornyelsesrutine,
  vedlegg og 2FA-plattform med TOTP-kode (RFC 6238).
- **Utløp og påminnelser:** Windows-varsel før en bruker, et passord eller et sertifikat går ut.
  Per oppføring velges varsel i appen eller e-post; e-post krever serveren i fase 2.
  Utløpsdatoer kan eksporteres som `.ics` eller legges rett i Outlook-kalenderen («Legg i kalender»).
- **Sertifikater:** les utløp, utsteder og tommelavtrykk fra `.pfx`/`.p12`/`.cer`/`.crt`/`.pem`/`.der`, eller hent
  sertifikatet direkte fra en server (TLS).
- **Import:** KeePass/KeePassXC XML, Bitwarden JSON, CSV fra Bitwarden, 1Password, KeePassXC og
  Chrome/Edge, generell CSV og andre `.hvelv`-filer. Duplikater hoppes over, og kunde/tagg kan
  settes på alt som importeres.
- **Eksport:** kryptert kopi (`.hvelv`) eller CSV (ukryptert, med advarsel).
- **Passordhelse:** svake, gjenbrukte og gamle passord, sjekk mot Have I Been Pwned
  (k-anonymitet – bare de 5 første tegnene av SHA-1-hashen sendes) og TLS-sjekk av sertifikatutløp.
- **Auto-Type og hurtigsøk:** globale hurtigtaster, som kan endres i innstillingene:
  - `Ctrl+Alt+H` åpner hurtigsøk fra hvor som helst.
  - `Ctrl+Alt+A` skriver inn påloggingen for oppføringen som passer best til det aktive vinduet.
  - Standardsekvensen er `{USERNAME}{TAB}{PASSWORD}{ENTER}`. Andre tokens: `{TOTP}`, `{URL}`,
    `{TITLE}`, `{DELAY n}`, piltaster m.m.
  - Egendefinerte felt `Auto-Type` (sekvens) og `Auto-Type-vindu` (vindustittel med `*`) overstyrer
    per oppføring.
- **QR-skanning:** les et TOTP-oppsett fra skjermen, utklippstavlen eller en bildefil.
- **Passordgenerator** med styrkemåler.

## Bygg

Krever Visual Studio 2022 og Qt 6.8.1 (`msvc2022_64`) i `C:\Qt`.

```bat
build.bat            :: Release: bygger og kjører alle tester
build.bat Debug
```

`vcvars64` fungerer bare inne i en `.bat`-fil, så bygg alltid via `build.bat`.

Appen havner i `build\Release\app\Hvelv.exe`, ferdig klargjort med `windeployqt`, slik at mappen
kan kopieres og kjøres på andre maskiner.

`Hvelv.exe sti\til\fil.hvelv` åpner en bestemt hvelvfil uten å endre standardvalget.

Ikonet (`assets/`) lages med `tools\make_icon.bat`.

## Tester

| Test                 | Innhold                                                              |
|----------------------|----------------------------------------------------------------------|
| `hvelv_tests`        | krypto, tukling, filformat og migrering, TOTP (RFC 6238-vektorer), kalender, helse, Auto-Type-parser |
| `hvelv_import_tests` | import/eksport og sammenslåing                                       |
| `hvelv_ui_tests`     | offscreen UI-tester av dialoger og flyt; bruker ingen ekte tastatur, mus eller vinduer |

Miljøvariabler:

- `HVELV_UI_SNAPSHOTS=<mappe>` lagrer skjermbilder fra UI-testene.
- `HVELV_NETWORK_TESTS=1` kjører også live-tester mot Have I Been Pwned og TLS.

Ikke dekket av automatiske tester, så de må prøves manuelt: Auto-Type (ekte tastetrykk), QR fra
skjermen og hvordan dialogene ser ut med ekte grafikkdriver.

## Sikkerhetsdesign

- **Kryptografi:** [Monocypher 4.0.3](third_party/monocypher/VERSION.txt) (revidert, vendret uendret).
  Ingen hjemmelaget kryptografi.
- **Nøkkelhierarki:** hovedpassord → Argon2id (64 MiB, 3 pass) → KEK → låser opp en tilfeldig
  256-bits hvelvnøkkel → XChaCha20-Poly1305 over hele innholdet. Bytte av hovedpassord pakker bare
  om hvelvnøkkelen.
- **Integritet:** headeren (KDF-parametre, salt, innpakket nøkkel) er autentisert som AD, så enhver
  endring i filen avvises. Grenser på KDF-parametre hindrer at en manipulert fil tvinger frem
  enorme minneallokeringer.
- **Lagring:** atomisk skriving (`QSaveFile`), forrige versjon beholdes som `.bak`. Eldre filer
  (innholdsformat 1) oppgraderes automatisk ved åpning.
- **Runtime:** automatisk låsing ved inaktivitet, utklippstavlen tømmes etter et valgfritt antall
  sekunder, kopierte hemmeligheter holdes utenfor Windows utklippshistorikk/skysynk, Argon2 kjører
  utenfor GUI-tråden.
- **Auto-Type:** venter til modifikatortastene er sluppet, og avbryter hvis et annet vindu får
  fokus underveis.
- **Nettverk:** bare HIBP- og TLS-sjekkene bruker nett, og bare når brukeren starter dem. Ingen
  hemmeligheter forlater maskinen.
- **Kjent begrensning:** dekrypterte verdier ligger i vanlige `QString`-er mens hvelvet er åpent og
  kan ikke garantert nullstilles fra minnet.

Filformatet er beskrevet i [`src/core/vaultfile.h`](src/core/vaultfile.h), innholdsformatet i
[`src/core/vaultdata.h`](src/core/vaultdata.h).

## Struktur

```
src/core/     ingen GUI: kryptografi, filformat, datamodell og typer, TOTP, passordgenerator,
              import/eksport, kalender (.ics), passordhelse, Auto-Type-sekvenser
src/app/      VaultController (QML-singleton «Vault», delt i vaultcontroller_*.cpp), listemodeller,
              Windows-spesifikt: globale hurtigtaster, Auto-Type (SendInput), sertifikater
              (CryptoAPI), QR-skanning, nettverkssjekker; main.cpp
qml/          grensesnittet (AppDialog er felles base for alle dialoger)
tests/        tst_core, tst_import, tst_ui + testdata i tests/data
third_party/  Monocypher 4.0.3, ZXing-cpp 3.1.1 (bare QR-leser)
tools/        ikongenerator
assets/       ikoner og Windows-ressursfil
```

## Fase 2 – felles server (plan)

- Server (f.eks. Python/FastAPI + PostgreSQL) som bare lagrer krypterte blobs.
- Deling: hver bruker får et X25519-nøkkelpar; delte samlinger har egen nøkkel som pakkes inn til
  hvert medlem. Serveren ser aldri klartekst.
- E-postvarsler: utløpsdato og mottaker lagres som eksplisitt ukryptert metadata (valgfritt per
  oppføring), slik at serveren vet når den skal sende. Brukeren velger e-post eller varsel i appen.
- Appen kan ha lokalt hvelv og felles hvelv åpne samtidig.
