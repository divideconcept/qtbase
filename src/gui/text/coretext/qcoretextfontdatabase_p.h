/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
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

#ifndef QCORETEXTFONTDATABASE_H
#define QCORETEXTFONTDATABASE_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists purely as an
// implementation detail.  This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.
//

#include <qglobal.h>

#include <qpa/qplatformfontdatabase.h>
#include <qpa/qplatformtheme.h>
#include <private/qcore_mac_p.h>

Q_FORWARD_DECLARE_CF_TYPE(CTFontDescriptor);
Q_FORWARD_DECLARE_CF_TYPE(CTFont);

QT_DECL_METATYPE_EXTERN_TAGGED(QCFType<CGFontRef>, QCFType_CGFontRef, Q_GUI_EXPORT)
QT_DECL_METATYPE_EXTERN_TAGGED(QCFType<CFURLRef>, QCFType_CFURLRef, Q_GUI_EXPORT)

QT_BEGIN_NAMESPACE

class Q_GUI_EXPORT QCoreTextFontDatabase : public QPlatformFontDatabase
{
public:
    QCoreTextFontDatabase();
    ~QCoreTextFontDatabase();
    void populateFontDatabase() override;
    bool populateFamilyAliases(const QString &missingFamily) override;
    void populateFamily(const QString &familyName) override;
    void invalidate() override;

    QStringList fallbacksForFamily(const QString &family, QFont::Style style, QFont::StyleHint styleHint, QChar::Script script) const override;
    QStringList addApplicationFont(const QByteArray &fontData, const QString &fileName, QFontDatabasePrivate::ApplicationFont *applicationFont = nullptr) override;
    void releaseHandle(void *handle) override;
    bool isPrivateFontFamily(const QString &family) const override;
    QFont defaultFont() const override;
    bool fontsAlwaysScalable() const override;
    QList<int> standardSizes() const override;

    // For iOS and macOS platform themes
    QFont *themeFont(QPlatformTheme::Font) const;

private:
    void populateThemeFonts();
    void populateFromDescriptor(CTFontDescriptorRef font, const QString &familyName = QString(), QFontDatabasePrivate::ApplicationFont *applicationFont = nullptr);
    static CFArrayRef fallbacksForFamily(const QString &family);

    QHash<QPlatformTheme::Font, QFont *> m_themeFonts;
    QHash<QString, QList<QCFType<CTFontDescriptorRef>>> m_systemFontDescriptors;

    bool m_hasPopulatedAliases;

#if defined(Q_OS_MACOS)
    QMacNotificationObserver m_fontSetObserver;
#endif
};

// Split out into separate template class so that the compiler doesn't have
// to generate code for each override in QCoreTextFontDatabase for each T.

template <class T>
class Q_GUI_EXPORT QCoreTextFontDatabaseEngineFactory : public QCoreTextFontDatabase
{
public:
    QFontEngine *fontEngine(const QFontDef &fontDef, void *handle) override;
    QFontEngine *fontEngine(const QByteArray &fontData, qreal pixelSize, QFont::HintingPreference hintingPreference) override;
};

QT_END_NAMESPACE

#endif // QCORETEXTFONTDATABASE_H
