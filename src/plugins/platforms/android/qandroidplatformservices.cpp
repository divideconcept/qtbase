/****************************************************************************
**
** Copyright (C) 2012 BogDan Vatra <bogdan@kde.org>
** Copyright (C) 2021 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the plugins of the Qt Toolkit.
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

#include "qandroidplatformservices.h"

#include <QDebug>
#include <QDesktopServices>
#include <QFile>
#include <QMimeDatabase>
#include <QtCore/QJniObject>
#include <QtCore/qcoreapplication.h>
#include <QtCore/qscopedvaluerollback.h>

QT_BEGIN_NAMESPACE

using namespace Qt::StringLiterals;

QAndroidPlatformServices::QAndroidPlatformServices()
{
    m_actionView = QJniObject::getStaticObjectField("android/content/Intent", "ACTION_VIEW",
                                                    "Ljava/lang/String;")
                           .toString();

    QtAndroidPrivate::registerNewIntentListener(this);

    QMetaObject::invokeMethod(
            this,
            [this] {
                QJniObject context = QJniObject(QtAndroidPrivate::context());
                QJniObject intent =
                        context.callObjectMethod("getIntent", "()Landroid/content/Intent;");
                handleNewIntent(nullptr, intent.object());
            },
            Qt::QueuedConnection);
}

bool QAndroidPlatformServices::openUrl(const QUrl &theUrl)
{
    QString mime;
    QUrl url(theUrl);

    // avoid recursing back into self
    if (url == m_handlingUrl)
        return false;

    // if the file is local, we need to pass the MIME type, otherwise Android
    // does not start an Intent to view this file
    const auto fileScheme = "file"_L1;
    if ((url.scheme().isEmpty() || url.scheme() == fileScheme) && QFile::exists(url.path())) {
        // a real URL including the scheme is needed, else the Intent can not be started
        url.setScheme(fileScheme);
        QMimeDatabase mimeDb;
        mime = mimeDb.mimeTypeForUrl(url).name();
    }

    using namespace QNativeInterface;
    QJniObject urlString = QJniObject::fromString(url.toString());
    QJniObject mimeString = QJniObject::fromString(mime);
    return QJniObject::callStaticMethod<jboolean>(
            QtAndroid::applicationClass(), "openURL",
            "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)Z",
            QAndroidApplication::context(), urlString.object(), mimeString.object());
}

bool QAndroidPlatformServices::openDocument(const QUrl &url)
{
    return openUrl(url);
}

QByteArray QAndroidPlatformServices::desktopEnvironment() const
{
    return QByteArray("Android");
}

bool QAndroidPlatformServices::handleNewIntent(JNIEnv *env, jobject intent)
{
    Q_UNUSED(env);

    const QJniObject jniIntent(intent);

    const QString action = jniIntent.callObjectMethod<jstring>("getAction").toString();
    if (action != m_actionView)
        return false;

    const QString url = jniIntent.callObjectMethod<jstring>("getDataString").toString();
    QScopedValueRollback<QUrl> rollback(m_handlingUrl, url);
    return QDesktopServices::openUrl(url);
}

QT_END_NAMESPACE
