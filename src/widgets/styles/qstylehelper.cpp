/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtWidgets module of the Qt Toolkit.
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

#include <qstyleoption.h>
#include <qpainter.h>
#include <qpixmapcache.h>
#include <private/qhighdpiscaling_p.h>
#include <private/qguiapplication_p.h>
#include <private/qmath_p.h>
#include <private/qstyle_p.h>
#include <qmath.h>
#if QT_CONFIG(scrollbar)
#include <qscrollbar.h>
#endif
#include <qabstractscrollarea.h>
#include <qwindow.h>

#include <qmetaobject.h>
#include "qstylehelper_p.h"
#include <qstringbuilder.h>

QT_BEGIN_NAMESPACE

Q_GUI_EXPORT int qt_defaultDpiX();

namespace QStyleHelper {

QString uniqueName(const QString &key, const QStyleOption *option, const QSize &size)
{
    const QStyleOptionComplex *complexOption = qstyleoption_cast<const QStyleOptionComplex *>(option);
    QString tmp = key % HexString<uint>(option->state)
                      % HexString<uint>(option->direction)
                      % HexString<uint>(complexOption ? uint(complexOption->activeSubControls) : 0u)
                      % HexString<quint64>(option->palette.cacheKey())
                      % HexString<uint>(size.width())
                      % HexString<uint>(size.height());

#if QT_CONFIG(spinbox)
    if (const QStyleOptionSpinBox *spinBox = qstyleoption_cast<const QStyleOptionSpinBox *>(option)) {
        tmp = tmp % HexString<uint>(spinBox->buttonSymbols)
                  % HexString<uint>(spinBox->stepEnabled)
                  % QChar(spinBox->frame ? u'1' : u'0');
    }
#endif // QT_CONFIG(spinbox)

    return tmp;
}

#ifdef Q_OS_DARWIN
static const qreal qstyleBaseDpi = 72;
#else
static const qreal qstyleBaseDpi = 96;
#endif

Q_WIDGETS_EXPORT qreal dpi(const QStyleOption *option)
{
#ifndef Q_OS_DARWIN
    // Prioritize the application override, except for on macOS where
    // we have historically not supported the AA_Use96Dpi flag.
    if (QCoreApplication::testAttribute(Qt::AA_Use96Dpi))
        return 96;
#endif

    // Expect that QStyleOption::QFontMetrics::QFont has the correct DPI set
    if (option)
        return option->fontMetrics.fontDpi();

    // Fall back to historical Qt behavior: hardocded 72 DPI on mac,
    // primary screen DPI on other platforms.
#ifdef Q_OS_DARWIN
    return qstyleBaseDpi;
#else
    return qt_defaultDpiX();
#endif
}

Q_WIDGETS_EXPORT qreal dpiScaled(qreal value, qreal dpi)
{
    return value * dpi / qstyleBaseDpi;
}

Q_WIDGETS_EXPORT qreal dpiScaled(qreal value, const QPaintDevice *device)
{
    return dpiScaled(value, device->logicalDpiX());
}

Q_WIDGETS_EXPORT qreal dpiScaled(qreal value, const QStyleOption *option)
{
    return dpiScaled(value, dpi(option));
}

#ifndef QT_NO_ACCESSIBILITY
bool isInstanceOf(QObject *obj, QAccessible::Role role)
{
    bool match = false;
    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(obj);
    match = iface && iface->role() == role;
    return match;
}

// Searches for an ancestor of a particular accessible role
bool hasAncestor(QObject *obj, QAccessible::Role role)
{
    bool found = false;
    QObject *parent = obj ? obj->parent() : nullptr;
    while (parent && !found) {
        if (isInstanceOf(parent, role))
            found = true;
        parent = parent->parent();
    }
    return found;
}
#endif // QT_NO_ACCESSIBILITY


#if QT_CONFIG(dial)

int calcBigLineSize(int radius)
{
    int bigLineSize = radius / 6;
    if (bigLineSize < 4)
        bigLineSize = 4;
    if (bigLineSize > radius / 2)
        bigLineSize = radius / 2;
    return bigLineSize;
}

static QPointF calcRadialPos(const QStyleOptionSlider *dial, qreal offset)
{
    const int width = dial->rect.width();
    const int height = dial->rect.height();
    const int r = qMin(width, height) / 2;
    const int currentSliderPosition = dial->upsideDown ? dial->sliderPosition : (dial->maximum - dial->sliderPosition);
    qreal a = 0;
    if (dial->maximum == dial->minimum)
        a = Q_PI / 2;
    else if (dial->dialWrapping)
        a = Q_PI * 3 / 2 - (currentSliderPosition - dial->minimum) * 2 * Q_PI
            / (dial->maximum - dial->minimum);
    else
        a = (Q_PI * 8 - (currentSliderPosition - dial->minimum) * 10 * Q_PI
            / (dial->maximum - dial->minimum)) / 6;
    qreal xc = width / 2.0;
    qreal yc = height / 2.0;
    qreal len = r - QStyleHelper::calcBigLineSize(r) - 3;
    qreal back = offset * len;
    QPointF pos(QPointF(xc + back * qCos(a), yc - back * qSin(a)));
    return pos + dial->rect.topLeft();
}

qreal angle(const QPointF &p1, const QPointF &p2)
{
    static const qreal rad_factor = 180 / Q_PI;
    qreal _angle = 0;

    if (p1.x() == p2.x()) {
        if (p1.y() < p2.y())
            _angle = 270;
        else
            _angle = 90;
    } else  {
        qreal x1, x2, y1, y2;

        if (p1.x() <= p2.x()) {
            x1 = p1.x(); y1 = p1.y();
            x2 = p2.x(); y2 = p2.y();
        } else {
            x2 = p1.x(); y2 = p1.y();
            x1 = p2.x(); y1 = p2.y();
        }

        qreal m = -(y2 - y1) / (x2 - x1);
        _angle = qAtan(m) *  rad_factor;

        if (p1.x() < p2.x())
            _angle = 180 - _angle;
        else
            _angle = -_angle;
    }
    return _angle;
}

QPolygonF calcLines(const QStyleOptionSlider *dial)
{
    QPolygonF poly;
    int width = dial->rect.width();
    int height = dial->rect.height();
    qreal r = qMin(width, height) / 2;
    int bigLineSize = calcBigLineSize(int(r));

    qreal xc = width / 2 + 0.5;
    qreal yc = height / 2 + 0.5;
    const int ns = dial->tickInterval;
    if (!ns) // Invalid values may be set by Qt Designer.
        return poly;
    int notches = (dial->maximum + ns - 1 - dial->minimum) / ns;
    if (notches <= 0)
        return poly;
    if (dial->maximum < dial->minimum || dial->maximum - dial->minimum > 1000) {
        int maximum = dial->minimum + 1000;
        notches = (maximum + ns - 1 - dial->minimum) / ns;
    }

    poly.resize(2 + 2 * notches);
    int smallLineSize = bigLineSize / 2;
    for (int i = 0; i <= notches; ++i) {
        qreal angle = dial->dialWrapping ? Q_PI * 3 / 2 - i * 2 * Q_PI / notches
                  : (Q_PI * 8 - i * 10 * Q_PI / notches) / 6;
        qreal s = qSin(angle);
        qreal c = qCos(angle);
        if (i == 0 || (((ns * i) % (dial->pageStep ? dial->pageStep : 1)) == 0)) {
            poly[2 * i] = QPointF(xc + (r - bigLineSize) * c,
                                  yc - (r - bigLineSize) * s);
            poly[2 * i + 1] = QPointF(xc + r * c, yc - r * s);
        } else {
            poly[2 * i] = QPointF(xc + (r - 1 - smallLineSize) * c,
                                  yc - (r - 1 - smallLineSize) * s);
            poly[2 * i + 1] = QPointF(xc + (r - 1) * c, yc -(r - 1) * s);
        }
    }
    return poly.translated(dial->rect.topLeft());
}

// This will draw a nice and shiny QDial for us. We don't want
// all the shinyness in QWindowsStyle, hence we place it here

void drawDial(const QStyleOptionSlider *option, QPainter *painter)
{
    QPalette pal = option->palette;
    QColor buttonColor = pal.button().color();
    const int width = option->rect.width();
    const int height = option->rect.height();
    const bool enabled = option->state & QStyle::State_Enabled;
    qreal r = qMin(width, height) / 2;
    r -= r/50;
    const qreal penSize = r/20.0;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    // Draw notches
    if (option->subControls & QStyle::SC_DialTickmarks) {
        painter->setPen(option->palette.dark().color().darker(120));
        painter->drawLines(QStyleHelper::calcLines(option));
    }

    // setting color before BEGIN_STYLE_PIXMAPCACHE since
    // otherwise it is not set when the image is in the cache
    buttonColor.setHsv(buttonColor .hue(),
                       qMin(140, buttonColor .saturation()),
                       qMax(180, buttonColor.value()));

    // Cache dial background
    BEGIN_STYLE_PIXMAPCACHE(QString::fromLatin1("qdial"));
    p->setRenderHint(QPainter::Antialiasing);

    const qreal d_ = r / 6;
    const qreal dx = d_ + (width - 2 * r) / 2 + 1;
    const qreal dy = d_ + (height - 2 * r) / 2 + 1;

    QRectF br = QRectF(dx + 0.5, dy + 0.5,
                       int(r * 2 - 2 * d_ - 2),
                       int(r * 2 - 2 * d_ - 2));

    if (enabled) {
        // Drop shadow
        qreal shadowSize = qMax(1.0, penSize/2.0);
        QRectF shadowRect= br.adjusted(-2*shadowSize, -2*shadowSize,
                                       2*shadowSize, 2*shadowSize);
        QRadialGradient shadowGradient(shadowRect.center().x(),
                                       shadowRect.center().y(), shadowRect.width()/2.0,
                                       shadowRect.center().x(), shadowRect.center().y());
        shadowGradient.setColorAt(qreal(0.91), QColor(0, 0, 0, 40));
        shadowGradient.setColorAt(qreal(1.0), Qt::transparent);
        p->setBrush(shadowGradient);
        p->setPen(Qt::NoPen);
        p->translate(shadowSize, shadowSize);
        p->drawEllipse(shadowRect);
        p->translate(-shadowSize, -shadowSize);

        // Main gradient
        QRadialGradient gradient(br.center().x() - br.width()/3, dy,
                                 br.width()*1.3, br.center().x(),
                                 br.center().y() - br.height()/2);
        gradient.setColorAt(0, buttonColor.lighter(110));
        gradient.setColorAt(qreal(0.5), buttonColor);
        gradient.setColorAt(qreal(0.501), buttonColor.darker(102));
        gradient.setColorAt(1, buttonColor.darker(115));
        p->setBrush(gradient);
    } else {
        p->setBrush(Qt::NoBrush);
    }

    p->setPen(QPen(buttonColor.darker(280)));
    p->drawEllipse(br);
    p->setBrush(Qt::NoBrush);
    p->setPen(buttonColor.lighter(110));
    p->drawEllipse(br.adjusted(1, 1, -1, -1));

    if (option->state & QStyle::State_HasFocus) {
        QColor highlight = pal.highlight().color();
        highlight.setHsv(highlight.hue(),
                         qMin(160, highlight.saturation()),
                         qMax(230, highlight.value()));
        highlight.setAlpha(127);
        p->setPen(QPen(highlight, 2.0));
        p->setBrush(Qt::NoBrush);
        p->drawEllipse(br.adjusted(-1, -1, 1, 1));
    }

    END_STYLE_PIXMAPCACHE

    QPointF dp = calcRadialPos(option, qreal(0.70));
    buttonColor = buttonColor.lighter(104);
    buttonColor.setAlphaF(0.8f);
    const qreal ds = r/qreal(7.0);
    QRectF dialRect(dp.x() - ds, dp.y() - ds, 2*ds, 2*ds);
    QRadialGradient dialGradient(dialRect.center().x() + dialRect.width()/2,
                                 dialRect.center().y() + dialRect.width(),
                                 dialRect.width()*2,
                                 dialRect.center().x(), dialRect.center().y());
    dialGradient.setColorAt(1, buttonColor.darker(140));
    dialGradient.setColorAt(qreal(0.4), buttonColor.darker(120));
    dialGradient.setColorAt(0, buttonColor.darker(110));
    if (penSize > 3.0) {
        painter->setPen(QPen(QColor(0, 0, 0, 25), penSize));
        painter->drawLine(calcRadialPos(option, qreal(0.90)), calcRadialPos(option, qreal(0.96)));
    }

    painter->setBrush(dialGradient);
    painter->setPen(QColor(255, 255, 255, 150));
    painter->drawEllipse(dialRect.adjusted(-1, -1, 1, 1));
    painter->setPen(QColor(0, 0, 0, 80));
    painter->drawEllipse(dialRect);
    painter->restore();
}
#endif //QT_CONFIG(dial)

void drawBorderPixmap(const QPixmap &pixmap, QPainter *painter, const QRect &rect,
                     int left, int top, int right,
                     int bottom)
{
    QSize size = pixmap.size();
    //painter->setRenderHint(QPainter::SmoothPixmapTransform);

    //top
    if (top > 0) {
        painter->drawPixmap(QRect(rect.left() + left, rect.top(), rect.width() -right - left, top), pixmap,
                            QRect(left, 0, size.width() -right - left, top));

        //top-left
        if (left > 0)
            painter->drawPixmap(QRect(rect.left(), rect.top(), left, top), pixmap,
                                QRect(0, 0, left, top));

        //top-right
        if (right > 0)
            painter->drawPixmap(QRect(rect.left() + rect.width() - right, rect.top(), right, top), pixmap,
                                QRect(size.width() - right, 0, right, top));
    }

    //left
    if (left > 0)
        painter->drawPixmap(QRect(rect.left(), rect.top()+top, left, rect.height() - top - bottom), pixmap,
                            QRect(0, top, left, size.height() - bottom - top));

    //center
    painter->drawPixmap(QRect(rect.left() + left, rect.top()+top, rect.width() -right - left,
                             rect.height() - bottom - top), pixmap,
                       QRect(left, top, size.width() -right -left,
                             size.height() - bottom - top));
    //right
    if (right > 0)
        painter->drawPixmap(QRect(rect.left() +rect.width() - right, rect.top()+top, right, rect.height() - top - bottom), pixmap,
                            QRect(size.width() - right, top, right, size.height() - bottom - top));

    //bottom
    if (bottom > 0) {
        painter->drawPixmap(QRect(rect.left() +left, rect.top() + rect.height() - bottom,
                                 rect.width() - right - left, bottom), pixmap,
                            QRect(left, size.height() - bottom,
                                 size.width() - right - left, bottom));
        //bottom-left
        if (left > 0)
            painter->drawPixmap(QRect(rect.left(), rect.top() + rect.height() - bottom, left, bottom), pixmap,
                                QRect(0, size.height() - bottom, left, bottom));

        //bottom-right
        if (right > 0)
            painter->drawPixmap(QRect(rect.left() + rect.width() - right, rect.top() + rect.height() - bottom, right, bottom), pixmap,
                                QRect(size.width() - right, size.height() - bottom, right, bottom));

    }
}

QColor backgroundColor(const QPalette &pal, const QWidget* widget)
{
#if QT_CONFIG(scrollarea)
    if (qobject_cast<const QScrollBar *>(widget) && widget->parent() &&
            qobject_cast<const QAbstractScrollArea *>(widget->parent()->parent()))
        return widget->parentWidget()->parentWidget()->palette().color(QPalette::Base);
#else
    Q_UNUSED(widget);
#endif
    return pal.color(QPalette::Base);
}

WidgetSizePolicy widgetSizePolicy(const QWidget *widget, const QStyleOption *opt)
{
    while (widget) {
        if (widget->testAttribute(Qt::WA_MacMiniSize)) {
            return SizeMini;
        } else if (widget->testAttribute(Qt::WA_MacSmallSize)) {
            return SizeSmall;
        } else if (widget->testAttribute(Qt::WA_MacNormalSize)) {
            return SizeLarge;
        }
        widget = widget->parentWidget();
    }

    if (opt && opt->state & QStyle::State_Mini)
        return SizeMini;
    else if (opt && opt->state & QStyle::State_Small)
        return SizeSmall;

    return SizeDefault;
}

}
QT_END_NAMESPACE
