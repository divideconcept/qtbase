/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the test suite of the Qt Toolkit.
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


#include <QTest>

#include "qpalette.h"

class tst_QPalette : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void roleValues_data();
    void roleValues();
    void resolve();
    void copySemantics();
    void moveSemantics();
    void setBrush();

    void isBrushSet();
    void setAllPossibleBrushes();
    void noBrushesSetForDefaultPalette();
    void cannotCheckIfInvalidBrushSet();
    void checkIfBrushForCurrentGroupSet();
};

void tst_QPalette::roleValues_data()
{
    QTest::addColumn<int>("role");
    QTest::addColumn<int>("value");

    QTest::newRow("QPalette::WindowText") << int(QPalette::WindowText) << 0;
    QTest::newRow("QPalette::Button") << int(QPalette::Button) << 1;
    QTest::newRow("QPalette::Light") << int(QPalette::Light) << 2;
    QTest::newRow("QPalette::Midlight") << int(QPalette::Midlight) << 3;
    QTest::newRow("QPalette::Dark") << int(QPalette::Dark) << 4;
    QTest::newRow("QPalette::Mid") << int(QPalette::Mid) << 5;
    QTest::newRow("QPalette::Text") << int(QPalette::Text) << 6;
    QTest::newRow("QPalette::BrightText") << int(QPalette::BrightText) << 7;
    QTest::newRow("QPalette::ButtonText") << int(QPalette::ButtonText) << 8;
    QTest::newRow("QPalette::Base") << int(QPalette::Base) << 9;
    QTest::newRow("QPalette::Window") << int(QPalette::Window) << 10;
    QTest::newRow("QPalette::Shadow") << int(QPalette::Shadow) << 11;
    QTest::newRow("QPalette::Highlight") << int(QPalette::Highlight) << 12;
    QTest::newRow("QPalette::HighlightedText") << int(QPalette::HighlightedText) << 13;
    QTest::newRow("QPalette::Link") << int(QPalette::Link) << 14;
    QTest::newRow("QPalette::LinkVisited") << int(QPalette::LinkVisited) << 15;
    QTest::newRow("QPalette::AlternateBase") << int(QPalette::AlternateBase) << 16;
    QTest::newRow("QPalette::NoRole") << int(QPalette::NoRole) << 17;
    QTest::newRow("QPalette::ToolTipBase") << int(QPalette::ToolTipBase) << 18;
    QTest::newRow("QPalette::ToolTipText") << int(QPalette::ToolTipText) << 19;
    QTest::newRow("QPalette::PlaceholderText") << int(QPalette::PlaceholderText) << 20;

    // Change this value as you add more roles.
    QTest::newRow("QPalette::NColorRoles") << int(QPalette::NColorRoles) << 21;
}

void tst_QPalette::roleValues()
{
    QFETCH(int, role);
    QFETCH(int, value);
    QCOMPARE(role, value);
}

void tst_QPalette::resolve()
{
    QPalette p1;
    p1.setBrush(QPalette::WindowText, Qt::green);
    p1.setBrush(QPalette::Button, Qt::green);

    QVERIFY(p1.isBrushSet(QPalette::Active, QPalette::WindowText));
    QVERIFY(p1.isBrushSet(QPalette::Active, QPalette::Button));

    QPalette p2;
    p2.setBrush(QPalette::WindowText, Qt::red);

    QVERIFY(p2.isBrushSet(QPalette::Active, QPalette::WindowText));
    QVERIFY(!p2.isBrushSet(QPalette::Active, QPalette::Button));

    QPalette p1ResolvedTo2 = p1.resolve(p2);
    // p1ResolvedTo2 gets everything from p1 and nothing copied from p2 because
    // it already has a WindowText. That is two brushes, and to the same value
    // as p1.
    QCOMPARE(p1ResolvedTo2, p1);
    QVERIFY(p1ResolvedTo2.isBrushSet(QPalette::Active, QPalette::WindowText));
    QCOMPARE(p1.windowText(), p1ResolvedTo2.windowText());
    QVERIFY(p1ResolvedTo2.isBrushSet(QPalette::Active, QPalette::Button));
    QCOMPARE(p1.button(), p1ResolvedTo2.button());

    QPalette p2ResolvedTo1 = p2.resolve(p1);
    // p2ResolvedTo1 gets the WindowText set, and to the same value as the
    // original p2, however, Button gets set from p1.
    QVERIFY(p2ResolvedTo1.isBrushSet(QPalette::Active, QPalette::WindowText));
    QCOMPARE(p2.windowText(), p2ResolvedTo1.windowText());
    QVERIFY(p2ResolvedTo1.isBrushSet(QPalette::Active, QPalette::Button));
    QCOMPARE(p1.button(), p2ResolvedTo1.button());

    QVERIFY(p2ResolvedTo1 != p1);
    QVERIFY(p2ResolvedTo1 != p2);

    QPalette p3;
    // ensure the resolve mask is full
    for (int r = 0; r < QPalette::NColorRoles; ++r)
        p3.setBrush(QPalette::All, QPalette::ColorRole(r), Qt::red);

    QPalette p3ResolvedToP1 = p3.resolve(p1);
    QVERIFY(p3ResolvedToP1.isCopyOf(p3));
}


static void compareAllPaletteData(const QPalette &firstPalette, const QPalette &secondPalette)
{
    QCOMPARE(firstPalette, secondPalette);

    // For historical reasons, operator== compares only brushes, but it's not enough for proper
    // comparison after move/copy, because some additional data can also be moved/copied.
    // Let's compare this data here.
    QCOMPARE(firstPalette.resolveMask(), secondPalette.resolveMask());
    QCOMPARE(firstPalette.currentColorGroup(), secondPalette.currentColorGroup());
}

void tst_QPalette::copySemantics()
{
    QPalette src(Qt::red), dst;
    const QPalette control = src; // copy construction
    QVERIFY(src != dst);
    QVERIFY(!src.isCopyOf(dst));
    compareAllPaletteData(src, control);
    QVERIFY(src.isCopyOf(control));
    dst = src; // copy assignment
    compareAllPaletteData(dst, src);
    compareAllPaletteData(dst, control);
    QVERIFY(dst.isCopyOf(src));

    dst = QPalette(Qt::green);
    QVERIFY(dst != src);
    QVERIFY(dst != control);
    compareAllPaletteData(src, control);
    QVERIFY(!dst.isCopyOf(src));
    QVERIFY(src.isCopyOf(control));
}

void tst_QPalette::moveSemantics()
{
    QPalette src(Qt::red), dst;
    const QPalette control = src;
    QVERIFY(src != dst);
    compareAllPaletteData(src, control);
    QVERIFY(!dst.isCopyOf(src));
    QVERIFY(!dst.isCopyOf(control));
    dst = std::move(src); // move assignment
    QVERIFY(!dst.isCopyOf(src)); // isCopyOf() works on moved-from palettes, too
    QVERIFY(dst.isCopyOf(control));
    compareAllPaletteData(dst, control);
    src = control; // check moved-from 'src' can still be assigned to (doesn't crash)
    QVERIFY(src.isCopyOf(dst));
    QVERIFY(src.isCopyOf(control));
    QPalette dst2(std::move(src)); // move construction
    QVERIFY(!src.isCopyOf(dst));
    QVERIFY(!src.isCopyOf(dst2));
    QVERIFY(!src.isCopyOf(control));
    compareAllPaletteData(dst2, control);
    QVERIFY(dst2.isCopyOf(dst));
    QVERIFY(dst2.isCopyOf(control));
    // check moved-from 'src' can still be destroyed (doesn't crash)
}

void tst_QPalette::setBrush()
{
    QPalette p(Qt::red);
    const QPalette q = p;
    QVERIFY(q.isCopyOf(p));

    // Setting a different brush will detach
    p.setBrush(QPalette::Disabled, QPalette::Button, Qt::green);
    QVERIFY(!q.isCopyOf(p));
    QVERIFY(q != p);

    // Check we only changed what we said we would
    for (int i = 0; i < QPalette::NColorGroups; i++)
        for (int j = 0; j < QPalette::NColorRoles; j++) {
            const auto g = QPalette::ColorGroup(i);
            const auto r = QPalette::ColorRole(j);
            const auto b = p.brush(g, r);
            if (g == QPalette::Disabled && r == QPalette::Button)
                QCOMPARE(b, QBrush(Qt::green));
            else
                QCOMPARE(b, q.brush(g, r));
        }

    const QPalette pp = p;
    QVERIFY(pp.isCopyOf(p));
    // Setting the same brush won't detach
    p.setBrush(QPalette::Disabled, QPalette::Button, Qt::green);
    QVERIFY(pp.isCopyOf(p));
}

void tst_QPalette::isBrushSet()
{
    QPalette p;

    // Set only one color group
    p.setBrush(QPalette::Active, QPalette::Mid, QBrush(Qt::red));
    QVERIFY(p.isBrushSet(QPalette::Active, QPalette::Mid));
    QVERIFY(!p.isBrushSet(QPalette::Inactive, QPalette::Mid));
    QVERIFY(!p.isBrushSet(QPalette::Disabled, QPalette::Mid));

    // Set all color groups
    p.setBrush(QPalette::LinkVisited, QBrush(Qt::green));
    QVERIFY(p.isBrushSet(QPalette::Active, QPalette::LinkVisited));
    QVERIFY(p.isBrushSet(QPalette::Inactive, QPalette::LinkVisited));
    QVERIFY(p.isBrushSet(QPalette::Disabled, QPalette::LinkVisited));

    // Don't set flag when brush doesn't change (and also don't detach - QTBUG-98762)
    QPalette p2;
    QPalette p3;
    QVERIFY(!p2.isBrushSet(QPalette::Active, QPalette::Dark));
    p2.setBrush(QPalette::Active, QPalette::Dark, p2.brush(QPalette::Active, QPalette::Dark));
    QVERIFY(!p3.isBrushSet(QPalette::Active, QPalette::Dark));
    QVERIFY(!p2.isBrushSet(QPalette::Active, QPalette::Dark));
}

void tst_QPalette::setAllPossibleBrushes()
{
    QPalette p;

    QCOMPARE(p.resolveMask(), QPalette::ResolveMask(0));

    for (int r = 0; r < QPalette::NColorRoles; ++r) {
        p.setBrush(QPalette::All, QPalette::ColorRole(r), Qt::red);
    }

    for (int r = 0; r < QPalette::NColorRoles; ++r) {
        for (int g = 0; g < QPalette::NColorGroups; ++g) {
            QVERIFY(p.isBrushSet(QPalette::ColorGroup(g), QPalette::ColorRole(r)));
        }
    }
}

void tst_QPalette::noBrushesSetForDefaultPalette()
{
    QCOMPARE(QPalette().resolveMask(), QPalette::ResolveMask(0));
}

void tst_QPalette::cannotCheckIfInvalidBrushSet()
{
    QPalette p(Qt::red);

    QVERIFY(!p.isBrushSet(QPalette::All, QPalette::LinkVisited));
    QVERIFY(!p.isBrushSet(QPalette::Active, QPalette::NColorRoles));
}

void tst_QPalette::checkIfBrushForCurrentGroupSet()
{
    QPalette p;

    p.setCurrentColorGroup(QPalette::Disabled);
    p.setBrush(QPalette::Current, QPalette::Link, QBrush(Qt::yellow));

    QVERIFY(p.isBrushSet(QPalette::Current, QPalette::Link));
}

QTEST_MAIN(tst_QPalette)
#include "tst_qpalette.moc"
