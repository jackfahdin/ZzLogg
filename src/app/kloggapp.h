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
#include <QDesktopServices>
#include <QFontDatabase>
#include <QMessageBox>
#include <QNetworkProxyFactory>
#include <QUuid>

#ifdef Q_OS_MAC
#include <QFileOpenEvent>
#endif

#include "configuration.h"
#include "applicationrunner.h"
#include "applicationupdateguard.h"
#include "applicationupdatehandoff.h"
#include "klogg_version.h"
#include "log.h"
#include "logger.h"
#include "persistentinfo.h"
#include "session.h"
#include "uuid.h"

#include <kdsingleapplication.h>

#include "mainwindow.h"
#include "messagereceiver.h"
#include "storagecontext.h"
#include "updatecheckdialog.h"
#include "zzlogg/updateqt/updateservice.h"
#include "zzlogg/updateqt/updatedownloadservice.h"
#include "zzlogg/updateqt/updatecachepaths.h"
#include "zzlogg/updateqt/currentinstallation.h"
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

        }
    }

    bool isSecondary() const {
        return !singleApplication_.isPrimaryInstance();
    }

    qint64 primaryPid() const {
        return singleApplication_.primaryPid();
    }

    // The verified installer package (3B.2 download output) and the release
    // version it was verified for, offered to an update handoff request.
    // Empty unless a package is verified; data only, never an execution
    // capability.
    std::pair<QString, QString> verifiedUpdateOffer() const
    {
        if ( !updateDownloadService_
             || updateDownloadService_->snapshot().status
                    != zzlogg::updateqt::DownloadStatus::Verified ) {
            return {};
        }
        QString version;
        if ( updateService_ && updateService_->snapshot().release ) {
            const auto& release = updateService_->snapshot().release->manifest().version;
            version = QString( "%1.%2.%3" )
                          .arg( release.year, 2, 10, QChar( '0' ) )
                          .arg( release.month, 2, 10, QChar( '0' ) )
                          .arg( release.patch, 2, 10, QChar( '0' ) );
        }
        return { updateDownloadService_->snapshot().verifiedPath, version };
    }

    // Installation activity lease for this process. runKloggApplication enters
    // it before any single-instance forwarding; the update handoff UI drives
    // reserve/cancel from the UI thread.
    ApplicationUpdateGuard& updateGuard() {
        return updateGuard_;
    }

  Q_SIGNALS:
    // Involuntary exit-preparation loss: a prepared participant was destroyed
    // and the preparation has already been cancelled. An explicit
    // cancelApplicationExitPreparation() is not a loss and stays silent.
    void applicationExitPreparationLost();

  public:

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
        if ( exitPreparationState_ != ExitPreparationState::Idle ) return nullptr;
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
        if ( exitPreparationState_ != ExitPreparationState::Idle ) return;
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
        if ( exitPreparationState_ != ExitPreparationState::Idle ) {
            LOG_WARNING << "Window creation rejected while application exit is prepared";
            return nullptr;
        }
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
        if ( exitPreparationState_ != ExitPreparationState::Idle ) {
            LOG_WARNING << "File request rejected while application exit is prepared: " << file;
            return;
        }
        while ( !activeWindows_.empty() && activeWindows_.top().isNull() ) {
            activeWindows_.pop();
        }

        if ( activeWindows_.empty() ) {
            newWindow();
        }

        activeWindows_.top()->loadFileNonInteractive( file );
    }

    // Preparation owns the saved session snapshot until explicitly cancelled or committed.
    // No event loop is entered between commit preflight and closing the participants.
    bool prepareApplicationExit()
    {
        if ( exitPreparationState_ != ExitPreparationState::Idle ) return false;
        previousExitRequested_ = session_ && session_->exitRequested();
        exitPreparationState_ = ExitPreparationState::Preparing;
        preparedWindows_.clear();
        for ( auto it = mainWindows_.crbegin(); it != mainWindows_.crend(); ++it ) {
            preparedWindows_.append( it->second );
        }
        const auto participants = preparedWindows_;
        for ( const auto& window : participants ) {
            if ( exitPreparationState_ != ExitPreparationState::Preparing ) return false;
            if ( !window || !window->prepareForApplicationExit() ) {
                cancelApplicationExitPreparation();
                return false;
            }
        }
        if ( exitPreparationState_ != ExitPreparationState::Preparing ) return false;
        if ( session_ ) {
            auto& settings = PersistentInfo::getSettings( session_settings{} );
            settings.sync();
            if ( settings.status() != QSettings::NoError
                 || property( "zzlogg.test.failSessionSync" ).toBool() ) {
                cancelApplicationExitPreparation();
                return false;
            }
        }
        exitPreparationState_ = ExitPreparationState::Prepared;
        return true;
    }

    void cancelApplicationExitPreparation()
    {
        if ( exitPreparationState_ == ExitPreparationState::Idle
             || exitPreparationState_ == ExitPreparationState::Cancelling
             || exitPreparationState_ == ExitPreparationState::Committing ) return;
        exitPreparationState_ = ExitPreparationState::Cancelling;
        const auto participants = preparedWindows_;
        preparedWindows_.clear();
        for ( const auto& window : participants ) {
            if ( window ) window->cancelApplicationExitPreparation();
        }
        if ( session_ ) session_->setExitRequested( previousExitRequested_ );
        exitPreparationState_ = ExitPreparationState::Idle;
    }

    bool isApplicationExitPrepared() const
    {
        return exitPreparationState_ == ExitPreparationState::Prepared;
    }

    bool commitApplicationExit()
    {
        if ( !isApplicationExitPrepared() ) return false;
        const auto participants = preparedWindows_;
        for ( const auto& window : participants ) {
            if ( !window || !window->canCommitApplicationExit() ) {
                cancelApplicationExitPreparation();
                return false;
            }
        }
        exitPreparationState_ = ExitPreparationState::Committing;
        if ( session_ ) session_->setExitRequested( true );
        for ( const auto& window : participants ) {
            if ( !window || !window->commitPreparedApplicationExit() ) {
                exitPreparationState_ = ExitPreparationState::Prepared;
                cancelApplicationExitPreparation();
                return false;
            }
        }
        preparedWindows_.clear();
        exitPreparationState_ = ExitPreparationState::Idle;
        return true;
    }

    void startBackgroundTasks()
    {
        LOG_DEBUG << "startBackgroundTasks";
        ensureUpdateService();
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
        connect( window, &MainWindow::newWindow, this, [ this ] {
            if ( auto* createdWindow = newWindow() ) createdWindow->show();
        } );
        connect( window, &QObject::destroyed, this, [this, window] {
            mainWindows_.remove_if( [window]( const auto& entry ) {
                return entry.second == window;
            } );
            // QWidget emits destroyed before all of its children are gone. Never
            // restore actions on that half-destructed participant during rollback.
            const auto removed = preparedWindows_.removeIf( [window]( const auto& participant ) {
                return participant.isNull() || participant.data() == window;
            } );
            if ( removed && ( exitPreparationState_ == ExitPreparationState::Prepared
                              || exitPreparationState_ == ExitPreparationState::Preparing ) ) {
                cancelApplicationExitPreparation();
                Q_EMIT applicationExitPreparationLost();
            }
        } );
        connect( window, &MainWindow::windowActivated,
                 [ this, window ]() { onWindowActivated( *window ); } );
        connect( window, &MainWindow::windowClosed,
                 [ this, window ]() { onWindowClosed( *window ); } );
        connect( window, &MainWindow::exitRequested, [ this ] { exitApplication(); } );
        connect( window, &MainWindow::restartRequested,
                 [ this ] { restartApplication(); } );
        connect(window, &MainWindow::checkUpdatesRequested, this, [this, window] {
            ensureUpdateService();
            if (!updateService_) return;
            updateDownloadService_->invalidate();
            updateDialogDismissed_=false;
            showUpdateDialog(window,true);
            updateService_->requestCheck(configuredUpdateChannel(),zzlogg::updateqt::CheckOrigin::Manual);
        });
        connect(window, &MainWindow::updatePreferencesChanged, this, [this] {
            ensureUpdateService();
            if (!updateService_) return;
            if(updateService_->snapshot().channel!=configuredUpdateChannel()) updateDownloadService_->invalidate();
            updateService_->setChannel(configuredUpdateChannel());
            updateService_->setAutomaticChecking(singleApplication_.isPrimaryInstance()
                && Configuration::get().versionCheckingEnabled());
        });

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
        return prepareApplicationExit() && commitApplicationExit();
    }

    void restartApplication()
    {
        if ( restartInProgress_ || exitPreparationState_ != ExitPreparationState::Idle ) {
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

    zzlogg::updateqt::Channel configuredUpdateChannel() const {
        return Configuration::get().updateChannel()=="preview"
            ? zzlogg::updateqt::Channel::Preview : zzlogg::updateqt::Channel::Stable;
    }

    void ensureUpdateService() {
        using namespace zzlogg::updateqt;
        if (updateService_ || !StorageContext::isInstalled()) return;
        // 检查与下载必须针对同一个安装身份，只探测一次再分发给两个服务。
        const auto installed=currentInstalledRelease();
        const auto root=StorageContext::current().runtimePaths().appConfigDirectory;
        const auto path=root.isEmpty() ? QString{} : QDir(root).filePath("updates/production/check-state-v1.json");
        updateService_=std::make_unique<UpdateService>(productionFeedConfiguration(),
            std::make_shared<UpdateStateStore>(path),installed,
            [] { return QDateTime::currentSecsSinceEpoch(); },this);
        updateService_->setObjectName("applicationUpdateService");
        updateService_->setDisplayVersion(zzlogg::update::parseVersion(QString(kloggVersion()).toStdString()));
        updateDownloadService_=std::make_unique<UpdateDownloadService>(productionFeedConfiguration(),
            installed,updateCachePath(),[] { return QDateTime::currentSecsSinceEpoch(); },this);
        updateDownloadService_->setObjectName("applicationUpdateDownloadService");
        connect(updateDownloadService_.get(),&UpdateDownloadService::snapshotChanged,this,[this] {
            if(updateDialog_) { updateDialog_->setDownloadSnapshot(updateDownloadService_->snapshot()); pushUpdateExecutionCapability(); }
        });
        updateService_->setChannel(configuredUpdateChannel());
        updateService_->setAutomaticChecking(singleApplication_.isPrimaryInstance()
            && Configuration::get().versionCheckingEnabled());
        connect(updateService_.get(),&UpdateService::snapshotChanged,this,[this] {
            const auto& snapshot=updateService_->snapshot();
            if (snapshot.status==CheckStatus::Checking) {
                updateDownloadService_->invalidate();
                updateDialogDismissed_=false;
            }
            if (updateDialog_) { updateDialog_->setSnapshot(snapshot); pushUpdateExecutionCapability(); }
            if (!snapshot.presentToUser || updateDialogDismissed_ || snapshot.status==CheckStatus::Checking) return;
            if (!updateDialog_ && !mainWindows_.empty())
                showUpdateDialog(mainWindows_.front().second,false);
        });
        connect(this,&QCoreApplication::aboutToQuit,this,[this] {
            updateDialogDismissed_=true;
            if(updateService_) updateService_->cancel();
            if(updateDownloadService_) updateDownloadService_->cancel();
        });
    }

    void showUpdateDialog(QWidget* owner,bool foreground) {
        if (!updateService_) return;
        QWidget* modal=QApplication::activeModalWidget();
        QWidget* parent=modal ? modal : owner;
        if (!updateDialog_) {
            auto* dialog=new UpdateCheckDialog(parent);
            updateDialog_=dialog;
            dialog->setAttribute(Qt::WA_ShowWithoutActivating,!foreground);
            connect(dialog,&UpdateCheckDialog::checkRequested,this,[this] {
                updateDownloadService_->invalidate();
                updateDialogDismissed_=false;
                updateService_->requestCheck(configuredUpdateChannel(),zzlogg::updateqt::CheckOrigin::Manual);
            });
            connect(dialog,&UpdateCheckDialog::cancelRequested,this,[this] { updateService_->cancel(); });
            connect(dialog,&UpdateCheckDialog::downloadRequested,this,[this] {
                updateDownloadService_->requestDownload(updateService_->snapshot());
            });
            connect(dialog,&UpdateCheckDialog::downloadCancelRequested,this,[this] { updateDownloadService_->cancel(); });
            connect(dialog,&UpdateCheckDialog::skipRequested,this,[this] {
                if(updateDownloadService_->snapshot().status==zzlogg::updateqt::DownloadStatus::Downloading) return;
                updateDownloadService_->invalidate();
                updateService_->skipCurrentRelease();
                if (updateDialog_ && !updateService_->snapshot().presentToUser) updateDialog_->close();
            });
            connect(dialog,&UpdateCheckDialog::releasesPageRequested,this,[](const QUrl& url) {
                QDesktopServices::openUrl(url);
            });
            connect(dialog,&UpdateCheckDialog::installRequested,this,[this] { updateHandoff().begin(); });
            connect(dialog,&UpdateCheckDialog::installCancelRequested,this,[this] { updateHandoff().cancel(); });
            connect(dialog,&UpdateCheckDialog::closing,this,[this,dialog] {
                updateDialogDismissed_=true;
                if(updateDialog_==dialog) updateDialog_.clear();
            });
            connect(dialog,&QDialog::finished,dialog,&QObject::deleteLater);
        } else if (foreground && modal && updateDialog_->parentWidget()!=modal) {
            updateDialog_->setParent(modal,Qt::Dialog);
        }
        updateDialog_->setSnapshot(updateService_->snapshot());
        updateDialog_->setDownloadSnapshot(updateDownloadService_->snapshot());
        pushUpdateExecutionCapability();
        updateDialog_->show();
        if (foreground) { updateDialog_->raise(); updateDialog_->activateWindow(); }
    }


    // Lazily owned restricted handoff controller. The production factory is
    // closed, so begin() always refuses; the wiring only forwards the dialog
    // requests and arranges the real exit after a committed handoff. The
    // session request carries the reserved directory identity plus the
    // verified package offer above; production never composes a factory, so
    // that data can never become execution authority this phase.
    ApplicationUpdateHandoff& updateHandoff()
    {
        if ( !updateHandoff_ ) {
            auto handoff = std::make_unique<ApplicationUpdateHandoff>( *this );
            connect( handoff.get(), &ApplicationUpdateHandoff::snapshotChanged, this,
                     [ this ]( const ApplicationUpdateHandoff::Snapshot& snapshot ) {
                         if ( updateDialog_ ) {
                             updateDialog_->setHandoffState( mapHandoffState( snapshot.state ),
                                                             mapHandoffError( snapshot.failure ) );
                         }
                         if ( snapshot.state == ApplicationUpdateHandoff::State::Waiting ) {
                             // Minimal prepared-to-commit interval: the peer is
                             // already live, authenticated and AwaitingAppExit.
                             updateHandoff_->commitExit();
                         }
                     } );
            connect( handoff.get(), &ApplicationUpdateHandoff::exitCommitted, this, [] {
                QCoreApplication::exit( 0 ); // arranged exit, never the restart code
            } );
            updateHandoff_ = std::move( handoff );
        }
        return *updateHandoff_;
    }

    // Capability is presentation only; the dialog clears it on every snapshot
    // change, so it is re-pushed after each delivery.
    void pushUpdateExecutionCapability()
    {
        if ( updateDialog_ ) {
            updateDialog_->setUpdateExecutionAvailable( updateHandoff().executionAvailable() );
        }
    }

    static UpdateCheckDialog::UpdateHandoffState mapHandoffState( ApplicationUpdateHandoff::State state )
    {
        using H = ApplicationUpdateHandoff;
        using D = UpdateCheckDialog;
        switch ( state ) {
        case H::State::Idle: return D::UpdateHandoffState::Idle;
        case H::State::Preparing: return D::UpdateHandoffState::Preparing;
        case H::State::Waiting: return D::UpdateHandoffState::Waiting;
        case H::State::ExitCommitted: return D::UpdateHandoffState::ExitCommitted;
        case H::State::Cancelled: return D::UpdateHandoffState::Cancelled;
        case H::State::Failed: return D::UpdateHandoffState::Failed;
        }
        return D::UpdateHandoffState::Idle;
    }

    static UpdateCheckDialog::UpdateHandoffError mapHandoffError( ApplicationUpdateHandoff::Failure failure )
    {
        using H = ApplicationUpdateHandoff;
        using D = UpdateCheckDialog;
        switch ( failure ) {
        case H::Failure::None: return D::UpdateHandoffError::None;
        case H::Failure::ExecutionClosed: return D::UpdateHandoffError::Closed;
        case H::Failure::PreparationFailed: return D::UpdateHandoffError::Preparation;
        case H::Failure::ReservationBlocked: return D::UpdateHandoffError::Blocked;
        case H::Failure::ReservationAbandoned: return D::UpdateHandoffError::Abandoned;
        case H::Failure::ReservationUnavailable: return D::UpdateHandoffError::Unavailable;
        case H::Failure::LaunchFailed:
        case H::Failure::PeerRejected:
        case H::Failure::PeerLost: return D::UpdateHandoffError::Helper;
        case H::Failure::CommitRejected: return D::UpdateHandoffError::Commit;
        case H::Failure::ApprovalDeclined: return D::UpdateHandoffError::ApprovalDeclined;
        // Diagnostic-only: the snapshot state stays Cancelled, so the dialog
        // keeps presenting the ordinary cancelled text.
        case H::Failure::CancellationInProgress: return D::UpdateHandoffError::None;
        }
        return D::UpdateHandoffError::Helper;
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
    // Directory-scoped instance name: same installation directory (any path
    // casing) forwards, different installation directories stay independent.
    KDSingleApplication singleApplication_{ ApplicationUpdateGuard::singleInstanceName(
        QCoreApplication::applicationDirPath(), QCoreApplication::applicationFilePath() ) };

    MessageReceiver messageReceiver_;

    ApplicationUpdateGuard updateGuard_;

    std::shared_ptr<Session> session_;

    std::list<std::pair<WindowSession, MainWindow*>> mainWindows_;
    QList<QPointer<MainWindow>> ownedWindows_;
    std::stack<QPointer<MainWindow>> activeWindows_;
    MainWindowFactory mainWindowFactory_;

    std::unique_ptr<zzlogg::updateqt::UpdateService> updateService_;
    std::unique_ptr<zzlogg::updateqt::UpdateDownloadService> updateDownloadService_;
    QPointer<UpdateCheckDialog> updateDialog_;
    bool updateDialogDismissed_=false;
    bool restartInProgress_ = false;
    enum class ExitPreparationState { Idle, Preparing, Prepared, Cancelling, Committing };
    ExitPreparationState exitPreparationState_ = ExitPreparationState::Idle;
    QList<QPointer<MainWindow>> preparedWindows_;
    bool previousExitRequested_ = false;
    // Destroyed first: a live handoff restores preparation and releases the
    // reservation while every other member is still valid.
    std::unique_ptr<ApplicationUpdateHandoff> updateHandoff_;
};

#endif // KLOGG_KLOGGAPP_H
