#include "custom3dchart.h"
#include "shaders_gl.h"
#include "constant_dataset.h"
#include "helper_funcs.h"

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
#include <QOpenGLExtraFunctions>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QOpenGLPaintDevice>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QPaintDevice>
#include <QPainter>
#include <QSurface>
#include <QSurfaceFormat>
#include <QThread>
#include <QTimer>

#include <lcms2.h>

#include <QtMath>
// #include <cmath>
// #include <future>

static constexpr int frameinterval = 2; // frame duration cap
static constexpr size_t absolutemax = 50000000;
static constexpr bool useShaderFile = false;

static constexpr bool useDepthOrdering = true;
static constexpr bool flattenGamut = true;

static constexpr int fpsBufferSize = 5;

// static constexpr int maxPlotModes = 8;
static constexpr int maxPlotModes = 13;

static const float xyz2srgb[9] = {3.2404542, -1.5371385, -0.4985314,
                                  -0.9692660, 1.8760108, 0.0415560,
                                  0.0556434, -0.2040259, 1.0572252};

static const float mainAxes[] = {-1.0, 0.0, 0.0, //X
                                1.0, 0.0, 0.0,
                                0.0, -1.0, 0.0, //Y
                                0.0, 1.0, 0.0,
                                0.0, 0.0, 0.0, //Z
                                0.0, 0.0, 1.0};

static const float mainTickLen = 0.005;
static const float crossLen = 0.01;

class Q_DECL_HIDDEN Custom3dChart::Private
{
public:
    PlotSetting2D plotSetting;

    QVector<QVector3D> vecPosData;
    QVector<QVector4D> vecColData;
    QVector<uint32_t> vecDataOrder;

    QVector3D m_whitePoint;
    QVector<QVector3D> axisTicks;
    QVector<QVector3D> axisGrids;
    QVector<QVector3D> spectralLocus;
    QVector<QVector3D> imageGamut;
    QVector<QVector3D> srgbGamut;

    QVector<QVector3D> adaptedColorChecker76;
    QVector<QVector3D> adaptedColorChecker;
    QVector<QVector3D> adaptedColorCheckerNew;

    QFont m_labelFont;

    size_t arrsize{0};

    quint32 framesRendered{0};
    quint64 frametime{0};
    QScopedPointer<QTimer> m_timer;
    QScopedPointer<QTimer> m_navTimeout{nullptr};
    QElapsedTimer elTim;
    float m_fps{0.0};
    float frameDelay{0.0};
    QVector<float> accumulatedFps;
    int accFpsIdx{0};
    int upscaler{1};
    int upscalerSet{1};

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
    bool useOrtho{true};
    bool useDepthOrder{true};
    bool expDepthOrder{false};
    bool drawAxes{true};
    int clipboardSize{0};
    float minalpha{0.1};
    float fov{45};
    float camDistToTarget{1.3};
    float pitchAngle{90.0};
    float yawAngle{180.0};
    float turntableAngle{0.0};
    float particleSize{1.0};
    float zScale{1.0};
    QVector3D targetPos{0.0, 0.0, 0.25};
    QVector3D resetTargetOrigin{};

    QString modeString{"CIE xyY"};
    int modeInt{-1};
    int ccModeInt{-1};
    int axisModeInt{6};

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

    QScopedPointer<QOpenGLShaderProgram> scatterPrg;
    QScopedPointer<QOpenGLBuffer> scatterPosVbo;
    QScopedPointer<QOpenGLBuffer> scatterColVbo;
    QScopedPointer<QOpenGLBuffer> scatterIdxVbo;
    QScopedPointer<QOpenGLVertexArrayObject> scatterVao;
    bool isValid{true};
    bool isShaderFile{true};

    QScopedPointer<QOpenGLShaderProgram> computePrg;
    QScopedPointer<QOpenGLBuffer> computeOut;
    QScopedPointer<QOpenGLBuffer> computeZBuffer;

    QScopedPointer<QOpenGLShaderProgram> convertPrg;
    QScopedPointer<QOpenGLBuffer> scatterPosVboCvt;

    QScopedPointer<QOpenGLShaderProgram> axisPrg;
    QScopedPointer<QOpenGLBuffer> axisPosVbo;
    QScopedPointer<QOpenGLBuffer> axisTicksVbo;
    QScopedPointer<QOpenGLBuffer> axisGridsVbo;
    QScopedPointer<QOpenGLBuffer> spectralLocusVbo;
    QScopedPointer<QOpenGLBuffer> imageGamutVbo;
    QScopedPointer<QOpenGLBuffer> srgbGamutVbo;
    QScopedPointer<QOpenGLBuffer> adaptedColorChecker76Vbo;
    QScopedPointer<QOpenGLBuffer> adaptedColorCheckerVbo;
    QScopedPointer<QOpenGLBuffer> adaptedColorCheckerNewVbo;
};

QVector<QVector3D> crossAtPos(const QVector3D &pos, float len)
{
    QVector<QVector3D> cross;

    cross.append(QVector3D{-len, 0.0f, 0.0f} + pos);
    cross.append(QVector3D{len, 0.0f, 0.0f} + pos);
    cross.append(QVector3D{0.0f, -len, 0.0f} + pos);
    cross.append(QVector3D{0.0f, len, 0.0f} + pos);
    cross.append(QVector3D{0.0f, 0.0f, -len} + pos);
    cross.append(QVector3D{0.0f, 0.0f, len} + pos);

    return cross;
}

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

    for (int i = -9; i < 10; i++) {
        if (i == 0) continue;
        const float iF = (float)i / 10.0;
        d->axisTicks.append(QVector3D{iF, mainTickLen, 0.0f});
        d->axisTicks.append(QVector3D{iF, -mainTickLen, 0.0f});
        d->axisTicks.append(QVector3D{mainTickLen, iF, 0.0f});
        d->axisTicks.append(QVector3D{-mainTickLen, iF, 0.0f});

        d->axisGrids.append(QVector3D{iF, 1.0f, -0.0001f});
        d->axisGrids.append(QVector3D{iF, -1.0f, -0.0001f});
        d->axisGrids.append(QVector3D{1.0f, iF, -0.0001f});
        d->axisGrids.append(QVector3D{-1.0f, iF, -0.0001f});
    }

    for (int i = 1; i < 10; i++) {
        const float iF = (float)i / 10.0;
        d->axisTicks.append(QVector3D{mainTickLen, 0.0f, iF});
        d->axisTicks.append(QVector3D{-mainTickLen, 0.0f, iF});
    }

    for (int i = 0; i < 81; i++) {
        const float sp[2] = {static_cast<float>(spectral_chromaticity[i][0]),
                             static_cast<float>(spectral_chromaticity[i][1])};
        d->spectralLocus.append(QVector3D{sp[0], sp[1], 0.0f});
    }

    // d->srgbGamut.append(QVector3D{0.6400f, 0.3300f, -0.001f});
    // d->srgbGamut.append(QVector3D{0.3000f, 0.6000f, -0.001f});
    // d->srgbGamut.append(QVector3D{0.1500f, 0.0600f, -0.001f});
    d->srgbGamut.append(getSrgbGamutxyy());

    d->m_timer.reset(new QTimer(this));
    d->m_navTimeout.reset(new QTimer(this));
    d->m_navTimeout->setSingleShot(true);
    d->elTim.start();

    connect(d->m_timer.get(), &QTimer::timeout, this, [&]() {
        doUpdate();
    });

    connect(d->m_navTimeout.get(), &QTimer::timeout, this, [&]() {
        if (!d->enableNav && !d->isMouseHold && !d->m_timer->isActive()) {
            d->useDepthOrder = true;
        }
        doUpdate();
    });
}

Custom3dChart::~Custom3dChart()
{
    qDebug() << "3D plot deleted";
    // makeCurrent();
    // glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    // doneCurrent();
    // context()->deleteLater();
    d.reset();
}

void Custom3dChart::initializeGL()
{
    QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
    QOpenGLExtraFunctions *ef = QOpenGLContext::currentContext()->extraFunctions();
    f->glClearColor(d->bgColor.redF(), d->bgColor.greenF(), d->bgColor.blueF(), d->bgColor.alphaF());
    f->glEnable(GL_PROGRAM_POINT_SIZE);

    qDebug() << "Initializing opengl context with format:";
    qDebug() << format();

    /*
     * --------------------------------------------------
     * Main gl program initiate
     * --------------------------------------------------
     */

    d->scatterPrg.reset(new QOpenGLShaderProgram(context()));
    d->scatterPrg->create();

    reloadShaders();

    if (!d->scatterPrg->link()) {
        qWarning() << "Failed to link shader program!";
        d->isValid = false;
        return;
    }

    d->scatterPrg->bind();

    // main position VBO storage, this one in xyY format
    d->scatterPosVbo.reset(new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer));
    d->scatterPosVbo->create();
    d->scatterPosVbo->bind();
    d->scatterPosVbo->setUsagePattern(QOpenGLBuffer::StaticDraw);
    d->scatterPosVbo->allocate(d->vecPosData.constData(), d->vecPosData.size() * sizeof(QVector3D));

    // VAO for main program
    d->scatterVao.reset(new QOpenGLVertexArrayObject(context()));
    d->scatterVao->create();
    d->scatterVao->bind();

    // secondary position VBO for main program, this one will change depends on plot mode
    d->scatterPosVboCvt.reset(new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer));
    d->scatterPosVboCvt->create();
    d->scatterPosVboCvt->bind();
    d->scatterPosVboCvt->setUsagePattern(QOpenGLBuffer::DynamicDraw);
    d->scatterPosVboCvt->allocate(d->vecPosData.constData(), d->vecPosData.size() * sizeof(QVector3D));
    d->scatterPrg->enableAttributeArray("aPosition");
    d->scatterPrg->setAttributeBuffer("aPosition", GL_FLOAT, 0, 3, 0);

    // color VBO for main program
    d->scatterColVbo.reset(new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer));
    d->scatterColVbo->create();
    d->scatterColVbo->bind();
    d->scatterColVbo->setUsagePattern(QOpenGLBuffer::StaticDraw);
    d->scatterColVbo->allocate(d->vecColData.constData(), d->vecColData.size() * sizeof(QVector4D));
    d->scatterPrg->enableAttributeArray("aColor");
    d->scatterPrg->setAttributeBuffer("aColor", GL_FLOAT, 0, 4, 0);

    d->scatterVao->release();
    d->scatterPrg->release();

    // clear all temporary storages
    d->vecPosData.clear();
    d->vecPosData.squeeze();
    d->vecColData.clear();
    d->vecColData.squeeze();

    /*
     * --------------------------------------------------
     * Depth / Z ordering compute program initiate
     * --------------------------------------------------
     */

    // position index VBO
    d->scatterIdxVbo.reset(new QOpenGLBuffer(QOpenGLBuffer::IndexBuffer));
    d->scatterIdxVbo->create();
    d->scatterIdxVbo->bind();
    d->scatterIdxVbo->setUsagePattern(QOpenGLBuffer::DynamicDraw);
    d->scatterIdxVbo->allocate(d->vecDataOrder.constData(), d->vecDataOrder.size() * sizeof(uint32_t));

    d->computePrg.reset(new QOpenGLShaderProgram(context()));
    d->computePrg->create();
    d->computePrg->addShaderFromSourceCode(QOpenGLShader::Compute, compShader);
    // d->computePrg->link();
    if (!d->computePrg->link()) {
        qWarning() << "Failed to link shader program!";
        d->isValid = false;
        return;
    }
    d->computePrg->bind();

    // bind main program's secondary position VBO as an input to depth program
    d->scatterPosVboCvt->bind();
    ef->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, d->scatterPosVboCvt->bufferId());

    // output position VBO to be used for main program
    d->computeOut.reset(new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer));
    d->computeOut->create();
    d->computeOut->bind();
    ef->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, d->computeOut->bufferId());
    d->computeOut->setUsagePattern(QOpenGLBuffer::DynamicCopy);
    d->computeOut->allocate(d->arrsize * sizeof(QVector4D));

    // separate Z buffer only VBO for sorting, otherwise the memory mapping and sorting will be tanked
    // if output buffer is used (vec4 object vs single float)
    d->computeZBuffer.reset(new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer));
    d->computeZBuffer->create();
    d->computeZBuffer->bind();
    ef->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, d->computeZBuffer->bufferId());
    d->computeZBuffer->setUsagePattern(QOpenGLBuffer::DynamicCopy);
    d->computeZBuffer->allocate(d->arrsize * sizeof(float));

    // d->scatterPosVboCvt->release();
    d->computeOut->release();
    d->computePrg->release();

    /*
     * --------------------------------------------------
     * Grid and axis program initiate
     * --------------------------------------------------
     */

    d->axisPrg.reset(new QOpenGLShaderProgram(context()));
    d->axisPrg->create();
    d->axisPrg->addShaderFromSourceCode(QOpenGLShader::Vertex, monoVertShader);
    d->axisPrg->addShaderFromSourceCode(QOpenGLShader::Fragment, monoFragShader);
    if (!d->axisPrg->link()) {
        qWarning() << "Failed to link shader program!";
        d->isValid = false;
        return;
    }

    // main axis position VBO
    d->axisPosVbo.reset(new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer));
    d->axisPosVbo->create();
    d->axisPosVbo->bind();
    d->axisPosVbo->setUsagePattern(QOpenGLBuffer::StaticDraw);
    d->axisPosVbo->allocate(&mainAxes, sizeof(mainAxes));

    // axis ticks position VBO
    d->axisTicksVbo.reset(new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer));
    d->axisTicksVbo->create();
    d->axisTicksVbo->bind();
    d->axisTicksVbo->setUsagePattern(QOpenGLBuffer::StaticDraw);
    d->axisTicksVbo->allocate(d->axisTicks.constData(), d->axisTicks.size() * sizeof(QVector3D));

    // axis grids position VBO
    d->axisGridsVbo.reset(new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer));
    d->axisGridsVbo->create();
    d->axisGridsVbo->bind();
    d->axisGridsVbo->setUsagePattern(QOpenGLBuffer::StaticDraw);
    d->axisGridsVbo->allocate(d->axisGrids.constData(), d->axisGrids.size() * sizeof(QVector3D));

    // spectral locus position VBO
    d->spectralLocusVbo.reset(new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer));
    d->spectralLocusVbo->create();
    d->spectralLocusVbo->bind();
    d->spectralLocusVbo->setUsagePattern(QOpenGLBuffer::DynamicDraw);
    d->spectralLocusVbo->allocate(d->spectralLocus.constData(), d->spectralLocus.size() * sizeof(QVector3D));

    QVector<QVector3D> imgGamut;
    QVector<QVector3D> srgbGamut;
    foreach (const auto &gmt, d->imageGamut) {
        imgGamut.append(QVector3D{gmt.x(), gmt.y(), flattenGamut ? 0.0f : gmt.z()});
    }
    foreach (const auto &gmt, d->srgbGamut) {
        srgbGamut.append(QVector3D{gmt.x(), gmt.y(), flattenGamut ? -0.0001f : gmt.z()});
    }

    // image gamut position VBO
    d->imageGamutVbo.reset(new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer));
    d->imageGamutVbo->create();
    d->imageGamutVbo->bind();
    d->imageGamutVbo->setUsagePattern(QOpenGLBuffer::DynamicDraw);
    d->imageGamutVbo->allocate(imgGamut.constData(), imgGamut.size() * sizeof(QVector3D));

    // sRGB gamut position VBO
    d->srgbGamutVbo.reset(new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer));
    d->srgbGamutVbo->create();
    d->srgbGamutVbo->bind();
    d->srgbGamutVbo->setUsagePattern(QOpenGLBuffer::DynamicDraw);
    d->srgbGamutVbo->allocate(srgbGamut.constData(), srgbGamut.size() * sizeof(QVector3D));

    // Colorcheckers
    QVector<QVector3D> cc76Cross;
    QVector<QVector3D> ccCross;
    QVector<QVector3D> ccNewCross;
    for (int i = 0; i < d->adaptedColorChecker76.size(); i++) {
        cc76Cross.append(crossAtPos(d->adaptedColorChecker76.at(i), crossLen));
        ccCross.append(crossAtPos(d->adaptedColorChecker.at(i), crossLen));
        ccNewCross.append(crossAtPos(d->adaptedColorCheckerNew.at(i), crossLen));
    }

    // ColorChecker76
    d->adaptedColorChecker76Vbo.reset(new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer));
    d->adaptedColorChecker76Vbo->create();
    d->adaptedColorChecker76Vbo->bind();
    d->adaptedColorChecker76Vbo->setUsagePattern(QOpenGLBuffer::DynamicDraw);
    d->adaptedColorChecker76Vbo->allocate(cc76Cross.constData(), cc76Cross.size() * sizeof(QVector3D));

    // ColorCheckerOld
    d->adaptedColorCheckerVbo.reset(new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer));
    d->adaptedColorCheckerVbo->create();
    d->adaptedColorCheckerVbo->bind();
    d->adaptedColorCheckerVbo->setUsagePattern(QOpenGLBuffer::DynamicDraw);
    d->adaptedColorCheckerVbo->allocate(ccCross.constData(), ccCross.size() * sizeof(QVector3D));

    // ColorCheckerNew
    d->adaptedColorCheckerNewVbo.reset(new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer));
    d->adaptedColorCheckerNewVbo->create();
    d->adaptedColorCheckerNewVbo->bind();
    d->adaptedColorCheckerNewVbo->setUsagePattern(QOpenGLBuffer::DynamicDraw);
    d->adaptedColorCheckerNewVbo->allocate(ccNewCross.constData(), ccNewCross.size() * sizeof(QVector3D));

    /*
     * --------------------------------------------------
     * Plot mode conversion compute program initiate
     * --------------------------------------------------
     */

    d->convertPrg.reset(new QOpenGLShaderProgram(context()));
    d->convertPrg->create();
    d->convertPrg->addShaderFromSourceCode(QOpenGLShader::Compute, conversionShader);
    if (!d->convertPrg->link()) {
        qWarning() << "Failed to link shader program!";
        d->isValid = false;
        return;
    }
}

void Custom3dChart::addDataPoints(QVector<ColorPoint> &dArray, QVector3D &dWhitePoint, QVector<ImageXYZDouble> &dOutGamut)
{
    // using double is a massive performance hit, so let's use float instead.. :3
    foreach (const auto &cp, dArray) {
        QVector3D xyypos{(float)cp.first.X, (float)cp.first.Y, (float)cp.first.Z};
        xyypos = xyypos - QVector3D{dWhitePoint.x(), dWhitePoint.y(), 0.0f};

        d->vecPosData.append(xyypos);
        d->vecColData.append(QVector4D{cp.second.R, cp.second.G, cp.second.B, cp.second.A});
    }
    d->arrsize = dArray.size();

    foreach (const auto &gm, dOutGamut) {
        d->imageGamut.append(QVector3D{static_cast<float>(gm.X), static_cast<float>(gm.Y), static_cast<float>(gm.Z)});
    }

    dArray.clear();
    dArray.squeeze();

    d->m_whitePoint = dWhitePoint;

    d->resetTargetOrigin = QVector3D{d->m_whitePoint.x(), d->m_whitePoint.y(), 0.5f * d->zScale};
    d->targetPos = d->resetTargetOrigin;

    // Depth ordering array
    d->vecDataOrder.resize(d->arrsize);
    std::iota(d->vecDataOrder.begin(), d->vecDataOrder.end(), 0);

    // automatically enable explicit depth ordering
    if (d->arrsize < 10000000) {
        d->expDepthOrder = true;
    } else {
        d->expDepthOrder = false;
    }

    // CC points

    const cmsCIExyY prfWPxyY{d->m_whitePoint.x(), d->m_whitePoint.y(), d->m_whitePoint.z()};
    const cmsCIExyY ccWPxyY76{Macbeth_chart_1976[18][0], Macbeth_chart_1976[18][1], Macbeth_chart_1976[18][2]};

    cmsCIEXYZ prfWPXYZ;
    cmsCIEXYZ ccWPXYZ76;

    cmsxyY2XYZ(&prfWPXYZ, &prfWPxyY);
    cmsxyY2XYZ(&ccWPXYZ76, &ccWPxyY76);

    // calculate ColorChecker points to adapted illuminant
    for (int i = 0; i < 24; i++) {
        // 1976
        const cmsCIExyY srcxyY76{Macbeth_chart_1976[i][0], Macbeth_chart_1976[i][1], Macbeth_chart_1976[i][2]};
        cmsCIEXYZ srcXYZ76;
        cmsCIEXYZ destXYZ76;
        cmsCIExyY destxyY76;

        cmsxyY2XYZ(&srcXYZ76, &srcxyY76);
        cmsAdaptToIlluminant(&destXYZ76, &ccWPXYZ76, &prfWPXYZ, &srcXYZ76);

        cmsXYZ2xyY(&destxyY76, &destXYZ76);
        d->adaptedColorChecker76.append(QVector3D{static_cast<float>(destxyY76.x),
                                                    static_cast<float>(destxyY76.y),
                                                    static_cast<float>(destxyY76.Y)});

        // Pre-Nov2014
        const cmsCIExyY srcxyY{Macbeth_chart_2005[i][0], Macbeth_chart_2005[i][1], Macbeth_chart_2005[i][2]};
        cmsCIEXYZ srcXYZ;
        cmsCIEXYZ destXYZ;
        cmsCIExyY destxyY;

        cmsxyY2XYZ(&srcXYZ, &srcxyY);
        cmsAdaptToIlluminant(&destXYZ, cmsD50_XYZ(), &prfWPXYZ, &srcXYZ);

        cmsXYZ2xyY(&destxyY, &destXYZ);
        d->adaptedColorChecker.append(
            QVector3D{static_cast<float>(destxyY.x), static_cast<float>(destxyY.y), static_cast<float>(destxyY.Y)});

        // Post-Nov2014
        const cmsCIELab srcNewLab{ColorChecker_After_Nov2014_Lab[i][0],
                                  ColorChecker_After_Nov2014_Lab[i][1],
                                  ColorChecker_After_Nov2014_Lab[i][2]};
        cmsCIEXYZ srcXYZNew;
        cmsCIEXYZ dstXYZNew;
        cmsCIExyY dstxyYNew;

        cmsLab2XYZ(cmsD50_XYZ(), &srcXYZNew, &srcNewLab);
        cmsAdaptToIlluminant(&dstXYZNew, cmsD50_XYZ(), &prfWPXYZ, &srcXYZNew);
        cmsXYZ2xyY(&dstxyYNew, &dstXYZNew);

        d->adaptedColorCheckerNew.append(QVector3D{static_cast<float>(dstxyYNew.x),
                                                     static_cast<float>(dstxyYNew.y),
                                                     static_cast<float>(dstxyYNew.Y)});
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
    if (!d->isValid) {
        QPainter pai(this);
        d->m_labelFont.setPixelSize(14);
        pai.setPen(Qt::lightGray);
        pai.setFont(d->m_labelFont);

        pai.drawText(rect(), Qt::AlignCenter, QString("Failed to initialize OpenGL"));

        pai.end();
        return;
    }

    QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
    QOpenGLExtraFunctions *ef = QOpenGLContext::currentContext()->extraFunctions();
    size_t maxPartNum = std::min((size_t)d->arrsize, absolutemax);

    // not quite a robust timer, but I think it's better rather than continously
    // drawing the screen when all of the objects are static.
    d->frameDelay = (double)(d->elTim.nsecsElapsed() - d->frametime) / 1.0e9;
    d->elTim.restart();
    d->frametime = d->elTim.nsecsElapsed();

    QOpenGLPaintDevice fboPaintDev(size() * d->upscaler);
    QPainter pai(&fboPaintDev);

    if (d->continousRotate) {
        const float currentRotation = (20.0 * d->frameDelay);
        if (currentRotation < 360) {
            // d->turntableAngle += currentRotation;
            d->yawAngle += currentRotation;
        }
        if (d->yawAngle >= 360) {
            d->yawAngle -= 360;
        }
        // if (d->turntableAngle >= 360) {
        //     d->turntableAngle -= 360;
        // }
    }

    if (d->enableNav) {
        doNavigation();
    }

    pai.beginNativePainting();

    bool useDepthTest = false;
    bool useOrdering = false;

    // portability reasons?
    f->glClearColor(d->bgColor.redF(), d->bgColor.greenF(), d->bgColor.blueF(), d->bgColor.alphaF());
    f->glEnable(GL_PROGRAM_POINT_SIZE);

    // Enable depth testing when alpha is >=0.9,
    // otherwise enable alpha blending and depth calculation
    if (d->minalpha < 0.9) {
        f->glEnable(GL_BLEND);
        f->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        useDepthTest = false;
        if (d->useMaxBlend) {
            useDepthTest = true;
            f->glBlendEquation(GL_MAX);
        }
    } else {
        f->glEnable(GL_DEPTH_TEST);
        useDepthTest = true;
    }

    f->glEnable(GL_LINE_SMOOTH);
    f->glEnable(GL_POLYGON_SMOOTH);

    if (d->useSmoothParticle) {
        f->glEnable(GL_POINT_SMOOTH);
    }

    f->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const float aspectRatio = (width() * devicePixelRatioF()) / (height() * devicePixelRatioF());

    // Perspective/ortho view matrix
    QMatrix4x4 persMatrix;
    if (!d->useOrtho) {
        persMatrix.perspective(d->fov, aspectRatio, 0.0005f, 50.0f);
    } else {
        persMatrix.ortho(-aspectRatio * d->camDistToTarget / 2.5f,
                         aspectRatio * d->camDistToTarget / 2.5f,
                         -d->camDistToTarget / 2.5f,
                         d->camDistToTarget / 2.5f,
                         -100.0f,
                         100.0f);
    }

    // Camera/target matrix
    QVector3D camPos{(float)(qSin(qDegreesToRadians(d->yawAngle)) * qCos(qDegreesToRadians(d->pitchAngle)) * d->camDistToTarget),
                     (float)(qCos(qDegreesToRadians(d->yawAngle)) * qCos(qDegreesToRadians(d->pitchAngle)) * d->camDistToTarget),
                     (float)((qSin(qDegreesToRadians(d->pitchAngle)) * d->camDistToTarget))};

    QMatrix4x4 lookMatrix;
    if (d->pitchAngle > -90.0 && d->pitchAngle < 90.0) {
        lookMatrix.lookAt(camPos + d->targetPos, d->targetPos, {0.0f, 0.0f, 1.0f});
    } else {
        d->yawAngle = 180.0f;
        if (d->pitchAngle > 0) {
            lookMatrix.lookAt(camPos + d->targetPos, d->targetPos, {0.0f, 1.0f, 0.0f});
        } else {
            lookMatrix.lookAt(camPos + d->targetPos, d->targetPos, {0.0f, -1.0f, 0.0f});
        }
    }

    // Model matrix
    QMatrix4x4 modelMatrix;
    if (d->modeInt == -1) {
        modelMatrix.translate(d->m_whitePoint.x(), d->m_whitePoint.y());
    }
    modelMatrix.scale(1.0f, 1.0f, d->zScale);
    modelMatrix.rotate(d->turntableAngle, {0.0f, 0.0f, 1.0f});

    // precalculated matrix to be sent into gl program
    const QMatrix4x4 intermediateMatrix = persMatrix * lookMatrix;
    const QMatrix4x4 totalMatrix = persMatrix * lookMatrix * modelMatrix;

    const bool shouldDepthOrder = (d->useDepthOrder && !useDepthTest && !d->useMonochrome) && (d->expDepthOrder && !useDepthTest);

    // depth ordering, GPU compute
    if (shouldDepthOrder) {
        useOrdering = true;
        d->computePrg->bind();
        d->scatterPosVboCvt->bind();
        d->computeZBuffer->bind();

        // QElapsedTimer etm;
        // etm.start();
        d->computePrg->setUniformValue("arraySize", (int)d->arrsize);
        d->computePrg->setUniformValue("transformMatrix", totalMatrix);

        ef->glDispatchCompute((d->arrsize + 31) / 32,1,1);
        // ef->glMemoryBarrier(GL_ALL_BARRIER_BITS);
        ef->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        // qDebug() << "compute:" << etm.nsecsElapsed() / 1.0e6 << "ms";

        // map and sorting is a huge performance hit.
        const float *otp = reinterpret_cast<float *>(d->computeZBuffer->map(QOpenGLBuffer::ReadOnly));
        // qDebug() << "map:" << etm.nsecsElapsed() / 1.0e6 << "ms";
        if (otp) {
            parallel_sort(d->vecDataOrder.data(),
                          d->vecDataOrder.size(),
                          std::max((int)100000, (int)d->vecDataOrder.size() / (QThread::idealThreadCount() / 2)),
                          otp);
            // non parallel version
            // std::sort(d->vecDataOrder.begin(), d->vecDataOrder.end(), [&](const uint32_t &lhs, const uint32_t &rhs) {
            //     // NaN check
            //     if (std::isnan(otp[lhs])) return false;
            //     if (std::isnan(otp[rhs])) return true;
            //     return otp[lhs] > otp[rhs];
            // });
        }
        // qDebug() << "sort:" << etm.nsecsElapsed() / 1.0e6 << "ms";
        d->computeZBuffer->unmap();
        d->scatterPosVboCvt->release();
        d->computeZBuffer->release();
        d->computePrg->release();
    }

    // draw axes, grids, and gamut outlines
    if (d->axisModeInt > -1) {
        d->axisPrg->bind();
        d->axisPrg->setUniformValue("mView", intermediateMatrix);

        if (d->axisModeInt >= 3) {
            d->axisPrg->setUniformValue("vColor", QVector4D{0.15f, 0.15f, 0.15f, 1.0f});
            d->axisGridsVbo->bind();
            d->axisPrg->enableAttributeArray("aPosition");
            d->axisPrg->setAttributeBuffer("aPosition", GL_FLOAT, 0, 3, 0);
            f->glDrawArrays(GL_LINES, 0, d->axisGrids.size());
            d->axisGridsVbo->release();

            d->axisPrg->setUniformValue("vColor", QVector4D{0.4f, 0.4f, 0.4f, 1.0f});
            d->axisPosVbo->bind();
            d->axisPrg->enableAttributeArray("aPosition");
            d->axisPrg->setAttributeBuffer("aPosition", GL_FLOAT, 0, 3, 0);
            f->glDrawArrays(GL_LINES, 0, sizeof(mainAxes) / sizeof(float) / 3);
            d->axisPosVbo->release();

            d->axisTicksVbo->bind();
            d->axisPrg->enableAttributeArray("aPosition");
            d->axisPrg->setAttributeBuffer("aPosition", GL_FLOAT, 0, 3, 0);
            f->glDrawArrays(GL_LINES, 0, d->axisTicks.size());
            d->axisTicksVbo->release();
        }

        if (d->modeInt >= -1 && d->modeInt < 5) {
            if (d->modeInt < 2 && (d->axisModeInt == 0 || d->axisModeInt == 2 || d->axisModeInt == 4 || d->axisModeInt == 6)) {
                d->axisPrg->setUniformValue("vColor", QVector4D{0.25f, 0.25f, 0.25f, 1.0f});
                d->spectralLocusVbo->bind();
                d->axisPrg->enableAttributeArray("aPosition");
                d->axisPrg->setAttributeBuffer("aPosition", GL_FLOAT, 0, 3, 0);
                f->glDrawArrays(GL_LINE_LOOP, 0, d->spectralLocus.size());
                d->spectralLocusVbo->release();
            }

            if (d->axisModeInt == 1 || d->axisModeInt == 2 || d->axisModeInt == 5 || d->axisModeInt == 6) {
                d->srgbGamutVbo->bind();
                d->axisPrg->enableAttributeArray("aPosition");
                d->axisPrg->setAttributeBuffer("aPosition", GL_FLOAT, 0, 3, 0);
                f->glDrawArrays(GL_LINE_LOOP, 0, d->srgbGamut.size());
                d->srgbGamutVbo->release();

                d->axisPrg->setUniformValue("vColor", QVector4D{0.4f, 0.0f, 0.0f, 1.0f});
                d->imageGamutVbo->bind();
                d->axisPrg->enableAttributeArray("aPosition");
                d->axisPrg->setAttributeBuffer("aPosition", GL_FLOAT, 0, 3, 0);
                f->glDrawArrays(GL_LINE_LOOP, 0, d->imageGamut.size());
                d->imageGamutVbo->release();
            }
        }

        d->axisPrg->release();
    }

    // finally begin drawing...
    d->scatterPrg->bind();

    if (d->useMaxBlend) {
        d->scatterPrg->setUniformValue("maxMode", true);
    } else {
        d->scatterPrg->setUniformValue("maxMode", false);
    }

    d->scatterPrg->setUniformValue("mView", totalMatrix);
    d->scatterPrg->setUniformValue("bUsePrecalc", shouldDepthOrder ? 1 : 0);

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
    d->scatterPrg->setUniformValue("mModel", modelMatrix);
    d->scatterPrg->setUniformValue("mXyzRgb", QMatrix3x3(xyz2srgb));
    d->scatterPrg->setUniformValue("vWhite", QVector2D(d->m_whitePoint.x(), d->m_whitePoint.y()));

    d->scatterVao->bind();

    if (shouldDepthOrder) {
        // element pipeline depth ordering, berat cuy
        d->computeOut->bind();
        d->scatterPrg->enableAttributeArray("aPositionSub");
        d->scatterPrg->setAttributeBuffer("aPositionSub", GL_FLOAT, 0, 4, 0);
        d->scatterIdxVbo->bind();
        d->scatterIdxVbo->allocate(d->vecDataOrder.constData(), d->vecDataOrder.size() * sizeof(uint32_t));
        f->glDrawElements(GL_POINTS, maxPartNum, GL_UNSIGNED_INT, 0);
        d->scatterIdxVbo->release();
        d->computeOut->release();
    } else {
        // normal pipeline
        f->glDrawArrays(GL_POINTS, 0, maxPartNum);
    }

    d->scatterVao->release();
    d->scatterPrg->release();

    QString ccString{"None"};
    // CC drawing on top of the color data
    if (d->ccModeInt > -1) {
        if (d->modeInt >= -1 && d->modeInt < 5) {
            d->axisPrg->bind();
            d->axisPrg->setUniformValue("mView", intermediateMatrix);

            switch (d->ccModeInt) {
            case 0:
                // CC76
                d->axisPrg->setUniformValue("vColor", QVector4D{0.0f, 0.8f, 0.8f, 1.0f});
                d->adaptedColorChecker76Vbo->bind();
                d->axisPrg->enableAttributeArray("aPosition");
                d->axisPrg->setAttributeBuffer("aPosition", GL_FLOAT, 0, 3, 0);
                f->glDrawArrays(GL_LINES, 0, d->adaptedColorChecker76.size() * 6);
                d->adaptedColorChecker76Vbo->release();

                ccString = "Classic 1976";
                break;
            case 1:
                // CCOld
                d->axisPrg->setUniformValue("vColor", QVector4D{0.8f, 0.8f, 0.0f, 1.0f});
                d->adaptedColorCheckerVbo->bind();
                d->axisPrg->enableAttributeArray("aPosition");
                d->axisPrg->setAttributeBuffer("aPosition", GL_FLOAT, 0, 3, 0);
                f->glDrawArrays(GL_LINES, 0, d->adaptedColorChecker.size() * 6);
                d->adaptedColorCheckerVbo->release();

                ccString = "Pre Nov 2014";
                break;
            case 2:
                // CCNew
                d->axisPrg->setUniformValue("vColor", QVector4D{0.8f, 0.8f, 0.8f, 1.0f});
                d->adaptedColorCheckerNewVbo->bind();
                d->axisPrg->enableAttributeArray("aPosition");
                d->axisPrg->setAttributeBuffer("aPosition", GL_FLOAT, 0, 3, 0);
                f->glDrawArrays(GL_LINES, 0, d->adaptedColorCheckerNew.size() * 6);
                d->adaptedColorCheckerNewVbo->release();

                ccString = "Post Nov 2014";
                break;
            default:
                break;
            }

            d->axisPrg->release();
        }
    }

    // some of these can break non-native painting when keep enabled...
    f->glDisable(GL_BLEND);
    f->glDisable(GL_DEPTH_TEST);
    f->glDisable(GL_POINT_SMOOTH);
    f->glDisable(GL_LINE_SMOOTH);
    f->glDisable(GL_POLYGON_SMOOTH);

    pai.endNativePainting();

    // placeholder, blending broke when there's nothing to draw after native painting...
    pai.setPen(Qt::transparent);
    pai.setBrush(Qt::transparent);
    pai.drawRect(0,0,1,1);

// Text rendering after native painting is absolutely broken in 5.15 for unknown reasons..
// using QImage painting will tank performance, but it is what it is for 5.15..
// So this is a bit of hack to draw the UIs in QImage first and then paint it to the FBO.
#if QT_VERSION < QT_VERSION_CHECK(6, 2, 0)
    QImage uiPix(size() * d->upscaler, QImage::Format_ARGB32);
    QPainter uiPtr(&uiPix);
#endif

    const QRect calcRect(0, 0, width() * d->upscaler, height() * d->upscaler);

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
#if QT_VERSION < QT_VERSION_CHECK(6, 2, 0)
        uiPtr.setPen(Qt::lightGray);
        uiPtr.setBrush(QColor(0, 0, 0, 160));
        uiPtr.setFont(d->m_labelFont);
#else
        pai.setPen(Qt::lightGray);
        pai.setBrush(QColor(0, 0, 0, 160));
        pai.setFont(d->m_labelFont);
#endif

        const QString fpsS =
            QString(
                "%15 | Colors: %2 | FPS: %1 | Alpha: %3 | Size: %10 (%13-%14) | CC: %17\n"
                "Yaw: %4 | Pitch: %9 | FOV: %5 | D: %6 | T:[%7:%8:%12] | %11 | Z-order: %16")
                .arg(QString::number(d->m_fps, 'f', 2),
                     (maxPartNum == absolutemax) ? QString("%1(capped)").arg(QString::number(maxPartNum))
                                                 : QString::number(maxPartNum),
                     QString::number(d->minalpha, 'f', 3),
                     QString::number(d->yawAngle, 'f', 2),
                     d->useOrtho ? QString("ortho") : QString::number(d->fov, 'f', 2),
                     QString::number(d->camDistToTarget, 'f', 3),
                     QString::number(d->targetPos.x(), 'f', 3),
                     QString::number(d->targetPos.y(), 'f', 3),
                     QString::number(d->pitchAngle, 'f', 2),
                     QString::number(d->particleSize, 'f', 1),
                     d->useMaxBlend ? QString("Max blending") : QString("Alpha blending"),
                     QString::number(d->targetPos.z() / d->zScale, 'f', 3),
                     d->useVariableSize ? QString("var") : QString("sta"),
                     d->useSmoothParticle ? QString("rnd") : QString("sqr"),
                     d->modeString,
                     useOrdering ? QString("on") : QString("off"),
                     ccString);

        QRect boundRect;
        const QMargins lblMargin(8, 8, 8, 8);
        const QMargins lblBorder(5, 5, 5, 5);
#if QT_VERSION < QT_VERSION_CHECK(6, 2, 0)
        uiPtr.drawText(calcRect - lblMargin, Qt::AlignBottom | Qt::AlignLeft, fpsS, &boundRect);
        uiPtr.setPen(Qt::NoPen);
        boundRect += lblBorder;
        uiPtr.drawRect(boundRect);
        uiPtr.setPen(Qt::lightGray);
        uiPtr.drawText(calcRect - lblMargin, Qt::AlignBottom | Qt::AlignLeft, fpsS);
#else
        pai.drawText(calcRect - lblMargin, Qt::AlignBottom | Qt::AlignLeft, fpsS, &boundRect);
        pai.setPen(Qt::NoPen);
        boundRect += lblBorder;
        pai.drawRect(boundRect);
        pai.setPen(Qt::lightGray);
        pai.drawText(calcRect - lblMargin, Qt::AlignBottom | Qt::AlignLeft, fpsS);
#endif
    }

    if (d->showHelp) {
        d->m_labelFont.setPixelSize(14);
#if QT_VERSION < QT_VERSION_CHECK(6, 2, 0)
        uiPtr.setPen(Qt::lightGray);
        uiPtr.setBrush(QColor(0, 0, 0, 160));
        uiPtr.setFont(d->m_labelFont);
#else
        pai.setPen(Qt::lightGray);
        pai.setBrush(QColor(0, 0, 0, 160));
        pai.setFont(d->m_labelFont);
#endif

        const QString fpsS = QString(
            "(F1): Show/hide this help\n"
            "Note: depth buffer is not used with min alpha < 0.9,\n"
            "therefore the depth rendering will get glitched\n"
            "if depth ordering is disabled.\n"
            "Max blending is useful to contrast the frequent colors.\n"
            "Shader source: %1\n"
            "-------------------\n"
            "(Wheel): Zoom\n"
            "(LMB): Pan\n"
            "(MMB/Shift+LMB): Rotate/orbit\n"
            "(WASD): Walk through\n"
            "(C/V): Move target down/up\n"
            "(Shift+wheel|[/]): Decrease/increase FOV\n"
            "(Q): Toggle alpha 1.0 - 0.1\n"
            "(Z/X): Decrease/increase particle size\n"
            "(-/=): Decrease/increase minimum alpha\n"
            "(F): Reset target\n"
            "(Shift+R): Reset view\n"
            "(T): Toggle orthogonal / perspective\n"
            "(K): Toggle color/monochrome\n"
            "(L): Show/hide label\n"
            "(M): Use Max/Lighten blending\n"
            "(N): Toggle mouse navigation on/off\n"
            "(P): Toggle variable particle size\n"
            "(O): Toggle smooth particle\n"
            "(Shift+B): Change background color\n"
            "(F2): Previous plot mode\n"
            "(F3): Next plot mode\n"
            "(F4): Cycle draw axis and gamut outline\n"
            "(F5): Cycle ColorChecker display\n"
            "(F7): Toggle depth ordering (may heavy)\n"
            "(F10): Toggle turntable animation\n"
            "(F11): Show fullscreen\n"
            "(F12): Save plot image").arg(d->isShaderFile ? "File" : "Internal");

        QRect boundRect;
        const QMargins lblMargin(8, 8, 8, 8);
        const QMargins lblBorder(5, 5, 5, 5);
#if QT_VERSION < QT_VERSION_CHECK(6, 2, 0)
        uiPtr.drawText(calcRect - lblMargin, Qt::AlignTop | Qt::AlignLeft, fpsS, &boundRect);
        uiPtr.setPen(Qt::NoPen);
        boundRect += lblBorder;
        uiPtr.drawRect(boundRect);
        uiPtr.setPen(Qt::lightGray);
        uiPtr.drawText(calcRect - lblMargin, Qt::AlignTop | Qt::AlignLeft, fpsS);
#else
        pai.drawText(calcRect - lblMargin, Qt::AlignTop | Qt::AlignLeft, fpsS, &boundRect);
        pai.setPen(Qt::NoPen);
        boundRect += lblBorder;
        pai.drawRect(boundRect);
        pai.setPen(Qt::lightGray);
        pai.drawText(calcRect - lblMargin, Qt::AlignTop | Qt::AlignLeft, fpsS);
#endif
    }

#if QT_VERSION < QT_VERSION_CHECK(6, 2, 0)
    uiPtr.end();
    pai.drawImage(0, 0, uiPix);
#endif

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
    d->useDepthOrder = false;
    d->m_navTimeout->start(200);
    doUpdate();
    QOpenGLWidget::resizeEvent(event);
}

void Custom3dChart::keyPressEvent(QKeyEvent *event)
{
    if (!d->isValid) return;

    const float shiftMultp = d->isShiftHold ? 10.0f : 1.0f;

    switch (event->key()) {
    case Qt::Key_Minus:
        d->minalpha -= 0.002f * shiftMultp;
        d->minalpha = std::max(0.0f, d->minalpha);
        break;
    case Qt::Key_Equal:
        d->minalpha += 0.002f * shiftMultp;
        d->minalpha = std::min(1.0f, d->minalpha);
        break;
    case Qt::Key_BracketLeft:
        if (!d->useOrtho) {
            d->fov += 1.0f * shiftMultp;
            d->fov = std::min(170.0f, d->fov);
        }
        break;
    case Qt::Key_BracketRight:
        if (!d->useOrtho) {
            d->fov -= 1.0f * shiftMultp;
            d->fov = std::max(1.0f, d->fov);
        }
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
    case Qt::Key_F:
        d->targetPos = d->resetTargetOrigin;
        break;
    case Qt::Key_C:
        if (event->modifiers() == Qt::ControlModifier) {
            copyState();
        } else {
            d->enableNav = true;
            d->nDown = true;
        }
        break;
    case Qt::Key_V:
        if (event->modifiers() == Qt::ControlModifier) {
            pasteState();
        } else {
            d->enableNav = true;
            d->nUp = true;
        }
        break;
    case Qt::Key_R:
        if (d->isShiftHold) {
            resetCamera();
        } else {
            d->turntableAngle = 0.0f;
        }
        break;
    case Qt::Key_T:
        d->useOrtho = !d->useOrtho;
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
            d->minalpha = 1.0f;
        } else {
            d->minalpha = 0.1f;
        }
        break;
    case Qt::Key_X:
        if (d->isShiftHold) {
            d->particleSize += 1.0f;
        } else {
            d->particleSize += 0.1f;
        }
        d->particleSize = std::min(20.0f, d->particleSize);
        break;
    case Qt::Key_Z:
        if (d->isShiftHold) {
            d->particleSize -= 1.0f;
        } else {
            d->particleSize -= 0.1f;
        }
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
    case Qt::Key_F2:
        d->modeInt--;
        if (d->modeInt < -1)
            d->modeInt = maxPlotModes;
        cycleModes();
        break;
    case Qt::Key_F3:
        d->modeInt++;
        if (d->modeInt > maxPlotModes)
            d->modeInt = -1;
        cycleModes();
        break;
    case Qt::Key_F4:
        // d->drawAxes = !d->drawAxes;
        d->axisModeInt++;
        if (d->axisModeInt > 6) {
            d->axisModeInt = -1;
        }
        break;
    case Qt::Key_F5:
        // reloadShaders();
        d->ccModeInt++;
        if (d->ccModeInt > 2) {
            d->ccModeInt = -1;
        }
        break;
    case Qt::Key_F7:
        d->expDepthOrder = !d->expDepthOrder;
        break;
    case Qt::Key_F10:
        if (d->continousRotate) {
            d->continousRotate = false;
            d->useDepthOrder = true;
            if (!d->enableNav) {
                d->m_timer->stop();
            }
        } else {
            d->continousRotate = true;
            d->useDepthOrder = false;
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
        d->useDepthOrder = false;
        d->m_timer->start(frameinterval);
        d->elTim.restart();
    }

    doUpdate();
}

void Custom3dChart::keyReleaseEvent(QKeyEvent *event)
{
    if (!d->isValid) return;

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
        d->useDepthOrder = true;
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
    if (!d->isValid) return;

    if (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton) {
        d->elTim.restart();
        setCursor(Qt::BlankCursor);
        d->m_lastPos = event->pos();
    }

    // if (event->buttons() == Qt::RightButton) {
    //     d->useDepthOrder = false;
    // }
}

void Custom3dChart::mouseMoveEvent(QMouseEvent *event)
{
    if (!d->isValid) return;

    const float orbitSpeedDivider = d->isShiftHold ? 10.0f : 30.0f;
    if (event->buttons() & Qt::MiddleButton || d->enableMouseNav) {
        setCursor(Qt::BlankCursor);
        if (!d->enableMouseNav) {
            d->isMouseHold = true;
        }
        d->useDepthOrder = false;
        if (d->enableMouseNav && !d->enableNav) {
            d->m_navTimeout->start(100);
        }

        // if (d->useOrtho) {
        //     d->useOrtho = false;
        // }

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
        d->pitchAngle = std::min(90.0f, std::max(-90.0f, d->pitchAngle));

        if (d->yawAngle >= 360) {
            d->yawAngle = 0;
        } else if (d->yawAngle < 0) {
            d->yawAngle = 360;
        }

        doUpdate();
    }

    if (event->buttons() & Qt::LeftButton) {
        // setCursor(Qt::OpenHandCursor);
        if (!d->enableMouseNav) {
            d->isMouseHold = true;
        }
        d->useDepthOrder = false;

        if (d->isShiftHold) {
            setCursor(Qt::BlankCursor);
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

            d->yawAngle += offsetX / 15.0f;

            d->pitchAngle += offsetY / 15.0f;
            d->pitchAngle = std::min(90.0f, std::max(-90.0f, d->pitchAngle));

            if (d->yawAngle >= 360) {
                d->yawAngle = 0;
            } else if (d->yawAngle < 0) {
                d->yawAngle = 360;
            }
        } else {
            setCursor(Qt::OpenHandCursor);
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
        }

        doUpdate();
    }
}

void Custom3dChart::mouseReleaseEvent(QMouseEvent *event)
{
    if (!d->isValid) return;
    d->useDepthOrder = true;

    setCursor(Qt::ArrowCursor);
    d->isMouseHold = false;
    doUpdate();
}

void Custom3dChart::wheelEvent(QWheelEvent *event)
{
    if (!d->isValid) return;
    const QPoint numDegrees = event->angleDelta();
    const double zoomIncrement = (numDegrees.y() / 1200.0f);

    d->useDepthOrder = false;
    d->m_navTimeout->start(200);

    if (!d->isShiftHold) {
        if (d->camDistToTarget > 0.0001) {
            d->camDistToTarget -= zoomIncrement * d->camDistToTarget;

            d->camDistToTarget = std::max(0.005f, d->camDistToTarget);
            doUpdate();
        }
    } else {
        if (!d->useOrtho) {
            if (zoomIncrement < 0) {
                d->fov += 5.0f;
                d->fov = std::min(170.0f, d->fov);
                doUpdate();
            } else if (zoomIncrement > 0) {
                d->fov -= 5.0f;
                d->fov = std::max(1.0f, d->fov);
                doUpdate();
            }
        }
    }
}

void Custom3dChart::cycleModes(const bool &changeTarget)
{
    if (QOpenGLContext::currentContext() != context() && context()->isValid()) {
        makeCurrent();
    }

    [[maybe_unused]]
    QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
    QOpenGLExtraFunctions *ef = QOpenGLContext::currentContext()->extraFunctions();

    d->convertPrg->bind();

    d->scatterPosVbo->bind();
    ef->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, d->scatterPosVbo->bufferId());

    d->scatterPosVboCvt->bind();
    ef->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, d->scatterPosVboCvt->bufferId());

    d->convertPrg->setUniformValue("arraySize", (int)d->arrsize);
    d->convertPrg->setUniformValue("vWhite", d->m_whitePoint);
    d->convertPrg->setUniformValue("iMode", d->modeInt);

    ef->glDispatchCompute((d->arrsize + 31) / 32,1,1);
    ef->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    d->scatterPosVbo->release();
    d->scatterPosVboCvt->release();
    d->convertPrg->release();

    switch (d->modeInt) {
    case 0: {
        d->modeString = QString("CIE 1960 UCS Yuv");

        const QVector3D wp = xyyToUCS(d->m_whitePoint, d->m_whitePoint, UCS_1960_YUV);
        QVector<QVector3D> imgGamut;
        QVector<QVector3D> srgbGamut;
        QVector<QVector3D> spectralLocus;
        foreach (const auto &gmt, d->imageGamut) {
            const QVector3D gm(xyyToUCS(gmt, d->m_whitePoint, UCS_1960_YUV));
            imgGamut.append(QVector3D{gm.x(), gm.y(), flattenGamut ? 0.0f : gm.z()});
        }
        foreach (const auto &gmt, d->srgbGamut) {
            const QVector3D gm(xyyToUCS(gmt, d->m_whitePoint, UCS_1960_YUV));
            srgbGamut.append(QVector3D{gm.x(), gm.y(), flattenGamut ? -0.0001f : gm.z()});
        }
        foreach (const auto &lcs, d->spectralLocus) {
            spectralLocus.append(xyToUv(lcs));
        }
        d->imageGamutVbo->bind();
        d->imageGamutVbo->allocate(imgGamut.constData(), imgGamut.size() * sizeof(QVector3D));
        d->imageGamutVbo->release();
        d->srgbGamutVbo->bind();
        d->srgbGamutVbo->allocate(srgbGamut.constData(), srgbGamut.size() * sizeof(QVector3D));
        d->srgbGamutVbo->release();
        d->spectralLocusVbo->bind();
        d->spectralLocusVbo->allocate(spectralLocus.constData(), spectralLocus.size() * sizeof(QVector3D));
        d->spectralLocusVbo->release();

        QVector<QVector3D> cc76Cross;
        QVector<QVector3D> ccCross;
        QVector<QVector3D> ccNewCross;
        for (int i = 0; i < d->adaptedColorChecker76.size(); i++) {
            cc76Cross.append(crossAtPos(xyyToUCS(d->adaptedColorChecker76.at(i), d->m_whitePoint, UCS_1960_YUV), crossLen));
            ccCross.append(crossAtPos(xyyToUCS(d->adaptedColorChecker.at(i), d->m_whitePoint, UCS_1960_YUV), crossLen));
            ccNewCross.append(crossAtPos(xyyToUCS(d->adaptedColorCheckerNew.at(i), d->m_whitePoint, UCS_1960_YUV), crossLen));
        }

        d->adaptedColorChecker76Vbo->bind();
        d->adaptedColorChecker76Vbo->allocate(cc76Cross.constData(), cc76Cross.size() * sizeof(QVector3D));
        d->adaptedColorChecker76Vbo->release();
        d->adaptedColorCheckerVbo->bind();
        d->adaptedColorCheckerVbo->allocate(ccCross.constData(), ccCross.size() * sizeof(QVector3D));
        d->adaptedColorCheckerVbo->release();
        d->adaptedColorCheckerNewVbo->bind();
        d->adaptedColorCheckerNewVbo->allocate(ccNewCross.constData(), ccNewCross.size() * sizeof(QVector3D));
        d->adaptedColorCheckerNewVbo->release();

        d->resetTargetOrigin = QVector3D{wp.x(), wp.y(), 0.5f * d->zScale};
    } break;
    case 1: {
        d->modeString = QString("CIE 1976 UCS L'u'v' (L' = 0.01x)");
        const QVector3D wp = xyyToUCS(d->m_whitePoint, d->m_whitePoint, UCS_1976_LUV);

        QVector<QVector3D> imgGamut;
        QVector<QVector3D> srgbGamut;
        QVector<QVector3D> spectralLocus;
        foreach (const auto &gmt, d->imageGamut) {
            const QVector3D gm(xyyToUCS(gmt, d->m_whitePoint, UCS_1976_LUV));
            imgGamut.append(QVector3D{gm.x(), gm.y(), flattenGamut ? 0.0f : gm.z()});
        }
        foreach (const auto &gmt, d->srgbGamut) {
            const QVector3D gm(xyyToUCS(gmt, d->m_whitePoint, UCS_1976_LUV));
            srgbGamut.append(QVector3D{gm.x(), gm.y(), flattenGamut ? -0.0001f : gm.z()});
        }
        foreach (const auto &lcs, d->spectralLocus) {
            spectralLocus.append(xyToUrvr(lcs, d->m_whitePoint));
        }
        d->imageGamutVbo->bind();
        d->imageGamutVbo->allocate(imgGamut.constData(), imgGamut.size() * sizeof(QVector3D));
        d->imageGamutVbo->release();
        d->srgbGamutVbo->bind();
        d->srgbGamutVbo->allocate(srgbGamut.constData(), srgbGamut.size() * sizeof(QVector3D));
        d->srgbGamutVbo->release();
        d->spectralLocusVbo->bind();
        d->spectralLocusVbo->allocate(spectralLocus.constData(), spectralLocus.size() * sizeof(QVector3D));
        d->spectralLocusVbo->release();

        QVector<QVector3D> cc76Cross;
        QVector<QVector3D> ccCross;
        QVector<QVector3D> ccNewCross;
        for (int i = 0; i < d->adaptedColorChecker76.size(); i++) {
            cc76Cross.append(crossAtPos(xyyToUCS(d->adaptedColorChecker76.at(i), d->m_whitePoint, UCS_1976_LUV), crossLen));
            ccCross.append(crossAtPos(xyyToUCS(d->adaptedColorChecker.at(i), d->m_whitePoint, UCS_1976_LUV), crossLen));
            ccNewCross.append(crossAtPos(xyyToUCS(d->adaptedColorCheckerNew.at(i), d->m_whitePoint, UCS_1976_LUV), crossLen));
        }

        d->adaptedColorChecker76Vbo->bind();
        d->adaptedColorChecker76Vbo->allocate(cc76Cross.constData(), cc76Cross.size() * sizeof(QVector3D));
        d->adaptedColorChecker76Vbo->release();
        d->adaptedColorCheckerVbo->bind();
        d->adaptedColorCheckerVbo->allocate(ccCross.constData(), ccCross.size() * sizeof(QVector3D));
        d->adaptedColorCheckerVbo->release();
        d->adaptedColorCheckerNewVbo->bind();
        d->adaptedColorCheckerNewVbo->allocate(ccNewCross.constData(), ccNewCross.size() * sizeof(QVector3D));
        d->adaptedColorCheckerNewVbo->release();

        d->resetTargetOrigin = QVector3D{wp.x(), wp.y(), 0.5f * d->zScale};
    } break;
    case 2: {
        d->modeString = QString("CIE 1976 L*u*v* (0.01x)");

        QVector<QVector3D> imgGamut;
        QVector<QVector3D> srgbGamut;
        foreach (const auto &gmt, d->imageGamut) {
            const QVector3D gm(xyyToUCS(gmt, d->m_whitePoint, UCS_1976_LUV_STAR));
            imgGamut.append(QVector3D{gm.x(), gm.y(), flattenGamut ? 0.0f : gm.z()});
        }
        foreach (const auto &gmt, d->srgbGamut) {
            const QVector3D gm(xyyToUCS(gmt, d->m_whitePoint, UCS_1976_LUV_STAR));
            srgbGamut.append(QVector3D{gm.x(), gm.y(), flattenGamut ? -0.0001f : gm.z()});
        }
        d->imageGamutVbo->bind();
        d->imageGamutVbo->allocate(imgGamut.constData(), imgGamut.size() * sizeof(QVector3D));
        d->imageGamutVbo->release();
        d->srgbGamutVbo->bind();
        d->srgbGamutVbo->allocate(srgbGamut.constData(), srgbGamut.size() * sizeof(QVector3D));
        d->srgbGamutVbo->release();

        QVector<QVector3D> cc76Cross;
        QVector<QVector3D> ccCross;
        QVector<QVector3D> ccNewCross;
        for (int i = 0; i < d->adaptedColorChecker76.size(); i++) {
            cc76Cross.append(crossAtPos(xyyToUCS(d->adaptedColorChecker76.at(i), d->m_whitePoint, UCS_1976_LUV_STAR), crossLen));
            ccCross.append(crossAtPos(xyyToUCS(d->adaptedColorChecker.at(i), d->m_whitePoint, UCS_1976_LUV_STAR), crossLen));
            ccNewCross.append(crossAtPos(xyyToUCS(d->adaptedColorCheckerNew.at(i), d->m_whitePoint, UCS_1976_LUV_STAR), crossLen));
        }

        d->adaptedColorChecker76Vbo->bind();
        d->adaptedColorChecker76Vbo->allocate(cc76Cross.constData(), cc76Cross.size() * sizeof(QVector3D));
        d->adaptedColorChecker76Vbo->release();
        d->adaptedColorCheckerVbo->bind();
        d->adaptedColorCheckerVbo->allocate(ccCross.constData(), ccCross.size() * sizeof(QVector3D));
        d->adaptedColorCheckerVbo->release();
        d->adaptedColorCheckerNewVbo->bind();
        d->adaptedColorCheckerNewVbo->allocate(ccNewCross.constData(), ccNewCross.size() * sizeof(QVector3D));
        d->adaptedColorCheckerNewVbo->release();

        d->resetTargetOrigin = QVector3D{0.0f, 0.0f, 0.5f * d->zScale};
    } break;
    case 3: {
        d->modeString = QString("CIE L*a*b* (0.01x)");

        QVector<QVector3D> imgGamut;
        QVector<QVector3D> srgbGamut;
        foreach (const auto &gmt, d->imageGamut) {
            const QVector3D gm(xyyToLab(gmt, d->m_whitePoint));
            imgGamut.append(QVector3D{gm.x(), gm.y(), flattenGamut ? 0.0f : gm.z()});
        }
        foreach (const auto &gmt, d->srgbGamut) {
            const QVector3D gm(xyyToLab(gmt, d->m_whitePoint));
            srgbGamut.append(QVector3D{gm.x(), gm.y(), flattenGamut ? -0.0001f : gm.z()});
        }
        d->imageGamutVbo->bind();
        d->imageGamutVbo->allocate(imgGamut.constData(), imgGamut.size() * sizeof(QVector3D));
        d->imageGamutVbo->release();
        d->srgbGamutVbo->bind();
        d->srgbGamutVbo->allocate(srgbGamut.constData(), srgbGamut.size() * sizeof(QVector3D));
        d->srgbGamutVbo->release();

        QVector<QVector3D> cc76Cross;
        QVector<QVector3D> ccCross;
        QVector<QVector3D> ccNewCross;
        for (int i = 0; i < d->adaptedColorChecker76.size(); i++) {
            cc76Cross.append(crossAtPos(xyyToLab(d->adaptedColorChecker76.at(i), d->m_whitePoint), crossLen));
            ccCross.append(crossAtPos(xyyToLab(d->adaptedColorChecker.at(i), d->m_whitePoint), crossLen));
            ccNewCross.append(crossAtPos(xyyToLab(d->adaptedColorCheckerNew.at(i), d->m_whitePoint), crossLen));
        }

        d->adaptedColorChecker76Vbo->bind();
        d->adaptedColorChecker76Vbo->allocate(cc76Cross.constData(), cc76Cross.size() * sizeof(QVector3D));
        d->adaptedColorChecker76Vbo->release();
        d->adaptedColorCheckerVbo->bind();
        d->adaptedColorCheckerVbo->allocate(ccCross.constData(), ccCross.size() * sizeof(QVector3D));
        d->adaptedColorCheckerVbo->release();
        d->adaptedColorCheckerNewVbo->bind();
        d->adaptedColorCheckerNewVbo->allocate(ccNewCross.constData(), ccNewCross.size() * sizeof(QVector3D));
        d->adaptedColorCheckerNewVbo->release();

        d->resetTargetOrigin = QVector3D{0.0f, 0.0f, 0.5f * d->zScale};
    } break;
    case 4: {
        d->modeString = QString("Oklab");

        QVector<QVector3D> imgGamut;
        QVector<QVector3D> srgbGamut;
        foreach (const auto &gmt, d->imageGamut) {
            const QVector3D gm(xyyToOklab(gmt, d->m_whitePoint));
            imgGamut.append(QVector3D{gm.x(), gm.y(), flattenGamut ? 0.0f : gm.z()});
        }
        foreach (const auto &gmt, d->srgbGamut) {
            const QVector3D gm(xyyToOklab(gmt, d->m_whitePoint));
            srgbGamut.append(QVector3D{gm.x(), gm.y(), flattenGamut ? -0.0001f : gm.z()});
        }
        d->imageGamutVbo->bind();
        d->imageGamutVbo->allocate(imgGamut.constData(), imgGamut.size() * sizeof(QVector3D));
        d->imageGamutVbo->release();
        d->srgbGamutVbo->bind();
        d->srgbGamutVbo->allocate(srgbGamut.constData(), srgbGamut.size() * sizeof(QVector3D));
        d->srgbGamutVbo->release();

        QVector<QVector3D> cc76Cross;
        QVector<QVector3D> ccCross;
        QVector<QVector3D> ccNewCross;
        for (int i = 0; i < d->adaptedColorChecker76.size(); i++) {
            cc76Cross.append(crossAtPos(xyyToOklab(d->adaptedColorChecker76.at(i), d->m_whitePoint), crossLen));
            ccCross.append(crossAtPos(xyyToOklab(d->adaptedColorChecker.at(i), d->m_whitePoint), crossLen));
            ccNewCross.append(crossAtPos(xyyToOklab(d->adaptedColorCheckerNew.at(i), d->m_whitePoint), crossLen));
        }

        d->adaptedColorChecker76Vbo->bind();
        d->adaptedColorChecker76Vbo->allocate(cc76Cross.constData(), cc76Cross.size() * sizeof(QVector3D));
        d->adaptedColorChecker76Vbo->release();
        d->adaptedColorCheckerVbo->bind();
        d->adaptedColorCheckerVbo->allocate(ccCross.constData(), ccCross.size() * sizeof(QVector3D));
        d->adaptedColorCheckerVbo->release();
        d->adaptedColorCheckerNewVbo->bind();
        d->adaptedColorCheckerNewVbo->allocate(ccNewCross.constData(), ccNewCross.size() * sizeof(QVector3D));
        d->adaptedColorCheckerNewVbo->release();

        d->resetTargetOrigin = QVector3D{0.0f, 0.0f, 0.5f * d->zScale};
    } break;
    case 5:
        d->modeString = QString("CIE XYZ");
        d->resetTargetOrigin = QVector3D{0.5f, 0.5f, 0.5f * d->zScale};
        break;
    case 6:
        d->modeString = QString("sRGB - Linear");
        d->resetTargetOrigin = QVector3D{0.5f, 0.5f, 0.5f * d->zScale};
        break;
    case 7:
        d->modeString = QString("sRGB - Gamma 2.2");
        d->resetTargetOrigin = QVector3D{0.5f, 0.5f, 0.5f * d->zScale};
        break;
    case 8:
        d->modeString = QString("sRGB - sRGB TRC");
        d->resetTargetOrigin = QVector3D{0.5f, 0.5f, 0.5f * d->zScale};
        break;
    case 9:
        d->modeString = QString("LMS - CAT02");
        d->resetTargetOrigin = QVector3D{0.5f, 0.5f, 0.5f * d->zScale};
        break;
    case 10:
        d->modeString = QString("LMS - E");
        d->resetTargetOrigin = QVector3D{0.5f, 0.5f, 0.5f * d->zScale};
        break;
    case 11:
        d->modeString = QString("LMS - D65");
        d->resetTargetOrigin = QVector3D{0.5f, 0.5f, 0.5f * d->zScale};
        break;
    case 12:
        d->modeString = QString("LMS - Phys. CMFs");
        d->resetTargetOrigin = QVector3D{0.5f, 0.5f, 0.5f * d->zScale};
        break;
    case 13:
        d->modeString = QString("XYB - D65 (0.5x)");
        d->resetTargetOrigin = QVector3D{0.0f, 0.25f, 0.5f * d->zScale};
        break;
    case -1: {
        d->modeString = QString("CIE 1931 xyY");

        QVector<QVector3D> imgGamut;
        QVector<QVector3D> srgbGamut;
        foreach (const auto &gmt, d->imageGamut) {
            imgGamut.append(QVector3D{gmt.x(), gmt.y(), flattenGamut ? 0.0f : gmt.z()});
        }
        foreach (const auto &gmt, d->srgbGamut) {
            srgbGamut.append(QVector3D{gmt.x(), gmt.y(), flattenGamut ? -0.0001f : gmt.z()});
        }

        d->imageGamutVbo->bind();
        d->imageGamutVbo->allocate(imgGamut.constData(), imgGamut.size() * sizeof(QVector3D));
        d->imageGamutVbo->release();
        d->srgbGamutVbo->bind();
        d->srgbGamutVbo->allocate(srgbGamut.constData(), srgbGamut.size() * sizeof(QVector3D));
        d->srgbGamutVbo->release();
        d->spectralLocusVbo->bind();
        d->spectralLocusVbo->allocate(d->spectralLocus.constData(), d->spectralLocus.size() * sizeof(QVector3D));
        d->spectralLocusVbo->release();

        QVector<QVector3D> cc76Cross;
        QVector<QVector3D> ccCross;
        QVector<QVector3D> ccNewCross;
        for (int i = 0; i < d->adaptedColorChecker76.size(); i++) {
            cc76Cross.append(crossAtPos(d->adaptedColorChecker76.at(i), crossLen));
            ccCross.append(crossAtPos(d->adaptedColorChecker.at(i), crossLen));
            ccNewCross.append(crossAtPos(d->adaptedColorCheckerNew.at(i), crossLen));
        }

        d->adaptedColorChecker76Vbo->bind();
        d->adaptedColorChecker76Vbo->allocate(cc76Cross.constData(), cc76Cross.size() * sizeof(QVector3D));
        d->adaptedColorChecker76Vbo->release();
        d->adaptedColorCheckerVbo->bind();
        d->adaptedColorCheckerVbo->allocate(ccCross.constData(), ccCross.size() * sizeof(QVector3D));
        d->adaptedColorCheckerVbo->release();
        d->adaptedColorCheckerNewVbo->bind();
        d->adaptedColorCheckerNewVbo->allocate(ccNewCross.constData(), ccNewCross.size() * sizeof(QVector3D));
        d->adaptedColorCheckerNewVbo->release();

        d->resetTargetOrigin = QVector3D{d->m_whitePoint.x(), d->m_whitePoint.y(), 0.5f * d->zScale};
    } break;
    default:
        d->modeString = QString("User defined %1").arg(QString::number(d->modeInt + 2));
        d->resetTargetOrigin = QVector3D{0.0f, 0.0f, 0.5f * d->zScale};
        break;
    }
    if (changeTarget) {
        d->targetPos = d->resetTargetOrigin;
    }
}

void Custom3dChart::resetCamera()
{
    if (d->continousRotate) {
        d->continousRotate = false;
        if (!d->enableNav) {
            d->m_timer->stop();
        }
    }
    d->useMaxBlend = false;
    d->toggleOpaque = false;
    d->useVariableSize = false;
    d->useSmoothParticle = true;
    d->useMonochrome = false;
    d->useOrtho = true;
    d->drawAxes = true;
    d->minalpha = 0.1f;
    d->yawAngle = 180.0f;
    d->turntableAngle = 0.0f;
    d->fov = 45.0f;
    d->camDistToTarget = 1.3f;
    d->pitchAngle = 90.0f;
    d->particleSize = 1.0f;
    d->targetPos = d->resetTargetOrigin;
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
    if (!d->isValid) return;

    d->useDepthOrder = false;

    QMenu menu(this);

    QAction copyThis(this);
    copyThis.setText("Copy plot state (Ctrl+C)");
    connect(&copyThis, &QAction::triggered, this, [&]() {
        copyState();
    });

    QAction pasteThis(this);
    pasteThis.setText("Paste plot state (Ctrl+V)");
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

    QAction setUpscaler(this);
    setUpscaler.setText("Set save render scaling...");
    connect(&setUpscaler, &QAction::triggered, this, [&]() {
        changeUpscaler();
    });

    menu.addAction(&copyThis);
    menu.addAction(&pasteThis);
    menu.addSeparator();
    menu.addAction(&changeBg);
    menu.addAction(&changeMono);
    menu.addSeparator();
    menu.addAction(&setUpscaler);

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

        d->useDepthOrder = true;

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

        d->useDepthOrder = true;

        doUpdate();
    }
}

void Custom3dChart::changeUpscaler()
{
    if (d->isShiftHold) {
        d->isShiftHold = false;
    }
    const int setUpscaler =
        QInputDialog::getInt(this, "Set upscale", "Set upscale times for image saving", d->upscalerSet, 1, 8);

    d->upscalerSet = setUpscaler;

    d->useDepthOrder = true;

    doUpdate();
}

QImage Custom3dChart::takeTheShot()
{
    d->upscaler = d->upscalerSet;
    makeCurrent();

    d->useDepthOrder = true;

    QOpenGLFramebufferObjectFormat format;
    format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
    if (d->plotSetting.multisample3d > 0) {
        format.setSamples(d->plotSetting.multisample3d);
    }
    format.setInternalTextureFormat(GL_RGB);
    QOpenGLFramebufferObject fbo(size() * d->upscaler, format);

    fbo.bind();

    paintGL();

    QImage fboImage(fbo.toImage());

    fbo.release();
    fbo.bindDefault();

    doneCurrent();
    d->upscaler = 1;

    return fboImage;
}

void Custom3dChart::copyState()
{
    QByteArray headClip{"Scatter3DClip:"};
    QByteArray toClip;
    toClip.append(
        QByteArray::fromRawData(reinterpret_cast<const char *>(&d->useMaxBlend), sizeof(d->useMaxBlend)));
    toClip.append(
        QByteArray::fromRawData(reinterpret_cast<const char *>(&d->toggleOpaque), sizeof(d->toggleOpaque)));
    toClip.append(
        QByteArray::fromRawData(reinterpret_cast<const char *>(&d->useVariableSize), sizeof(d->useVariableSize)));
    toClip.append(
        QByteArray::fromRawData(reinterpret_cast<const char *>(&d->useSmoothParticle), sizeof(d->useSmoothParticle)));
    toClip.append(
        QByteArray::fromRawData(reinterpret_cast<const char *>(&d->useMonochrome), sizeof(d->useMonochrome)));
    toClip.append(
        QByteArray::fromRawData(reinterpret_cast<const char *>(&d->minalpha), sizeof(d->minalpha)));
    toClip.append(
        QByteArray::fromRawData(reinterpret_cast<const char *>(&d->yawAngle), sizeof(d->yawAngle)));
    toClip.append(
        QByteArray::fromRawData(reinterpret_cast<const char *>(&d->turntableAngle), sizeof(d->turntableAngle)));
    toClip.append(
        QByteArray::fromRawData(reinterpret_cast<const char *>(&d->fov), sizeof(d->fov)));
    toClip.append(
        QByteArray::fromRawData(reinterpret_cast<const char *>(&d->camDistToTarget), sizeof(d->camDistToTarget)));
    toClip.append(
        QByteArray::fromRawData(reinterpret_cast<const char *>(&d->pitchAngle), sizeof(d->pitchAngle)));
    toClip.append(
        QByteArray::fromRawData(reinterpret_cast<const char *>(&d->particleSize), sizeof(d->particleSize)));
    toClip.append(
        QByteArray::fromRawData(reinterpret_cast<const char *>(&d->targetPos), sizeof(d->targetPos)));
    toClip.append(
        QByteArray::fromRawData(reinterpret_cast<const char *>(&d->monoColor), sizeof(d->monoColor)));
    toClip.append(
        QByteArray::fromRawData(reinterpret_cast<const char *>(&d->bgColor), sizeof(d->bgColor)));
    toClip.append(
        QByteArray::fromRawData(reinterpret_cast<const char *>(&d->useOrtho), sizeof(d->useOrtho)));
    toClip.append(
        QByteArray::fromRawData(reinterpret_cast<const char *>(&d->drawAxes), sizeof(d->drawAxes)));
    toClip.append(
        QByteArray::fromRawData(reinterpret_cast<const char *>(&d->modeInt), sizeof(d->modeInt)));

    headClip.append(toClip.toBase64());

    d->m_clipb->setText(headClip);

    d->useDepthOrder = true;
    doUpdate();
}

void Custom3dChart::pasteState()
{
    QString fromClipStr = d->m_clipb->text();
    if (fromClipStr.contains("Scatter3DClip:")) {
        QByteArray fromClip = QByteArray::fromBase64(fromClipStr.mid(fromClipStr.indexOf(":") + 1, -1).toUtf8());
        const int bufferSize =
            (sizeof(bool) * 7) + (sizeof(float) * 7) + sizeof(QVector3D) + (sizeof(QColor) * 2) + (sizeof(int) * 1);
        if (fromClip.size() != bufferSize)
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
        clipPointer += sizeof(QColor);
        const bool useOrtho = *reinterpret_cast<const bool *>(clipPointer);;
        clipPointer += sizeof(bool);
        const bool drawAxes = *reinterpret_cast<const bool *>(clipPointer);;
        clipPointer += sizeof(bool);
        const int modeInt = *reinterpret_cast<const int *>(clipPointer);;

        d->useMaxBlend = useMaxBlend;
        d->toggleOpaque = toggleOpaque;
        d->useVariableSize = useVariableSize;
        d->useSmoothParticle = useSmoothParticle;
        d->useMonochrome = useMonochrome;
        d->minalpha = std::max(0.0f, std::min(1.0f, minalpha));
        d->yawAngle = std::max(0.0f, std::min(360.0f, yawAngle));
        d->turntableAngle = std::max(0.0f, std::min(360.0f, turntableAngle));
        d->fov = std::max(1.0f, std::min(170.0f, fov));
        d->camDistToTarget = std::max(0.001f, camDistToTarget);
        d->pitchAngle = std::max(-90.0f, std::min(90.0f, pitchAngle));
        d->particleSize = std::max(0.0f, std::min(20.0f, particleSize));
        d->targetPos = targetPos;
        d->monoColor = monoColor;
        d->useOrtho = useOrtho;
        d->drawAxes = drawAxes;
        d->modeInt = std::max(-1, std::min(5, modeInt));
        if (d->bgColor != bgColor) {
            d->bgColor = bgColor;
            makeCurrent();
            QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
            f->glClearColor(d->bgColor.redF(), d->bgColor.greenF(), d->bgColor.blueF(), d->bgColor.alphaF());
            doneCurrent();
        }

        d->useDepthOrder = true;

        cycleModes(false);
        doUpdate();
    }
}
