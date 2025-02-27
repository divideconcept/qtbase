/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the tools applications of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:GPL-EXCEPT$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#ifndef OPTION_H
#define OPTION_H

#include <qstring.h>
#include <qdir.h>

QT_BEGIN_NAMESPACE

struct Option
{
    unsigned int headerProtection : 1;
    unsigned int copyrightHeader : 1;
    unsigned int generateImplemetation : 1;
    unsigned int generateNamespace : 1;
    unsigned int autoConnection : 1;
    unsigned int dependencies : 1;
    unsigned int limitXPM_LineLength : 1;
    unsigned int implicitIncludes: 1;
    unsigned int idBased: 1;
    unsigned int fromImports: 1;
    unsigned int forceMemberFnPtrConnectionSyntax: 1;
    unsigned int forceStringConnectionSyntax: 1;
    unsigned int useStarImports: 1;

    QString inputFile;
    QString outputFile;
    QString qrcOutputFile;
    QString indent;
    QString prefix;
    QString postfix;
    QString translateFunction;
    QString includeFile;

    Option()
        : headerProtection(1),
          copyrightHeader(1),
          generateImplemetation(0),
          generateNamespace(1),
          autoConnection(1),
          dependencies(0),
          limitXPM_LineLength(0),
          implicitIncludes(1),
          idBased(0),
          fromImports(0),
          forceMemberFnPtrConnectionSyntax(0),
          forceStringConnectionSyntax(0),
          useStarImports(0),
          prefix(QLatin1StringView("Ui_"))
    { indent.fill(u' ', 4); }

    QString messagePrefix() const
    {
        return inputFile.isEmpty() ?
               QString(QLatin1StringView("stdin")) :
               QDir::toNativeSeparators(inputFile);
    }
};

QT_END_NAMESPACE

#endif // OPTION_H
