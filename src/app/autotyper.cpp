#include "autotyper.h"

#include <QCoreApplication>
#include <QTimer>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

using hvelv::autotype::Action;

namespace {

#ifdef Q_OS_WIN
bool modifiersHeld()
{
    for (int vk : {VK_CONTROL, VK_MENU, VK_SHIFT, VK_LWIN, VK_RWIN})
        if (GetAsyncKeyState(vk) & 0x8000)
            return true;
    return false;
}

WORD virtualKey(Action::SpecialKey k)
{
    switch (k) {
    case Action::Tab: return VK_TAB;
    case Action::Enter: return VK_RETURN;
    case Action::Space: return VK_SPACE;
    case Action::Backspace: return VK_BACK;
    case Action::Escape: return VK_ESCAPE;
    case Action::Up: return VK_UP;
    case Action::Down: return VK_DOWN;
    case Action::Left: return VK_LEFT;
    case Action::Right: return VK_RIGHT;
    case Action::Home: return VK_HOME;
    case Action::End: return VK_END;
    case Action::Delete: return VK_DELETE;
    }
    return VK_TAB;
}

void sendKey(WORD vk)
{
    INPUT in[2] = {};
    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wVk = vk;
    in[1] = in[0];
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
}

// Unicode-inntasting er uavhengig av tastaturoppsett (æøå, @ osv. virker overalt).
void sendText(const QString &text)
{
    QList<INPUT> inputs;
    inputs.reserve(text.size() * 2);
    for (QChar c : text) {
        if (c == QLatin1Char('\n') || c == QLatin1Char('\r'))
            continue;
        INPUT down = {};
        down.type = INPUT_KEYBOARD;
        down.ki.wScan = c.unicode();
        down.ki.dwFlags = KEYEVENTF_UNICODE;
        INPUT up = down;
        up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        inputs << down << up;
    }
    if (!inputs.isEmpty())
        SendInput(UINT(inputs.size()), inputs.data(), sizeof(INPUT));
    SecureZeroMemory(inputs.data(), size_t(inputs.size()) * sizeof(INPUT));
}
#endif

} // namespace

AutoTyper::Target AutoTyper::foreground()
{
    Target t;
#ifdef Q_OS_WIN
    HWND hwnd = GetForegroundWindow();
    t.window = reinterpret_cast<quintptr>(hwnd);
    wchar_t buf[512] = {};
    GetWindowTextW(hwnd, buf, 511);
    t.title = QString::fromWCharArray(buf);
#endif
    return t;
}

bool AutoTyper::isOwnWindow(const Target &target)
{
#ifdef Q_OS_WIN
    DWORD pid = 0;
    GetWindowThreadProcessId(reinterpret_cast<HWND>(target.window), &pid);
    return pid == GetCurrentProcessId();
#else
    Q_UNUSED(target);
    return false;
#endif
}

void AutoTyper::type(const Target &target, const QList<Action> &actions)
{
    if (m_busy || !target.isValid() || isOwnWindow(target)) {
        emit finished(false, QStringLiteral("Fant ikke et vindu å skrive i."));
        return;
    }
    m_target = target;
    m_queue = actions;
    m_waitedMs = 0;
    m_started = false;
    m_busy = true;
    QTimer::singleShot(50, this, &AutoTyper::step);
}

void AutoTyper::step()
{
#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(m_target.window);
    if (!m_started) {
        // Vent til brukeren har sluppet Ctrl/Alt fra hurtigtasten (maks 3 s).
        if (modifiersHeld() && m_waitedMs < 3000) {
            m_waitedMs += 50;
            QTimer::singleShot(50, this, &AutoTyper::step);
            return;
        }
        if (!IsWindow(hwnd)) {
            m_busy = false;
            m_queue.clear();
            emit finished(false, QStringLiteral("Målvinduet er lukket."));
            return;
        }
        SetForegroundWindow(hwnd);
        m_started = true;
        QTimer::singleShot(150, this, &AutoTyper::step); // la vinduet få fokus
        return;
    }
    // Avbryt hvis brukeren har byttet vindu underveis, så vi ikke skriver passord på feil sted.
    if (GetForegroundWindow() != hwnd) {
        m_busy = false;
        m_queue.clear();
        emit finished(false, QStringLiteral("Auto-Type avbrutt: et annet vindu fikk fokus."));
        return;
    }
    while (!m_queue.isEmpty()) {
        Action a = m_queue.takeFirst();
        if (a.kind == Action::Delay) {
            QTimer::singleShot(a.ms, this, &AutoTyper::step);
            return;
        }
        if (a.kind == Action::Key)
            sendKey(virtualKey(a.key));
        else
            sendText(a.text);
        a.text.fill(QLatin1Char('\0'));
    }
#endif
    m_busy = false;
    emit finished(true, QString());
}
