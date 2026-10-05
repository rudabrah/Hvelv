// VaultController: globale hurtigtaster, hurtigsøk og Auto-Type.

#include "vaultcontroller.h"

#include "autotype.h"
#include "autotyper.h"
#include "entryfiltermodel.h"
#include "entrymodel.h"
#include "globalhotkeys.h"
#include "totp.h"

#include <QDateTime>
#include <QGuiApplication>

using namespace hvelv;

namespace {
enum HotkeyId { QuickSearchHotkey = 1, AutoTypeHotkey = 2 };
}

void VaultController::initAutoType()
{
    m_quickFilter = new EntryFilterModel(this);
    m_quickFilter->setSourceModel(m_model);

    m_typer = new AutoTyper(this);
    connect(m_typer, &AutoTyper::finished, this, [this](bool ok, const QString &message) {
        if (!ok && !message.isEmpty())
            emit toast(message);
    });

    m_hotkeys = new GlobalHotkeys(this);
    connect(m_hotkeys, &GlobalHotkeys::activated, this, [this](int id) {
        if (id == QuickSearchHotkey)
            requestQuickSearch();
        else if (id == AutoTypeHotkey)
            requestAutoType();
    });
    applyHotkeys();
}

void VaultController::applyHotkeys()
{
    // Tester og annen kjøring uten skjerm skal ikke ta systemets hurtigtaster.
    if (QGuiApplication::platformName() == QLatin1String("offscreen"))
        return;
    QStringList problems;
    const QString search = hotkeySearch();
    const QString autoType = hotkeyAutoType();
    if (!search.isEmpty() && !m_hotkeys->registerHotkey(QuickSearchHotkey, QKeySequence(search, QKeySequence::PortableText)))
        problems << QStringLiteral("hurtigsøk (%1)").arg(search);
    if (search.isEmpty())
        m_hotkeys->unregisterHotkey(QuickSearchHotkey);
    if (!autoType.isEmpty() && !m_hotkeys->registerHotkey(AutoTypeHotkey, QKeySequence(autoType, QKeySequence::PortableText)))
        problems << QStringLiteral("Auto-Type (%1)").arg(autoType);
    if (autoType.isEmpty())
        m_hotkeys->unregisterHotkey(AutoTypeHotkey);
    m_hotkeyStatus = problems.isEmpty()
                         ? QString()
                         : QStringLiteral("Kunne ikke registrere hurtigtast for %1. Den er ugyldig eller brukes av et annet program.")
                               .arg(problems.join(QStringLiteral(" og ")));
    emit settingsChanged();
}

QString VaultController::hotkeySearch() const
{
    return m_settings.value(QStringLiteral("hotkeySearch"), QStringLiteral("Ctrl+Alt+H")).toString();
}

void VaultController::setHotkeySearch(const QString &keys)
{
    m_settings.setValue(QStringLiteral("hotkeySearch"), keys.trimmed());
    applyHotkeys();
}

QString VaultController::hotkeyAutoType() const
{
    return m_settings.value(QStringLiteral("hotkeyAutoType"), QStringLiteral("Ctrl+Alt+A")).toString();
}

void VaultController::setHotkeyAutoType(const QString &keys)
{
    m_settings.setValue(QStringLiteral("hotkeyAutoType"), keys.trimmed());
    applyHotkeys();
}

QString VaultController::autoTypeSequence() const
{
    return m_settings.value(QStringLiteral("autoTypeSequence"), QString::fromLatin1(autotype::DefaultSequence)).toString();
}

void VaultController::setAutoTypeSequence(const QString &sequence)
{
    m_settings.setValue(QStringLiteral("autoTypeSequence"),
                        sequence.trimmed().isEmpty() ? QString::fromLatin1(autotype::DefaultSequence) : sequence.trimmed());
    emit settingsChanged();
}

QString VaultController::validateSequence(const QString &sequence) const
{
    return autotype::parseSequence(sequence, Entry()).error;
}

void VaultController::requestQuickSearch()
{
    const auto target = AutoTyper::foreground();
    m_autoTypeTarget = AutoTyper::isOwnWindow(target) ? 0 : target.window;
    m_autoTypeTitle = AutoTyper::isOwnWindow(target) ? QString() : target.title;
    if (!isUnlocked()) {
        emit showAttentionRequested();
        emit toast(QStringLiteral("Lås opp hvelvet for å bruke hurtigsøk."));
        return;
    }
    m_quickFilter->setOnlyIds({});
    m_quickFilter->setSearchText({});
    emit quickSearchRequested(m_autoTypeTitle, false);
}

void VaultController::requestAutoType()
{
    const auto target = AutoTyper::foreground();
    if (AutoTyper::isOwnWindow(target))
        return;
    m_autoTypeTarget = target.window;
    m_autoTypeTitle = target.title;
    if (!isUnlocked()) {
        emit showAttentionRequested();
        emit toast(QStringLiteral("Lås opp hvelvet for å bruke Auto-Type."));
        return;
    }

    int best = 0;
    QStringList bestIds;
    for (const auto &e : std::as_const(m_data.entries)) {
        const int score = autotype::matchScore(e, target.title);
        if (score <= 0)
            continue;
        if (score > best) {
            best = score;
            bestIds = {e.id};
        } else if (score == best) {
            bestIds << e.id;
        }
    }
    if (bestIds.size() == 1) {
        autoType(bestIds.first(), false);
        return;
    }
    // Flere eller ingen treff: la brukeren velge (treffene vises først hvis det finnes noen).
    m_quickFilter->setSearchText({});
    m_quickFilter->setOnlyIds(bestIds);
    emit quickSearchRequested(m_autoTypeTitle, true);
}

bool VaultController::autoType(const QString &entryId, bool rememberWindow)
{
    Entry *e = mutableEntry(entryId);
    if (!e)
        return false;
    if (m_autoTypeTarget == 0) {
        emit toast(QStringLiteral("Ingen målvindu. Trykk %1 mens du står i påloggingsvinduet.").arg(hotkeyAutoType()));
        return false;
    }
    if (rememberWindow && !m_autoTypeTitle.isEmpty() && autotype::matchScore(*e, m_autoTypeTitle) == 0) {
        const Entry previous = *e;
        e->customFields << CustomField{QString::fromLatin1(autotype::WindowFieldName), m_autoTypeTitle, false};
        e->updatedAt = QDateTime::currentDateTimeUtc();
        if (persist())
            m_model->upsert(*e);
        else
            *e = previous;
    }

    QString code;
    if (const auto cfg = totp::parse(e->totpSecret))
        code = totp::code(*cfg, QDateTime::currentSecsSinceEpoch());
    const auto parsed = autotype::parseSequence(autotype::sequenceFor(*e, autoTypeSequence()), *e, code);
    if (!parsed.error.isEmpty()) {
        emit toast(parsed.error);
        return false;
    }
    m_typer->type({m_autoTypeTarget, m_autoTypeTitle}, parsed.actions);
    m_autoTypeTarget = 0;
    return true;
}
