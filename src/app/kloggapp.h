/*
 * Copyright (C) 2016 -- 2019 Anton Filimonov and other contributors
 *
 * This file is part of klogg.
 *
 * klogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * klogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with klogg.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef KLOGG_KLOGGAPP_H
#define KLOGG_KLOGGAPP_H

#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <numeric>
#include <qapplication.h>
#include <stack>
#include <stdexcept>

#include <QApplication>
#include <vector>

#include <QCborValue>

#include <QDir>
#include <QFontDatabase>
#include <QMessageBox>
#include <QNetworkProxyFactory>
#include <QUuid>

#ifdef Q_OS_MAC
#include <QFileOpenEvent>
#endif

#include "configuration.h"
#include "applicationrunner.h"
#include "klogg_version.h"
#include "log.h"
#include "logger.h"
#include "persistentinfo.h"
#include "session.h"
#include "uuid.h"

#include <kdsingleapplication.h>

#include "mainwindow.h"
#include "messagereceiver.h"
#include "versionchecker.h"
#include "zzlogg_brand.h"

class KloggApp : public QApplication {

    Q_OBJECT

  public:
    KloggApp( int& argc, char* argv[] )
        : QApplication( argc, argv)
    {
        QFontDatabase::addApplicationFont( ":/fonts/DejaVuSansMono.ttf" );

        QNetworkProxyFactory::setUseSystemConfiguration( true );

        qRegisterMetaType<LoadingStatus>( "LoadingStatus" );
        qRegisterMetaType<LinesCount>( "LinesCount" );
        qRegisterMetaType<LineNumber>( "LineNumber" );
        qRegisterMetaType<std::vector<LineNumber>>( "std::vector<LineNumber>" );
        qRegisterMetaType<klogg::vector<LineNumber>>( "klogg::vector<LineNumber>" );
        qRegisterMetaType<LineLength>( "LineLength" );
        qRegisterMetaType<Portion>( "Portion" );
        qRegisterMetaType<Selection>( "Selection" );
        qRegisterMetaType<QFNotification>( "QFNotification" );
        qRegisterMetaType<QFNotificationReachedEndOfFile>( "QFNotificationReachedEndOfFile" );
        qRegisterMetaType<QFNotificationReachedBegininningOfFile>(
            "QFNotificationReachedBegininningOfFile" );
        qRegisterMetaType<QFNotificationProgress>( "QFNotificationProgress" );
        qRegisterMetaType<QFNotificationInterrupted>( "QFNotificationInterrupted" );
        qRegisterMetaType<QuickFindMatcher>( "QuickFindMatcher" );

        if ( singleApplication_.isPrimaryInstance() ) {
            QObject::connect( &singleApplication_, &KDSingleApplication::messageReceived, &messageReceiver_,
                              &MessageReceiver::receiveMessage, Qt::QueuedConnection );

            QObject::connect( &messageReceiver_, &MessageReceiver::loadFile, this,
                              &KloggApp::loadFileNonInteractive );

            // Version checker notification
            connect( &versionChecker_, &VersionChecker::newVersionFound,
                     [ this ]( const QString& new_version, const QString& url,
                               const QStringList& changes ) {
                         newVersionNotification( new_version, url, changes );
                     } );
        }
    }

    bool isSecondary() const {
        return !singleApplication_.isPrimaryInstance();
    }

    qint64 primaryPid() const {
        return singleApplication_.primaryPid();
    }

    void sendFilesToPrimaryInstance( std::vector<QString> filenames )
    {
#ifdef Q_OS_WIN
        // TODO: fix pid passing
        ::AllowSetForegroundWindow( static_cast<DWORD>( primaryPid() ) );
#endif

        QTimer::singleShot( 100, [ files = std::move( filenames ), this ] {
            QStringList filesToOpen;
            std::copy( files.cbegin(), files.cend(), std::back_inserter( filesToOpen ) );

            QVariantMap data;
            data.insert( "version", kloggVersion() );
            data.insert( "files", QVariant{ filesToOpen } );

            auto cbor = QCborValue::fromVariant( data );
            singleApplication_.sendMessageWithTimeout( cbor.toCbor(), 5000 );

            QTimer::singleShot( 100, this, &QApplication::quit );
        } );
    }


    using MainWindowFactory = std::function<MainWindow*(WindowSession)>;
    void setMainWindowFactory(MainWindowFactory factory) {
        mainWindowFactory_ = std::move(factory);
    }
    // Final teardown after the event loop, before releasing the shared theme.
    void destroyMainWindows() {
        const auto windows = ownedWindows_;
        ownedWindows_.clear();
        mainWindows_.clear();
        activeWindows_ = {};
        for (const auto& window : windows) delete window.data();
    }
    QList<MainWindow*> mainWindows() const
    {
        QList<MainWindow*> result;
        for ( const auto& entry : mainWindows_ ) {
            result.push_back( entry.second );
        }
        return result;
    }

    MainWindow* reloadSession()
    {
        if ( !session_ ) {
            session_ = std::make_shared<Session>();
        }

        for ( auto&& windowSession : session_->windowSessions() ) {
            auto w = newWindow( std::move( windowSession ) );
            w->reloadGeometry();
            w->reloadSession();
            w->show();
        }

        if ( mainWindows_.empty() ) {
            auto w = newWindow();
            w->show();
        }

        return mainWindows_.back().second;
    }

    void clearInactiveSessions()
    {
        LOG_INFO << "Clear inactive sessions";

        auto existingSessions = session_->windowSessions();
        existingSessions.erase( std::remove_if( existingSessions.begin(), existingSessions.end(),
                                                [ this ]( const auto& session ) {
                                                    return std::any_of(
                                                        mainWindows_.begin(), mainWindows_.end(),
                                                        [ &session ]( const auto& window ) {
                                                            return window.first.windowId()
                                                                   == session.windowId();
                                                        } );
                                                } ),
                                existingSessions.end() );

        for ( auto& session : existingSessions ) {
            session.close();
        }
    }

    MainWindow* newWindow()
    {
        if ( !session_ ) {
            session_ = std::make_shared<Session>();
        }

        const auto previousSessions = session_->windowSessions();

        QByteArray geometry;
        if ( !previousSessions.empty() ) {
            previousSessions.back().restoreGeometry( &geometry );
        }

        auto window = newWindow( { session_, generateIdFromUuid(), nextWindowIndex() } );
        window->restoreGeometry( geometry );

        return window;
    }

    void loadFileNonInteractive( const QString& file )
    {
        while ( !activeWindows_.empty() && activeWindows_.top().isNull() ) {
            activeWindows_.pop();
        }

        if ( activeWindows_.empty() ) {
            newWindow();
        }

        activeWindows_.top()->loadFileNonInteractive( file );
    }

    void startBackgroundTasks()
    {
        LOG_DEBUG << "startBackgroundTasks";
        versionChecker_.startCheck();
    }

#ifdef Q_OS_MAC
    bool event( QEvent* event ) override
    {
        if ( event->type() == QEvent::FileOpen ) {
            QFileOpenEvent* openEvent = static_cast<QFileOpenEvent*>( event );
            LOG_INFO << "File open request " << openEvent->file();

            if ( !isSecondary() ) {
                loadFileNonInteractive( openEvent->file() );
            }
            else {
                sendFilesToPrimaryInstance( { openEvent->file() } );
            }
        }

        return QApplication::event( event );
    }
#endif

  private:
    MainWindow* newWindow( WindowSession&& session )
    {
        std::unique_ptr<MainWindow> created(mainWindowFactory_
            ? mainWindowFactory_(session) : new MainWindow(session));
        if (!created) throw std::runtime_error("Main window factory returned null");
        mainWindows_.emplace_back(session, created.get());
        ownedWindows_.removeIf([](const auto& window) { return window.isNull(); });
        ownedWindows_.append(created.get());
        created.release();

        auto& window = mainWindows_.back().second;

        activeWindows_.push( QPointer<MainWindow>( window ) );

        LOG_INFO << "Window " << &window << " created";
        connect( window, &MainWindow::newWindow, [ = ]() { newWindow()->show(); } );
        connect( window, &MainWindow::windowActivated,
                 [ this, window ]() { onWindowActivated( *window ); } );
        connect( window, &MainWindow::windowClosed,
                 [ this, window ]() { onWindowClosed( *window ); } );
        connect( window, &MainWindow::exitRequested, [ this ] { exitApplication(); } );
        connect( window, &MainWindow::restartRequested,
                 [ this ] { restartApplication(); } );

        return window;
    }

    void onWindowActivated( MainWindow& window )
    {
        LOG_INFO << "Window " << &window << " activated";
        activeWindows_.push( QPointer<MainWindow>( &window ) );
    }

    void onWindowClosed( MainWindow& window )
    {
        LOG_INFO << "Window " << &window << " closed";
        auto w = std::find_if( mainWindows_.begin(), mainWindows_.end(),
                               [ &window ]( const auto& p ) { return p.second == &window; } );

        if ( w != mainWindows_.end() ) {
            mainWindows_.erase( w );
            window.deleteLater();
        }
    }

    void exitApplication()
    {
        LOG_INFO << "exit application";
        if ( !closeAllWindowsForApplicationExit() ) {
            return;
        }

        QTimer::singleShot( 100, this, &QCoreApplication::quit );
    }

    bool closeAllWindowsForApplicationExit()
    {
        if ( !session_ ) {
            return true;
        }
        const bool previousExitRequested = session_->exitRequested();
        auto mainWindows = mainWindows_;
        mainWindows.reverse();
        QList<MainWindow*> prepared;
        for ( const auto& [ session, window ] : mainWindows ) {
            Q_UNUSED( session );
            if ( window == nullptr || !window->prepareForApplicationExit() ) {
                for ( MainWindow* preparedWindow : prepared ) {
                    preparedWindow->cancelApplicationExitPreparation();
                }
                session_->setExitRequested( previousExitRequested );
                return false;
            }
            prepared.append( window );
        }

        auto& sessionSettings = PersistentInfo::getSettings( session_settings{} );
        sessionSettings.sync();
        const bool synced = sessionSettings.status() == QSettings::NoError
                            && !property( "zzlogg.test.failSessionSync" ).toBool();
        if ( !synced ) {
            for ( MainWindow* preparedWindow : prepared ) {
                preparedWindow->cancelApplicationExitPreparation();
            }
            session_->setExitRequested( previousExitRequested );
            return false;
        }

        session_->setExitRequested( true );
        for ( const auto& [ session, window ] : mainWindows ) {
            Q_UNUSED( session );
            if ( window != nullptr && !window->closeForApplicationExit() ) {
                for ( MainWindow* preparedWindow : prepared ) {
                    preparedWindow->cancelApplicationExitPreparation();
                }
                session_->setExitRequested( previousExitRequested );
                return false;
            }
        }
        return true;
    }

    void restartApplication()
    {
        if ( restartInProgress_ ) {
            return;
        }
        restartInProgress_ = true;
        LOG_INFO << "restart application";
        if ( !closeAllWindowsForApplicationExit() ) {
            restartInProgress_ = false;
            if ( !property( "zzlogg.test.suppressRestartErrors" ).toBool() ) {
                QMessageBox::critical( nullptr, applicationDisplayName(),
                                       tr( "Unable to save the current session for restart." ) );
            }
            return;
        }
        logging::enableFileLogging( false );
        QCoreApplication::exit( ZzLoggRestartExitCode );
    }

    void newVersionNotification( const QString& new_version, const QString& url,
                                 const QStringList& changes )
    {
        LOG_DEBUG << "newVersionNotification( " << new_version << " from " << url << " )";

        QString message
            = tr( "<p>A new version of %1 (%2) is available for download</p>"
                  "<a href=\"%3\">%3</a>" )
                  .arg( QString::fromLatin1( zzlogg::brand::ProductName ), new_version, url );

        if ( !changes.empty() ) {
            message.append( tr( "<p>Important changes:</p><ul>" ) );
            for ( const auto& change : changes ) {
                message.append( QString( "<li>%1</li>" ).arg( change ) );
            }
            message.append( "</ul>" );
        }

        QMessageBox msgBox;
        msgBox.setText( message );
        msgBox.exec();
    }

    size_t nextWindowIndex() const
    {
        if ( mainWindows_.empty() ) {
            return 0;
        }
        else {
            const auto windowWithMaxIndex = std::max_element(
                mainWindows_.begin(), mainWindows_.end(), []( const auto& lhs, const auto& rhs ) {
                    return lhs.first.windowIndex() < rhs.first.windowIndex();
                } );
            return windowWithMaxIndex->first.windowIndex() + 1;
        }
    }

  private:
    KDSingleApplication singleApplication_;

    MessageReceiver messageReceiver_;

    std::shared_ptr<Session> session_;

    std::list<std::pair<WindowSession, MainWindow*>> mainWindows_;
    QList<QPointer<MainWindow>> ownedWindows_;
    std::stack<QPointer<MainWindow>> activeWindows_;
    MainWindowFactory mainWindowFactory_;

    VersionChecker versionChecker_;
    bool restartInProgress_ = false;
};

#endif // KLOGG_KLOGGAPP_H
