/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtCore module of the Qt Toolkit.
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

#ifndef QABSTRACTPROXYMODEL_P_H
#define QABSTRACTPROXYMODEL_P_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists for the convenience
// of QAbstractItemModel*.  This header file may change from version
// to version without notice, or even be removed.
//
// We mean it.
//
//

#include "private/qabstractitemmodel_p.h"
#include "private/qproperty_p.h"

QT_REQUIRE_CONFIG(proxymodel);

QT_BEGIN_NAMESPACE

class Q_CORE_EXPORT QAbstractProxyModelPrivate : public QAbstractItemModelPrivate
{
    Q_DECLARE_PUBLIC(QAbstractProxyModel)
public:
    QAbstractProxyModelPrivate()
        : QAbstractItemModelPrivate(),
        sourceHadZeroRows(false),
        sourceHadZeroColumns(false)
    {}
    void setModelForwarder(QAbstractItemModel *sourceModel)
    {
        q_func()->setSourceModel(sourceModel);
    }
    void modelChangedForwarder()
    {
        Q_EMIT q_func()->sourceModelChanged(QAbstractProxyModel::QPrivateSignal());
    }
    QAbstractItemModel *getModelForwarder() const { return q_func()->sourceModel(); }

    Q_OBJECT_COMPAT_PROPERTY_WITH_ARGS(QAbstractProxyModelPrivate, QAbstractItemModel *, model,
                                       &QAbstractProxyModelPrivate::setModelForwarder,
                                       &QAbstractProxyModelPrivate::modelChangedForwarder,
                                       &QAbstractProxyModelPrivate::getModelForwarder, nullptr)
    virtual void _q_sourceModelDestroyed();
    void _q_sourceModelRowsAboutToBeInserted(const QModelIndex &parent, int first, int last);
    void _q_sourceModelRowsInserted(const QModelIndex &parent, int first, int last);
    void _q_sourceModelRowsRemoved(const QModelIndex &parent, int first, int last);
    void _q_sourceModelColumnsAboutToBeInserted(const QModelIndex &parent, int first, int last);
    void _q_sourceModelColumnsInserted(const QModelIndex &parent, int first, int last);
    void _q_sourceModelColumnsRemoved(const QModelIndex &parent, int first, int last);

    void mapDropCoordinatesToSource(int row, int column, const QModelIndex &parent,
                                    int *source_row, int *source_column, QModelIndex *source_parent) const;

    unsigned int sourceHadZeroRows : 1;
    unsigned int sourceHadZeroColumns : 1;
};

QT_END_NAMESPACE

#endif // QABSTRACTPROXYMODEL_P_H
