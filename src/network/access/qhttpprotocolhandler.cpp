/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Copyright (C) 2014 BlackBerry Limited. All rights reserved.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtNetwork module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include <private/qhttpprotocolhandler_p.h>
#include <private/qnoncontiguousbytedevice_p.h>
#include <private/qhttpnetworkconnectionchannel_p.h>

QT_BEGIN_NAMESPACE

using namespace Qt::StringLiterals;

QHttpProtocolHandler::QHttpProtocolHandler(QHttpNetworkConnectionChannel *channel)
    : QAbstractProtocolHandler(channel)
{
}

void QHttpProtocolHandler::_q_receiveReply()
{
    Q_ASSERT(m_socket);

    if (!m_reply) {
        if (m_socket->bytesAvailable() > 0)
            qWarning() << "QAbstractProtocolHandler::_q_receiveReply() called without QHttpNetworkReply,"
                       << m_socket->bytesAvailable() << "bytes on socket.";
        m_channel->close();
        return;
    }

    // only run when the QHttpNetworkConnection is not currently being destructed, e.g.
    // this function is called from _q_disconnected which is called because
    // of ~QHttpNetworkConnectionPrivate
    if (!qobject_cast<QHttpNetworkConnection*>(m_connection)) {
        return;
    }

    QAbstractSocket::SocketState socketState = m_socket->state();

    // connection might be closed to signal the end of data
    if (socketState == QAbstractSocket::UnconnectedState) {
        if (m_socket->bytesAvailable() <= 0) {
            if (m_reply->d_func()->state == QHttpNetworkReplyPrivate::ReadingDataState) {
                // finish this reply. this case happens when the server did not send a content length
                m_reply->d_func()->state = QHttpNetworkReplyPrivate::AllDoneState;
                m_channel->allDone();
                return;
            } else {
                m_channel->handleUnexpectedEOF();
                return;
            }
        } else {
            // socket not connected but still bytes for reading.. just continue in this function
        }
    }

    // read loop for the response
    qint64 bytes = 0;
    qint64 lastBytes = bytes;
    do {
        lastBytes = bytes;

        QHttpNetworkReplyPrivate::ReplyState state = m_reply->d_func()->state;
        switch (state) {
        case QHttpNetworkReplyPrivate::NothingDoneState: {
            m_reply->d_func()->state = QHttpNetworkReplyPrivate::ReadingStatusState;
            Q_FALLTHROUGH();
        }
        case QHttpNetworkReplyPrivate::ReadingStatusState: {
            qint64 statusBytes = m_reply->d_func()->readStatus(m_socket);
            if (statusBytes == -1) {
                // connection broke while reading status. also handled if later _q_disconnected is called
                m_channel->handleUnexpectedEOF();
                return;
            }
            bytes += statusBytes;
            m_channel->lastStatus = m_reply->statusCode();
            break;
        }
        case QHttpNetworkReplyPrivate::ReadingHeaderState: {
            QHttpNetworkReplyPrivate *replyPrivate = m_reply->d_func();
            qint64 headerBytes = replyPrivate->readHeader(m_socket);
            if (headerBytes == -1) {
                // connection broke while reading headers. also handled if later _q_disconnected is called
                m_channel->handleUnexpectedEOF();
                return;
            }
            bytes += headerBytes;
            // If headers were parsed successfully now it is the ReadingDataState
            if (replyPrivate->state == QHttpNetworkReplyPrivate::ReadingDataState) {
                if (replyPrivate->isCompressed() && replyPrivate->autoDecompress) {
                    // remove the Content-Length from header
                    replyPrivate->removeAutoDecompressHeader();
                } else {
                    replyPrivate->autoDecompress = false;
                }
                if (m_reply->statusCode() == 100) {
                    replyPrivate->clearHttpLayerInformation();
                    replyPrivate->state = QHttpNetworkReplyPrivate::ReadingStatusState;
                    break; // ignore
                }
                if (replyPrivate->shouldEmitSignals())
                    emit m_reply->headerChanged();
                // After headerChanged had been emitted
                // we can suddenly have a replyPrivate->userProvidedDownloadBuffer
                // this is handled in the ReadingDataState however

                if (!replyPrivate->expectContent()) {
                    replyPrivate->state = QHttpNetworkReplyPrivate::AllDoneState;
                    m_channel->allDone();
                    break;
                }
            }
            break;
        }
        case QHttpNetworkReplyPrivate::ReadingDataState: {
           QHttpNetworkReplyPrivate *replyPrivate = m_reply->d_func();
           if (m_socket->state() == QAbstractSocket::ConnectedState &&
               replyPrivate->downstreamLimited && !replyPrivate->responseData.isEmpty() && replyPrivate->shouldEmitSignals()) {
               // (only do the following when still connected, not when we have already been disconnected and there is still data)
               // We already have some HTTP body data. We don't read more from the socket until
               // this is fetched by QHttpNetworkAccessHttpBackend. If we would read more,
               // we could not limit our read buffer usage.
               // We only do this when shouldEmitSignals==true because our HTTP parsing
               // always needs to parse the 401/407 replies. Therefore they don't really obey
               // to the read buffer maximum size, but we don't care since they should be small.
               return;
           }

           if (replyPrivate->userProvidedDownloadBuffer) {
               // the user provided a direct buffer where we should put all our data in.
               // this only works when we can tell the user the content length and he/she can allocate
               // the buffer in that size.
               // note that this call will read only from the still buffered data
               qint64 haveRead = replyPrivate->readBodyVeryFast(m_socket, replyPrivate->userProvidedDownloadBuffer + replyPrivate->totalProgress);
               if (haveRead > 0) {
                   bytes += haveRead;
                   replyPrivate->totalProgress += haveRead;
                   // the user will get notified of it via progress signal
                   emit m_reply->dataReadProgress(replyPrivate->totalProgress, replyPrivate->bodyLength);
               } else if (haveRead == 0) {
                   // Happens since this called in a loop. Currently no bytes available.
               } else if (haveRead < 0) {
                   m_connection->d_func()->emitReplyError(m_socket, m_reply, QNetworkReply::RemoteHostClosedError);
                   break;
               }
           } else if (!replyPrivate->isChunked() && replyPrivate->bodyLength > 0) {
                 // bulk files like images should fulfill these properties and
                 // we can therefore save on memory copying
                qint64 haveRead = replyPrivate->readBodyFast(m_socket, &replyPrivate->responseData);
                bytes += haveRead;
                replyPrivate->totalProgress += haveRead;
                if (replyPrivate->shouldEmitSignals()) {
                    emit m_reply->readyRead();
                    emit m_reply->dataReadProgress(replyPrivate->totalProgress, replyPrivate->bodyLength);
                }
            }
            else
            {
                // use the traditional slower reading (for compressed encoding, chunked encoding,
                // no content-length etc)
                qint64 haveRead = replyPrivate->readBody(m_socket, &replyPrivate->responseData);
                if (haveRead > 0) {
                    bytes += haveRead;
                    replyPrivate->totalProgress += haveRead;
                    if (replyPrivate->shouldEmitSignals()) {
                        emit m_reply->readyRead();
                        emit m_reply->dataReadProgress(replyPrivate->totalProgress, replyPrivate->bodyLength);
                    }
                } else if (haveRead == -1) {
                    // Some error occurred
                    m_connection->d_func()->emitReplyError(m_socket, m_reply, QNetworkReply::ProtocolFailure);
                    break;
                }
            }
            // still in ReadingDataState? This function will be called again by the socket's readyRead
            if (replyPrivate->state == QHttpNetworkReplyPrivate::ReadingDataState)
                break;

            // everything done
            Q_FALLTHROUGH();
            }
      case QHttpNetworkReplyPrivate::AllDoneState:
            m_channel->allDone();
            if (state == QHttpNetworkReplyPrivate::AllDoneState)
                lastBytes = bytes; // No need to loop more just to call m_channel->allDone again.
            break;
        default:
            break;
        }
    } while (bytes != lastBytes && m_reply);
}

void QHttpProtocolHandler::_q_readyRead()
{
    if (m_socket->state() == QAbstractSocket::ConnectedState && m_socket->bytesAvailable() == 0) {
        // We got a readyRead but no bytes are available..
        // This happens for the Unbuffered QTcpSocket
        // Also check if socket is in ConnectedState since
        // this function may also be invoked via the event loop.
        char c;
        qint64  ret = m_socket->peek(&c, 1);
        if (ret < 0) {
            m_channel->_q_error(m_socket->error());
            // We still need to handle the reply so it emits its signals etc.
            if (m_reply)
                _q_receiveReply();
            return;
        }
    }

    if (m_channel->isSocketWaiting() || m_channel->isSocketReading()) {
        if (m_socket->bytesAvailable()) {
            // We might get a spurious call from readMoreLater()
            // call of the QHttpNetworkConnection even while the socket is disconnecting.
            // Therefore check if there is actually bytes available before changing the channel state.
            m_channel->state = QHttpNetworkConnectionChannel::ReadingState;
        }
        if (m_reply)
            _q_receiveReply();
    }
}

bool QHttpProtocolHandler::sendRequest()
{
    m_reply = m_channel->reply;

    if (!m_reply) {
        // heh, how should that happen!
        qWarning("QAbstractProtocolHandler::sendRequest() called without QHttpNetworkReply");
        return false;
    }

    switch (m_channel->state) {
    case QHttpNetworkConnectionChannel::IdleState: { // write the header
        if (!m_channel->ensureConnection()) {
            // wait for the connection (and encryption) to be done
            // sendRequest will be called again from either
            // _q_connected or _q_encrypted
            return false;
        }
        QString scheme = m_channel->request.url().scheme();
        if (scheme == "preconnect-http"_L1 || scheme == "preconnect-https"_L1) {
            m_channel->state = QHttpNetworkConnectionChannel::IdleState;
            m_reply->d_func()->state = QHttpNetworkReplyPrivate::AllDoneState;
            m_channel->allDone();
            m_connection->preConnectFinished(); // will only decrease the counter
            m_reply = nullptr; // so we can reuse this channel
            return true; // we have a working connection and are done
        }

        m_channel->written = 0; // excluding the header
        m_channel->bytesTotal = 0;

        QHttpNetworkReplyPrivate *replyPrivate = m_reply->d_func();
        replyPrivate->clear();
        replyPrivate->connection = m_connection;
        replyPrivate->connectionChannel = m_channel;
        replyPrivate->autoDecompress = m_channel->request.d->autoDecompress;
        replyPrivate->pipeliningUsed = false;

        // if the url contains authentication parameters, use the new ones
        // both channels will use the new authentication parameters
        if (!m_channel->request.url().userInfo().isEmpty() && m_channel->request.withCredentials()) {
            QUrl url = m_channel->request.url();
            QAuthenticator &auth = m_channel->authenticator;
            if (url.userName() != auth.user()
                || (!url.password().isEmpty() && url.password() != auth.password())) {
                auth.setUser(url.userName());
                auth.setPassword(url.password());
                m_connection->d_func()->copyCredentials(m_connection->d_func()->indexOf(m_socket), &auth, false);
            }
            // clear the userinfo,  since we use the same request for resending
            // userinfo in url can conflict with the one in the authenticator
            url.setUserInfo(QString());
            m_channel->request.setUrl(url);
        }
        // Will only be false if Qt WebKit is performing a cross-origin XMLHttpRequest
        // and withCredentials has not been set to true.
        if (m_channel->request.withCredentials())
            m_connection->d_func()->createAuthorization(m_socket, m_channel->request);
#ifndef QT_NO_NETWORKPROXY
        m_header = QHttpNetworkRequestPrivate::header(m_channel->request,
            (m_connection->d_func()->networkProxy.type() != QNetworkProxy::NoProxy));
#else
        m_header = QHttpNetworkRequestPrivate::header(m_channel->request, false);
#endif

        // flushing is dangerous (QSslSocket calls transmit which might read or error)
//        m_socket->flush();
        QNonContiguousByteDevice* uploadByteDevice = m_channel->request.uploadByteDevice();
        if (uploadByteDevice) {
            // connect the signals so this function gets called again
            QObject::connect(uploadByteDevice, SIGNAL(readyRead()), m_channel, SLOT(_q_uploadDataReadyRead()));

            m_channel->bytesTotal = m_channel->request.contentLength();

            m_channel->state = QHttpNetworkConnectionChannel::WritingState; // start writing data
            sendRequest(); //recurse
        } else {
            // no data to send: just send the HTTP headers
            m_socket->write(qExchange(m_header, {}));
            QMetaObject::invokeMethod(m_reply, "requestSent", Qt::QueuedConnection);
            m_channel->state = QHttpNetworkConnectionChannel::WaitingState; // now wait for response
            sendRequest(); //recurse
        }

        break;
    }
    case QHttpNetworkConnectionChannel::WritingState:
    {
        // write the data
        QNonContiguousByteDevice* uploadByteDevice = m_channel->request.uploadByteDevice();
        if (!uploadByteDevice || m_channel->bytesTotal == m_channel->written) {
            // the upload device might have no data to send, but we still have to send the headers,
            // do it now.
            if (!m_header.isEmpty())
                m_socket->write(qExchange(m_header, {}));
            if (uploadByteDevice)
                emit m_reply->dataSendProgress(m_channel->written, m_channel->bytesTotal);
            m_channel->state = QHttpNetworkConnectionChannel::WaitingState; // now wait for response
            sendRequest(); // recurse
            break;
        }

        // only feed the QTcpSocket buffer when there is less than 32 kB in it;
        // note that the headers do not count towards these limits.
        const qint64 socketBufferFill = 32*1024;
        const qint64 socketWriteMaxSize = 16*1024;

        // if it is really an ssl socket, check more than just bytesToWrite()
#ifndef QT_NO_SSL
        QSslSocket *sslSocket = qobject_cast<QSslSocket*>(m_socket);
        const auto encryptedBytesToWrite = [sslSocket]() -> qint64
        {
            return sslSocket ? sslSocket->encryptedBytesToWrite() : 0;
        };
#else
        const auto encryptedBytesToWrite = [](){ return qint64(0); };
#endif

        // throughout this loop, we want to send the data coming from uploadByteDevice.
        // we also need to send the headers, as we try to coalesce their write with the data.
        // we won't send more than the limits above, excluding the headers
        while ((m_socket->bytesToWrite() + encryptedBytesToWrite()) <= socketBufferFill
               && m_channel->bytesTotal != m_channel->written)
        {
            // get pointer to upload data
            qint64 currentReadSize = 0;
            const qint64 desiredReadSize = qMin(socketWriteMaxSize, m_channel->bytesTotal - m_channel->written);
            const char *readPointer = uploadByteDevice->readPointer(desiredReadSize, currentReadSize);

            if (currentReadSize == -1) {
                // premature eof happened
                m_connection->d_func()->emitReplyError(m_socket, m_reply, QNetworkReply::UnknownNetworkError);
                return false;
            } else if (readPointer == nullptr || currentReadSize == 0) {
                // nothing to read currently, break the loop
                break;
            } else {
                if (m_channel->written != uploadByteDevice->pos()) {
                    // Sanity check. This was useful in tracking down an upload corruption.
                    qWarning() << "QHttpProtocolHandler: Internal error in sendRequest. Expected to write at position" << m_channel->written << "but read device is at" << uploadByteDevice->pos();
                    Q_ASSERT(m_channel->written == uploadByteDevice->pos());
                    m_connection->d_func()->emitReplyError(m_socket, m_reply, QNetworkReply::ProtocolFailure);
                    return false;
                }
                qint64 currentWriteSize;
                if (m_header.isEmpty()) {
                    currentWriteSize = m_socket->write(readPointer, currentReadSize);
                } else {
                    // assemble header and data and send them together
                    const qint64 headerSize = m_header.size();
                    m_header.append(readPointer, currentReadSize);
                    currentWriteSize = m_socket->write(qExchange(m_header, {}));
                    if (currentWriteSize != -1)
                        currentWriteSize -= headerSize;
                    QMetaObject::invokeMethod(m_reply, "requestSent", Qt::QueuedConnection);
                }
                if (currentWriteSize == -1 || currentWriteSize != currentReadSize) {
                    // socket broke down
                    m_connection->d_func()->emitReplyError(m_socket, m_reply, QNetworkReply::UnknownNetworkError);
                    return false;
                } else {
                    m_channel->written += currentWriteSize;
                    uploadByteDevice->advanceReadPointer(currentWriteSize);

                    emit m_reply->dataSendProgress(m_channel->written, m_channel->bytesTotal);

                    if (m_channel->written == m_channel->bytesTotal) {
                        // make sure this function is called once again
                        m_channel->state = QHttpNetworkConnectionChannel::WaitingState;
                        sendRequest();
                        break;
                    }
                }
            }
        }
        break;
    }

    case QHttpNetworkConnectionChannel::WaitingState:
    {
        QNonContiguousByteDevice* uploadByteDevice = m_channel->request.uploadByteDevice();
        if (uploadByteDevice) {
            QObject::disconnect(uploadByteDevice, SIGNAL(readyRead()), m_channel, SLOT(_q_uploadDataReadyRead()));
        }

        // HTTP pipelining
        //m_connection->d_func()->fillPipeline(m_socket);
        //m_socket->flush();

        // ensure we try to receive a reply in all cases, even if _q_readyRead_ hat not been called
        // this is needed if the sends an reply before we have finished sending the request. In that
        // case receiveReply had been called before but ignored the server reply
        if (m_socket->bytesAvailable())
            QMetaObject::invokeMethod(m_channel, "_q_receiveReply", Qt::QueuedConnection);
        break;
    }
    case QHttpNetworkConnectionChannel::ReadingState:
        // ignore _q_bytesWritten in these states
        Q_FALLTHROUGH();
    default:
        break;
    }
    return true;
}

QT_END_NAMESPACE
