#include "custom3dchart.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QColorDialog>
#include <QColorSpace>
#include <QDebug>
#include <QElapsedTimer>
#include <QInputDialog>
#include <QMenu>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QOpenGLPaintDevice>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QPaintDevice>
#include <QPainter>
#include <QSurface>
#include <QSurfaceFormat>
#include <QTimer>

#include <QtMath>

static constexpr int frameinterval = 2; // frame duration cap
static constexpr size_t absolutemax = 50000000;
static constexpr bool useShaderFile = true;

static constexpr int fpsBufferSize = 10;

static constexpr char vertShader[] = {
    "#version 330 core\n"
    "layout (location = 0) in vec3 aPosition;\n"
    "layout (location = 1) in vec4 aColor;\n\n"

    "out vec4 vertexColor;\n"

    "uniform int bVarPointSize;\n"
    "uniform float fVarPointSizeK;\n"
    "uniform float fVarPointSizeDepth;\n"
    "uniform mat4 mView;\n"
    "uniform float fPointSize;\n\n"

    "void main()\n"
    "{\n"
    "    gl_Position = mView * vec4(aPosition, 1.0f);\n"
    "    if (bVarPointSize != 0) {\n"
    "        float calcSize = (fPointSize * fVarPointSizeK) / (fVarPointSizeDepth * gl_Position.z + 0.01f);\n"
    "        gl_PointSize = max(calcSize, 0.1f);\n"
    "    } else {\n"
    "        gl_PointSize = max(fPointSize, 0.1f);\n"
    "    }\n"
    "    vertexColor = aColor;\n"
    "}\n\0"};

static constexpr char fragShader[] = {
    "#version 330 core\n"
    "out vec4 FragColor;\n\n"

    "in vec4 vertexColor;\n\n"

    "uniform int monoMode;\n"
    "uniform vec3 monoColor;\n"
    "uniform int maxMode;\n"
    "uniform float minAlpha;\n\n"

    "void main()\n"
    "{\n"
    "    if (maxMode != 0) {\n"
    "        float alphaV = ((pow(vertexColor.a, 0.454545f) * (1.0f - minAlpha)) + minAlpha);\n"
    "        vec4 absCol;\n"
    "        if (monoMode == 0) {\n"
    "            absCol = vec4(vertexColor.r * alphaV, vertexColor.g * alphaV, vertexColor.b * alphaV, 1.0f);\n"
    "        } else {\n"
    "            absCol = vec4(monoColor.r * alphaV, monoColor.g * alphaV, monoColor.b * alphaV, 1.0f);\n"
    "        }\n"
    "        FragColor = absCol;\n"
    "    } else {\n"
    "        if (monoMode == 0) {\n"
    "            FragColor = vec4(vertexColor.r, vertexColor.g, vertexColor.b, max(vertexColor.a, minAlpha));\n"
    "        } else {\n"
    "            FragColor = vec4(monoColor.r, monoColor.g, monoColor.b, max(0.005f, minAlpha));\n"
    "        }\n"
    "    }\n"
    "}\n\0"};

class Q_DECL_HIDDEN Custom3dChart::Private
{
public:
    PlotSetting2D plotSetting;

    QVector<QVector3D> vecPosData;
    QVector<QVector4D> vecColData;
    QVector<uint32_t> vecDataOrder;

    QVector3D m_whitePoint{};

    QFont m_labelFont;

    size_t arrsize{0};

    quint32 framesRendered{0};
    quint64 frametime{0};
    QTimer *m_timer{nullptr};
    QElapsedTimer elTim;
    float m_fps{0.0};
    float frameDelay{0.0};
    QVector<float> accumulatedFps;
    int accFpsIdx{0};

    bool enableMouseNav{false};
    bool continousRotate{false};
    bool doZoom{false};
    bool doTranslate{false};
    bool doRotate{false};
    bool showLabel{true};
    bool showHelp{false};
    bool toggleOpaque{false};
    bool useMaxBlend{false};
    bool useVariableSize{false};
    bool useSmoothParticle{true};
    bool useMonochrome{false};
    int clipboardSize{0};
    float minalpha{0.0};
    float fov{45};
    float camDistToTarget{0.75};
    float pitchAngle{0.0};
    float yawAngle{180.0};
    float turntableAngle{0.0};
    float particleSize{2.0};
    QVector3D targetPos{0.0, 0.0, 0.25};

    QColor bgColor{16, 16, 16};
    QColor monoColor{255, 255, 255};

    QClipboard *m_clipb;

    QPoint m_lastPos{0, 0};
    bool isMouseHold{false};
    bool isShiftHold{false};

    bool ongoingNav{false};
    bool enableNav{false};
    bool nForward{false};
    bool nBackward{false};
    bool nStrifeLeft{false};
    bool nStrifeRight{false};
    bool nUp{false};
    bool nDown{false};

    QOpenGLShaderProgram *scatterPrg;
    QOpenGLBuffer *scatterPosVbo;
    QOpenGLBuffer *scatterColVbo;
    QOpenGLBuffer *scatterIdxVbo;
    QOpenGLVertexArrayObject *scatterVao;
    bool isValid{true};
    bool isShaderFile{true};
};

Custom3dChart::Custom3dChart(PlotSetting2D &plotSetting, QWidget *parent)
    : QOpenGLWidget(parent)
    , d(new Private)
{
    d->plotSetting = plotSetting;
    d->m_clipb = QApplication::clipboard();
    d->accumulatedFps.resize(fpsBufferSize);

    setMinimumSize(10, 10);
    setFocusPolicy(Qt::ClickFocus);
    d->m_labelFont = QFont("Courier New", 11, QFont::Medium);
    d->m_labelFont.setPixelSize(14);

    QSurfaceFormat fmt;
    // fmt.setVersion(4,5);
    // fmt.setColorSpace(QColorSpace::SRgb);
    if (d->plotSetting.multisample3d > 0) {
        fmt.setSamples(d->plotSetting.multisample3d);
    }
    // fmt.setRedBufferSize(16);
    // fmt.setGreenBufferSize(16);
    // fmt.setBlueBufferSize(16);
    // fmt.setAlphaBufferSize(16);
    // fmt.setDepthBufferSize(24);
    // fmt.setStencilBufferSize(16);
    // fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSwapInterval(1);
    setFormat(fmt);

    d->m_timer = new QTimer(this);
    d->elTim.start();

    connect(d->m_timer, &QTimer::timeout, this, [&]() {
        doUpdate();
    });
}

Custom3dChart::~Custom3dChart()
{
    makeCurrent();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    doneCurrent();
    context()->deleteLater();
    delete d;
}

void Custom3dChart::initializeGL()
{
    QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
    f->glClearColor(d->bgColor.redF(), d->bgColor.greenF(), d->bgColor.blueF(), d->bgColor.alphaF());
    f->glEnable(GL_PROGRAM_POINT_SIZE);

    qDebug() << "Initializing opengl context with format:";
    qDebug() << format();

    d->scatterPrg = new QOpenGLShaderProgram(context());
    d->scatterPrg->create();

    reloadShaders();

    if (!d->scatterPrg->link()) {
        qWarning() << "Failed to link shader program!";
        d->isValid = false;
        return;
    }

    d->scatterPrg->bind();

    d->scatterVao = new QOpenGLVertexArrayObject(context());
    d->scatterVao->create();
    d->scatterVao->bind();

    d->scatterPosVbo = new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
    d->scatterPosVbo->create();
    d->scatterPosVbo->bind();
    d->scatterPosVbo->setUsagePattern(QOpenGLBuffer::StaticDraw);
    d->scatterPosVbo->allocate(d->vecPosData.constData(), d->vecPosData.size() * sizeof(QVector3D));
    d->scatterPrg->enableAttributeArray("aPosition");
    d->scatterPrg->setAttributeBuffer("aPosition", GL_FLOAT, 0, 3, 0);

    d->scatterColVbo = new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
    d->scatterColVbo->create();
    d->scatterColVbo->bind();
    d->scatterColVbo->setUsagePattern(QOpenGLBuffer::StaticDraw);
    d->scatterColVbo->allocate(d->vecColData.constData(), d->vecColData.size() * sizeof(QVector4D));
    d->scatterPrg->enableAttributeArray("aColor");
    d->scatterPrg->setAttributeBuffer("aColor", GL_FLOAT, 0, 4, 0);

    // depth ordering berat cuy
    // d->scatterIdxVbo = new QOpenGLBuffer(QOpenGLBuffer::IndexBuffer);
    // d->scatterIdxVbo->create();
    // d->scatterIdxVbo->bind();
    // d->scatterIdxVbo->setUsagePattern(QOpenGLBuffer::DynamicDraw);
    // d->scatterIdxVbo->allocate(d->vecDataOrder.constData(), d->vecDataOrder.size() * sizeof(uint32_t));

    d->scatterVao->release();

    d->vecPosData.clear();
    d->vecPosData.squeeze();
    d->vecColData.clear();
    d->vecColData.squeeze();
}

void Custom3dChart::addDataPoints(QVector<ColorPoint> &dArray, QVector3D &dWhitePoint)
{
    // using double is a massive performance hit, so let's use float instead.. :3
    foreach (const auto &cp, dArray) {
        const float xyytrans[3] = {(float)(cp.first.X - dWhitePoint.x()),
                                   (float)(cp.first.Y - dWhitePoint.y()),
                                   (float)(cp.first.Z)};
        // const float rgba[4] = {cp.second.R, cp.second.G, cp.second.B, cp.second.A};

        d->vecPosData.append(QVector3D{xyytrans[0], xyytrans[1], xyytrans[2]});
        d->vecColData.append(QVector4D{cp.second.R, cp.second.G, cp.second.B, cp.second.A});
    }
    d->arrsize = dArray.size();

    dArray.clear();
    dArray.squeeze();

    d->m_whitePoint = dWhitePoint;

    // berat cuy
    // d->vecDataOrder.resize(d->arrsize);
    // std::iota(d->vecDataOrder.begin(), d->vecDataOrder.end(), 0);

    if (d->continousRotate) {
        d->m_timer->start(frameinterval);
    }
}

void Custom3dChart::reloadShaders()
{
    if (QOpenGLContext::currentContext() != context() && context()->isValid()) {
        makeCurrent();
    }

    if (!d->scatterPrg->shaders().isEmpty()) {
        qDebug() << "Reloading shaders...";
        d->scatterPrg->removeAllShaders();
    } else {
        qDebug() << "Initializing shaders...";
    }

    if (useShaderFile) {
        d->isShaderFile = true;
        qDebug() << "Loading vertex shader file...";
        if (!d->scatterPrg->addShaderFromSourceFile(QOpenGLShader::Vertex, "./3dpoint-vertex.glsl")) {
            d->isShaderFile = false;
            qDebug() << "Loading shader from file failed, using internal shader instead.";
            if (!d->scatterPrg->addShaderFromSourceCode(QOpenGLShader::Vertex, vertShader)) {
                qWarning() << "Failed to load vertex shader!";
                d->isValid = false;
                return;
            }
        }
        qDebug() << "Loading fragment shader file...";
        if (!d->scatterPrg->addShaderFromSourceFile(QOpenGLShader::Fragment, "./3dpoint-fragment.glsl")) {
            d->isShaderFile = false;
            qDebug() << "Loading shader from file failed, using internal shader instead.";
            if (!d->scatterPrg->addShaderFromSourceCode(QOpenGLShader::Fragment, fragShader)) {
                qWarning() << "Failed to load fragment shader!";
                d->isValid = false;
                return;
            }
        }
    } else {
        d->isShaderFile = false;
        if (!d->scatterPrg->addShaderFromSourceCode(QOpenGLShader::Vertex, vertShader)) {
            qWarning() << "Failed to load vertex shader!";
            d->isValid = false;
            return;
        }
        if (!d->scatterPrg->addShaderFromSourceCode(QOpenGLShader::Fragment, fragShader)) {
            qWarning() << "Failed to load fragment shader!";
            d->isValid = false;
            return;
        }
    }

    if (!d->scatterPrg->link()) {
        qWarning() << "Failed to link shader program!";
        d->isValid = false;
        return;
    }
}

bool Custom3dChart::checkValidity()
{
    // reserved, how do I check if all shader programs are fine before showing...
    return d->isValid;
}

void Custom3dChart::paintGL()
{
    QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
    size_t maxPartNum = std::min((size_t)d->arrsize, absolutemax);

    d->frameDelay = (double)(d->elTim.nsecsElapsed() - d->frametime) / 1.0e9;
    d->elTim.restart();
    d->frametime = d->elTim.nsecsElapsed();

    QPainter pai(this);

    if (d->continousRotate) {
        const float currentRotation = (20.0 * d->frameDelay);
        if (currentRotation < 360) {
            d->turntableAngle += currentRotation;
        }
        if (d->turntableAngle >= 360) {
            d->turntableAngle -= 360;
        }
    }

    if (d->enableNav) {
        doNavigation();
    }

    pai.beginNativePainting();

    if (d->minalpha < 0.9) {
        f->glEnable(GL_BLEND);
        f->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        if (d->useMaxBlend) {
            f->glBlendEquation(GL_MAX);
        }
    } else {
        f->glEnable(GL_DEPTH_TEST);
    }

    if (d->useSmoothParticle) {
        f->glEnable(GL_POINT_SMOOTH);
    }

    f->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    QMatrix4x4 persMatrix;
    persMatrix.perspective(d->fov, (width() * devicePixelRatioF()) / (height() * devicePixelRatioF()), 0.0001, 50.0);

    QVector3D camPos{(float)(qSin(qDegreesToRadians(d->yawAngle)) * qCos(qDegreesToRadians(d->pitchAngle)) * d->camDistToTarget),
                     (float)(qCos(qDegreesToRadians(d->yawAngle)) * qCos(qDegreesToRadians(d->pitchAngle)) * d->camDistToTarget),
                     (float)((qSin(qDegreesToRadians(d->pitchAngle)) * d->camDistToTarget))};

    QMatrix4x4 lookMatrix;
    lookMatrix.lookAt(camPos + d->targetPos, d->targetPos, {0, 0, 1});

    QMatrix4x4 modelMatrix;
    modelMatrix.scale(1, 1, 0.5);
    modelMatrix.rotate(d->turntableAngle, {0, 0, 1});

    const QMatrix4x4 totalMatrix = persMatrix * lookMatrix * modelMatrix;

    // berat cuy
    // if (f->glIsEnabled(GL_DEPTH_TEST) == 0) {
    //     std::sort(d->vecDataOrder.begin(), d->vecDataOrder.end(), [&](uint32_t lhs, uint32_t rhs){
    //         const QVector4D lhh = totalMatrix * QVector4D(d->vecPosData[lhs], 1.0);
    //         const QVector4D rhh = totalMatrix * QVector4D(d->vecPosData[rhs], 1.0);
    //         return lhh.z() < rhh.z();
    //     });
    //     std::reverse(d->vecDataOrder.begin(), d->vecDataOrder.end());
    // }

    d->scatterPrg->bind();

    if (d->useMaxBlend) {
        d->scatterPrg->setUniformValue("maxMode", true);
    } else {
        d->scatterPrg->setUniformValue("maxMode", false);
    }

    d->scatterPrg->setUniformValue("mView", totalMatrix);

    if (d->useVariableSize) {
        d->scatterPrg->setUniformValue("bVarPointSize", true);
        d->scatterPrg->setUniformValue("fVarPointSizeK", 1.0f);
        d->scatterPrg->setUniformValue("fVarPointSizeDepth", 5.0f);
    } else {
        d->scatterPrg->setUniformValue("bVarPointSize", false);
    }
    d->scatterPrg->setUniformValue("fPointSize", d->particleSize);
    d->scatterPrg->setUniformValue("minAlpha", d->minalpha);
    d->scatterPrg->setUniformValue("monoColor", QVector3D{(float)d->monoColor.redF(), (float)d->monoColor.greenF(), (float)d->monoColor.blueF()});
    if (d->useMonochrome) {
        d->scatterPrg->setUniformValue("monoMode", true);
    } else {
        d->scatterPrg->setUniformValue("monoMode", false);
    }

    d->scatterVao->bind();

    f->glDrawArrays(GL_POINTS, 0, maxPartNum);

    // berat cuy
    // d->scatterIdxVbo->write(0, d->vecDataOrder.constData(), d->vecDataOrder.size() * sizeof(uint32_t));
    // f->glDrawElements(GL_POINTS, maxPartNum, GL_UNSIGNED_INT, (void*)0);

    d->scatterVao->release();

    f->glDisable(GL_BLEND);
    f->glDisable(GL_DEPTH_TEST);
    f->glDisable(GL_POINT_SMOOTH);

    pai.endNativePainting();

    // placeholder, blending broke when there's nothing to draw after native painting...
    pai.setPen(Qt::transparent);
    pai.setBrush(Qt::transparent);
    pai.drawRect(0,0,1,1);

    if (d->showLabel) {
        if (d->accFpsIdx >= d->accumulatedFps.size()) {
            d->accFpsIdx = 0;
        }
        d->accumulatedFps[d->accFpsIdx] = 1.0f / d->frameDelay;
        d->accFpsIdx++;
        // d->m_fps = 1.0f / d->frameDelay;
        if (d->accFpsIdx >= d->accumulatedFps.size()) {
            d->m_fps = std::accumulate(d->accumulatedFps.constBegin(), d->accumulatedFps.constEnd(), 0.0f) / (float)d->accumulatedFps.size();
            d->accFpsIdx = 0;
        }

        d->m_labelFont.setPixelSize(14);
        pai.setPen(Qt::lightGray);
        pai.setBrush(QColor(0, 0, 0, 160));
        pai.setFont(d->m_labelFont);

        const QString fpsS =
            QString(
                "Unique colors: %2 | FPS: %1 | Min alpha: %3 | Size: %10 (%14-%15) | %11\nYaw: %4 | Pitch: %9 | FOV: %5 | Dist: %6 | "
                "Target:[%7:%8:%12] | Turntable: %13")
                .arg(QString::number(d->m_fps, 'f', 2),
                     (maxPartNum == absolutemax) ? QString("%1(capped)").arg(QString::number(maxPartNum))
                                                 : QString::number(maxPartNum),
                     QString::number(d->minalpha, 'f', 3),
                     QString::number(d->yawAngle, 'f', 2),
                     QString::number(d->fov, 'f', 2),
                     QString::number(d->camDistToTarget, 'f', 3),
                     QString::number(d->targetPos.x(), 'f', 3),
                     QString::number(d->targetPos.y(), 'f', 3),
                     QString::number(d->pitchAngle, 'f', 2),
                     QString::number(d->particleSize, 'f', 1),
                     d->useMaxBlend ? QString("Max blending") : QString("Alpha blending"),
                     QString::number(d->targetPos.z(), 'f', 3),
                     QString::number(d->turntableAngle, 'f', 2),
                     d->useVariableSize ? QString("variable") : QString("static"),
                     d->useSmoothParticle ? QString("rnd") : QString("sq"));

        QRect boundRect;
        const QMargins lblMargin(8, 8, 8, 8);
        const QMargins lblBorder(5, 5, 5, 5);
        pai.drawText(rect() - lblMargin, Qt::AlignBottom | Qt::AlignLeft, fpsS, &boundRect);
        pai.setPen(Qt::NoPen);
        boundRect += lblBorder;
        pai.drawRect(boundRect);
        pai.setPen(Qt::lightGray);
        pai.drawText(rect() - lblMargin, Qt::AlignBottom | Qt::AlignLeft, fpsS);
    }

    if (d->showHelp) {
        d->m_labelFont.setPixelSize(14);
        pai.setPen(Qt::lightGray);
        pai.setBrush(QColor(0, 0, 0, 160));
        pai.setFont(d->m_labelFont);

        const QString fpsS = QString(
            "(F1): Show/hide this help\n"
            "Note: depth buffer is not used with alpha < 0.9\n"
            "Max blending is useful to see the frequent colors\n"
            "Shader source: %1\n"
            "-------------------\n"
            "(Wheel): Zoom\n"
            "(LMB): Rotate/orbit\n"
            "(RMB): Pan\n"
            "(WASD): Walk through\n"
            "(C/V): Move target down/up\n"
            "(Shift+wheel|[/]): Decrease/increase FOV\n"
            "(Q): Toggle alpha 1.0 - 0.0\n"
            "(Z/X): Decrease/increase particle size\n"
            "(-/=): Decrease/increase minimum alpha\n"
            "(R): Reset camera\n"
            "(K): Toggle color/monochrome\n"
            "(L): Show/hide label\n"
            "(M): Use Max/Lighten blending\n"
            "(N): Toggle mouse navigation on/off\n"
            "(P): Toggle variable particle size\n"
            "(O): Toggle smooth particle\n"
            "(Shift+B): Change background color\n"
            "(F5): Reload shaders\n"
            "(F10): Toggle turntable animation\n"
            "(F11): Show fullscreen\n"
            "(F12): Save plot image").arg(d->isShaderFile ? "File" : "Internal");

        QRect boundRect;
        const QMargins lblMargin(8, 8, 8, 8);
        const QMargins lblBorder(5, 5, 5, 5);
        pai.drawText(rect() - lblMargin, Qt::AlignTop | Qt::AlignLeft, fpsS, &boundRect);
        pai.setPen(Qt::NoPen);
        boundRect += lblBorder;
        pai.drawRect(boundRect);
        pai.setPen(Qt::lightGray);
        pai.drawText(rect() - lblMargin, Qt::AlignTop | Qt::AlignLeft, fpsS);
    }

    pai.end();
}

void Custom3dChart::doUpdate()
{
    update();
}

void Custom3dChart::doNavigation()
{
    const float sinYaw = qSin(qDegreesToRadians(d->yawAngle));
    const float cosYaw = qCos(qDegreesToRadians(d->yawAngle));
    const float sinPitch = qSin(qDegreesToRadians(d->pitchAngle));
    const float cosPitch = qCos(qDegreesToRadians(d->pitchAngle));

    const float shiftMultp = d->isShiftHold ? 5.0 : 1.0;

    const float baseSpeed = 0.5;
    const float calcSpeed = baseSpeed * shiftMultp * d->frameDelay;

    if (d->enableNav) {
        QVector3D camFrontBack{sinYaw * cosPitch * d->camDistToTarget,
                               cosYaw * cosPitch * d->camDistToTarget,
                               sinPitch * d->camDistToTarget};

        if (d->nForward)
            d->targetPos = d->targetPos - (camFrontBack * calcSpeed); // move forward / W
        if (d->nBackward)
            d->targetPos = d->targetPos + (camFrontBack * calcSpeed); // move backward / A

        QVector3D camStride{cosYaw * d->camDistToTarget, sinYaw * d->camDistToTarget * -1.0f, 0.0};

        if (d->nStrifeLeft)
            d->targetPos = d->targetPos + (camStride * calcSpeed); // stride left / S
        if (d->nStrifeRight)
            d->targetPos = d->targetPos - (camStride * calcSpeed); // stride right / D

        if (d->nDown)
            d->targetPos.setZ(d->targetPos.z() - (d->camDistToTarget * calcSpeed)); // move down / C
        if (d->nUp)
            d->targetPos.setZ(d->targetPos.z() + (d->camDistToTarget * calcSpeed)); // move up / V
    }
}

void Custom3dChart::resizeEvent(QResizeEvent *event)
{
    // resizeGL(width(), height());
    doUpdate();
    QOpenGLWidget::resizeEvent(event);
}

void Custom3dChart::keyPressEvent(QKeyEvent *event)
{
    const float shiftMultp = d->isShiftHold ? 10.0 : 1.0;

    switch (event->key()) {
    case Qt::Key_Minus:
        d->minalpha -= 0.002 * shiftMultp;
        d->minalpha = std::max(0.0f, d->minalpha);
        break;
    case Qt::Key_Equal:
        d->minalpha += 0.002 * shiftMultp;
        d->minalpha = std::min(1.0f, d->minalpha);
        break;
    case Qt::Key_BracketLeft:
        d->fov += 1.0 * shiftMultp;
        d->fov = std::min(170.0f, d->fov);
        break;
    case Qt::Key_BracketRight:
        d->fov -= 1.0 * shiftMultp;
        d->fov = std::max(1.0f, d->fov);
        break;
    case Qt::Key_W:
        d->enableNav = true;
        d->nForward = true;
        break;
    case Qt::Key_S:
        d->enableNav = true;
        d->nBackward = true;
        break;
    case Qt::Key_A:
        d->enableNav = true;
        d->nStrifeLeft = true;
        break;
    case Qt::Key_D:
        d->enableNav = true;
        d->nStrifeRight = true;
        break;
    case Qt::Key_C:
        d->enableNav = true;
        d->nDown = true;
        break;
    case Qt::Key_V:
        d->enableNav = true;
        d->nUp = true;
        break;
    case Qt::Key_R:
        resetCamera();
        break;
    case Qt::Key_K:
        d->useMonochrome = !d->useMonochrome;
        break;
    case Qt::Key_L:
        d->showLabel = !d->showLabel;
        break;
    case Qt::Key_Q:
        d->toggleOpaque = !d->toggleOpaque;
        if (d->toggleOpaque) {
            d->minalpha = 1.0;
        } else {
            d->minalpha = 0.0;
        }
        break;
    case Qt::Key_X:
        d->particleSize += 0.1;
        d->particleSize = std::min(20.0f, d->particleSize);
        break;
    case Qt::Key_Z:
        d->particleSize -= 0.1;
        d->particleSize = std::max(0.0f, d->particleSize);
        break;
    case Qt::Key_M:
        d->useMaxBlend = !d->useMaxBlend;
        break;
    case Qt::Key_N:
        d->enableMouseNav = !d->enableMouseNav;
        if (!d->enableMouseNav) {
            setCursor(Qt::ArrowCursor);
            setMouseTracking(false);
        } else {
            setCursor(Qt::BlankCursor);
            setMouseTracking(true);
        }
        break;
    case Qt::Key_B:
        if (d->isShiftHold) {
            changeBgColor();
        }
        break;
    case Qt::Key_P:
        d->useVariableSize = !d->useVariableSize;
        break;
    case Qt::Key_O:
        d->useSmoothParticle = !d->useSmoothParticle;
        break;
    case Qt::Key_F1:
        d->showHelp = !d->showHelp;
        break;
    case Qt::Key_F5:
        reloadShaders();
        break;
    case Qt::Key_F10:
        if (d->continousRotate) {
            d->continousRotate = false;
            if (!d->enableNav) {
                d->m_timer->stop();
            }
        } else {
            d->continousRotate = true;
            if (!d->enableNav) {
                d->m_timer->start(frameinterval);
                d->elTim.restart();
            }
        }
        break;
    case Qt::Key_Shift:
        d->isShiftHold = true;
        break;
    case Qt::Key_Escape:
        if (d->enableMouseNav) {
            d->enableMouseNav = false;
            setCursor(Qt::ArrowCursor);
            setMouseTracking(false);
        }
        break;
    default:
        // do not render
        return;
        break;
    }

    if (d->enableNav && event->isAutoRepeat()) {
        return;
    }

    if (d->enableNav && !d->m_timer->isActive() && !event->isAutoRepeat()) {
        d->m_timer->start(frameinterval);
        d->elTim.restart();
    }

    doUpdate();
}

void Custom3dChart::keyReleaseEvent(QKeyEvent *event)
{
    if (event->isAutoRepeat()) {
        return;
    }

    switch (event->key()) {
    case Qt::Key_Shift:
        d->isShiftHold = false;
        break;
    case Qt::Key_W:
        d->nForward = false;
        break;
    case Qt::Key_S:
        d->nBackward = false;
        break;
    case Qt::Key_A:
        d->nStrifeLeft = false;
        break;
    case Qt::Key_D:
        d->nStrifeRight = false;
        break;
    case Qt::Key_C:
        d->nDown = false;
        break;
    case Qt::Key_V:
        d->nUp = false;
        break;
    default:
        break;
    }

    if (!(d->nForward || d->nBackward || d->nStrifeLeft || d->nStrifeRight || d->nUp || d->nDown) && !d->continousRotate) {
        d->enableNav = false;
        d->m_timer->stop();
    }
}

void Custom3dChart::passKeypres(QKeyEvent *e)
{
    keyPressEvent(e);
}

void Custom3dChart::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton) {
        setCursor(Qt::BlankCursor);
        d->m_lastPos = event->pos();
    }
}

void Custom3dChart::mouseMoveEvent(QMouseEvent *event)
{
    const float orbitSpeedDivider = d->isShiftHold ? 10.0 : 30.0;
    if (event->buttons() & Qt::LeftButton || d->enableMouseNav) {
        setCursor(Qt::BlankCursor);
        d->isMouseHold = true;

        QPoint delposs(event->pos() - d->m_lastPos);

        const QPoint toGlobal = mapToGlobal(QPoint(width() / 2, height() / 2));

        if (toGlobal == QPoint(width() / 2, height() / 2)) {
            // hack, either this is because of fullscreen border or smth idk..
            delposs -= QPoint(-1,-1);
        }

        QCursor::setPos(toGlobal);
        d->m_lastPos = QPoint(width() / 2, height() / 2);

        const float offsetX = delposs.x() * 1.0f;
        const float offsetY = delposs.y() * 1.0f;

        d->yawAngle += offsetX / orbitSpeedDivider;

        d->pitchAngle += offsetY / orbitSpeedDivider;
        d->pitchAngle = std::min(89.99f, std::max(-89.99f, d->pitchAngle));

        if (d->yawAngle >= 360) {
            d->yawAngle = 0;
        } else if (d->yawAngle < 0) {
            d->yawAngle = 360;
        }

        doUpdate();
    }

    if (event->buttons() & Qt::MiddleButton) {
        setCursor(Qt::OpenHandCursor);
        d->isMouseHold = true;

        const QPoint delposs(event->pos() - d->m_lastPos);
        d->m_lastPos = event->pos();

        const float rawoffsetX = delposs.x() * 1.0f;
        const float rawoffsetY = delposs.y() * 1.0f;

        // did I just spent my whole day to reinvent the wheel...
        const float offsetX = (rawoffsetX / 1500.0) * d->camDistToTarget;
        const float offsetY = (rawoffsetY / 1500.0) * d->camDistToTarget;

        const float sinYaw = qSin(qDegreesToRadians(d->yawAngle));
        const float cosYaw = qCos(qDegreesToRadians(d->yawAngle));
        const float sinPitch = qSin(qDegreesToRadians(d->pitchAngle));
        const float cosPitch = qCos(qDegreesToRadians(d->pitchAngle));

        QVector3D camPan{((offsetX * cosYaw) + (offsetY * -1.0f * sinYaw * sinPitch)),
                         ((offsetX * -1.0f * sinYaw) + (offsetY * -1.0f * cosYaw * sinPitch)),
                         offsetY * cosPitch};

        d->targetPos = d->targetPos + camPan;

        doUpdate();
    }
}

void Custom3dChart::mouseReleaseEvent(QMouseEvent *event)
{
    setCursor(Qt::ArrowCursor);
    d->isMouseHold = false;
    doUpdate();
}

void Custom3dChart::wheelEvent(QWheelEvent *event)
{
    const QPoint numDegrees = event->angleDelta();
    const double zoomIncrement = (numDegrees.y() / 1200.0);

    if (!d->isShiftHold) {
        if (d->camDistToTarget > 0.0001) {
            d->camDistToTarget -= zoomIncrement * d->camDistToTarget;

            if (d->camDistToTarget < 0.01)
                d->camDistToTarget = 0.01;
            doUpdate();
        }
    } else {
        if (zoomIncrement < 0) {
            d->fov += 5.0;
            d->fov = std::min(170.0f, d->fov);
            doUpdate();
        } else if (zoomIncrement > 0) {
            d->fov -= 5.0;
            d->fov = std::max(1.0f, d->fov);
            doUpdate();
        }
    }
}

void Custom3dChart::resetCamera()
{
    d->useMaxBlend = false;
    d->toggleOpaque = false;
    d->useVariableSize = false;
    d->useSmoothParticle = true;
    d->useMonochrome = false;
    d->minalpha = 0.0;
    d->yawAngle = 180.0;
    d->turntableAngle = 0.0;
    d->fov = 45;
    d->camDistToTarget = 0.75;
    d->pitchAngle = 0.0;
    d->particleSize = 2.0;
    d->targetPos = QVector3D{0.0, 0.0, 0.25};
    d->monoColor = QColor{255, 255, 255};
    d->bgColor = QColor{16, 16, 16};
    makeCurrent();
    QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
    f->glClearColor(d->bgColor.redF(), d->bgColor.greenF(), d->bgColor.blueF(), d->bgColor.alphaF());
    doneCurrent();

    doUpdate();
}

void Custom3dChart::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);

    QAction copyThis(this);
    copyThis.setText("Copy plot state");
    connect(&copyThis, &QAction::triggered, this, [&]() {
        copyState();
    });

    QAction pasteThis(this);
    pasteThis.setText("Paste plot state");
    connect(&pasteThis, &QAction::triggered, this, [&]() {
        pasteState();
    });

    QAction changeBg(this);
    changeBg.setText("Change background color...");
    connect(&changeBg, &QAction::triggered, this, [&]() {
        changeBgColor();
    });

    QAction changeMono(this);
    changeMono.setText("Change monochrome color...");
    connect(&changeMono, &QAction::triggered, this, [&]() {
        changeMonoColor();
    });

    menu.addAction(&copyThis);
    menu.addAction(&pasteThis);
    menu.addSeparator();
    menu.addAction(&changeBg);
    menu.addAction(&changeMono);

    menu.exec(event->globalPos());
}

void Custom3dChart::changeBgColor()
{
    if (d->isShiftHold) {
        d->isShiftHold = false;
    }
    const QColor currentBg = d->bgColor;
    const QColor setBgColor =
        QColorDialog::getColor(currentBg, this, "Set background color", QColorDialog::ShowAlphaChannel);
    if (setBgColor.isValid()) {
        d->bgColor = setBgColor;

        makeCurrent();
        QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
        f->glClearColor(d->bgColor.redF(), d->bgColor.greenF(), d->bgColor.blueF(), d->bgColor.alphaF());
        doneCurrent();

        doUpdate();
    }
}

void Custom3dChart::changeMonoColor()
{
    if (d->isShiftHold) {
        d->isShiftHold = false;
    }
    const QColor currentBg = d->monoColor;
    const QColor setMonoColor =
        QColorDialog::getColor(currentBg, this, "Set monochrome color", QColorDialog::ShowAlphaChannel);
    if (setMonoColor.isValid()) {
        d->monoColor = setMonoColor;

        doUpdate();
    }
}

void Custom3dChart::copyState()
{
    QByteArray headClip{"Scatter3DClip:"};
    QByteArray toClip;
    toClip.append(
        QByteArray::fromRawData(reinterpret_cast<const char *>(&d->useMaxBlend), sizeof(d->useMaxBlend)).toHex());
    toClip.append(
        QByteArray::fromRawData(reinterpret_cast<const char *>(&d->toggleOpaque), sizeof(d->toggleOpaque)).toHex());
    toClip.append(
        QByteArray::fromRawData(reinterpret_cast<const char *>(&d->useVariableSize), sizeof(d->useVariableSize)).toHex());
    toClip.append(
        QByteArray::fromRawData(reinterpret_cast<const char *>(&d->useSmoothParticle), sizeof(d->useSmoothParticle)).toHex());
    toClip.append(
        QByteArray::fromRawData(reinterpret_cast<const char *>(&d->useMonochrome), sizeof(d->useMonochrome)).toHex());
    toClip.append(
        QByteArray::fromRawData(reinterpret_cast<const char *>(&d->minalpha), sizeof(d->minalpha)).toHex());
    toClip.append(
        QByteArray::fromRawData(reinterpret_cast<const char *>(&d->yawAngle), sizeof(d->yawAngle)).toHex());
    toClip.append(
        QByteArray::fromRawData(reinterpret_cast<const char *>(&d->turntableAngle), sizeof(d->turntableAngle)).toHex());
    toClip.append(
        QByteArray::fromRawData(reinterpret_cast<const char *>(&d->fov), sizeof(d->fov)).toHex());
    toClip.append(
        QByteArray::fromRawData(reinterpret_cast<const char *>(&d->camDistToTarget), sizeof(d->camDistToTarget)).toHex());
    toClip.append(
        QByteArray::fromRawData(reinterpret_cast<const char *>(&d->pitchAngle), sizeof(d->pitchAngle)).toHex());
    toClip.append(
        QByteArray::fromRawData(reinterpret_cast<const char *>(&d->particleSize), sizeof(d->particleSize)).toHex());
    toClip.append(
        QByteArray::fromRawData(reinterpret_cast<const char *>(&d->targetPos), sizeof(d->targetPos)).toHex());
    toClip.append(
        QByteArray::fromRawData(reinterpret_cast<const char *>(&d->monoColor), sizeof(d->monoColor)).toHex());
    toClip.append(
        QByteArray::fromRawData(reinterpret_cast<const char *>(&d->bgColor), sizeof(d->bgColor)).toHex());

    headClip.append(toClip);

    d->m_clipb->setText(headClip);
}

void Custom3dChart::pasteState()
{
    QString fromClipStr = d->m_clipb->text();
    if (fromClipStr.contains("Scatter3DClip:")) {
        QByteArray fromClip = QByteArray::fromHex(fromClipStr.mid(fromClipStr.indexOf(":") + 1, -1).toUtf8());
        if (fromClip.size() != 77)
            return;

        const char* clipPointer = fromClip.constData();

        const bool useMaxBlend = *reinterpret_cast<const bool *>(clipPointer);
        clipPointer += sizeof(bool);
        const bool toggleOpaque = *reinterpret_cast<const bool *>(clipPointer);
        clipPointer += sizeof(bool);
        const bool useVariableSize = *reinterpret_cast<const bool *>(clipPointer);
        clipPointer += sizeof(bool);
        const bool useSmoothParticle = *reinterpret_cast<const bool *>(clipPointer);
        clipPointer += sizeof(bool);
        const bool useMonochrome = *reinterpret_cast<const bool *>(clipPointer);
        clipPointer += sizeof(bool);
        const float minalpha = *reinterpret_cast<const float *>(clipPointer);
        clipPointer += sizeof(float);
        const float yawAngle = *reinterpret_cast<const float *>(clipPointer);
        clipPointer += sizeof(float);
        const float turntableAngle = *reinterpret_cast<const float *>(clipPointer);
        clipPointer += sizeof(float);
        const float fov = *reinterpret_cast<const float *>(clipPointer);
        clipPointer += sizeof(float);
        const float camDistToTarget = *reinterpret_cast<const float *>(clipPointer);
        clipPointer += sizeof(float);
        const float pitchAngle = *reinterpret_cast<const float *>(clipPointer);
        clipPointer += sizeof(float);
        const float particleSize = *reinterpret_cast<const float *>(clipPointer);
        clipPointer += sizeof(float);
        QVector3D targetPos = *reinterpret_cast<const QVector3D *>(clipPointer);
        clipPointer += sizeof(QVector3D);
        QColor monoColor = *reinterpret_cast<const QColor *>(clipPointer);
        clipPointer += sizeof(QColor);
        QColor bgColor = *reinterpret_cast<const QColor *>(clipPointer);

        d->useMaxBlend = useMaxBlend;
        d->toggleOpaque = toggleOpaque;
        d->useVariableSize = useVariableSize;
        d->useSmoothParticle = useSmoothParticle;
        d->useMonochrome = useMonochrome;
        d->minalpha = std::max(0.0f, std::min(1.0f, minalpha));
        d->yawAngle = std::max(0.0f, std::min(360.0f, yawAngle));
        d->turntableAngle = std::max(0.0f, std::min(360.0f, turntableAngle));
        d->fov = std::max(1.0f, std::min(170.0f, fov));
        d->camDistToTarget = std::max(0.1f, camDistToTarget);
        d->pitchAngle = std::max(-89.99f, std::min(89.99f, pitchAngle));
        d->particleSize = std::max(0.0f, std::min(20.0f, particleSize));
        d->targetPos = targetPos;
        d->monoColor = monoColor;
        if (d->bgColor != bgColor) {
            d->bgColor = bgColor;
            makeCurrent();
            QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
            f->glClearColor(d->bgColor.redF(), d->bgColor.greenF(), d->bgColor.blueF(), d->bgColor.alphaF());
            doneCurrent();
        }

        doUpdate();
    }
}
