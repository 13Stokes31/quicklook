/*
 * Renderizado basado en el blur de KWin v6.7.5, reducido a una aplicación.
 * SPDX-FileCopyrightText: 2010 Fredrik Höglund <fredrik@kde.org>
 * SPDX-FileCopyrightText: 2011 Philipp Knechtges <philipp-dev@knechtges.com>
 * SPDX-FileCopyrightText: 2018 Alex Nemeth <alex.nemeth329@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "core/rendertarget.h"
#include "core/renderviewport.h"
#include "effect/effect.h"
#include "effect/effecthandler.h"
#include "effect/effectwindow.h"
#include "opengl/glutils.h"
#include "scene/backgroundeffectitem.h"
#include "scene/windowitem.h"
#include "window.h"
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QPointer>
#include <QPainterPath>
#include <QRegularExpression>
#include <QVariantMap>
#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_map>

namespace KWin {

class GeometryReceiver final : public QObject
{
    Q_OBJECT
public:
    GeometryReceiver(const QString &service, std::function<void(RectF, double, bool)> changed, QObject *parent)
        : QObject(parent), m_changed(std::move(changed))
    {
        auto bus = QDBusConnection::sessionBus();
        bus.connect(service, "/QuickLook", "org.quicklook.Blur", "geometryChanged", this,
                    SLOT(receive(double,double,double,double,double,bool)));
        auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(QDBusMessage::createMethodCall(
            service, "/QuickLook", "org.quicklook.Blur", "imageGeometry")), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
            const QDBusPendingReply<QVariantMap> reply = *watcher;
            if (!reply.isError()) {
                const QVariantMap map = reply.value();
                receive(map.value("x").toDouble(), map.value("y").toDouble(),
                        map.value("width").toDouble(), map.value("height").toDouble(),
                        map.value("radius").toDouble(),
                        map.value("enabled").toBool());
            }
            watcher->deleteLater();
        });
    }
public Q_SLOTS:
    void receive(double x, double y, double width, double height, double radius, bool enabled)
    {
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) || !std::isfinite(height)
            || x < 0 || y < 0 || width < 0 || height < 0 || x > 1 || y > 1
            || width > 1 || height > 1 || !std::isfinite(radius) || radius < 0 || radius > 0.5) { return; }
        m_changed(RectF(x, y, width, height), radius, enabled);
    }
private:
    std::function<void(RectF, double, bool)> m_changed;
};

class QuickLookSoftBlur final : public Effect
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.quicklook.SoftBlurEffect")
    struct Pass {
        std::unique_ptr<GLShader> shader;
        int matrix = -1, offset = -1, halfpixel = -1;
    };
    struct Buffers {
        std::vector<std::unique_ptr<GLTexture>> textures;
        std::vector<std::unique_ptr<GLFramebuffer>> framebuffers;
    };
    struct WindowData {
        std::unique_ptr<BackgroundEffectItem> item;
        std::unordered_map<RenderView *, Buffers> views;
        std::unique_ptr<GeometryReceiver> receiver;
        std::optional<RectF> image;
        RegionF blurRegion;
        double radius = 0;
        bool enabled = true;
    };

public:
    QuickLookSoftBlur()
    {
        QDBusConnection::sessionBus().registerObject("/QuickLookSoftBlur", this,
            QDBusConnection::ExportScriptableSlots);
        for (auto [pass, fragment] : {std::pair{&m_down, "downsample"},
                                     std::pair{&m_up, "upsample"}, std::pair{&m_screen, "onscreen"}}) {
            pass->shader = ShaderManager::instance()->generateShaderFromFile(ShaderTrait::MapTexture,
                ":/quicklook-softblur/shaders/vertex.vert",
                QString(":/quicklook-softblur/shaders/%1.frag").arg(fragment));
            if (!pass->shader) {
                qWarning("Quicklook: no se pudo compilar el shader de blur");
                return;
            }
            pass->matrix = pass->shader->uniformLocation("modelViewProjectionMatrix");
            pass->offset = pass->shader->uniformLocation("offset");
            pass->halfpixel = pass->shader->uniformLocation("halfpixel");
        }
        // El nivel 3 de KWin 6.7.5 usa dos reducciones y offset entero 2.
        m_valid = true;
        connect(effects, &EffectsHandler::windowAdded, this, &QuickLookSoftBlur::watch);
        connect(effects, &EffectsHandler::windowDeleted, this, [this](EffectWindow *w) {
            effects->makeOpenGLContextCurrent();
            m_windows.erase(w);
        });
        connect(effects, &EffectsHandler::viewRemoved, this, [this](RenderView *view) {
            effects->makeOpenGLContextCurrent();
            for (auto &[window, data] : m_windows) {
                data.views.erase(view);
            }
        });
        for (EffectWindow *w : effects->stackingOrder()) {
            watch(w);
        }
    }

    ~QuickLookSoftBlur() override
    {
        QDBusConnection::sessionBus().unregisterObject("/QuickLookSoftBlur");
    }

public Q_SLOTS:
    Q_SCRIPTABLE bool ready() const { return m_valid; }
    Q_SCRIPTABLE int protocolVersion() const { return 2; }

public:

    static bool supported() { return effects->isOpenGLCompositing(); }
    bool isActive() const override
    {
        return m_valid && !effects->isScreenLocked()
            && std::any_of(m_windows.begin(), m_windows.end(), [](const auto &entry) { return entry.second.enabled; });
    }
    int requestedEffectChainPosition() const override { return 21; }
    bool blocksDirectScanout() const override { return false; }
    void prePaintScreen(ScreenPrePaintData &data) override
    {
        m_view = data.view;
        effects->prePaintScreen(data);
    }

    void drawWindow(const RenderTarget &target, const RenderViewport &viewport, EffectWindow *w,
                    int mask, const Region &region, WindowPaintData &data) override
    {
        if (m_windows.contains(w) && m_windows.at(w).enabled && !effects->activeFullScreenEffect()
            && !(mask & PAINT_WINDOW_TRANSFORMED) && data.xScale() == 1 && data.yScale() == 1
            && data.xTranslation() == 0 && data.yTranslation() == 0) {
            blur(target, viewport, w, region, data);
        }
        effects->drawWindow(target, viewport, w, mask, region, data);
    }

private:
    RegionF shape(EffectWindow *w) const
    {
        const WindowData &data = m_windows.at(w);
        if (!data.enabled) { return {}; }
        const RectF client = w->contentsRect();
        RegionF region(client);
        if (data.radius > 0) {
            const double radius = data.radius * std::min(client.width(), client.height());
            QPainterPath path;
            path.addRoundedRect(QRectF(client.x(), client.y(), client.width(), client.height()), radius, radius);
            region &= RegionF(QRegion(path.toFillPolygon().toPolygon()));
        }
        if (data.image) {
            const RectF image(client.x() + data.image->x() * client.width(),
                client.y() + data.image->y() * client.height(),
                data.image->width() * client.width(), data.image->height() * client.height());
            return region - RegionF(image.intersected(client));
        }
        // Mensaje geométrico del prototipo en el título, sin rutas ni datos personales.
        static const QRegularExpression geometry("\\[QLB:(\\d+):(\\d+):(\\d+):(\\d+)\\]$");
        const auto match = geometry.match(w->caption());
        if (match.hasMatch()) {
            const RectF image(match.captured(1).toInt(), match.captured(2).toInt(),
                              match.captured(3).toInt(), match.captured(4).toInt());
            region -= RegionF(image.intersected(w->contentsRect()));
        }
        return region;
    }

    void watch(EffectWindow *w)
    {
        // El identificador distingue esta prueba de Quicklook estable y de Qt normal.
        if (!w->windowClass().endsWith("org.quicklook.softblur", Qt::CaseInsensitive)) {
            return;
        }
        const QString service = QString("org.quicklook.Blur.p%1").arg(w->pid());
        const auto interface = QDBusConnection::sessionBus().interface();
        const bool registered = interface && interface->isServiceRegistered(service).value();
        const bool legacy = w->caption().contains("[QLB:");
        if (!registered && !legacy) { return; }
        WindowData &data = m_windows[w];
        data.enabled = legacy;
        data.item = std::make_unique<BackgroundEffectItem>(w->windowItem());
        data.item->setPixelsToExpandRepaintsBelowOpaqueRegions(20);
        auto update = [this, w]() {
            const auto found = m_windows.find(w);
            if (found == m_windows.end()) { return; }
            found->second.blurRegion = shape(w);
            found->second.item->setEffectBoundingRect(found->second.blurRegion.boundingRect());
            effects->addRepaintFull();
        };
        connect(w, &EffectWindow::windowFrameGeometryChanged, this, update);
        connect(w->window(), &Window::captionChanged, this, update);
        if (registered) {
            QPointer<EffectWindow> window(w);
            data.receiver = std::make_unique<GeometryReceiver>(service,
                [this, window, update](const RectF &rect, double radius, bool enabled) {
                    if (!window || !m_windows.contains(window.data())) { return; }
                    auto &data = m_windows.at(window.data());
                    data.image = rect;
                    data.radius = radius;
                    data.enabled = enabled;
                    update();
                }, this);
        }
        update();
    }

    static void quad(std::vector<GLVertex2D> &vertices, const RectF &r, const QSize &size)
    {
        const float x0 = r.left(), y0 = r.top(), x1 = r.right(), y1 = r.bottom();
        auto vertex = [&size](float x, float y) {
            return GLVertex2D{QVector2D(x, y), QVector2D(x / size.width(), 1 - y / size.height())};
        };
        for (const auto &v : {vertex(x0, y0), vertex(x1, y1), vertex(x0, y1),
                             vertex(x0, y0), vertex(x1, y0), vertex(x1, y1)}) {
            vertices.push_back(v);
        }
    }

    static void bind(Pass &pass, const QMatrix4x4 &matrix)
    {
        ShaderManager::instance()->pushShader(pass.shader.get());
        pass.shader->setUniform(pass.matrix, matrix);
        pass.shader->setUniform(pass.offset, 2.0f);
    }

    static void sample(Pass &pass, GLTexture *texture)
    {
        pass.shader->setUniform(pass.halfpixel, QVector2D(0.5 / texture->width(), 0.5 / texture->height()));
        texture->bind();
    }

    void blur(const RenderTarget &target, const RenderViewport &viewport, EffectWindow *w,
              const Region &region, const WindowPaintData &data)
    {
        const RegionF globalShape = m_windows.at(w).blurRegion.translated(w->pos());
        const Rect bounds = globalShape.boundingRect().rounded();
        if (bounds.width() < 4 || bounds.height() < 4 || bounds.width() > 16384 || bounds.height() > 16384) {
            return;
        }
        const Rect scaled = bounds.scaled(viewport.scale()).rounded();
        const Rect device = viewport.mapToDeviceCoordinates(bounds).rounded();
        std::vector<GLVertex2D> vertices;
        quad(vertices, RectF(0, 0, bounds.width(), bounds.height()), bounds.size());
        for (const RectF &r : globalShape.rects()) {
            const RectF local = r.translated(-bounds.topLeft()).scaled(viewport.scale()).rounded();
            if (region == Region::infinite()) {
                quad(vertices, local, scaled.size());
            } else {
                for (const Rect &clip : region.rects()) {
                    const RectF visible = local.intersected(RectF(clip.translated(-device.topLeft())));
                    if (!visible.isEmpty()) {
                        quad(vertices, visible, scaled.size());
                    }
                }
            }
        }
        if (vertices.size() == 6) {
            return;
        }
        Buffers &buffers = m_windows.at(w).views[m_view];
        const GLenum format = target.texture() ? target.texture()->internalFormat() : GL_RGBA8;
        if (buffers.textures.size() != 3 || buffers.textures[0]->size() != bounds.size()
            || buffers.textures[0]->internalFormat() != format) {
            Buffers next;
            for (int i = 0; i < 3; ++i) {
                auto texture = GLTexture::allocate(format, bounds.size() / (1 << i));
                if (!texture) {
                    return;
                }
                texture->setFilter(GL_LINEAR);
                texture->setWrapMode(GL_CLAMP_TO_EDGE);
                auto framebuffer = std::make_unique<GLFramebuffer>(texture.get());
                if (!framebuffer->valid()) {
                    return;
                }
                EglContext::currentContext()->pushFramebuffer(framebuffer.get());
                glClearColor(0, 0, 0, 0);
                glClear(GL_COLOR_BUFFER_BIT);
                EglContext::currentContext()->popFramebuffer();
                next.textures.push_back(std::move(texture));
                next.framebuffers.push_back(std::move(framebuffer));
            }
            buffers.framebuffers.clear();
            buffers.textures.clear();
            buffers = std::move(next);
        }
        const Region dirty = viewport.mapFromDeviceCoordinatesContained(region) & bounds;
        for (const Rect &r : dirty.rects()) {
            buffers.framebuffers[0]->blitFromRenderTarget(target, viewport, r, r.translated(-bounds.topLeft()));
        }
        GLVertexBuffer *vbo = GLVertexBuffer::streamingBuffer();
        vbo->reset();
        vbo->setVertices(vertices);
        vbo->bindArrays();
        QMatrix4x4 projection;
        projection.ortho(QRectF(0, 0, bounds.width(), bounds.height()));
        bind(m_down, projection);
        for (int i = 1; i <= 2; ++i) {
            sample(m_down, buffers.textures[i - 1].get());
            GLFramebuffer::pushFramebuffer(buffers.framebuffers[i].get());
            vbo->draw(GL_TRIANGLES, 0, 6);
        }
        ShaderManager::instance()->popShader();
        bind(m_up, projection);
        GLFramebuffer::popFramebuffer();
        sample(m_up, buffers.textures[2].get());
        vbo->draw(GL_TRIANGLES, 0, 6);
        ShaderManager::instance()->popShader();
        GLFramebuffer::popFramebuffer();
        QMatrix4x4 screen = viewport.projectionMatrix();
        screen.translate(scaled.x(), scaled.y());
        bind(m_screen, screen);
        m_screen.shader->setUniform("colorMatrix", QMatrix4x4());
        sample(m_screen, buffers.textures[1].get());
        const float opacity = w->opacity() * data.opacity();
        if (opacity < 1) {
            glEnable(GL_BLEND);
            glBlendColor(0, 0, 0, opacity * opacity);
            glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);
        }
        vbo->draw(GL_TRIANGLES, 6, vertices.size() - 6);
        if (opacity < 1) {
            glDisable(GL_BLEND);
        }
        ShaderManager::instance()->popShader();
        vbo->unbindArrays();
    }

    bool m_valid = false;
    RenderView *m_view = nullptr;
    Pass m_down, m_up, m_screen;
    std::unordered_map<EffectWindow *, WindowData> m_windows;
};

KWIN_EFFECT_FACTORY_SUPPORTED(QuickLookSoftBlur, "metadata.json", return QuickLookSoftBlur::supported();)
} // namespace KWin
#include "softblur.moc"
