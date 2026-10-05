#pragma once

#include "autotype.h"

#include <QList>
#include <QObject>
#include <QString>

// Skriver inn en Auto-Type-sekvens i et annet vindu med Windows SendInput.
class AutoTyper : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    struct Target
    {
        quintptr window = 0;
        QString title;
        bool isValid() const { return window != 0; }
    };

    // Vinduet som er i forgrunnen nå (før Hvelv viser seg).
    static Target foreground();
    static bool isOwnWindow(const Target &target);

    // Venter til modifikatortastene er sluppet, aktiverer målvinduet og skriver.
    void type(const Target &target, const QList<hvelv::autotype::Action> &actions);
    bool isBusy() const { return m_busy; }

signals:
    void finished(bool ok, const QString &message);

private:
    void step();

    Target m_target;
    QList<hvelv::autotype::Action> m_queue;
    int m_waitedMs = 0;
    bool m_started = false;
    bool m_busy = false;
};
