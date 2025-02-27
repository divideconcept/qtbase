/****************************************************************************
**
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

#include "qbackingstoredefaultcompositor_p.h"
#include <QtGui/private/qwindow_p.h>
#include <qpa/qplatformgraphicsbuffer.h>
#include <QtCore/qfile.h>

QT_BEGIN_NAMESPACE

using namespace Qt::StringLiterals;

QBackingStoreDefaultCompositor::~QBackingStoreDefaultCompositor()
{
    reset();
}

void QBackingStoreDefaultCompositor::reset()
{
    delete m_psNoBlend;
    m_psNoBlend = nullptr;
    delete m_psBlend;
    m_psBlend = nullptr;
    delete m_psPremulBlend;
    m_psPremulBlend = nullptr;
    delete m_sampler;
    m_sampler = nullptr;
    delete m_vbuf;
    m_vbuf = nullptr;
    delete m_texture;
    m_texture = nullptr;
    m_widgetQuadData.reset();
    for (PerQuadData &d : m_textureQuadData)
        d.reset();
}

QRhiTexture *QBackingStoreDefaultCompositor::toTexture(const QPlatformBackingStore *backingStore,
                                                       QRhi *rhi,
                                                       QRhiResourceUpdateBatch *resourceUpdates,
                                                       const QRegion &dirtyRegion,
                                                       QPlatformBackingStore::TextureFlags *flags) const
{
    return toTexture(backingStore->toImage(), rhi, resourceUpdates, dirtyRegion, flags);
}

QRhiTexture *QBackingStoreDefaultCompositor::toTexture(const QImage &sourceImage,
                                                       QRhi *rhi,
                                                       QRhiResourceUpdateBatch *resourceUpdates,
                                                       const QRegion &dirtyRegion,
                                                       QPlatformBackingStore::TextureFlags *flags) const
{
    Q_ASSERT(rhi);
    Q_ASSERT(resourceUpdates);
    Q_ASSERT(flags);

    if (!m_rhi) {
        m_rhi = rhi;
    } else if (m_rhi != rhi) {
        qWarning("QBackingStoreDefaultCompositor: the QRhi has changed unexpectedly, this should not happen");
        return nullptr;
    }

    QImage image = sourceImage;

    bool needsConversion = false;
    *flags = {};

    switch (image.format()) {
    case QImage::Format_ARGB32_Premultiplied:
        *flags |= QPlatformBackingStore::TexturePremultiplied;
        Q_FALLTHROUGH();
    case QImage::Format_RGB32:
    case QImage::Format_ARGB32:
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
        *flags |= QPlatformBackingStore::TextureSwizzle;
#endif
        break;
    case QImage::Format_RGBA8888_Premultiplied:
        *flags |= QPlatformBackingStore::TexturePremultiplied;
        Q_FALLTHROUGH();
    case QImage::Format_RGBX8888:
    case QImage::Format_RGBA8888:
        break;
    case QImage::Format_BGR30:
    case QImage::Format_A2BGR30_Premultiplied:
        // no fast path atm
        needsConversion = true;
        break;
    case QImage::Format_RGB30:
    case QImage::Format_A2RGB30_Premultiplied:
        // no fast path atm
        needsConversion = true;
        break;
    default:
        needsConversion = true;
        break;
    }

    if (image.size().isEmpty())
        return nullptr;

    const bool resized = !m_texture || m_texture->pixelSize() != image.size();
    if (dirtyRegion.isEmpty() && !resized)
        return m_texture;

    if (needsConversion)
        image = image.convertToFormat(QImage::Format_RGBA8888);
    else
        image.detach(); // if it was just wrapping data, that's no good, we need ownership, so detach

    if (resized) {
        if (!m_texture)
            m_texture = rhi->newTexture(QRhiTexture::RGBA8, image.size());
        else
            m_texture->setPixelSize(image.size());
        m_texture->create();
        resourceUpdates->uploadTexture(m_texture, image);
    } else {
        QRect imageRect = image.rect();
        QRect rect = dirtyRegion.boundingRect() & imageRect;
        QRhiTextureSubresourceUploadDescription subresDesc(image);
        subresDesc.setSourceTopLeft(rect.topLeft());
        subresDesc.setSourceSize(rect.size());
        subresDesc.setDestinationTopLeft(rect.topLeft());
        QRhiTextureUploadDescription uploadDesc(QRhiTextureUploadEntry(0, 0, subresDesc));
        resourceUpdates->uploadTexture(m_texture, uploadDesc);
    }

    return m_texture;
}

static inline QRect deviceRect(const QRect &rect, QWindow *window)
{
    return QRect(rect.topLeft() * window->devicePixelRatio(),
                 rect.size() * window->devicePixelRatio());
}

static inline QPoint deviceOffset(const QPoint &pt, QWindow *window)
{
    return pt * window->devicePixelRatio();
}

static QRegion deviceRegion(const QRegion &region, QWindow *window, const QPoint &offset)
{
    if (offset.isNull() && window->devicePixelRatio() <= 1)
        return region;

    QVarLengthArray<QRect, 4> rects;
    rects.reserve(region.rectCount());
    for (const QRect &rect : region)
        rects.append(deviceRect(rect.translated(offset), window));

    QRegion deviceRegion;
    deviceRegion.setRects(rects.constData(), rects.count());
    return deviceRegion;
}

static QMatrix4x4 targetTransform(const QRectF &target, const QRect &viewport, bool invertY)
{
    qreal x_scale = target.width() / viewport.width();
    qreal y_scale = target.height() / viewport.height();

    const QPointF relative_to_viewport = target.topLeft() - viewport.topLeft();
    qreal x_translate = x_scale - 1 + ((relative_to_viewport.x() / viewport.width()) * 2);
    qreal y_translate;
    if (invertY)
        y_translate = y_scale - 1 + ((relative_to_viewport.y() / viewport.height()) * 2);
    else
        y_translate = -y_scale + 1 - ((relative_to_viewport.y() / viewport.height()) * 2);

    QMatrix4x4 matrix;
    matrix(0,3) = x_translate;
    matrix(1,3) = y_translate;

    matrix(0,0) = x_scale;
    matrix(1,1) = y_scale;

    return matrix;
}

enum class SourceTransformOrigin {
    BottomLeft,
    TopLeft
};

static QMatrix3x3 sourceTransform(const QRectF &subTexture,
                                  const QSize &textureSize,
                                  SourceTransformOrigin origin)
{
    qreal x_scale = subTexture.width() / textureSize.width();
    qreal y_scale = subTexture.height() / textureSize.height();

    const QPointF topLeft = subTexture.topLeft();
    qreal x_translate = topLeft.x() / textureSize.width();
    qreal y_translate = topLeft.y() / textureSize.height();

    if (origin == SourceTransformOrigin::TopLeft) {
        y_scale = -y_scale;
        y_translate = 1 - y_translate;
    }

    QMatrix3x3 matrix;
    matrix(0,2) = x_translate;
    matrix(1,2) = y_translate;

    matrix(0,0) = x_scale;
    matrix(1,1) = y_scale;

    return matrix;
}

static inline QRect toBottomLeftRect(const QRect &topLeftRect, int windowHeight)
{
    return QRect(topLeftRect.x(), windowHeight - topLeftRect.bottomRight().y() - 1,
                 topLeftRect.width(), topLeftRect.height());
}

static bool prepareDrawForRenderToTextureWidget(const QPlatformTextureList *textures,
                                                int idx,
                                                QWindow *window,
                                                const QRect &deviceWindowRect,
                                                const QPoint &offset,
                                                bool invertTargetY,
                                                bool invertSource,
                                                QMatrix4x4 *target,
                                                QMatrix3x3 *source)
{
    const QRect clipRect = textures->clipRect(idx);
    if (clipRect.isEmpty())
        return false;

    QRect rectInWindow = textures->geometry(idx);
    // relative to the TLW, not necessarily our window (if the flush is for a native child widget), have to adjust
    rectInWindow.translate(-offset);

    const QRect clippedRectInWindow = rectInWindow & clipRect.translated(rectInWindow.topLeft());
    const QRect srcRect = toBottomLeftRect(clipRect, rectInWindow.height());

    *target = targetTransform(deviceRect(clippedRectInWindow, window),
                              deviceWindowRect,
                              invertTargetY);

    *source = sourceTransform(deviceRect(srcRect, window),
                              deviceRect(rectInWindow, window).size(),
                              invertSource ? SourceTransformOrigin::TopLeft : SourceTransformOrigin::BottomLeft);

    return true;
}

static QShader getShader(const QString &name)
{
    QFile f(name);
    if (f.open(QIODevice::ReadOnly))
        return QShader::fromSerialized(f.readAll());

    qWarning("QBackingStoreDefaultCompositor: Could not find built-in shader %s "
             "(is something wrong with QtGui library resources?)",
             qPrintable(name));
    return QShader();
}

static void updateMatrix3x3(QRhiResourceUpdateBatch *resourceUpdates, QRhiBuffer *ubuf, const QMatrix3x3 &m)
{
    // mat3 is still 4 floats per column in the uniform buffer (but there is no
    // 4th column), so 48 bytes altogether, not 36 or 64.

    float f[12];
    const float *src = static_cast<const float *>(m.constData());
    float *dst = f;
    memcpy(dst, src, 3 * sizeof(float));
    memcpy(dst + 4, src + 3, 3 * sizeof(float));
    memcpy(dst + 8, src + 6, 3 * sizeof(float));

    resourceUpdates->updateDynamicBuffer(ubuf, 64, 48, f);
}

enum class PipelineBlend {
    None,
    Alpha,
    PremulAlpha
};

static QRhiGraphicsPipeline *createGraphicsPipeline(QRhi *rhi,
                                                    QRhiShaderResourceBindings *srb,
                                                    QRhiSwapChain *swapchain,
                                                    PipelineBlend blend)
{
    QRhiGraphicsPipeline *ps = rhi->newGraphicsPipeline();

    switch (blend) {
    case PipelineBlend::Alpha:
    {
        QRhiGraphicsPipeline::TargetBlend blend;
        blend.enable = true;
        blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
        blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        blend.srcAlpha = QRhiGraphicsPipeline::One;
        blend.dstAlpha = QRhiGraphicsPipeline::One;
        ps->setTargetBlends({ blend });
    }
        break;
    case PipelineBlend::PremulAlpha:
    {
        QRhiGraphicsPipeline::TargetBlend blend;
        blend.enable = true;
        blend.srcColor = QRhiGraphicsPipeline::One;
        blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        blend.srcAlpha = QRhiGraphicsPipeline::One;
        blend.dstAlpha = QRhiGraphicsPipeline::One;
        ps->setTargetBlends({ blend });
    }
        break;
    default:
        break;
    }

    ps->setShaderStages({
        { QRhiShaderStage::Vertex, getShader(":/qt-project.org/gui/painting/shaders/backingstorecompose.vert.qsb"_L1) },
        { QRhiShaderStage::Fragment, getShader(":/qt-project.org/gui/painting/shaders/backingstorecompose.frag.qsb"_L1) }
    });
    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({ { 5 * sizeof(float) } });
    inputLayout.setAttributes({
        { 0, 0, QRhiVertexInputAttribute::Float3, 0 },
        { 0, 1, QRhiVertexInputAttribute::Float2, quint32(3 * sizeof(float)) }
    });
    ps->setVertexInputLayout(inputLayout);
    ps->setShaderResourceBindings(srb);
    ps->setRenderPassDescriptor(swapchain->renderPassDescriptor());

    if (!ps->create()) {
        qWarning("QBackingStoreDefaultCompositor: Failed to build graphics pipeline");
        delete ps;
        return nullptr;
    }
    return ps;
}

static const int UBUF_SIZE = 120;

QBackingStoreDefaultCompositor::PerQuadData QBackingStoreDefaultCompositor::createPerQuadData(QRhiTexture *texture)
{
    PerQuadData d;

    d.ubuf = m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, UBUF_SIZE);
    if (!d.ubuf->create())
        qWarning("QBackingStoreDefaultCompositor: Failed to create uniform buffer");

    d.srb = m_rhi->newShaderResourceBindings();
    d.srb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, d.ubuf, 0, UBUF_SIZE),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, texture, m_sampler)
    });
    if (!d.srb->create())
        qWarning("QBackingStoreDefaultCompositor: Failed to create srb");

    d.lastUsedTexture = texture;

    return d;
}

void QBackingStoreDefaultCompositor::updatePerQuadData(PerQuadData *d, QRhiTexture *texture)
{
    // This whole check-if-texture-ptr-is-different is needed because there is
    // nothing saying a QPlatformTextureList cannot return a different
    // QRhiTexture* from the same index in a subsequent flush.

    if (d->lastUsedTexture == texture || !d->srb)
        return;

    d->srb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, d->ubuf, 0, UBUF_SIZE),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, texture, m_sampler)
    });

    d->srb->updateResources(QRhiShaderResourceBindings::BindingsAreSorted);

    d->lastUsedTexture = texture;
}

void QBackingStoreDefaultCompositor::updateUniforms(PerQuadData *d, QRhiResourceUpdateBatch *resourceUpdates,
                                                    const QMatrix4x4 &target, const QMatrix3x3 &source, bool needsRedBlueSwap)
{
    resourceUpdates->updateDynamicBuffer(d->ubuf, 0, 64, target.constData());
    updateMatrix3x3(resourceUpdates, d->ubuf, source);
    float opacity = 1.0f;
    resourceUpdates->updateDynamicBuffer(d->ubuf, 112, 4, &opacity);
    qint32 swapRedBlue = needsRedBlueSwap ? 1 : 0;
    resourceUpdates->updateDynamicBuffer(d->ubuf, 116, 4, &swapRedBlue);
}

void QBackingStoreDefaultCompositor::ensureResources(QRhiSwapChain *swapchain, QRhiResourceUpdateBatch *resourceUpdates)
{
    static const float vertexData[] = {
        -1, -1, 0,   0, 0,
        -1,  1, 0,   0, 1,
         1, -1, 0,   1, 0,
        -1,  1, 0,   0, 1,
         1, -1, 0,   1, 0,
         1,  1, 0,   1, 1
    };

    if (!m_vbuf) {
        m_vbuf = m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(vertexData));
        if (m_vbuf->create())
            resourceUpdates->uploadStaticBuffer(m_vbuf, vertexData);
        else
            qWarning("QBackingStoreDefaultCompositor: Failed to create vertex buffer");
    }

    if (!m_sampler) {
        m_sampler = m_rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                                      QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
        if (!m_sampler->create())
            qWarning("QBackingStoreDefaultCompositor: Failed to create sampler");
    }

    if (!m_widgetQuadData.isValid())
        m_widgetQuadData = createPerQuadData(m_texture);

    QRhiShaderResourceBindings *srb = m_widgetQuadData.srb; // just for the layout
    if (!m_psNoBlend)
        m_psNoBlend = createGraphicsPipeline(m_rhi, srb, swapchain, PipelineBlend::None);
    if (!m_psBlend)
        m_psBlend = createGraphicsPipeline(m_rhi, srb, swapchain, PipelineBlend::Alpha);
    if (!m_psPremulBlend)
        m_psPremulBlend = createGraphicsPipeline(m_rhi, srb, swapchain, PipelineBlend::PremulAlpha);
}

QPlatformBackingStore::FlushResult QBackingStoreDefaultCompositor::flush(QPlatformBackingStore *backingStore,
                                                                         QRhi *rhi,
                                                                         QRhiSwapChain *swapchain,
                                                                         QWindow *window,
                                                                         const QRegion &region,
                                                                         const QPoint &offset,
                                                                         QPlatformTextureList *textures,
                                                                         bool translucentBackground)
{
    Q_ASSERT(textures); // may be empty if there are no render-to-texture widgets at all, but null it cannot be

    if (!m_rhi) {
        m_rhi = rhi;
    } else if (m_rhi != rhi) {
        qWarning("QBackingStoreDefaultCompositor: the QRhi has changed unexpectedly, this should not happen");
        return QPlatformBackingStore::FlushFailed;
    }

    if (!qt_window_private(window)->receivedExpose)
        return QPlatformBackingStore::FlushSuccess;

    qCDebug(lcQpaBackingStore) << "Composing and flushing" << region << "of" << window
                               << "at offset" << offset << "with" << textures->count() << "texture(s) in" << textures
                               << "via swapchain" << swapchain;

    QWindowPrivate::get(window)->lastComposeTime.start();

    if (swapchain->currentPixelSize() != swapchain->surfacePixelSize())
        swapchain->createOrResize();

    // Start recording a new frame.
    QRhi::FrameOpResult frameResult = rhi->beginFrame(swapchain);
    if (frameResult == QRhi::FrameOpSwapChainOutOfDate) {
        if (!swapchain->createOrResize())
            return QPlatformBackingStore::FlushFailed;
        frameResult = rhi->beginFrame(swapchain);
    }
    if (frameResult == QRhi::FrameOpDeviceLost)
        return QPlatformBackingStore::FlushFailedDueToLostDevice;
    if (frameResult != QRhi::FrameOpSuccess)
        return QPlatformBackingStore::FlushFailed;

    // Prepare resource updates.
    QRhiResourceUpdateBatch *resourceUpdates = rhi->nextResourceUpdateBatch();
    QPlatformBackingStore::TextureFlags flags;

    bool gotTextureFromGraphicsBuffer = false;
    if (QPlatformGraphicsBuffer *graphicsBuffer = backingStore->graphicsBuffer()) {
        if (graphicsBuffer->lock(QPlatformGraphicsBuffer::SWReadAccess)) {
            const QImage::Format format = QImage::toImageFormat(graphicsBuffer->format());
            const QSize size = graphicsBuffer->size();
            QImage wrapperImage(graphicsBuffer->data(), size.width(), size.height(), graphicsBuffer->bytesPerLine(), format);
            toTexture(wrapperImage, rhi, resourceUpdates, deviceRegion(region, window, offset), &flags);
            gotTextureFromGraphicsBuffer = true;
            graphicsBuffer->unlock();
            if (graphicsBuffer->origin() == QPlatformGraphicsBuffer::OriginBottomLeft)
                flags |= QPlatformBackingStore::TextureFlip;
        }
    }
    if (!gotTextureFromGraphicsBuffer)
        toTexture(backingStore, rhi, resourceUpdates, deviceRegion(region, window, offset), &flags);

    ensureResources(swapchain, resourceUpdates);

    const bool needsRedBlueSwap = (flags & QPlatformBackingStore::TextureSwizzle) != 0;
    const bool premultiplied = (flags & QPlatformBackingStore::TexturePremultiplied) != 0;
    SourceTransformOrigin origin = SourceTransformOrigin::TopLeft;
    if (flags & QPlatformBackingStore::TextureFlip)
        origin = SourceTransformOrigin::BottomLeft;

    const QRect deviceWindowRect = deviceRect(QRect(QPoint(), window->size()), window);
    const QPoint deviceWindowOffset = deviceOffset(offset, window);

    const bool invertTargetY = rhi->clipSpaceCorrMatrix().data()[5] < 0.0f;
    const bool invertSource = rhi->isYUpInFramebuffer() != rhi->isYUpInNDC();
    if (m_texture) {
        // The backingstore is for the entire tlw.
        // In case of native children offset tells the position relative to the tlw.
        const QRect srcRect = toBottomLeftRect(deviceWindowRect.translated(deviceWindowOffset), m_texture->pixelSize().height());
        const QMatrix3x3 source = sourceTransform(srcRect, m_texture->pixelSize(), origin);
        QMatrix4x4 target; // identity
        if (invertTargetY)
            target.data()[5] = -1.0f;
        updateUniforms(&m_widgetQuadData, resourceUpdates, target, source, needsRedBlueSwap);
    }

    const int textureWidgetCount = textures->count();
    const int oldTextureQuadDataCount = m_textureQuadData.count();
    if (oldTextureQuadDataCount != textureWidgetCount) {
        for (int i = textureWidgetCount; i < oldTextureQuadDataCount; ++i)
            m_textureQuadData[i].reset();
        m_textureQuadData.resize(textureWidgetCount);
    }

    for (int i = 0; i < textureWidgetCount; ++i) {
        QMatrix4x4 target;
        QMatrix3x3 source;
        if (!prepareDrawForRenderToTextureWidget(textures, i, window, deviceWindowRect,
                                                 offset, invertTargetY, invertSource, &target, &source))
        {
            m_textureQuadData[i].reset();
            continue;
        }
        QRhiTexture *t = textures->texture(i);
        if (t) {
            if (!m_textureQuadData[i].isValid())
                m_textureQuadData[i] = createPerQuadData(t);
            else
                updatePerQuadData(&m_textureQuadData[i], t);
            updateUniforms(&m_textureQuadData[i], resourceUpdates, target, source, false);
        } else {
            m_textureQuadData[i].reset();
        }
    }

    // Record the render pass (with committing the resource updates).
    QRhiCommandBuffer *cb = swapchain->currentFrameCommandBuffer();
    const QSize outputSizeInPixels = swapchain->currentPixelSize();
    QColor clearColor = translucentBackground ? Qt::transparent : Qt::black;
    cb->beginPass(swapchain->currentFrameRenderTarget(), clearColor, { 1.0f, 0 }, resourceUpdates);

    cb->setGraphicsPipeline(m_psNoBlend);
    cb->setViewport({ 0, 0, float(outputSizeInPixels.width()), float(outputSizeInPixels.height()) });
    QRhiCommandBuffer::VertexInput vbufBinding(m_vbuf, 0);
    cb->setVertexInput(0, 1, &vbufBinding);

    // Textures for renderToTexture widgets.
    for (int i = 0; i < textureWidgetCount; ++i) {
        if (!textures->flags(i).testFlag(QPlatformTextureList::StacksOnTop)) {
            if (m_textureQuadData[i].isValid()) {
                cb->setShaderResources(m_textureQuadData[i].srb);
                cb->draw(6);
            }
        }
    }

    cb->setGraphicsPipeline(premultiplied ? m_psPremulBlend : m_psBlend);

    // Backingstore texture with the normal widgets.
    if (m_texture) {
        cb->setShaderResources(m_widgetQuadData.srb);
        cb->draw(6);
    }

    // Textures for renderToTexture widgets that have WA_AlwaysStackOnTop set.
    for (int i = 0; i < textureWidgetCount; ++i) {
        const QPlatformTextureList::Flags flags = textures->flags(i);
        if (flags.testFlag(QPlatformTextureList::StacksOnTop)) {
            if (m_textureQuadData[i].isValid()) {
                if (flags.testFlag(QPlatformTextureList::NeedsPremultipliedAlphaBlending))
                    cb->setGraphicsPipeline(m_psPremulBlend);
                else
                    cb->setGraphicsPipeline(m_psBlend);
                cb->setShaderResources(m_textureQuadData[i].srb);
                cb->draw(6);
            }
        }
    }

    cb->endPass();

    rhi->endFrame(swapchain);

    return QPlatformBackingStore::FlushSuccess;
}

QT_END_NAMESPACE
