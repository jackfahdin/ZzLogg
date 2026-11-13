/*
 * Trailing-only adaptation of KDToolBox's KDSignalThrottler.
 * SPDX-FileCopyrightText: 2020 Klarälvdalens Datakonsult AB, a KDAB Group company <info@kdab.com>
 * Original author: Giuseppe D'Angelo <giuseppe.dangelo@kdab.com>
 * SPDX-License-Identifier: MIT
 * See NOTICE for the full license text.
 */
#include "signalthrottler.h"

SignalThrottler::SignalThrottler( QObject* parent )
    : QObject( parent )
    , timer_( this )
{
    timer_.setSingleShot( true );
    // QTimer becomes inactive before timeout, so callbacks may start a new window
    // or delete this object without any subsequent member access here.
    connect( &timer_, &QTimer::timeout, this, &SignalThrottler::triggered );
}

SignalThrottler::~SignalThrottler()
{
    // Preserve KDSignalThrottler's synchronous flush of a pending notification.
    if ( timer_.isActive() ) {
        timer_.stop();
        Q_EMIT triggered();
    }
}

void SignalThrottler::setTimeout( int timeout )
{
    if ( timer_.interval() != timeout ) {
        timer_.setInterval( timeout );
    }
}

void SignalThrottler::throttle()
{
    if ( !timer_.isActive() ) {
        timer_.start();
    }
}
