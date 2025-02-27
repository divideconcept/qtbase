/****************************************************************************
**
** Copyright (C) 2018 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the plugins of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:GPL$
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
** General Public License version 3 or (at your option) any later version
** approved by the KDE Free Qt Foundation. The licenses are as published by
** the Free Software Foundation and appearing in the file LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qwasmintegration.h"
#include "qwasmeventtranslator.h"
#include "qwasmeventdispatcher.h"
#include "qwasmcompositor.h"
#include "qwasmopenglcontext.h"
#include "qwasmtheme.h"
#include "qwasmclipboard.h"
#include "qwasmservices.h"
#include "qwasmoffscreensurface.h"
#include "qwasmstring.h"

#include "qwasmwindow.h"
#ifndef QT_NO_OPENGL
# include "qwasmbackingstore.h"
#endif
#include "qwasmfontdatabase.h"
#if defined(Q_OS_UNIX)
#include <QtGui/private/qgenericunixeventdispatcher_p.h>
#endif
#include <qpa/qplatformwindow.h>
#include <QtGui/qscreen.h>
#include <qpa/qwindowsysteminterface.h>
#include <QtCore/qcoreapplication.h>
#include <qpa/qplatforminputcontextfactory_p.h>

#include <emscripten/bind.h>
#include <emscripten/val.h>

// this is where EGL headers are pulled in, make sure it is last
#include "qwasmscreen.h"

using namespace emscripten;
QT_BEGIN_NAMESPACE

static void addContainerElement(emscripten::val element)
{
    QWasmIntegration::get()->addScreen(element);
}

static void removeContainerElement(emscripten::val element)
{
    QWasmIntegration::get()->removeScreen(element);
}

static void resizeContainerElement(emscripten::val element)
{
    QWasmIntegration::get()->resizeScreen(element);
}

static void qtUpdateDpi()
{
    QWasmIntegration::get()->updateDpi();
}

static void resizeAllScreens(emscripten::val event)
{
    Q_UNUSED(event);
    QWasmIntegration::get()->resizeAllScreens();
}

EMSCRIPTEN_BINDINGS(qtQWasmIntegraton)
{
    function("qtAddContainerElement", &addContainerElement);
    function("qtRemoveContainerElement", &removeContainerElement);
    function("qtResizeContainerElement", &resizeContainerElement);
    function("qtUpdateDpi", &qtUpdateDpi);
    function("qtResizeAllScreens", &resizeAllScreens);
}

QWasmIntegration *QWasmIntegration::s_instance;

QWasmIntegration::QWasmIntegration()
    : m_fontDb(nullptr),
      m_desktopServices(nullptr),
      m_clipboard(new QWasmClipboard)
{
    s_instance = this;

   touchPoints = emscripten::val::global("navigator")["maxTouchPoints"].as<int>();
    // The Platform Detect: expand coverage as needed
    platform = GenericPlatform;
    emscripten::val rawPlatform = emscripten::val::global("navigator")["platform"];

    if (rawPlatform.call<bool>("includes", emscripten::val("Mac")))
        platform = MacOSPlatform;
    if (rawPlatform.call<bool>("includes", emscripten::val("iPhone")))
        platform = iPhonePlatform;
    if (rawPlatform.call<bool>("includes", emscripten::val("Win32")))
        platform = WindowsPlatform;
    if (rawPlatform.call<bool>("includes", emscripten::val("Linux"))) {
        platform = LinuxPlatform;
        emscripten::val uAgent = emscripten::val::global("navigator")["userAgent"];
        if (uAgent.call<bool>("includes", emscripten::val("Android")))
            platform = AndroidPlatform;
    }

    // Create screens for container elements. Each container element can be a div element (preferred),
    // or a canvas element (legacy). Qt versions prior to 6.x read the "qtCanvasElements" module property,
    // which we continue to do to preserve compatibility. The preferred property is now "qtContainerElements".
    emscripten::val qtContainerElements = val::module_property("qtContainerElements");
    emscripten::val qtCanvasElements = val::module_property("qtCanvasElements");
    if (!qtContainerElements.isUndefined()) {
        emscripten::val length = qtContainerElements["length"];
        int count = length.as<int>();
        if (length.isUndefined())
            qWarning("qtContainerElements does not have the length property set. Qt expects an array of html elements (possibly containing one element only)");
        for (int i = 0; i < count; ++i) {
            emscripten::val element = qtContainerElements[i].as<emscripten::val>();
            if (element.isNull() ||element.isUndefined()) {
                 qWarning() << "Skipping null or undefined element in qtContainerElements";
            } else {
                addScreen(element);
            }
        }
    } else if (!qtCanvasElements.isUndefined()) {
        qWarning() << "The qtCanvaseElements property is deprecated. Qt will stop reading"
                   << "it in some future veresion, please use qtContainerElements instead";
        emscripten::val length = qtCanvasElements["length"];
        int count = length.as<int>();
        for (int i = 0; i < count; ++i)
            addScreen(qtCanvasElements[i].as<emscripten::val>());
    } else {
        // No screens, which may or may not be intended
        qWarning() << "Note: The qtContainerElements module property was not set. Proceeding with no screens.";
    }

    // install browser window resize handler
    auto onWindowResize = [](int eventType, const EmscriptenUiEvent *e, void *userData) -> int {
        Q_UNUSED(eventType);
        Q_UNUSED(e);
        Q_UNUSED(userData);

        // This resize event is called when the HTML window is resized. Depending
        // on the page layout the canvas(es) might also have been resized, so we
        // update the Qt screen sizes (and canvas render sizes).
        if (QWasmIntegration *integration = QWasmIntegration::get())
            integration->resizeAllScreens();
        return 0;
    };
    emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, onWindowResize);

    // install visualViewport resize handler which picks up size and scale change on mobile.
    emscripten::val visualViewport = emscripten::val::global("window")["visualViewport"];
    if (!visualViewport.isUndefined()) {
        visualViewport.call<void>("addEventListener", val("resize"),
                          val::module_property("qtResizeAllScreens"));
    }
}

QWasmIntegration::~QWasmIntegration()
{
    // Remove event listener
    emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, nullptr);
    emscripten::val visualViewport = emscripten::val::global("window")["visualViewport"];
    if (!visualViewport.isUndefined()) {
        visualViewport.call<void>("removeEventListener", val("resize"),
                          val::module_property("qtResizeAllScreens"));
    }

    delete m_fontDb;
    delete m_desktopServices;
    if (m_platformInputContext)
        delete m_platformInputContext;

    for (const auto &elementAndScreen : m_screens)
        QWindowSystemInterface::handleScreenRemoved(elementAndScreen.second);
    m_screens.clear();

    s_instance = nullptr;
}

bool QWasmIntegration::hasCapability(QPlatformIntegration::Capability cap) const
{
    switch (cap) {
    case ThreadedPixmaps: return true;
    case OpenGL: return true;
    case ThreadedOpenGL: return false;
    case RasterGLSurface: return false; // to enable this you need to fix qopenglwidget and quickwidget for wasm
    case MultipleWindows: return true;
    case WindowManagement: return true;
    case OpenGLOnRasterSurface: return true;
    default: return QPlatformIntegration::hasCapability(cap);
    }
}

QPlatformWindow *QWasmIntegration::createPlatformWindow(QWindow *window) const
{
    QWasmCompositor *compositor = QWasmScreen::get(window->screen())->compositor();
    return new QWasmWindow(window, compositor, m_backingStores.value(window));
}

QPlatformBackingStore *QWasmIntegration::createPlatformBackingStore(QWindow *window) const
{
#ifndef QT_NO_OPENGL
    QWasmCompositor *compositor = QWasmScreen::get(window->screen())->compositor();
    QWasmBackingStore *backingStore = new QWasmBackingStore(compositor, window);
    m_backingStores.insert(window, backingStore);
    return backingStore;
#else
    return nullptr;
#endif
}

void QWasmIntegration::removeBackingStore(QWindow* window)
{
    m_backingStores.remove(window);
}

#ifndef QT_NO_OPENGL
QPlatformOpenGLContext *QWasmIntegration::createPlatformOpenGLContext(QOpenGLContext *context) const
{
    return new QWasmOpenGLContext(context->format());
}
#endif

void QWasmIntegration::initialize()
{
    if (touchPoints < 1) // only touchscreen need inputcontexts
        return;

    QString icStr = QPlatformInputContextFactory::requested();
    if (!icStr.isNull())
        m_inputContext.reset(QPlatformInputContextFactory::create(icStr));
    else
        m_inputContext.reset(new QWasmInputContext());
}

QPlatformInputContext *QWasmIntegration::inputContext() const
{
    return m_inputContext.data();
}

QPlatformOffscreenSurface *QWasmIntegration::createPlatformOffscreenSurface(QOffscreenSurface *surface) const
{
    return new QWasmOffscrenSurface(surface);
}

QPlatformFontDatabase *QWasmIntegration::fontDatabase() const
{
    if (m_fontDb == nullptr)
        m_fontDb = new QWasmFontDatabase;

    return m_fontDb;
}

QAbstractEventDispatcher *QWasmIntegration::createEventDispatcher() const
{
    return new QWasmEventDispatcher;
}

QVariant QWasmIntegration::styleHint(QPlatformIntegration::StyleHint hint) const
{
    if (hint == ShowIsFullScreen)
        return true;

    return QPlatformIntegration::styleHint(hint);
}

Qt::WindowState QWasmIntegration::defaultWindowState(Qt::WindowFlags flags) const
{
    // Don't maximize dialogs or popups
    if (flags.testFlag(Qt::Dialog) || flags.testFlag(Qt::Popup))
        return Qt::WindowNoState;

    return QPlatformIntegration::defaultWindowState(flags);
}

QStringList QWasmIntegration::themeNames() const
{
    return QStringList() << QLatin1String("webassembly");
}

QPlatformTheme *QWasmIntegration::createPlatformTheme(const QString &name) const
{
    if (name == QLatin1String("webassembly"))
        return new QWasmTheme;
    return QPlatformIntegration::createPlatformTheme(name);
}

QPlatformServices *QWasmIntegration::services() const
{
    if (m_desktopServices == nullptr)
        m_desktopServices = new QWasmServices();
    return m_desktopServices;
}

QPlatformClipboard* QWasmIntegration::clipboard() const
{
    return m_clipboard;
}

void QWasmIntegration::addScreen(const emscripten::val &element)
{
    QWasmScreen *screen = new QWasmScreen(element);
    m_screens.append(qMakePair(element, screen));
    m_clipboard->installEventHandlers(element);
    QWindowSystemInterface::handleScreenAdded(screen);
}

void QWasmIntegration::removeScreen(const emscripten::val &element)
{
    auto it = std::find_if(m_screens.begin(), m_screens.end(),
        [&] (const QPair<emscripten::val, QWasmScreen *> &candidate) { return candidate.first.equals(element); });
    if (it == m_screens.end()) {
        qWarning() << "Attempting to remove non-existing screen for element" << QWasmString::toQString(element["id"]);;
        return;
    }
    QWasmScreen *exScreen = it->second;
    m_screens.erase(it);
    exScreen->destroy(); // clean up before deleting the screen
    QWindowSystemInterface::handleScreenRemoved(exScreen);
}

void QWasmIntegration::resizeScreen(const emscripten::val &element)
{
    auto it = std::find_if(m_screens.begin(), m_screens.end(),
        [&] (const QPair<emscripten::val, QWasmScreen *> &candidate) { return candidate.first.equals(element); });
    if (it == m_screens.end()) {
        qWarning() << "Attempting to resize non-existing screen for element" << QWasmString::toQString(element["id"]);;
        return;
    }
    it->second->updateQScreenAndCanvasRenderSize();
}

void QWasmIntegration::updateDpi()
{
    emscripten::val dpi = emscripten::val::module_property("qtFontDpi");
    if (dpi.isUndefined())
        return;
    qreal dpiValue = dpi.as<qreal>();
    for (const auto &elementAndScreen : m_screens)
        QWindowSystemInterface::handleScreenLogicalDotsPerInchChange(elementAndScreen.second->screen(), dpiValue, dpiValue);
}

void QWasmIntegration::resizeAllScreens()
{
    for (const auto &elementAndScreen : m_screens)
        elementAndScreen.second->updateQScreenAndCanvasRenderSize();
}

quint64 QWasmIntegration::getTimestamp()
{
    return emscripten_performance_now();
}

QT_END_NAMESPACE
