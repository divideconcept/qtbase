/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Copyright (C) 2016 Intel Corporation.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtDBus module of the Qt Toolkit.
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

#include <QtCore/qglobal.h>
#if QT_CONFIG(library)
#include <QtCore/qlibrary.h>
#include <QtCore/private/qlocking_p.h>
#endif
#include <QtCore/qmutex.h>

#ifndef QT_NO_DBUS

extern "C" void dbus_shutdown();

QT_BEGIN_NAMESPACE

using namespace Qt::StringLiterals;

void (*qdbus_resolve_me(const char *name))();

#if !defined QT_LINKED_LIBDBUS

#if QT_CONFIG(library)
Q_CONSTINIT static QLibrary *qdbus_libdbus = nullptr;

void qdbus_unloadLibDBus()
{
    if (qdbus_libdbus) {
        if (qEnvironmentVariableIsSet("QDBUS_FORCE_SHUTDOWN"))
            qdbus_libdbus->resolve("dbus_shutdown")();
        qdbus_libdbus->unload();
    }
    delete qdbus_libdbus;
    qdbus_libdbus = nullptr;
}
#endif

bool qdbus_loadLibDBus()
{
#if QT_CONFIG(library)
#ifdef QT_BUILD_INTERNAL
    // this is to simulate a library load failure for our autotest suite.
    if (!qEnvironmentVariableIsEmpty("QT_SIMULATE_DBUS_LIBFAIL"))
        return false;
#endif

    Q_CONSTINIT static bool triedToLoadLibrary = false;
    Q_CONSTINIT static QBasicMutex mutex;
    const auto locker = qt_scoped_lock(mutex);

    QLibrary *&lib = qdbus_libdbus;
    if (triedToLoadLibrary)
        return lib && lib->isLoaded();

    lib = new QLibrary;
    lib->setLoadHints(QLibrary::ExportExternalSymbolsHint); // make libdbus symbols available for apps that need more advanced control over the dbus
    triedToLoadLibrary = true;

    static int majorversions[] = { 3, 2, -1 };
    const QString baseNames[] = {
#ifdef Q_OS_WIN
        "dbus-1"_L1,
#endif
        "libdbus-1"_L1
    };

    lib->unload();
    for (const int majorversion : majorversions) {
        for (const QString &baseName : baseNames) {
#ifdef Q_OS_WIN
            QString suffix;
            if (majorversion != -1)
                suffix = QString::number(- majorversion); // negative so it prepends the dash
            lib->setFileName(baseName + suffix);
#else
            lib->setFileNameAndVersion(baseName, majorversion);
#endif
            if (lib->load() && lib->resolve("dbus_connection_open_private"))
                return true;

            lib->unload();
        }
    }

    delete lib;
    lib = nullptr;
    return false;
#else
    return true;
#endif
}

void (*qdbus_resolve_conditionally(const char *name))()
{
#if QT_CONFIG(library)
    if (qdbus_loadLibDBus())
        return qdbus_libdbus->resolve(name);
#else
    Q_UNUSED(name);
#endif
    return nullptr;
}

void (*qdbus_resolve_me(const char *name))()
{
#if QT_CONFIG(library)
    if (Q_UNLIKELY(!qdbus_loadLibDBus()))
        qFatal("Cannot find libdbus-1 in your system to resolve symbol '%s'.", name);

    QFunctionPointer ptr = qdbus_libdbus->resolve(name);
    if (Q_UNLIKELY(!ptr))
        qFatal("Cannot resolve '%s' in your libdbus-1.", name);

    return ptr;
#else
    Q_UNUSED(name);
    return nullptr;
#endif
}

#else
static void qdbus_unloadLibDBus()
{
    if (qEnvironmentVariableIsSet("QDBUS_FORCE_SHUTDOWN"))
        dbus_shutdown();
}

#endif // !QT_LINKED_LIBDBUS

#if defined(QT_LINKED_LIBDBUS) || QT_CONFIG(library)
Q_DESTRUCTOR_FUNCTION(qdbus_unloadLibDBus)
#endif

QT_END_NAMESPACE

#endif // QT_NO_DBUS
