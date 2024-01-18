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

static constexpr int frameinterval = 5; // frame duration cap
// static constexpr float minAlpha = 0.03;
static constexpr size_t absolutemax = 50000000;

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
    "    //gl_Position = mPers * mLook * mModel * vec4(aPosition, 1.0f);\n"
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

    "uniform int maxMode;\n"
    "uniform float minAlpha;\n\n"

    "void main()\n"
    "{\n"
    "    if (maxMode != 0) {\n"
    "        float alphaV = ((pow(vertexColor.a, 0.454545f) * (1.0f - minAlpha)) + minAlpha);\n"
    "        vec4 absCol = vec4(vertexColor.r * alphaV, vertexColor.g * alphaV, vertexColor.b * alphaV, 1.0f);\n"
    "        FragColor = absCol;\n"
    "    } else {\n"
    "        FragColor = vec4(vertexColor.r, vertexColor.g, vertexColor.b, max(vertexColor.a, minAlpha));\n"
    "    }\n"
    "}\n\0"};

class Q_DECL_HIDDEN Custom3dChart::Private
{
public:
    PlotSetting2D plotSetting;

    QByteArray m_iccProfile{};
    QByteArray m_rawData{};

    QByteArray m_vecpos{};
    QByteArray m_veccol{};

    QVector3D m_whitePoint{};

    QFont m_labelFont;

    size_t arrsize{0};

    quint32 framesRendered{0};
    quint64 frametime{0};
    QTimer *m_timer{nullptr};
    QElapsedTimer m_frameTimer;
    QElapsedTimer elTim;
    float m_fps{0.0};
    float frameDelay{0.0};

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
    float minalpha{0.0};
    float fov{45};
    // float camPosZ{0.25};
    // float camPosX{0.0};
    float camDistToTarget{0.75};
    float pitchAngle{0.0};
    float yawAngle{180.0};
    float turntableAngle{0.0};
    float particleSize{2.0};
    // QVector3D translateVal{0, 0, 0};
    // QVector3D rotateVal{0, 0, 0};
    QVector3D targetPos{0.0, 0.0, 0.25};

    QColor bgColor{16, 16, 16};

    // float zoomBuffer{0.0};
    // QVector3D translateBuffer{0, 0, 0};
    // QVector3D rotateBuffer{0, 0, 0};

    QClipboard *m_clipb;

    QPoint m_lastPos{0, 0};
    bool isMouseHold{false};
    bool isShiftHold{false};
    // double m_offsetX{0.0};
    // double m_offsetY{0.0};

    bool enableNav{false};
    bool nForward{false};
    bool nBackward{false};
    bool nStrifeLeft{false};
    bool nStrifeRight{false};
    bool nUp{false};
    bool nDown{false};

    // int mouseMoveX{0};
    // int mouseMoveY{0};
    // QPoint mouseDelta{0,0};

    QOpenGLShaderProgram *scatterPrg;
    QOpenGLBuffer *scatterPosVbo;
    QOpenGLBuffer *scatterColVbo;
    QOpenGLVertexArrayObject *scatterVao;
    bool isValid{true};
};

Custom3dChart::Custom3dChart(PlotSetting2D &plotSetting, QWidget *parent)
    : QOpenGLWidget(parent)
    , d(new Private)
{
    d->plotSetting = plotSetting;
    d->m_clipb = QApplication::clipboard();

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
    // fmt.setDepthBufferSize(48);
    // fmt.setStencilBufferSize(16);
    // fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSwapInterval(1);
    setFormat(fmt);

    d->m_frameTimer.start();

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
    qDebug() << "init";
    QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
    f->glClearColor(d->bgColor.redF(), d->bgColor.greenF(), d->bgColor.blueF(), d->bgColor.alphaF());
    qDebug() << format();

    f->glEnable(GL_PROGRAM_POINT_SIZE);

    d->scatterPrg = new QOpenGLShaderProgram(context());
    d->scatterPrg->create();

    // if (!d->scatterPrg->addShaderFromSourceFile(QOpenGLShader::Vertex, "./vertex.glsl")) {
    //     qWarning() << "Failed to load vertex shader!";
    //     d->isValid = false;
    // }
    // if (!d->scatterPrg->addShaderFromSourceFile(QOpenGLShader::Fragment, "./fragment.glsl")) {
    //     qWarning() << "Failed to load fragment shader!";
    //     d->isValid = false;
    // }

    if (!d->scatterPrg->addShaderFromSourceCode(QOpenGLShader::Vertex, vertShader)) {
        qWarning() << "Failed to load vertex shader!";
        d->isValid = false;
    }
    if (!d->scatterPrg->addShaderFromSourceCode(QOpenGLShader::Fragment, fragShader)) {
        qWarning() << "Failed to load fragment shader!";
        d->isValid = false;
    }

    if (!d->scatterPrg->link()) {
        qWarning() << "Failed to link shader program!";
        d->isValid = false;
    }

    d->scatterPrg->bind();

    // qDebug() << d->scatterPrg->uniformLocation("fPointSize");

    d->scatterVao = new QOpenGLVertexArrayObject(context());
    d->scatterVao->create();
    d->scatterVao->bind();

    d->scatterPosVbo = new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
    d->scatterPosVbo->create();
    d->scatterPosVbo->bind();
    d->scatterPosVbo->setUsagePattern(QOpenGLBuffer::StaticDraw);
    d->scatterPosVbo->allocate(d->m_vecpos.constData(), d->m_vecpos.size());
    d->scatterPrg->enableAttributeArray("aPosition");
    d->scatterPrg->setAttributeBuffer("aPosition", GL_FLOAT, 0, 3, 0);

    d->scatterColVbo = new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
    d->scatterColVbo->create();
    d->scatterColVbo->bind();
    d->scatterColVbo->setUsagePattern(QOpenGLBuffer::StaticDraw);
    d->scatterColVbo->allocate(d->m_veccol.constData(), d->m_veccol.size());
    d->scatterPrg->enableAttributeArray("aColor");
    d->scatterPrg->setAttributeBuffer("aColor", GL_FLOAT, 0, 4, 0);

    d->scatterVao->release();

    d->m_vecpos.clear();
    d->m_veccol.clear();
    d->m_vecpos.squeeze();
    d->m_veccol.squeeze();
}

void Custom3dChart::addDataPoints(QVector<ColorPoint> &dArray, QVector3D &dWhitePoint)
{
    // using double is a massive performance hit, so let's use float instead.. :3
    foreach (const auto &cp, dArray) {
        const float xyytrans[3] = {(float)(cp.first.X - dWhitePoint.x()),
                                   (float)(cp.first.Y - dWhitePoint.y()),
                                   (float)(cp.first.Z)};
        const float rgba[4] = {cp.second.R, cp.second.G, cp.second.B, cp.second.A};

        d->m_vecpos.append(QByteArray::fromRawData(reinterpret_cast<const char *>(xyytrans), sizeof(xyytrans)));
        d->m_veccol.append(QByteArray::fromRawData(reinterpret_cast<const char *>(rgba), sizeof(rgba)));
    }
    d->arrsize = dArray.size();

    dArray.clear();
    dArray.squeeze();

    d->m_whitePoint = dWhitePoint;

    if (d->continousRotate) {
        d->m_timer->start(frameinterval);
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

    d->frameDelay = (float)(d->elTim.nsecsElapsed() - d->frametime) / 1.0e9;
    d->elTim.restart();
    d->frametime = d->elTim.nsecsElapsed();

    QPainter pai(this);

    if (d->continousRotate) {
        const float currentRotation = (20.0 * d->frameDelay);
        if (currentRotation < 360) {
            // d->yawAngle += currentRotation;
            d->turntableAngle += currentRotation;
        }
        if (d->turntableAngle >= 360) {
            // d->yawAngle -= 360;
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
        // f->glDepthMask(GL_FALSE);
        // f->glDepthFunc(GL_LESS);
    }

    if (d->useSmoothParticle) {
        f->glEnable(GL_POINT_SMOOTH);
    }

    // ...why there's no this function on context...
    // forget it, I set in shader instead
    // glPointSize(d->particleSize);

    // Already taken care of during resizeGL
    // f->glViewport(0, 0, width(), height());

    f->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    QMatrix4x4 persMatrix;
    persMatrix.perspective(d->fov, (width() * devicePixelRatioF()) / (height() * devicePixelRatioF()), 0.0001, 50.0);

    QVector3D camIfYaw{qSin(qDegreesToRadians(d->yawAngle)) * qCos(qDegreesToRadians(d->pitchAngle)) * d->camDistToTarget,
                       qCos(qDegreesToRadians(d->yawAngle)) * qCos(qDegreesToRadians(d->pitchAngle)) * d->camDistToTarget,
                       (qSin(qDegreesToRadians(d->pitchAngle)) * d->camDistToTarget)};

    // QVector3D camPos{d->camPosX,
    //                  (qCos(qDegreesToRadians(d->pitchAngle)) * d->camDistToTarget),
    //                  (qSin(qDegreesToRadians(d->pitchAngle)) * d->camDistToTarget) + d->camPosZ};

    QMatrix4x4 lookMatrix;
    lookMatrix.lookAt(camIfYaw + d->targetPos, d->targetPos, {0, 0, 1});

    QMatrix4x4 modelMatrix;
    modelMatrix.scale(1, 1, 0.5);
    modelMatrix.rotate(d->turntableAngle, {0, 0, 1});

    const QMatrix4x4 totalMatrix = persMatrix * lookMatrix * modelMatrix;

    d->scatterPrg->bind();

    // d->scatterPosVbo->bind();
    // d->scatterPrg->enableAttributeArray("aPosition");
    // d->scatterPrg->setAttributeBuffer("aPosition", GL_FLOAT, 0, 3, 0);

    // d->scatterColVbo->bind();
    // d->scatterPrg->enableAttributeArray("aColor");
    // d->scatterPrg->setAttributeBuffer("aColor", GL_FLOAT, 0, 4, 0);

    if (d->useMaxBlend) {
        d->scatterPrg->setUniformValue("maxMode", true);
    } else {
        d->scatterPrg->setUniformValue("maxMode", false);
    }

    // d->scatterPrg->setUniformValue("mPers", persMatrix);
    // d->scatterPrg->setUniformValue("mLook", lookMatrix);
    // d->scatterPrg->setUniformValue("mModel", modelMatrix);
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

    d->scatterVao->bind();

    f->glDrawArrays(GL_POINTS, 0, maxPartNum);

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
        // d->framesRendered++;

        // if (d->m_frameTimer.elapsed() >= 100) {
        //     d->m_fps = d->framesRendered / ((double)d->m_frameTimer.nsecsElapsed() / 1.0e9);
        //     d->framesRendered = 0;
        //     d->m_frameTimer.restart();
        // }

        d->m_fps = 1.0 / d->frameDelay;

        d->m_labelFont.setPixelSize(14);
        pai.setPen(Qt::lightGray);
        pai.setBrush(QColor(0, 0, 0, 160));
        pai.setFont(d->m_labelFont);

        const QString fpsS =
            QString(
                "Unique colors: %2 | FPS: %1 | Min alpha: %3 | Size: %10 (%14-%15) | %11\nYaw: %4 | Pitch: %9 | FOV: %5 | Dist: %6 | "
                "Target:[%7:%8:%12] | Turntable: %13")
                .arg(QString::number(d->m_fps, 'f', 0),
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
            "(L): Show/hide label\n"
            "(M): Use Max/Lighten blending\n"
            "(N): Toggle mouse navigation on/off\n"
            "(P): Toggle variable particle size\n"
            "(O): Toggle smooth particle\n"
            "(Shift+B): Change background color\n"
            "(F10): Toggle turntable animation\n"
            "(F11): Show fullscreen\n"
            "(F12): Save plot image");

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

    if (d->enableNav) { // WASD
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
    default:
        // do not render
        return;
        break;
    }

    if (d->enableNav && !d->m_timer->isActive()) {
        d->m_timer->start(frameinterval);
        d->elTim.restart();
    }

    doUpdate();
}

void Custom3dChart::keyReleaseEvent(QKeyEvent *event)
{
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
        // setCursor(Qt::OpenHandCursor);
        setCursor(Qt::BlankCursor);
        d->m_lastPos = event->pos();
        // d->m_lastPos = QPoint(width() / 2, height() / 2);
        // d->m_lastPos = {width() / 2, height() / 2};
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
    d->minalpha = 0.0;
    d->yawAngle = 180.0;
    d->turntableAngle = 0.0;
    d->fov = 45;
    d->camDistToTarget = 0.75;
    d->pitchAngle = 0.0;
    d->particleSize = 2.0;
    d->targetPos = QVector3D{0.0, 0.0, 0.25};

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

    menu.addAction(&copyThis);
    menu.addAction(&pasteThis);
    menu.addSeparator();
    menu.addAction(&changeBg);

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

void Custom3dChart::copyState()
{
    QByteArray toClip{"Scatter3DClip:"};
    toClip.append(
        QByteArray::fromRawData(reinterpret_cast<const char *>(&d->useMaxBlend), sizeof(d->useMaxBlend)).toHex());
    toClip.append(
        QByteArray::fromRawData(reinterpret_cast<const char *>(&d->toggleOpaque), sizeof(d->toggleOpaque)).toHex());
    toClip.append(
        QByteArray::fromRawData(reinterpret_cast<const char *>(&d->useVariableSize), sizeof(d->useVariableSize)).toHex());
    toClip.append(
        QByteArray::fromRawData(reinterpret_cast<const char *>(&d->useSmoothParticle), sizeof(d->useSmoothParticle)).toHex());
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

    d->m_clipb->setText(toClip);
}

void Custom3dChart::pasteState()
{
    QString fromClipStr = d->m_clipb->text();
    if (fromClipStr.contains("Scatter3DClip:")) {
        QByteArray fromClip = QByteArray::fromHex(fromClipStr.mid(fromClipStr.indexOf(":") + 1, -1).toUtf8());
        if (fromClip.size() != 44)
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

        d->useMaxBlend = useMaxBlend;
        d->toggleOpaque = toggleOpaque;
        d->useVariableSize = useVariableSize;
        d->useSmoothParticle = useSmoothParticle;
        d->minalpha = std::max(0.0f, std::min(1.0f, minalpha));
        d->yawAngle = std::max(0.0f, std::min(360.0f, yawAngle));
        d->turntableAngle = std::max(0.0f, std::min(360.0f, turntableAngle));
        d->fov = std::max(1.0f, std::min(170.0f, fov));
        d->camDistToTarget = std::max(0.1f, camDistToTarget);
        d->pitchAngle = std::max(-89.99f, std::min(89.99f, pitchAngle));
        d->particleSize = std::max(0.0f, std::min(20.0f, particleSize));

        d->targetPos = targetPos;

        doUpdate();
    }
}
