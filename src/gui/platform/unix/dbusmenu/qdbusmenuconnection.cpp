/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtGui module of the Qt Toolkit.
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

#include <QtGui/qtgui-config.h>

#include <QDBusMessage>
#include <QDBusInterface>
#include <QDBusServiceWatcher>
#include <QDBusConnectionInterface>
#include <QDebug>
#include <QCoreApplication>

#ifndef QT_NO_SYSTEMTRAYICON
#include <private/qdbustrayicon_p.h>
#endif
#include <private/qdbusmenuconnection_p.h>
#include <private/qdbusmenuadaptor_p.h>
#include <private/qdbusplatformmenu_p.h>

QT_BEGIN_NAMESPACE

using namespace Qt::StringLiterals;

Q_DECLARE_LOGGING_CATEGORY(qLcMenu)

const QString StatusNotifierWatcherService = "org.kde.StatusNotifierWatcher"_L1;
const QString StatusNotifierWatcherPath = "/StatusNotifierWatcher"_L1;
const QString StatusNotifierItemPath = "/StatusNotifierItem"_L1;
const QString MenuBarPath = "/MenuBar"_L1;

/*!
    \class QDBusMenuConnection
    \internal
    A D-Bus connection which is used for both menu and tray icon services.
    Connects to the session bus and registers with the respective watcher services.
*/
QDBusMenuConnection::QDBusMenuConnection(QObject *parent, const QString &serviceName)
    : QObject(parent)
    , m_serviceName(serviceName)
    , m_connection(serviceName.isNull() ? QDBusConnection::sessionBus()
                                        : QDBusConnection::connectToBus(QDBusConnection::SessionBus, serviceName))
    , m_dbusWatcher(new QDBusServiceWatcher(StatusNotifierWatcherService, m_connection, QDBusServiceWatcher::WatchForRegistration, this))
    , m_statusNotifierHostRegistered(false)
{
#ifndef QT_NO_SYSTEMTRAYICON
    QDBusInterface systrayHost(StatusNotifierWatcherService, StatusNotifierWatcherPath, StatusNotifierWatcherService, m_connection);
    if (systrayHost.isValid() && systrayHost.property("IsStatusNotifierHostRegistered").toBool())
        m_statusNotifierHostRegistered = true;
    else
        qCDebug(qLcMenu) << "StatusNotifierHost is not registered";
#endif
}

QDBusMenuConnection::~QDBusMenuConnection()
{
  if (!m_serviceName.isEmpty() && m_connection.isConnected())
      QDBusConnection::disconnectFromBus(m_serviceName);
}

void QDBusMenuConnection::dbusError(const QDBusError &error)
{
    qWarning() << "QDBusTrayIcon encountered a D-Bus error:" << error;
}

#ifndef QT_NO_SYSTEMTRAYICON
bool QDBusMenuConnection::registerTrayIconMenu(QDBusTrayIcon *item)
{
    bool success = connection().registerObject(MenuBarPath, item->menu());
    if (!success)  // success == false is normal, because the object may be already registered
        qCDebug(qLcMenu) << "failed to register" << item->instanceId() << MenuBarPath;
    return success;
}

void QDBusMenuConnection::unregisterTrayIconMenu(QDBusTrayIcon *item)
{
    if (item->menu())
        connection().unregisterObject(MenuBarPath);
}

bool QDBusMenuConnection::registerTrayIcon(QDBusTrayIcon *item)
{
    bool success = connection().registerObject(StatusNotifierItemPath, item);
    if (!success) {
        unregisterTrayIcon(item);
        qWarning() << "failed to register" << item->instanceId() << StatusNotifierItemPath;
        return false;
    }

    if (item->menu())
        registerTrayIconMenu(item);

    return registerTrayIconWithWatcher(item);
}

bool QDBusMenuConnection::registerTrayIconWithWatcher(QDBusTrayIcon *item)
{
    Q_UNUSED(item);
    QDBusMessage registerMethod = QDBusMessage::createMethodCall(
                StatusNotifierWatcherService, StatusNotifierWatcherPath, StatusNotifierWatcherService,
                "RegisterStatusNotifierItem"_L1);
    registerMethod.setArguments(QVariantList() << m_connection.baseService());
    return m_connection.callWithCallback(registerMethod, this, SIGNAL(trayIconRegistered()), SLOT(dbusError(QDBusError)));
}

void QDBusMenuConnection::unregisterTrayIcon(QDBusTrayIcon *item)
{
    unregisterTrayIconMenu(item);
    connection().unregisterObject(StatusNotifierItemPath);
}
#endif // QT_NO_SYSTEMTRAYICON

QT_END_NAMESPACE

#include "moc_qdbusmenuconnection_p.cpp"
