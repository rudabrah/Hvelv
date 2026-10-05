#pragma once

#include <QAbstractNativeEventFilter>
#include <QHash>
#include <QKeySequence>
#include <QObject>

// Systemomfattende hurtigtaster (Windows RegisterHotKey).
class GlobalHotkeys : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT

public:
    explicit GlobalHotkeys(QObject *parent = nullptr);
    ~GlobalHotkeys() override;

    // Returnerer false hvis kombinasjonen er ugyldig eller allerede brukes av et annet program.
    bool registerHotkey(int id, const QKeySequence &sequence);
    void unregisterHotkey(int id);

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

signals:
    void activated(int id);

private:
    QHash<int, QKeySequence> m_registered;
};
