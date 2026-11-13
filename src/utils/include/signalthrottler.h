/*
 * Trailing-only adaptation of KDToolBox's KDSignalThrottler.
 * SPDX-FileCopyrightText: 2020 Klarälvdalens Datakonsult AB, a KDAB Group company <info@kdab.com>
 * Original author: Giuseppe D'Angelo <giuseppe.dangelo@kdab.com>
 * SPDX-License-Identifier: MIT
 * See NOTICE for the full license text.
 */
#pragma once

#include <QObject>
#include <QTimer>

// Coalesces requests without postponing an active window. Use in its QObject thread.
class SignalThrottler final : public QObject {
    Q_OBJECT

  public:
    explicit SignalThrottler( QObject* parent = nullptr );
    // Synchronously emits any pending notification; receivers must outlive this flush.
    ~SignalThrottler() override;

    void setTimeout( int timeout );

  public Q_SLOTS:
    void throttle();

  Q_SIGNALS:
    void triggered();

  private:
    QTimer timer_;
};
