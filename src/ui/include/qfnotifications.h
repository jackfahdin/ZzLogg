/*
 * Copyright (C) 2013 Nicolas Bonnefon and other contributors
 *
 * This file is part of glogg.
 *
 * glogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * glogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with glogg.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef QFNOTIFICATIONS_H
#define QFNOTIFICATIONS_H

#include <QFontMetrics>
#include <QCoreApplication>
#include <QObject>
#include <QWidget>

// Notifications sent by the QF for displaying to the user
// and their translation in UI text.
class QFNotification {
  public:
    explicit QFNotification( const QString& message = "" )
        : message_{ message }
    {
    }

    QString message() const
    {
        return message_;
    }

    // Max width of the message (in pixels)
    static int maxWidth( const QWidget* widget )
    {
        QFontMetrics fm = widget->fontMetrics();
        return qMax( fm.size( Qt::TextSingleLine, reachedBeginningOfFileText() ).width(),
                     fm.size( Qt::TextSingleLine, reachedEndOfFileText() ).width() );
    }

  protected:
    static QString reachedEndOfFileText()
    {
        return QCoreApplication::translate(
            "QFNotification", "Reached end of file, no occurrence found." );
    }

    static QString reachedBeginningOfFileText()
    {
        return QCoreApplication::translate(
            "QFNotification", "Reached beginning of file, no occurrence found." );
    }

    static QString interruptedText()
    {
        return QCoreApplication::translate( "QFNotification", "Search interrupted" );
    }

  private:
    QString message_;
};

class QFNotificationReachedEndOfFile : public QFNotification {
  public:
    QFNotificationReachedEndOfFile()
        : QFNotification( reachedEndOfFileText() )
    {
    }
};

class QFNotificationReachedBegininningOfFile : public QFNotification {
  public:
    QFNotificationReachedBegininningOfFile()
        : QFNotification( reachedBeginningOfFileText() )
    {
    }
};

class QFNotificationInterrupted : public QFNotification {
  public:
    QFNotificationInterrupted()
        : QFNotification( interruptedText() )
    {
    }
};

class QFNotificationProgress : public QFNotification {
  public:
    // Constructor taking the progress (in percent)
    explicit QFNotificationProgress( int progress_percent = 0 )
        : QFNotification( QObject::tr( "Searching (position %1 %)" ).arg( progress_percent ) )
    {
    }
};

#endif
