/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtSql module of the Qt Toolkit.
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

#ifndef QSQLQUERYMODEL_P_H
#define QSQLQUERYMODEL_P_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists for the convenience
// of qsql*model.h .  This header file may change from version to version
// without notice, or even be removed.
//
// We mean it.
//

#include <QtSql/private/qtsqlglobal_p.h>
#include "private/qabstractitemmodel_p.h"
#include "QtSql/qsqlerror.h"
#include "QtSql/qsqlquery.h"
#include "QtSql/qsqlrecord.h"
#include "QtCore/qhash.h"
#include "QtCore/qlist.h"
#include "QtCore/qvarlengtharray.h"

QT_REQUIRE_CONFIG(sqlmodel);

QT_BEGIN_NAMESPACE

class QSqlQueryModelPrivate: public QAbstractItemModelPrivate
{
    Q_DECLARE_PUBLIC(QSqlQueryModel)
public:
    QSqlQueryModelPrivate() : atEnd(false), nestedResetLevel(0) {}
    ~QSqlQueryModelPrivate();

    void prefetch(int);
    void initColOffsets(int size);
    int columnInQuery(int modelColumn) const;

    mutable QSqlQuery query = { QSqlQuery(nullptr) };
    mutable QSqlError error;
    QModelIndex bottom;
    QSqlRecord rec;
    uint atEnd : 1;
    QList<QHash<int, QVariant>> headers;
    QVarLengthArray<int, 56> colOffsets; // used to calculate indexInQuery of columns
    int nestedResetLevel;
};

// helpers for building SQL expressions
class QSqlQueryModelSql
{
public:
    // SQL keywords
    inline const static QLatin1StringView as() { return QLatin1StringView("AS"); }
    inline const static QLatin1StringView asc() { return QLatin1StringView("ASC"); }
    inline const static QLatin1StringView comma() { return QLatin1StringView(","); }
    inline const static QLatin1StringView desc() { return QLatin1StringView("DESC"); }
    inline const static QLatin1StringView eq() { return QLatin1StringView("="); }
    // "and" is a C++ keyword
    inline const static QLatin1StringView et() { return QLatin1StringView("AND"); }
    inline const static QLatin1StringView from() { return QLatin1StringView("FROM"); }
    inline const static QLatin1StringView leftJoin() { return QLatin1StringView("LEFT JOIN"); }
    inline const static QLatin1StringView on() { return QLatin1StringView("ON"); }
    inline const static QLatin1StringView orderBy() { return QLatin1StringView("ORDER BY"); }
    inline const static QLatin1StringView parenClose() { return QLatin1StringView(")"); }
    inline const static QLatin1StringView parenOpen() { return QLatin1StringView("("); }
    inline const static QLatin1StringView select() { return QLatin1StringView("SELECT"); }
    inline const static QLatin1StringView sp() { return QLatin1StringView(" "); }
    inline const static QLatin1StringView where() { return QLatin1StringView("WHERE"); }

    // Build expressions based on key words
    inline const static QString as(const QString &a, const QString &b) { return b.isEmpty() ? a : concat(concat(a, as()), b); }
    inline const static QString asc(const QString &s) { return concat(s, asc()); }
    inline const static QString comma(const QString &a, const QString &b) { return a.isEmpty() ? b : b.isEmpty() ? a : QString(a).append(comma()).append(b); }
    inline const static QString concat(const QString &a, const QString &b) { return a.isEmpty() ? b : b.isEmpty() ? a : QString(a).append(sp()).append(b); }
    inline const static QString desc(const QString &s) { return concat(s, desc()); }
    inline const static QString eq(const QString &a, const QString &b) { return QString(a).append(eq()).append(b); }
    inline const static QString et(const QString &a, const QString &b) { return a.isEmpty() ? b : b.isEmpty() ? a : concat(concat(a, et()), b); }
    inline const static QString from(const QString &s) { return concat(from(), s); }
    inline const static QString leftJoin(const QString &s) { return concat(leftJoin(), s); }
    inline const static QString on(const QString &s) { return concat(on(), s); }
    inline const static QString orderBy(const QString &s) { return s.isEmpty() ? s : concat(orderBy(), s); }
    inline const static QString paren(const QString &s) { return s.isEmpty() ? s : parenOpen() + s + parenClose(); }
    inline const static QString select(const QString &s) { return concat(select(), s); }
    inline const static QString where(const QString &s) { return s.isEmpty() ? s : concat(where(), s); }
};

QT_END_NAMESPACE

#endif // QSQLQUERYMODEL_P_H
