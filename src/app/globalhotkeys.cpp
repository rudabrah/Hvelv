#include "globalhotkeys.h"

#include <QCoreApplication>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {

#ifdef Q_OS_WIN
bool toNative(const QKeySequence &seq, UINT &mods, UINT &vk)
{
    if (seq.isEmpty())
        return false;
    const QKeyCombination combo = seq[0];
    const Qt::KeyboardModifiers m = combo.keyboardModifiers();
    mods = MOD_NOREPEAT;
    if (m & Qt::ControlModifier) mods |= MOD_CONTROL;
    if (m & Qt::AltModifier) mods |= MOD_ALT;
    if (m & Qt::ShiftModifier) mods |= MOD_SHIFT;
    if (m & Qt::MetaModifier) mods |= MOD_WIN;
    if (mods == MOD_NOREPEAT)
        return false; // krever minst én modifikator, ellers stjeler vi vanlige taster

    const int key = combo.key();
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        vk = UINT('A' + (key - Qt::Key_A));
    else if (key >= Qt::Key_0 && key <= Qt::Key_9)
        vk = UINT('0' + (key - Qt::Key_0));
    else if (key >= Qt::Key_F1 && key <= Qt::Key_F24)
        vk = UINT(VK_F1 + (key - Qt::Key_F1));
    else if (key == Qt::Key_Space)
        vk = VK_SPACE;
    else if (key == Qt::Key_Insert)
        vk = VK_INSERT;
    else
        return false;
    return true;
}
#endif

} // namespace

GlobalHotkeys::GlobalHotkeys(QObject *parent)
    : QObject(parent)
{
    QCoreApplication::instance()->installNativeEventFilter(this);
}

GlobalHotkeys::~GlobalHotkeys()
{
    for (auto id : m_registered.keys())
        unregisterHotkey(id);
    if (QCoreApplication::instance())
        QCoreApplication::instance()->removeNativeEventFilter(this);
}

bool GlobalHotkeys::registerHotkey(int id, const QKeySequence &sequence)
{
    unregisterHotkey(id);
#ifdef Q_OS_WIN
    UINT mods = 0, vk = 0;
    if (!toNative(sequence, mods, vk))
        return false;
    if (!RegisterHotKey(nullptr, id, mods, vk))
        return false;
    m_registered.insert(id, sequence);
    return true;
#else
    Q_UNUSED(sequence);
    return false;
#endif
}

void GlobalHotkeys::unregisterHotkey(int id)
{
#ifdef Q_OS_WIN
    if (m_registered.remove(id))
        UnregisterHotKey(nullptr, id);
#else
    m_registered.remove(id);
#endif
}

bool GlobalHotkeys::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
#ifdef Q_OS_WIN
    if (eventType == "windows_dispatcher_MSG" || eventType == "windows_generic_MSG") {
        const MSG *msg = static_cast<const MSG *>(message);
        if (msg->message == WM_HOTKEY && m_registered.contains(int(msg->wParam))) {
            emit activated(int(msg->wParam));
            if (result)
                *result = 0;
            return true;
        }
    }
#else
    Q_UNUSED(eventType);
    Q_UNUSED(message);
    Q_UNUSED(result);
#endif
    return false;
}
