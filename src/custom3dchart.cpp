#include "custom3dchart.h"

#include <QAction>
#include <QDebug>
#include <QElapsedTimer>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QOpenGLPaintDevice>
#include <QOpenGLShaderProgram>
#include <QPaintDevice>
#include <QPainter>
#include <QSurface>
#include <QSurfaceFormat>
#include <QTimer>
#include <QMenu>
#include <QInputDialog>
#include <QColorDialog>

#include <QtMath>

static constexpr int frameinterval = 5;
static constexpr float minAlpha = 0.03;
static constexpr size_t absolutemax = 20000000;

static constexpr char vertShader[] = {
    "#version 330 core\n"
    "layout (location = 0) in vec3 aPosition;\n"
    "layout (location = 1) in vec4 aColor;\n\n"

    "out vec4 vertexColor;\n"

    "uniform mat4 mLook;\n"
    "uniform mat4 mPers;\n"
    "uniform mat4 mModel;\n\n"

    "void main()\n"
    "{\n"
    "    gl_Position = mPers * mLook * mModel * vec4(aPosition, 1.0f);\n"
    "    vertexColor = aColor;\n"
    "}\n\0"};

static constexpr char fragShader[] = {
    "#version 330 core\n"
    "out vec4 FragColor;\n\n"

    "in vec4 vertexColor;\n\n"

    "uniform bool maxMode;\n"
    "uniform float minAlpha;\n\n"

    "void main()\n"
    "{\n"
    "    if (maxMode) {\n"
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

    bool continousRotate{false};
    bool doZoom{false};
    bool doTranslate{false};
    bool doRotate{false};
    bool showLabel{true};
    bool showHelp{false};
    bool toggleOpaque{false};
    bool useMaxBlend{false};
    float minalpha{0.0};
    float fov{45};
    float camPosZ{0.25};
    float camPosX{0.0};
    float camDistToTarget{1.0};
    float pitchAngle{0.0};
    float yawAngle{180.0};
    float turntableAngle{0.0};
    float particleSize{2.0};
    QVector3D translateVal{0, 0, 0};
    QVector3D rotateVal{0, 0, 0};
    QVector3D targetPos{0.0, 0.0, 0.25};

    QColor bgColor{16, 16, 16};

    float zoomBuffer{0.0};
    QVector3D translateBuffer{0, 0, 0};
    QVector3D rotateBuffer{0, 0, 0};

    QPoint m_lastPos{0, 0};
    bool isMouseHold{false};
    bool isShiftHold{false};
    double m_offsetX{0.0};
    double m_offsetY{0.0};

    QOpenGLShaderProgram *program;
    QOpenGLBuffer *posBuffer;
    QOpenGLBuffer *colBuffer;
    bool isValid{true};
};

Custom3dChart::Custom3dChart(QWidget *parent)
    : QOpenGLWidget(parent)
    , d(new Private)
{
    setMinimumSize(10, 10);
    setFocusPolicy(Qt::ClickFocus);
    d->m_labelFont = QFont("Courier New", 11, QFont::Medium);
    d->m_labelFont.setPixelSize(14);

    QSurfaceFormat fmt;
    // fmt.setVersion(4,0);
    // fmt.setColorSpace(QColorSpace::SRgbLinear);
    fmt.setSamples(16);
    // fmt.setRedBufferSize(16);
    // fmt.setGreenBufferSize(16);
    // fmt.setBlueBufferSize(16);
    // fmt.setDepthBufferSize(48);
    // fmt.setStencilBufferSize(16);
    // fmt.setProfile(QSurfaceFormat::CompatibilityProfile);
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
    // qDebug() << format();

    // f->glEnable(GL_BLEND);
    // f->glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    // f->glBlendEquation(GL_MAX);

    // qDebug() << context()->functions();

    glEnable(GL_POINT_SMOOTH);
    glPointSize(3);

    d->program = new QOpenGLShaderProgram(context());
    d->program->create();

    // if (!d->program->addShaderFromSourceFile(QOpenGLShader::Vertex, "./vertex.glsl")) {
    //     qWarning() << "Failed to load vertex shader!";
    //     d->isValid = false;
    // }
    // if (!d->program->addShaderFromSourceFile(QOpenGLShader::Fragment, "./fragment.glsl")) {
    //     qWarning() << "Failed to load fragment shader!";
    //     d->isValid = false;
    // }

    if (!d->program->addShaderFromSourceCode(QOpenGLShader::Vertex, vertShader)) {
        qWarning() << "Failed to load vertex shader!";
        d->isValid = false;
    }
    if (!d->program->addShaderFromSourceCode(QOpenGLShader::Fragment, fragShader)) {
        qWarning() << "Failed to load fragment shader!";
        d->isValid = false;
    }

    if (!d->program->link()) {
        qWarning() << "Failed to link shader program!";
        d->isValid = false;
    }

    d->program->bind();

    qDebug() << d->program->uniformLocation("maxMode");

    d->posBuffer = new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
    d->posBuffer->create();
    d->posBuffer->bind();
    d->posBuffer->setUsagePattern(QOpenGLBuffer::StaticDraw);
    d->posBuffer->allocate(d->m_vecpos.constData(), d->m_vecpos.size());

    d->colBuffer = new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
    d->colBuffer->create();
    d->colBuffer->bind();
    d->colBuffer->setUsagePattern(QOpenGLBuffer::StaticDraw);
    d->colBuffer->allocate(d->m_veccol.constData(), d->m_veccol.size());

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

    QPainter pai(this);

    if (d->continousRotate) {
        const float currentRotation = (20.0 * ((float)(d->elTim.nsecsElapsed() - d->frametime) / 1.0e9));
        if (currentRotation < 360) {
            // d->yawAngle += currentRotation;
            d->turntableAngle += currentRotation;
        }
        if (d->turntableAngle >= 360) {
            // d->yawAngle -= 360;
            d->turntableAngle -= 360;
        }
        d->frametime = d->elTim.nsecsElapsed();
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

    if (d->particleSize > 0) {
        f->glEnable(GL_POINT_SMOOTH);
    }
    // ...why there's no this function on context...
    glPointSize(d->particleSize);

    // Already taken care of during resizeGL
    // f->glViewport(0, 0, width(), height());

    f->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    QMatrix4x4 persMatrix;
    persMatrix.perspective(d->fov, (width() * devicePixelRatioF()) / (height() * devicePixelRatioF()), 0.001, 100.0);

    QVector3D camIfYaw{qSin(qDegreesToRadians(d->yawAngle)) * qCos(qDegreesToRadians(d->pitchAngle)) * d->camDistToTarget,
                       qCos(qDegreesToRadians(d->yawAngle)) * qCos(qDegreesToRadians(d->pitchAngle)) * d->camDistToTarget,
                       (qSin(qDegreesToRadians(d->pitchAngle)) * d->camDistToTarget)};

    QVector3D camPos{d->camPosX,
                     (qCos(qDegreesToRadians(d->pitchAngle)) * d->camDistToTarget),
                     (qSin(qDegreesToRadians(d->pitchAngle)) * d->camDistToTarget) + d->camPosZ};
    // QVector3D targetPos{0, d->camPosX, d->camPosZ};

    QMatrix4x4 lookMatrix;
    lookMatrix.lookAt(camIfYaw + d->targetPos, d->targetPos, {0, 0, 1});

    QMatrix4x4 modelMatrix;
    modelMatrix.scale(1, 1, 0.5);
    modelMatrix.rotate(d->turntableAngle, {0, 0, 1});

    d->program->bind();

    d->posBuffer->bind();
    d->program->enableAttributeArray("aPosition");
    d->program->setAttributeBuffer("aPosition", GL_FLOAT, 0, 3, 0);

    d->colBuffer->bind();
    d->program->enableAttributeArray("aColor");
    d->program->setAttributeBuffer("aColor", GL_FLOAT, 0, 4, 0);

    if (d->useMaxBlend) {
        d->program->setUniformValue("maxMode", true);
    } else {
        d->program->setUniformValue("maxMode", false);
    }
    d->program->setUniformValue("mPers", persMatrix);
    d->program->setUniformValue("mLook", lookMatrix);
    d->program->setUniformValue("mModel", modelMatrix);
    d->program->setUniformValue("minAlpha", d->minalpha);

    f->glDrawArrays(GL_POINTS, 0, maxPartNum);

    f->glDisable(GL_BLEND);
    f->glDisable(GL_DEPTH_TEST);
    f->glDisable(GL_POINT_SMOOTH);

    pai.endNativePainting();

    // placeholder, blending broke when there's nothing to draw after native painting...
    pai.setPen(Qt::transparent);
    pai.setBrush(Qt::transparent);
    pai.drawEllipse(0,0,1,1);

    if (d->showLabel) {
        d->framesRendered++;

        if (d->m_frameTimer.elapsed() >= 100) {
            d->m_fps = d->framesRendered / ((double)d->m_frameTimer.nsecsElapsed() / 1.0e9);
            d->framesRendered = 0;
            d->m_frameTimer.restart();
        }

        d->m_labelFont.setPixelSize(14);
        pai.setPen(Qt::lightGray);
        pai.setBrush(QColor(0, 0, 0, 160));
        pai.setFont(d->m_labelFont);

        const QString fpsS =
            QString(
                "Unique colors: %2 | FPS: %1 | Min alpha: %3 | Size: %10 | %11\nYaw: %4 | Pitch: %9 | FOV: %5 | Dist: %6 | "
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
                     QString::number(d->particleSize, 'f', 0),
                     d->useMaxBlend ? QString("Max blending") : QString("Alpha blending"),
                     QString::number(d->targetPos.z(), 'f', 3),
                     QString::number(d->turntableAngle, 'f', 2));

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
            "(Shift+LMB): Pan\n"
            "(WASD): Walk through\n"
            "(C/V): Move target down/up\n"
            "(Shift+wheel|[/]): Decrease/increase FOV\n"
            "(Q): Toggle alpha 1.0 - 0.0\n"
            "(Z/X): Decrease/increase particle size\n"
            "(-/=): Decrease/increase minimum alpha\n"
            "(R): Reset camera\n"
            "(L): Show/hide label\n"
            "(M): Use Max/Lighten blending\n"
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
        d->camPosZ += 0.01 * d->camDistToTarget;
        // d->targetPos.setZ(d->targetPos.z() + (0.01 * d->camDistToTarget));
        {
            QVector3D camIfYaw{
                qSin(qDegreesToRadians(d->yawAngle)) * qCos(qDegreesToRadians(d->pitchAngle)) * d->camDistToTarget,
                qCos(qDegreesToRadians(d->yawAngle)) * qCos(qDegreesToRadians(d->pitchAngle)) * d->camDistToTarget,
                (qSin(qDegreesToRadians(d->pitchAngle)) * d->camDistToTarget)};

            d->targetPos = d->targetPos - (camIfYaw * 0.01 * shiftMultp);
        }

        break;
    case Qt::Key_S:
        d->camPosZ -= 0.01 * d->camDistToTarget;
        // d->targetPos.setZ(d->targetPos.z() - (0.01 * d->camDistToTarget));
        {
            QVector3D camIfYaw{
                qSin(qDegreesToRadians(d->yawAngle)) * qCos(qDegreesToRadians(d->pitchAngle)) * d->camDistToTarget,
                qCos(qDegreesToRadians(d->yawAngle)) * qCos(qDegreesToRadians(d->pitchAngle)) * d->camDistToTarget,
                (qSin(qDegreesToRadians(d->pitchAngle)) * d->camDistToTarget)};

            d->targetPos = d->targetPos + (camIfYaw * 0.01 * shiftMultp);
        }
        break;
    case Qt::Key_A:
        d->camPosX -= 0.01 * d->camDistToTarget;
        // d->targetPos.setX(d->targetPos.x() - (0.01 * d->camDistToTarget));
        {
            QVector3D camIfYaw{qCos(qDegreesToRadians(d->yawAngle)) * d->camDistToTarget,
                               qSin(qDegreesToRadians(d->yawAngle)) * d->camDistToTarget * -1.0f,
                               0.0};

            d->targetPos = d->targetPos + (camIfYaw * 0.01 * shiftMultp);
        }
        break;
    case Qt::Key_D:
        d->camPosX += 0.01 * d->camDistToTarget;
        // d->targetPos.setX(d->targetPos.x() + (0.01 * d->camDistToTarget));
        {
            QVector3D camIfYaw{qCos(qDegreesToRadians(d->yawAngle)) * d->camDistToTarget,
                               qSin(qDegreesToRadians(d->yawAngle)) * d->camDistToTarget * -1.0f,
                               0.0};

            d->targetPos = d->targetPos - (camIfYaw * 0.01 * shiftMultp);
        }
        break;
    case Qt::Key_C:
        d->targetPos.setZ(d->targetPos.z() - (0.01 * d->camDistToTarget * shiftMultp));
        break;
    case Qt::Key_V:
        d->targetPos.setZ(d->targetPos.z() + (0.01 * d->camDistToTarget * shiftMultp));
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
        d->particleSize += 1.0;
        d->particleSize = std::min(10.0f, d->particleSize);
        break;
    case Qt::Key_Z:
        d->particleSize -= 1.0;
        d->particleSize = std::max(0.0f, d->particleSize);
        break;
    case Qt::Key_M:
        d->useMaxBlend = !d->useMaxBlend;
        break;
    case Qt::Key_B:
        if (d->isShiftHold) {
            changeBgColor();
        }
        break;
    case Qt::Key_F1:
        d->showHelp = !d->showHelp;
        break;
    case Qt::Key_F10:
        if (d->continousRotate) {
            d->continousRotate = false;
            d->m_timer->stop();
        } else {
            d->continousRotate = true;
            d->m_timer->start(frameinterval);
            d->elTim.restart();
        }
        break;
    case Qt::Key_Shift:
        setCursor(Qt::OpenHandCursor);
        d->isShiftHold = true;
        break;
    default:
        // do not render
        return;
        break;
    }

    doUpdate();
}

void Custom3dChart::keyReleaseEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Shift:
        setCursor(Qt::ArrowCursor);
        d->isShiftHold = false;
        break;
    default:
        break;
    }
}

void Custom3dChart::passKeypres(QKeyEvent *e)
{
    keyPressEvent(e);
}

void Custom3dChart::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton || event->button() == Qt::RightButton) {
        setCursor(Qt::OpenHandCursor);
        d->m_lastPos = event->pos();
    }
}

void Custom3dChart::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton) {
        if (!d->isShiftHold) {
            setCursor(Qt::ClosedHandCursor);
            // d->isMouseHold = true;

            const QPoint delposs(event->pos() - d->m_lastPos);
            d->m_lastPos = event->pos();

            d->m_offsetX = delposs.x();
            d->m_offsetY = delposs.y();

            d->yawAngle += d->m_offsetX / 5.0;

            d->pitchAngle += d->m_offsetY / 5.0;
            d->pitchAngle = std::min(89.9f, std::max(-89.9f, d->pitchAngle));

            if (d->yawAngle >= 360) {
                d->yawAngle = 0;
            } else if (d->yawAngle < 0) {
                d->yawAngle = 360;
            }
        } else {
            setCursor(Qt::ClosedHandCursor);
            // d->isMouseHold = true;

            const QPoint delposs(event->pos() - d->m_lastPos);
            d->m_lastPos = event->pos();

            d->m_offsetX = delposs.x();
            d->m_offsetY = delposs.y();

            // did I just spent my whole day to reinvent the wheel...
            const float offsetX = (d->m_offsetX / 1500.0) * d->camDistToTarget;
            const float offsetY = (d->m_offsetY / 1500.0) * d->camDistToTarget;

            const float sinYaw = qSin(qDegreesToRadians(d->yawAngle));
            const float cosYaw = qCos(qDegreesToRadians(d->yawAngle));
            const float sinPitch = qSin(qDegreesToRadians(d->pitchAngle));
            const float cosPitch = qCos(qDegreesToRadians(d->pitchAngle));

            d->camPosZ += (d->m_offsetY / 1500.0) * d->camDistToTarget;
            d->camPosX -= (d->m_offsetX / 1500.0) * d->camDistToTarget;

            QVector3D camIfYaw{((offsetX * cosYaw) + (offsetY * -1.0f * sinYaw * sinPitch)),
                               ((offsetX * -1.0f * sinYaw) + (offsetY * -1.0f * cosYaw * sinPitch)),
                               offsetY * cosPitch};

            d->targetPos = d->targetPos + camIfYaw;

            // d->targetPos.setZ(d->targetPos.z() + ((d->m_offsetY / 1500.0) * d->camDistToTarget));
            // d->targetPos.setX(d->targetPos.x() - ((d->m_offsetX / 1500.0) * d->camDistToTarget));
        }

        doUpdate();
    }

    // if (event->buttons() & Qt::RightButton) {
    //     setCursor(Qt::ClosedHandCursor);
    //     // d->isMouseHold = true;

    //     const QPoint delposs(event->pos() - d->m_lastPos);
    //     d->m_lastPos = event->pos();

    //     d->m_offsetX = delposs.x();
    //     d->m_offsetY = delposs.y();

    //     d->camPosZ += (d->m_offsetY / 1500.0) * d->camDistToTarget;
    //     d->camPosX -= (d->m_offsetX / 1500.0) * d->camDistToTarget;

    //     doUpdate();
    // }
}

void Custom3dChart::mouseReleaseEvent(QMouseEvent *event)
{
    setCursor(Qt::ArrowCursor);
    // if ((event->button() == Qt::LeftButton) && d->isMouseHold) {
    //     d->isMouseHold = false;
    //     doUpdate();
    // }
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
    d->minalpha = 0.0;
    d->yawAngle = 180.0;
    d->turntableAngle = 0.0;
    d->fov = 45;
    d->camPosZ = 0.25;
    d->camPosX = 0.0;
    d->camDistToTarget = 1.0;
    d->pitchAngle = 0.0;
    d->particleSize = 2.0;
    d->targetPos = QVector3D{0.0, 0.0, 0.25};

    doUpdate();
}

void Custom3dChart::contextMenuEvent(QContextMenuEvent *event)
{
    // reserved, only works on non-fullscreen...
    // QMenu menu(this);

    // QAction changeBg(this);
    // changeBg.setText("Change background color...");
    // connect(&changeBg, &QAction::triggered, this, [&]() {
    //     changeBgColor();
    // });

    // menu.addAction(&changeBg);

    // menu.exec(event->globalPos());
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
