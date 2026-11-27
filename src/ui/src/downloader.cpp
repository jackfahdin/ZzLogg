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

#include <QNetworkReply>

#include "configuration.h"
#include "log.h"

#include "downloader.h"

Downloader::Downloader( QObject* parent )
    : QObject( parent )
{
    manager_.setRedirectPolicy( QNetworkRequest::NoLessSafeRedirectPolicy );
}

QString Downloader::lastError() const
{
    switch ( lastError_ ) {
    case QNetworkReply::NoError: return {};
    case QNetworkReply::TimeoutError: return tr( "The download timed out. Try again later." );
    case QNetworkReply::HostNotFoundError: return tr( "The server could not be found. Check the URL and network connection." );
    case QNetworkReply::ConnectionRefusedError: return tr( "The server refused the connection." );
    case QNetworkReply::SslHandshakeFailedError: return tr( "The secure connection could not be established. Check the server certificate and system time." );
    case QNetworkReply::ContentNotFoundError: return tr( "The requested file was not found on the server." );
    case QNetworkReply::ContentAccessDenied: return tr( "Access to the requested file was denied." );
    case QNetworkReply::OperationCanceledError: return tr( "The download was cancelled." );
    default: return tr( "The download failed. Check the URL and network connection." );
    }
}

void Downloader::download( const QUrl& url, QFile* outputFile )
{
    output_ = outputFile;
    lastError_ = QNetworkReply::NoError;

    QNetworkRequest request( url );

    if ( !Configuration::get().verifySslPeers() ) {
        auto sslConfiguration = QSslConfiguration::defaultConfiguration();
        sslConfiguration.setPeerVerifyMode( QSslSocket::VerifyNone );
        request.setSslConfiguration( sslConfiguration );
    }

    currentDownload_ = manager_.get( request );

    connect( currentDownload_, &QNetworkReply::downloadProgress, this,
             &Downloader::downloadProgress );

    connect( currentDownload_, &QNetworkReply::finished, this, &Downloader::downloadFinished );

    connect( currentDownload_, &QNetworkReply::readyRead, this, &Downloader::downloadReadyRead );

    LOG_INFO << "Downloading " << url.toEncoded();
}

void Downloader::downloadFinished()
{
    output_->close();
    currentDownload_->deleteLater();

    if ( currentDownload_->error() ) {
        // download failed
        LOG_ERROR << "Download failed: " << currentDownload_->errorString();
        lastError_ = currentDownload_->error();
        output_->remove();
        Q_EMIT finished( false );
    }
    else {
        LOG_INFO << "Download done";
        output_->close();
        Q_EMIT finished( true );
    }
}

void Downloader::downloadReadyRead()
{
    output_->write( currentDownload_->readAll() );
}