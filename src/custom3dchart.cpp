#include "custom3dchart.h"
#include "shaders_gl.h"
#include "constant_dataset.h"
#include "helper_funcs.h"
#include "camera3dsettingdialog.h"

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

#include <QMessageBox>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>

#include <QtMath>

static constexpr int frameinterval = 2; // frame duration cap
static constexpr size_t absolutemax = 50000000;

// debug only
static constexpr bool useShaderFile = false;
static constexpr bool useDepthOrdering = true;
static constexpr bool flattenGamut = false;

static constexpr int fpsBufferSize = 5;

static constexpr int maximumPlotModes = 19;

static constexpr float mainAxes[] = {-1.0, 0.0, 0.0, //X
                                    1.0, 0.0, 0.0,
                                    0.0, -1.0, 0.0, //Y
                                    0.0, 1.0, 0.0,
                                    0.0, 0.0, 0.0, //Z
                                    0.0, 0.0, 1.0};

static constexpr float mainTickLen = 0.005;
static constexpr float crossLen = 0.01;

static constexpr char userDefinedShaderFile[] = "./shaders/3dpoint-userplot.comp";
static constexpr char userDefinedShaderTextFile[] = "./shaders/3dpoint-userplotnames.txt";

class Q_DECL_HIDDEN Custom3dChart::Private
{
public:
    PlotSetting2D plotSetting;
    PlotSetting3D pState;

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
    bool useDepthOrder{true};
    bool expDepthOrder{false};

    QVector3D resetTargetOrigin{};
    QString modeString{"CIE 1931 xyY"};
    int maxPlotModes{maximumPlotModes};

    QClipboard *m_clipb;

    QPoint m_lastPos{0, 0};
    bool isMouseHold{false};
    bool isShiftHold{false};

    bool enableNav{false};
    bool nForward{false};
    bool nBackward{false};
    bool nStrifeLeft{false};
    bool nStrifeRight{false};
    bool nUp{false};
    bool nDown{false};
    bool nZoomIn{false};
    bool nZoomOut{false};
    bool nPitchUp{false};
    bool nPitchDown{false};
    bool nYawRight{false};
    bool nYawLeft{false};

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
    QScopedPointer<QOpenGLBuffer> imageGamutVboIn;
    QScopedPointer<QOpenGLBuffer> imageGamutVboOut;
    QScopedPointer<QOpenGLBuffer> srgbGamutVboIn;
    QScopedPointer<QOpenGLBuffer> srgbGamutVboOut;
    QScopedPointer<QOpenGLBuffer> adaptedColorChecker76Vbo;
    QScopedPointer<QOpenGLBuffer> adaptedColorChecker76VboOut;
    QScopedPointer<QOpenGLBuffer> adaptedColorCheckerVbo;
    QScopedPointer<QOpenGLBuffer> adaptedColorCheckerVboOut;
    QScopedPointer<QOpenGLBuffer> adaptedColorCheckerNewVbo;
    QScopedPointer<QOpenGLBuffer> adaptedColorCheckerNewVboOut;

    // debug only
    // QScopedPointer<QOpenGLBuffer> testVboIn;
    // QScopedPointer<QOpenGLBuffer> testVboOut;

    QScopedPointer<QOpenGLShader> userDefinedShader;
    QScopedPointer<QFileSystemWatcher> shaderFileWatcher;

    QByteArray userDefinedShaderRaw;
    QByteArray userDefinedShaderRawText;
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

    d->srgbGamut.append(getSrgbGamutxyy());

    d->m_timer.reset(new QTimer(this));
    d->m_navTimeout.reset(new QTimer(this));
    d->m_navTimeout->setSingleShot(true);
    d->elTim.start();

    d->shaderFileWatcher.reset(new QFileSystemWatcher(this));

    connect(d->shaderFileWatcher.get(), &QFileSystemWatcher::fileChanged, this, [&](const QString &path) {
        if (!d->shaderFileWatcher->files().contains(path)) {
            d->shaderFileWatcher->addPath(path);
        }
        qDebug() << "Reloading:" << path;
        if (path == userDefinedShaderFile) {
            QFileInfo udsfinfo(path);
            // cap filesize (approx 5MB)
            if (udsfinfo.exists() && (udsfinfo.size() < 5 * 1024 * 1024 && udsfinfo.size() > 0)) {
                QFile usdf(path);
                if (usdf.open(QIODevice::ReadOnly)) {
                    d->userDefinedShaderRaw = usdf.readAll();
                }
                usdf.close();
            } else {
                d->userDefinedShaderRaw.clear();
            }
        }
        if (path == userDefinedShaderTextFile) {
            QFileInfo udsfinfo(path);
            if (udsfinfo.exists() && (udsfinfo.size() < 5 * 1024 * 1024 && udsfinfo.size() > 0)) {
                QFile usdf(path);
                if (usdf.open(QIODevice::ReadOnly)) {
                    d->userDefinedShaderRawText = usdf.readAll();
                }
                usdf.close();
            } else {
                d->userDefinedShaderRawText.clear();
            }
        }
        reloadShaders();
        cycleModes(false);
        doUpdate();
    });

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
    d.reset();
}

void Custom3dChart::initializeGL()
{
    QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
    QOpenGLExtraFunctions *ef = QOpenGLContext::currentContext()->extraFunctions();
    f->glClearColor(d->pState.bgColor.redF(),
                    d->pState.bgColor.greenF(),
                    d->pState.bgColor.blueF(),
                    d->pState.bgColor.alphaF());
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

    /*
     * --------------------------------------------------
     * Plot mode conversion compute program initiate
     * --------------------------------------------------
     */

    d->convertPrg.reset(new QOpenGLShaderProgram(context()));
    d->convertPrg->create();
    d->userDefinedShader.reset(new QOpenGLShader(QOpenGLShader::Compute, context()));

    reloadShaders();

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

    // image gamut position VBO
    d->imageGamutVboIn.reset(new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer));
    d->imageGamutVboIn->create();
    d->imageGamutVboIn->bind();
    d->imageGamutVboIn->setUsagePattern(QOpenGLBuffer::DynamicDraw);
    d->imageGamutVboIn->allocate(d->imageGamut.constData(), d->imageGamut.size() * sizeof(QVector3D));

    d->imageGamutVboOut.reset(new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer));
    d->imageGamutVboOut->create();
    d->imageGamutVboOut->bind();
    d->imageGamutVboOut->setUsagePattern(QOpenGLBuffer::DynamicDraw);
    d->imageGamutVboOut->allocate(d->imageGamut.constData(), d->imageGamut.size() * sizeof(QVector3D));

    // sRGB gamut position VBO
    d->srgbGamutVboIn.reset(new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer));
    d->srgbGamutVboIn->create();
    d->srgbGamutVboIn->bind();
    d->srgbGamutVboIn->setUsagePattern(QOpenGLBuffer::DynamicDraw);
    d->srgbGamutVboIn->allocate(d->srgbGamut.constData(), d->srgbGamut.size() * sizeof(QVector3D));

    d->srgbGamutVboOut.reset(new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer));
    d->srgbGamutVboOut->create();
    d->srgbGamutVboOut->bind();
    d->srgbGamutVboOut->setUsagePattern(QOpenGLBuffer::DynamicDraw);
    d->srgbGamutVboOut->allocate(d->srgbGamut.constData(), d->srgbGamut.size() * sizeof(QVector3D));

    // ColorChecker76
    d->adaptedColorChecker76Vbo.reset(new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer));
    d->adaptedColorChecker76Vbo->create();
    d->adaptedColorChecker76Vbo->bind();
    d->adaptedColorChecker76Vbo->setUsagePattern(QOpenGLBuffer::DynamicDraw);
    d->adaptedColorChecker76Vbo->allocate(d->adaptedColorChecker76.constData(), d->adaptedColorChecker76.size() * sizeof(QVector3D));

    d->adaptedColorChecker76VboOut.reset(new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer));
    d->adaptedColorChecker76VboOut->create();
    d->adaptedColorChecker76VboOut->bind();
    d->adaptedColorChecker76VboOut->setUsagePattern(QOpenGLBuffer::DynamicDraw);
    d->adaptedColorChecker76VboOut->allocate(d->adaptedColorChecker76.constData(), d->adaptedColorChecker76.size() * sizeof(QVector3D));

    // ColorCheckerOld
    d->adaptedColorCheckerVbo.reset(new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer));
    d->adaptedColorCheckerVbo->create();
    d->adaptedColorCheckerVbo->bind();
    d->adaptedColorCheckerVbo->setUsagePattern(QOpenGLBuffer::DynamicDraw);
    d->adaptedColorCheckerVbo->allocate(d->adaptedColorChecker.constData(), d->adaptedColorChecker.size() * sizeof(QVector3D));

    d->adaptedColorCheckerVboOut.reset(new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer));
    d->adaptedColorCheckerVboOut->create();
    d->adaptedColorCheckerVboOut->bind();
    d->adaptedColorCheckerVboOut->setUsagePattern(QOpenGLBuffer::DynamicDraw);
    d->adaptedColorCheckerVboOut->allocate(d->adaptedColorChecker.constData(), d->adaptedColorChecker.size() * sizeof(QVector3D));

    // ColorCheckerNew
    d->adaptedColorCheckerNewVbo.reset(new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer));
    d->adaptedColorCheckerNewVbo->create();
    d->adaptedColorCheckerNewVbo->bind();
    d->adaptedColorCheckerNewVbo->setUsagePattern(QOpenGLBuffer::DynamicDraw);
    d->adaptedColorCheckerNewVbo->allocate(d->adaptedColorCheckerNew.constData(), d->adaptedColorCheckerNew.size() * sizeof(QVector3D));

    d->adaptedColorCheckerNewVboOut.reset(new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer));
    d->adaptedColorCheckerNewVboOut->create();
    d->adaptedColorCheckerNewVboOut->bind();
    d->adaptedColorCheckerNewVboOut->setUsagePattern(QOpenGLBuffer::DynamicDraw);
    d->adaptedColorCheckerNewVboOut->allocate(d->adaptedColorCheckerNew.constData(), d->adaptedColorCheckerNew.size() * sizeof(QVector3D));
}

void Custom3dChart::addDataPoints(QVector<ColorPoint> &dArray, QVector3D &dWhitePoint, QVector<ImageXYZDouble> &dOutGamut)
{
    // using double is a massive performance hit, so let's use float instead.. :3
    foreach (const auto &cp, dArray) {
        QVector3D xyypos{(float)cp.first.X, (float)cp.first.Y, (float)cp.first.Z};
        // xyypos = xyypos - QVector3D{dWhitePoint.x(), dWhitePoint.y(), 0.0f};

        d->vecPosData.append(xyypos);
        d->vecColData.append(QVector4D{cp.second.R, cp.second.G, cp.second.B, cp.second.A});
    }
    d->arrsize = dArray.size();

    foreach (const auto &gm, dOutGamut) {
        d->imageGamut.append(QVector3D{static_cast<float>(gm.X), static_cast<float>(gm.Y), static_cast<float>(gm.Z)});
    }

    dArray.clear();
    dArray.squeeze();

    if (dWhitePoint.isNull()) {
        d->m_whitePoint = QVector3D{D65WPxyy[0], D65WPxyy[1], D65WPxyy[2]};
    } else {
        d->m_whitePoint = dWhitePoint;
    }

    d->resetTargetOrigin = QVector3D{d->m_whitePoint.x(), d->m_whitePoint.y(), 0.5f};
    d->pState.targetPos = d->resetTargetOrigin;

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
    const QVector3D prfWPxyz = xyyToXyz(d->m_whitePoint);
    const QVector3D cc76WPxyz = xyyToXyz(QVector3D{static_cast<float>(Macbeth_chart_1976[18][0]),
                                                   static_cast<float>(Macbeth_chart_1976[18][1]),
                                                   static_cast<float>(Macbeth_chart_1976[18][2])});

    // calculate ColorChecker points to adapted illuminant
    for (int i = 0; i < 24; i++) {
        // 1976
        const QVector3D src76xyy{static_cast<float>(Macbeth_chart_1976[i][0]),
                                 static_cast<float>(Macbeth_chart_1976[i][1]),
                                 static_cast<float>(Macbeth_chart_1976[i][2])};
        const QVector3D dst76xyy = xyzToXyy(xyzAdaptToIlluminant(cc76WPxyz, prfWPxyz, xyyToXyz(src76xyy)));
        d->adaptedColorChecker76.append(dst76xyy);

        // Pre-Nov2014
        const QVector3D srcOldxyy{static_cast<float>(Macbeth_chart_2005[i][0]),
                                 static_cast<float>(Macbeth_chart_2005[i][1]),
                                 static_cast<float>(Macbeth_chart_2005[i][2])};
        const QVector3D dstOldxyy = xyzToXyy(xyzAdaptToIlluminant(getD50WPxyz(), prfWPxyz, xyyToXyz(srcOldxyy)));
        d->adaptedColorChecker.append(dstOldxyy);

        // Post-Nov2014
        const QVector3D srcNewlab{static_cast<float>(ColorChecker_After_Nov2014_Lab[i][0]),
                                  static_cast<float>(ColorChecker_After_Nov2014_Lab[i][1]),
                                  static_cast<float>(ColorChecker_After_Nov2014_Lab[i][2])};
        const QVector3D srcNewxyz = labToXYZ(srcNewlab, getD50WPxyz());
        const QVector3D dstNewxyy = xyzToXyy(xyzAdaptToIlluminant(getD50WPxyz(), prfWPxyz, srcNewxyz));
        d->adaptedColorCheckerNew.append(dstNewxyy);
    }
}

void Custom3dChart::reloadShaders()
{
    if (QOpenGLContext::currentContext() != context() && context()->isValid()) {
        makeCurrent();
    }

    [[maybe_unused]]
    QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
    [[maybe_unused]]
    QOpenGLExtraFunctions *ef = QOpenGLContext::currentContext()->extraFunctions();

    const QFileInfo vertFile("./shaders/3dpoint-vertex.vert");
    const QFileInfo fragFile("./shaders/3dpoint-fragment.frag");
    const QFileInfo compFile("./shaders/3dpoint-converter.comp");

    if (useShaderFile) {
        if (vertFile.exists()) {
            d->shaderFileWatcher->addPath(vertFile.filePath());
        }
        if (fragFile.exists()) {
            d->shaderFileWatcher->addPath(fragFile.filePath());
        }
        if (compFile.exists()) {
            d->shaderFileWatcher->addPath(compFile.filePath());
        }
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
        if (!d->scatterPrg->addShaderFromSourceFile(QOpenGLShader::Vertex, vertFile.filePath())) {
            d->isShaderFile = false;
            qDebug() << "Loading shader from file failed, using internal shader instead.";
            if (!d->scatterPrg->addShaderFromSourceCode(QOpenGLShader::Vertex, vertShader)) {
                qWarning() << "Failed to load vertex shader!";
                d->isValid = false;
                return;
            }
        }
        qDebug() << "Loading fragment shader file...";
        if (!d->scatterPrg->addShaderFromSourceFile(QOpenGLShader::Fragment, fragFile.filePath())) {
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

    qDebug() << "Linking shader program...";
    if (!d->scatterPrg->link()) {
        qWarning() << "Failed to link shader program!";
        d->isValid = false;
        return;
    }

    if (!d->convertPrg->shaders().isEmpty()) {
        qDebug() << "Reloading compute shaders...";
        d->convertPrg->removeAllShaders();
    } else {
        qDebug() << "Initializing compute shaders...";
    }

    if (useShaderFile) {
        d->isShaderFile = true;
        qDebug() << "Loading compute shader file...";
        if (!d->convertPrg->addShaderFromSourceFile(QOpenGLShader::Compute, compFile.filePath())) {
            if (compFile.exists()) {
                QMessageBox errs(this);
                errs.setText("Compute shader compile error! Using internal shader instead");
                errs.setIcon(QMessageBox::Warning);
                errs.setInformativeText(d->convertPrg->log());
                errs.exec();
            }
            d->isShaderFile = false;
            qDebug() << "Loading shader from file failed, using internal shader instead.";
            if (!d->convertPrg->addShaderFromSourceCode(QOpenGLShader::Compute, conversionShader)) {
                qWarning() << "Failed to load compute shader!";
                d->isValid = false;
                return;
            }
        }
    } else {
        d->isShaderFile = false;
        if (!d->convertPrg->addShaderFromSourceCode(QOpenGLShader::Compute, conversionShader)) {
            qWarning() << "Failed to load compute shader!";
            d->isValid = false;
            return;
        }
    }

    // Used defined shader compiler
    const QFileInfo extraCompFile(userDefinedShaderFile);
    const QFileInfo extraCompFileName(userDefinedShaderTextFile);

    if (extraCompFile.exists() && d->userDefinedShaderRaw.isEmpty()
        && (extraCompFile.size() < 5 * 1024 * 1024 && extraCompFile.size() > 0)) {
        d->shaderFileWatcher->addPath(extraCompFile.filePath());
        QFile usdf(extraCompFile.filePath());
        if (usdf.open(QIODevice::ReadOnly)) {
            d->userDefinedShaderRaw = usdf.readAll();
        }
        usdf.close();
    }
    if (extraCompFileName.exists() && d->userDefinedShaderRawText.isEmpty()
        && (extraCompFileName.size() < 5 * 1024 * 1024 && extraCompFileName.size() > 0)) {
        d->shaderFileWatcher->addPath(extraCompFileName.filePath());
        QFile usdf(extraCompFileName.filePath());
        if (usdf.open(QIODevice::ReadOnly)) {
            d->userDefinedShaderRawText = usdf.readAll();
        }
        usdf.close();
    }

    if (!d->userDefinedShaderRaw.isEmpty()) {
        qDebug() << "Loading extra compute shader file...";
        const QByteArray extraRaw = d->userDefinedShaderRaw;

        if (extraRaw.contains("#define MODENUM")) {
            const int modeNumIdx = extraRaw.indexOf("#define MODENUM") + sizeof("#define MODENUM");
            bool scs = false;
            const int numModes =
                extraRaw.mid(modeNumIdx, extraRaw.indexOf("\n", modeNumIdx) - modeNumIdx).trimmed().toInt(&scs);
            if (numModes > 0 && scs) {
                // limit user shaders considerably
                d->maxPlotModes = maximumPlotModes + std::min(numModes, 50);
            } else {
                d->maxPlotModes = maximumPlotModes;
            }
        }
        const bool success = d->userDefinedShader->compileSourceCode(extraRaw);
        if (success && d->userDefinedShader->isCompiled()) {
            d->convertPrg->addShader(d->userDefinedShader.get());
        } else {
            QMessageBox errs(this);
            errs.setText("User defined compute shader compile error!");
            errs.setIcon(QMessageBox::Warning);
            errs.setInformativeText(d->userDefinedShader->log());
            errs.exec();
            qDebug() << "Loading extra shader from file failed.";
            d->convertPrg->addShaderFromSourceCode(QOpenGLShader::Compute, compatabilityShader);
            d->maxPlotModes = maximumPlotModes;
        }
    } else {
        d->convertPrg->addShaderFromSourceCode(QOpenGLShader::Compute, compatabilityShader);
        d->maxPlotModes = maximumPlotModes;
    }

    qDebug() << "Linking compute shader program...";
    if (!d->convertPrg->link()) {
        qWarning() << "Failed to link compute shader program!";
        d->isValid = false;
        return;
    }

    // d->convertPrg->bind();
    // const int numLoc = d->convertPrg->uniformLocation("modeNum");
    // if (numLoc != -1) {
    //     int numModes = 0;
    //     f->glGetUniformiv(d->convertPrg->programId(), numLoc, &numModes);
    //     if (numModes > 0) {
    //         // limit user shaders considerably
    //         d->maxPlotModes = maximumPlotModes + std::min(numModes, 50);
    //     } else {
    //         d->maxPlotModes = maximumPlotModes;
    //     }
    // }
    // d->convertPrg->release();

    d->isValid = true;
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

    // absolute benchmark, epilepsy warning lol
    // d->pState.modeInt++;
    // if (d->pState.modeInt >= maxPlotModes) {
    //     d->pState.modeInt = -1;
    // }
    // cycleModes(false);

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
            d->pState.yawAngle += currentRotation;
        }
        if (d->pState.yawAngle >= 360) {
            d->pState.yawAngle -= 360;
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
    f->glClearColor(d->pState.bgColor.redF(),
                    d->pState.bgColor.greenF(),
                    d->pState.bgColor.blueF(),
                    d->pState.bgColor.alphaF());
    f->glEnable(GL_PROGRAM_POINT_SIZE);

    // Enable depth testing when alpha is >=0.9,
    // otherwise enable alpha blending and depth calculation
    if (d->pState.minAlpha < 0.9) {
        f->glEnable(GL_BLEND);
        // f->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        f->glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        useDepthTest = false;
        if (d->pState.useMaxBlend) {
            useDepthTest = true;
            f->glBlendEquation(GL_MAX);
        }
    } else {
        f->glEnable(GL_DEPTH_TEST);
        useDepthTest = true;
    }

    f->glEnable(GL_LINE_SMOOTH);
    f->glEnable(GL_POLYGON_SMOOTH);

    if (d->pState.useSmoothParticle) {
        f->glEnable(GL_POINT_SMOOTH);
    }

    f->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const float aspectRatio = (width() * devicePixelRatioF()) / (height() * devicePixelRatioF());

    // Perspective/ortho view matrix
    QMatrix4x4 persMatrix;
    if (!d->pState.useOrtho) {
        persMatrix.perspective(d->pState.fov, aspectRatio, 0.0005f, 50.0f);
    } else {
        persMatrix.ortho(-aspectRatio * d->pState.camDistToTarget / 2.5f,
                         aspectRatio * d->pState.camDistToTarget / 2.5f,
                         -d->pState.camDistToTarget / 2.5f,
                         d->pState.camDistToTarget / 2.5f,
                         -100.0f,
                         100.0f);
    }

    // Camera/target matrix
    QVector3D camPos{(float)(qSin(qDegreesToRadians(d->pState.yawAngle)) * qCos(qDegreesToRadians(d->pState.pitchAngle)) * d->pState.camDistToTarget),
                     (float)(qCos(qDegreesToRadians(d->pState.yawAngle)) * qCos(qDegreesToRadians(d->pState.pitchAngle)) * d->pState.camDistToTarget),
                     (float)((qSin(qDegreesToRadians(d->pState.pitchAngle)) * d->pState.camDistToTarget))};

    QMatrix4x4 lookMatrix;
    if (d->pState.pitchAngle > -90.0 && d->pState.pitchAngle < 90.0) {
        lookMatrix.lookAt(camPos + d->pState.targetPos, d->pState.targetPos, {0.0f, 0.0f, 1.0f});
    } else {
        d->pState.yawAngle = 180.0f;
        if (d->pState.pitchAngle > 0) {
            lookMatrix.lookAt(camPos + d->pState.targetPos, d->pState.targetPos, {0.0f, 1.0f, 0.0f});
        } else {
            lookMatrix.lookAt(camPos + d->pState.targetPos, d->pState.targetPos, {0.0f, -1.0f, 0.0f});
        }
    }

    // Model matrix
    QMatrix4x4 modelMatrix;
    // if (d->modeInt == -1) {
    //     modelMatrix.translate(d->m_whitePoint.x(), d->m_whitePoint.y());
    // }
    // modelMatrix.scale(1.0f, 1.0f, d->zScale);
    modelMatrix.rotate(d->pState.turntableAngle, {0.0f, 0.0f, 1.0f});

    // precalculated matrix to be sent into gl program
    const QMatrix4x4 intermediateMatrix = persMatrix * lookMatrix;
    const QMatrix4x4 totalMatrix = persMatrix * lookMatrix * modelMatrix;

    const bool shouldDepthOrder = (d->useDepthOrder && !useDepthTest && !d->pState.useMonochrome) && (d->expDepthOrder && !useDepthTest);

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
    if (d->pState.axisModeInt > -1) {
        d->axisPrg->bind();
        d->axisPrg->setUniformValue("mView", intermediateMatrix);

        if (d->pState.axisModeInt >= 3) {
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

        if ((d->pState.modeInt >= -1 && d->pState.modeInt < 5) || true) {
            if (d->pState.modeInt < 2
                && (d->pState.axisModeInt == 0 || d->pState.axisModeInt == 2 || d->pState.axisModeInt == 4
                    || d->pState.axisModeInt == 6)) {
                d->axisPrg->setUniformValue("vColor", QVector4D{0.25f, 0.25f, 0.25f, 1.0f});
                d->spectralLocusVbo->bind();
                d->axisPrg->enableAttributeArray("aPosition");
                d->axisPrg->setAttributeBuffer("aPosition", GL_FLOAT, 0, 3, 0);
                f->glDrawArrays(GL_LINE_LOOP, 0, d->spectralLocus.size());
                d->spectralLocusVbo->release();
            }

            if (d->pState.axisModeInt == 1 || d->pState.axisModeInt == 2 || d->pState.axisModeInt == 5
                || d->pState.axisModeInt == 6) {
                d->axisPrg->setUniformValue("vColor", QVector4D{0.25f, 0.25f, 0.25f, 1.0f});
                d->srgbGamutVboOut->bind();
                d->axisPrg->enableAttributeArray("aPosition");
                d->axisPrg->setAttributeBuffer("aPosition", GL_FLOAT, 0, 3, 0);
                f->glDrawArrays(GL_LINE_LOOP, 0, d->srgbGamut.size());
                d->srgbGamutVboOut->release();

                d->axisPrg->setUniformValue("vColor", QVector4D{0.4f, 0.0f, 0.0f, 1.0f});
                d->imageGamutVboOut->bind();
                d->axisPrg->enableAttributeArray("aPosition");
                d->axisPrg->setAttributeBuffer("aPosition", GL_FLOAT, 0, 3, 0);
                f->glDrawArrays(GL_LINE_LOOP, 0, d->imageGamut.size());
                d->imageGamutVboOut->release();
            }
        }

        d->axisPrg->release();
    }

    // finally begin drawing...
    d->scatterPrg->bind();

    if (d->pState.useMaxBlend) {
        d->scatterPrg->setUniformValue("maxMode", true);
    } else {
        d->scatterPrg->setUniformValue("maxMode", false);
    }

    d->scatterPrg->setUniformValue("mView", totalMatrix);
    d->scatterPrg->setUniformValue("bUsePrecalc", shouldDepthOrder ? 1 : 0);

    if (d->pState.useVariableSize) {
        d->scatterPrg->setUniformValue("bVarPointSize", true);
        d->scatterPrg->setUniformValue("fVarPointSizeK", 1.0f);
        d->scatterPrg->setUniformValue("fVarPointSizeDepth", 5.0f);
    } else {
        d->scatterPrg->setUniformValue("bVarPointSize", false);
    }
    d->scatterPrg->setUniformValue("fPointSize", d->pState.particleSize);
    d->scatterPrg->setUniformValue("minAlpha", d->pState.minAlpha);
    d->scatterPrg->setUniformValue("monoColor",
                                   QVector3D{(float)d->pState.monoColor.redF(),
                                             (float)d->pState.monoColor.greenF(),
                                             (float)d->pState.monoColor.blueF()});
    if (d->pState.useMonochrome) {
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
    if (d->pState.ccModeInt > -1) {
        if ((d->pState.modeInt >= -1 && d->pState.modeInt < 5) || true) {
            d->axisPrg->bind();
            d->axisPrg->setUniformValue("mView", intermediateMatrix);

            switch (d->pState.ccModeInt) {
            case 0: {
                // CC76

                d->adaptedColorChecker76VboOut->bind();
                QVector3D *ccpos = reinterpret_cast<QVector3D *>(d->adaptedColorChecker76VboOut->map(QOpenGLBuffer::ReadOnly));
                QVector<QVector3D> ccposcross;
                for (int i = 0; i < d->adaptedColorChecker76.size(); i++) {
                    ccposcross.append(crossAtPos(ccpos[i], crossLen * d->pState.camDistToTarget));
                }
                d->adaptedColorChecker76VboOut->unmap();
                d->adaptedColorChecker76VboOut->release();

                d->axisPrg->setUniformValue("vColor", QVector4D{0.0f, 0.8f, 0.8f, 1.0f});
                // d->adaptedColorChecker76Vbo->bind();
                d->axisPrg->enableAttributeArray("aPosition");
                d->axisPrg->setAttributeArray("aPosition", ccposcross.data());
                // d->axisPrg->setAttributeBuffer("aPosition", GL_FLOAT, 0, 3, 0);
                f->glDrawArrays(GL_LINES, 0, d->adaptedColorChecker76.size() * 6);
                // d->adaptedColorChecker76Vbo->release();

                ccString = "Classic 1976";
            } break;
            case 1: {
                // CCOld

                d->adaptedColorCheckerVboOut->bind();
                QVector3D *ccpos = reinterpret_cast<QVector3D *>(d->adaptedColorCheckerVboOut->map(QOpenGLBuffer::ReadOnly));
                QVector<QVector3D> ccposcross;
                for (int i = 0; i < d->adaptedColorChecker.size(); i++) {
                    ccposcross.append(crossAtPos(ccpos[i], crossLen * d->pState.camDistToTarget));
                }
                d->adaptedColorCheckerVboOut->unmap();
                d->adaptedColorCheckerVboOut->release();

                d->axisPrg->setUniformValue("vColor", QVector4D{0.8f, 0.8f, 0.0f, 1.0f});
                // d->adaptedColorCheckerVbo->bind();
                d->axisPrg->enableAttributeArray("aPosition");
                d->axisPrg->setAttributeArray("aPosition", ccposcross.data());
                // d->axisPrg->setAttributeBuffer("aPosition", GL_FLOAT, 0, 3, 0);
                f->glDrawArrays(GL_LINES, 0, d->adaptedColorChecker.size() * 6);
                // d->adaptedColorCheckerVbo->release();

                ccString = "Pre Nov 2014";
            } break;
            case 2: {
                // CCNew

                d->adaptedColorCheckerNewVboOut->bind();
                QVector3D *ccpos = reinterpret_cast<QVector3D *>(d->adaptedColorCheckerNewVboOut->map(QOpenGLBuffer::ReadOnly));
                QVector<QVector3D> ccposcross;
                for (int i = 0; i < d->adaptedColorCheckerNew.size(); i++) {
                    ccposcross.append(crossAtPos(ccpos[i], crossLen * d->pState.camDistToTarget));
                }
                d->adaptedColorCheckerNewVboOut->unmap();
                d->adaptedColorCheckerNewVboOut->release();

                d->axisPrg->setUniformValue("vColor", QVector4D{0.8f, 0.8f, 0.8f, 1.0f});
                // d->adaptedColorCheckerNewVbo->bind();
                d->axisPrg->enableAttributeArray("aPosition");
                d->axisPrg->setAttributeArray("aPosition", ccposcross.data());
                // d->axisPrg->setAttributeBuffer("aPosition", GL_FLOAT, 0, 3, 0);
                f->glDrawArrays(GL_LINES, 0, d->adaptedColorCheckerNew.size() * 6);
                // d->adaptedColorCheckerNewVbo->release();

                ccString = "Post Nov 2014";
            } break;
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

    // axis label stuffs
    // pai.setPen(Qt::lightGray);
    // pai.setBrush(QColor(0, 0, 0, 160));
    // pai.setFont(d->m_labelFont);

    // pai.drawText(
    //     projected(QVector3D(0.0f, 1.0f, 0.0f), totalMatrix, QSizeF(width() * d->upscaler, height() * d->upscaler)),
    //     "y");
    // pai.drawText(
    //     projected(QVector3D(1.0f, 0.0f, 0.0f), totalMatrix, QSizeF(width() * d->upscaler, height() * d->upscaler)),
    //     "x");
    // pai.drawText(
    //     projected(QVector3D(0.0f, 0.0f, 1.0f), totalMatrix, QSizeF(width() * d->upscaler, height() * d->upscaler)),
    //     "Y");

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
                "%15\nColors:%2 | FPS:%1 | Alpha:%3 | Size:%10(%13-%14) | CC:%17 | Blend:%11\n"
                "Y:%4° | P:%9° | FOV:%5 | D:%6 | T:[%7:%8:%12] | Z-order:%16")
                .arg(QString::number(d->m_fps, 'f', 1),
                     (maxPartNum == absolutemax) ? QString("%1(capped)").arg(QString::number(maxPartNum))
                                                 : QString::number(maxPartNum),
                     QString::number(d->pState.minAlpha, 'f', 3),
                     QString::number(d->pState.yawAngle, 'f', 2),
                     d->pState.useOrtho ? QString("0°") : (QString::number(d->pState.fov, 'f', 0) + QString("°")),
                     QString::number(d->pState.camDistToTarget, 'f', 3),
                     QString::number(d->pState.targetPos.x(), 'f', 3),
                     QString::number(d->pState.targetPos.y(), 'f', 3),
                     QString::number(d->pState.pitchAngle, 'f', 2),
                     QString::number(d->pState.particleSize, 'f', 1),
                     d->pState.useMaxBlend ? QString("Max") : QString("Alpha"),
                     QString::number(d->pState.targetPos.z(), 'f', 3),
                     d->pState.useVariableSize ? QString("var") : QString("sta"),
                     d->pState.useSmoothParticle ? QString("rnd") : QString("sqr"),
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
            "(F6): Reload shaders\n"
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
    const float sinYaw = qSin(qDegreesToRadians(d->pState.yawAngle));
    const float cosYaw = qCos(qDegreesToRadians(d->pState.yawAngle));
    const float sinPitch = qSin(qDegreesToRadians(d->pState.pitchAngle));
    const float cosPitch = qCos(qDegreesToRadians(d->pState.pitchAngle));

    const float shiftMultp = d->isShiftHold ? 5.0 : 1.0;

    const float baseSpeed = 0.5;
    const float calcSpeed = baseSpeed * shiftMultp * d->frameDelay;

    if (d->enableNav) {
        QVector3D camFrontBack{sinYaw * cosPitch * d->pState.camDistToTarget,
                               cosYaw * cosPitch * d->pState.camDistToTarget,
                               sinPitch * d->pState.camDistToTarget};

        if (d->nForward)
            d->pState.targetPos -= (camFrontBack * calcSpeed); // move forward / W
        if (d->nBackward)
            d->pState.targetPos += (camFrontBack * calcSpeed); // move backward / A

        QVector3D camStride{cosYaw * d->pState.camDistToTarget, sinYaw * d->pState.camDistToTarget * -1.0f, 0.0};

        if (d->nStrifeLeft)
            d->pState.targetPos += (camStride * calcSpeed); // stride left / S
        if (d->nStrifeRight)
            d->pState.targetPos -= (camStride * calcSpeed); // stride right / D

        if (d->nDown)
            d->pState.targetPos.setZ(d->pState.targetPos.z() - (d->pState.camDistToTarget * calcSpeed)); // move down / C
        if (d->nUp)
            d->pState.targetPos.setZ(d->pState.targetPos.z() + (d->pState.camDistToTarget * calcSpeed)); // move up / V

        if (d->nZoomIn)
            d->pState.camDistToTarget -= (d->pState.camDistToTarget * calcSpeed); // zoom in
        if (d->nZoomOut)
            d->pState.camDistToTarget += (d->pState.camDistToTarget * calcSpeed); // zoom out
        d->pState.camDistToTarget = std::max(0.005f, d->pState.camDistToTarget);

        if (d->nPitchUp)
            d->pState.pitchAngle -= calcSpeed * 25.0; // pitch up
        if (d->nPitchDown)
            d->pState.pitchAngle += calcSpeed * 25.0; // pitch down
        if (d->nYawRight)
            d->pState.yawAngle -= calcSpeed * 25.0; // yaw right
        if (d->nYawLeft)
            d->pState.yawAngle += calcSpeed * 25.0; // yaw left

        d->pState.pitchAngle = std::max(-90.0f, std::min(90.0f, d->pState.pitchAngle));
        if (d->pState.yawAngle > 360.0) {
            d->pState.yawAngle -= 360.0;
        } else if (d->pState.yawAngle < 0.0) {
            d->pState.yawAngle += 360.0;
        }
    }
}

void Custom3dChart::resizeEvent(QResizeEvent *event)
{
    // resizeGL(width(), height());
    d->useDepthOrder = false;
    d->m_navTimeout->start(500);
    doUpdate();
    QOpenGLWidget::resizeEvent(event);
}

void Custom3dChart::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_F6 && !event->isAutoRepeat()) {
        reloadShaders();
        cycleModes();
        doUpdate();
        return;
    }

    if (!d->isValid) return;

    const float shiftMultp = d->isShiftHold ? 10.0f : 1.0f;

    switch (event->key()) {
    case Qt::Key_Minus:
        d->pState.minAlpha -= 0.002f;
        d->pState.minAlpha = std::max(0.0f, d->pState.minAlpha);
        break;
    case Qt::Key_Equal:
        d->pState.minAlpha += 0.002f;
        d->pState.minAlpha = std::min(1.0f, d->pState.minAlpha);
        break;
    case Qt::Key_Underscore:
        d->pState.minAlpha -= 0.05f;
        d->pState.minAlpha = std::max(0.0f, d->pState.minAlpha);
        break;
    case Qt::Key_Plus:
        d->pState.minAlpha += 0.05f;
        d->pState.minAlpha = std::min(1.0f, d->pState.minAlpha);
        break;
    case Qt::Key_BracketLeft:
        if (!d->pState.useOrtho) {
            d->pState.fov += 1.0f * shiftMultp;
            d->pState.fov = std::min(170.0f, d->pState.fov);
        }
        break;
    case Qt::Key_BracketRight:
        if (!d->pState.useOrtho) {
            d->pState.fov -= 1.0f * shiftMultp;
            d->pState.fov = std::max(1.0f, d->pState.fov);
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
    case Qt::Key_Up:
        d->enableNav = true;
        d->nPitchDown = true;
        break;
    case Qt::Key_Down:
        d->enableNav = true;
        d->nPitchUp = true;
        break;
    case Qt::Key_Left:
        d->enableNav = true;
        d->nYawLeft = true;
        break;
    case Qt::Key_Right:
        d->enableNav = true;
        d->nYawRight = true;
        break;
    case Qt::Key_PageUp:
        d->enableNav = true;
        d->nZoomIn = true;
        break;
    case Qt::Key_PageDown:
        d->enableNav = true;
        d->nZoomOut = true;
        break;
    case Qt::Key_F:
        d->pState.targetPos = d->resetTargetOrigin;
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
            d->pState.turntableAngle = 0.0f;
        }
        break;
    case Qt::Key_T:
        d->pState.useOrtho = !d->pState.useOrtho;
        break;
    case Qt::Key_K:
        d->pState.useMonochrome = !d->pState.useMonochrome;
        break;
    case Qt::Key_L:
        d->showLabel = !d->showLabel;
        break;
    case Qt::Key_Q:
        d->pState.toggleOpaque = !d->pState.toggleOpaque;
        if (d->pState.toggleOpaque) {
            d->pState.minAlpha = 1.0f;
        } else {
            d->pState.minAlpha = 0.1f;
        }
        break;
    case Qt::Key_X:
        if (d->isShiftHold) {
            d->pState.particleSize += 1.0f;
        } else {
            d->pState.particleSize += 0.1f;
        }
        d->pState.particleSize = std::min(20.0f, d->pState.particleSize);
        break;
    case Qt::Key_Z:
        if (d->isShiftHold) {
            d->pState.particleSize -= 1.0f;
        } else {
            d->pState.particleSize -= 0.1f;
        }
        d->pState.particleSize = std::max(0.0f, d->pState.particleSize);
        break;
    case Qt::Key_M:
        d->pState.useMaxBlend = !d->pState.useMaxBlend;
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
        d->pState.useVariableSize = !d->pState.useVariableSize;
        break;
    case Qt::Key_O:
        d->pState.useSmoothParticle = !d->pState.useSmoothParticle;
        break;
    case Qt::Key_F1:
        d->showHelp = !d->showHelp;
        break;
    case Qt::Key_F2:
        d->pState.modeInt--;
        if (d->pState.modeInt < -1)
            d->pState.modeInt = d->maxPlotModes;
        cycleModes();
        break;
    case Qt::Key_F3:
        d->pState.modeInt++;
        if (d->pState.modeInt > d->maxPlotModes)
            d->pState.modeInt = -1;
        cycleModes();
        break;
    case Qt::Key_F4:
        d->pState.axisModeInt++;
        if (d->pState.axisModeInt > 6) {
            d->pState.axisModeInt = -1;
        }
        break;
    case Qt::Key_F5:
        // reloadShaders();
        d->pState.ccModeInt++;
        if (d->pState.ccModeInt > 2) {
            d->pState.ccModeInt = -1;
        }
        break;
    case Qt::Key_F6:
        // reloadShaders();
        // cycleModes();
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
    case Qt::Key_Up:
        d->nPitchDown = false;
        break;
    case Qt::Key_Down:
        d->nPitchUp = false;
        break;
    case Qt::Key_Left:
        d->nYawLeft = false;
        break;
    case Qt::Key_Right:
        d->nYawRight = false;
        break;
    case Qt::Key_PageUp:
        d->nZoomIn = false;
        break;
    case Qt::Key_PageDown:
        d->nZoomOut = false;
        break;
    default:
        break;
    }

    if (!(d->nForward || d->nBackward || d->nStrifeLeft || d->nStrifeRight || d->nUp || d->nDown || d->nZoomIn
          || d->nZoomOut || d->nPitchDown || d->nPitchUp || d->nYawLeft || d->nYawRight)
        && !d->continousRotate) {
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

        d->pState.yawAngle += offsetX / orbitSpeedDivider;

        d->pState.pitchAngle += offsetY / orbitSpeedDivider;
        d->pState.pitchAngle = std::min(90.0f, std::max(-90.0f, d->pState.pitchAngle));

        if (d->pState.yawAngle >= 360) {
            d->pState.yawAngle = 0;
        } else if (d->pState.yawAngle < 0) {
            d->pState.yawAngle = 360;
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

            d->pState.yawAngle += offsetX / 15.0f;

            d->pState.pitchAngle += offsetY / 15.0f;
            d->pState.pitchAngle = std::min(90.0f, std::max(-90.0f, d->pState.pitchAngle));

            if (d->pState.yawAngle >= 360) {
                d->pState.yawAngle = 0;
            } else if (d->pState.yawAngle < 0) {
                d->pState.yawAngle = 360;
            }
        } else {
            setCursor(Qt::OpenHandCursor);
            const QPoint delposs(event->pos() - d->m_lastPos);
            d->m_lastPos = event->pos();

            const float rawoffsetX = delposs.x() * 1.0f;
            const float rawoffsetY = delposs.y() * 1.0f;

            // did I just spent my whole day to reinvent the wheel...
            const float offsetX = (rawoffsetX / 1500.0) * d->pState.camDistToTarget;
            const float offsetY = (rawoffsetY / 1500.0) * d->pState.camDistToTarget;

            const float sinYaw = qSin(qDegreesToRadians(d->pState.yawAngle));
            const float cosYaw = qCos(qDegreesToRadians(d->pState.yawAngle));
            const float sinPitch = qSin(qDegreesToRadians(d->pState.pitchAngle));
            const float cosPitch = qCos(qDegreesToRadians(d->pState.pitchAngle));

            QVector3D camPan{((offsetX * cosYaw) + (offsetY * -1.0f * sinYaw * sinPitch)),
                             ((offsetX * -1.0f * sinYaw) + (offsetY * -1.0f * cosYaw * sinPitch)),
                             offsetY * cosPitch};

            d->pState.targetPos = d->pState.targetPos + camPan;
        }

        doUpdate();
    }
}

void Custom3dChart::mouseReleaseEvent(QMouseEvent *event)
{
    if (!d->isValid) return;

    if (!d->enableNav && !d->m_timer->isActive()) {
        d->useDepthOrder = true;
    }

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
        if (d->pState.camDistToTarget > 0.0001) {
            d->pState.camDistToTarget -= zoomIncrement * d->pState.camDistToTarget;

            d->pState.camDistToTarget = std::max(0.005f, d->pState.camDistToTarget);
            doUpdate();
        }
    } else {
        if (!d->pState.useOrtho) {
            if (zoomIncrement < 0) {
                d->pState.fov += 5.0f;
                d->pState.fov = std::min(170.0f, d->pState.fov);
                doUpdate();
            } else if (zoomIncrement > 0) {
                d->pState.fov -= 5.0f;
                d->pState.fov = std::max(1.0f, d->pState.fov);
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

    if (d->pState.modeInt > d->maxPlotModes) {
        d->pState.modeInt = d->maxPlotModes;
    }

    [[maybe_unused]]
    QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
    QOpenGLExtraFunctions *ef = QOpenGLContext::currentContext()->extraFunctions();

    d->convertPrg->bind();

    // main data
    {
        d->scatterPosVbo->bind();
        ef->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, d->scatterPosVbo->bufferId());

        d->scatterPosVboCvt->bind();
        ef->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, d->scatterPosVboCvt->bufferId());

        d->convertPrg->setUniformValue("arraySize", (int)d->arrsize);
        d->convertPrg->setUniformValue("vWhite", d->m_whitePoint);
        d->convertPrg->setUniformValue("iMode", d->pState.modeInt);

        ef->glDispatchCompute((d->arrsize + 31) / 32,1,1);
        ef->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        d->scatterPosVbo->release();
        d->scatterPosVboCvt->release();
    }

    // cc 76
    {
        d->adaptedColorChecker76Vbo->bind();
        ef->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, d->adaptedColorChecker76Vbo->bufferId());

        d->adaptedColorChecker76VboOut->bind();
        ef->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, d->adaptedColorChecker76VboOut->bufferId());

        d->convertPrg->setUniformValue("arraySize", (int)d->adaptedColorChecker76.size());
        d->convertPrg->setUniformValue("vWhite", d->m_whitePoint);
        d->convertPrg->setUniformValue("iMode", d->pState.modeInt);

        ef->glDispatchCompute((d->adaptedColorChecker76.size() + 31) / 32,1,1);
        ef->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        d->adaptedColorCheckerVbo->release();
        d->adaptedColorCheckerVboOut->release();
    }

    // cc old
    {
        d->adaptedColorCheckerVbo->bind();
        ef->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, d->adaptedColorCheckerVbo->bufferId());

        d->adaptedColorCheckerVboOut->bind();
        ef->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, d->adaptedColorCheckerVboOut->bufferId());

        d->convertPrg->setUniformValue("arraySize", (int)d->adaptedColorChecker.size());
        d->convertPrg->setUniformValue("vWhite", d->m_whitePoint);
        d->convertPrg->setUniformValue("iMode", d->pState.modeInt);

        ef->glDispatchCompute((d->adaptedColorChecker.size() + 31) / 32,1,1);
        ef->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        d->adaptedColorCheckerVbo->release();
        d->adaptedColorCheckerVboOut->release();
    }

    // cc new
    {
        d->adaptedColorCheckerNewVbo->bind();
        ef->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, d->adaptedColorCheckerNewVbo->bufferId());

        d->adaptedColorCheckerNewVboOut->bind();
        ef->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, d->adaptedColorCheckerNewVboOut->bufferId());

        d->convertPrg->setUniformValue("arraySize", (int)d->adaptedColorCheckerNew.size());
        d->convertPrg->setUniformValue("vWhite", d->m_whitePoint);
        d->convertPrg->setUniformValue("iMode", d->pState.modeInt);

        ef->glDispatchCompute((d->adaptedColorCheckerNew.size() + 31) / 32,1,1);
        ef->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        d->adaptedColorCheckerNewVbo->release();
        d->adaptedColorCheckerNewVboOut->release();
    }

    // image gamut
    {
        d->imageGamutVboIn->bind();
        ef->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, d->imageGamutVboIn->bufferId());

        d->imageGamutVboOut->bind();
        ef->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, d->imageGamutVboOut->bufferId());

        d->convertPrg->setUniformValue("arraySize", (int)d->imageGamut.size());
        d->convertPrg->setUniformValue("vWhite", d->m_whitePoint);
        d->convertPrg->setUniformValue("iMode", d->pState.modeInt);

        ef->glDispatchCompute((d->imageGamut.size() + 31) / 32,1,1);
        ef->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        d->imageGamutVboIn->release();
        d->imageGamutVboOut->release();
    }

    // srgb gamut
    {
        d->srgbGamutVboIn->bind();
        ef->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, d->srgbGamutVboIn->bufferId());

        d->srgbGamutVboOut->bind();
        ef->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, d->srgbGamutVboOut->bufferId());

        d->convertPrg->setUniformValue("arraySize", (int)d->srgbGamut.size());
        d->convertPrg->setUniformValue("vWhite", d->m_whitePoint);
        d->convertPrg->setUniformValue("iMode", d->pState.modeInt);

        ef->glDispatchCompute((d->srgbGamut.size() + 31) / 32,1,1);
        ef->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        d->srgbGamutVboIn->release();
        d->srgbGamutVboOut->release();
    }

    d->convertPrg->release();

    switch (d->pState.modeInt) {
    case 0: {
        d->modeString = QString("CIE 1960 UCS Yuv");

        const QVector3D wp = xyyToUCS(d->m_whitePoint, d->m_whitePoint, UCS_1960_YUV);
        QVector<QVector3D> spectralLocus;
        foreach (const auto &lcs, d->spectralLocus) {
            spectralLocus.append(xyToUv(lcs));
        }
        d->spectralLocusVbo->bind();
        d->spectralLocusVbo->allocate(spectralLocus.constData(), spectralLocus.size() * sizeof(QVector3D));
        d->spectralLocusVbo->release();

        d->resetTargetOrigin = QVector3D{wp.x(), wp.y(), 0.5f};
    } break;
    case 1: {
        d->modeString = QString("CIE 1976 UCS Yu'v'");
        const QVector3D wp = xyyToUCS(d->m_whitePoint, d->m_whitePoint, UCS_1976_LUV);
        QVector<QVector3D> spectralLocus;
        foreach (const auto &lcs, d->spectralLocus) {
            spectralLocus.append(xyToUrvr(lcs, d->m_whitePoint));
        }
        d->spectralLocusVbo->bind();
        d->spectralLocusVbo->allocate(spectralLocus.constData(), spectralLocus.size() * sizeof(QVector3D));
        d->spectralLocusVbo->release();

        d->resetTargetOrigin = QVector3D{wp.x(), wp.y(), 0.5f};
    } break;
    case 2: {
        d->modeString = QString("CIE 1976 L*u*v* (0.01x)");
        d->resetTargetOrigin = QVector3D{0.0f, 0.0f, 0.5f};
    } break;
    case 3: {
        d->modeString = QString("CIE L*a*b* (0.01x)");
        d->resetTargetOrigin = QVector3D{0.0f, 0.0f, 0.5f};
    } break;
    case 4: {
        d->modeString = QString("Oklab");
        d->resetTargetOrigin = QVector3D{0.0f, 0.0f, 0.5f};
    } break;
    case 5:
        d->modeString = QString("CIE XYZ");
        d->resetTargetOrigin = QVector3D{0.5f, 0.5f, 0.5f};
        break;
    case 6:
        d->modeString = QString("sRGB - Linear");
        d->resetTargetOrigin = QVector3D{0.5f, 0.5f, 0.5f};
        break;
    case 7:
        d->modeString = QString("sRGB - Gamma 2.2");
        d->resetTargetOrigin = QVector3D{0.5f, 0.5f, 0.5f};
        break;
    case 8:
        d->modeString = QString("sRGB - sRGB TRC");
        d->resetTargetOrigin = QVector3D{0.5f, 0.5f, 0.5f};
        break;
    case 9:
        d->modeString = QString("LMS - CAT02");
        d->resetTargetOrigin = QVector3D{0.5f, 0.5f, 0.5f};
        break;
    case 10:
        d->modeString = QString("LMS - E");
        d->resetTargetOrigin = QVector3D{0.5f, 0.5f, 0.5f};
        break;
    case 11:
        d->modeString = QString("LMS - D65");
        d->resetTargetOrigin = QVector3D{0.5f, 0.5f, 0.5f};
        break;
    case 12:
        d->modeString = QString("LMS - Phys. CMFs");
        d->resetTargetOrigin = QVector3D{0.5f, 0.5f, 0.5f};
        break;
    case 13:
        d->modeString = QString("XYB - D65");
        d->resetTargetOrigin = QVector3D{0.0f, 0.0f, 0.5f};
        break;
    case 14:
        d->modeString = QString("ITU.BT-601 Y'CbCr");
        d->resetTargetOrigin = QVector3D{0.0f, 0.0f, 0.5f};
        break;
    case 15:
        d->modeString = QString("ITU.BT-709 Y'CbCr");
        d->resetTargetOrigin = QVector3D{0.0f, 0.0f, 0.5f};
        break;
    case 16:
        d->modeString = QString("SMPTE-240M Y'PbPr");
        d->resetTargetOrigin = QVector3D{0.0f, 0.0f, 0.5f};
        break;
    case 17:
        d->modeString = QString("Kodak YCC g1.8");
        d->resetTargetOrigin = QVector3D{0.0f, 0.0f, 0.5f};
        break;
    case 18:
        d->modeString = QString("ICtCp PQ (LMS=1.0 / SDR)");
        d->resetTargetOrigin = QVector3D{0.0f, 0.0f, 0.5f};
        break;
    case 19:
        d->modeString = QString("ICtCp PQ (LMS=0.0001 / HDR)");
        d->resetTargetOrigin = QVector3D{0.0f, 0.0f, 0.5f};
        break;
    case -1: {
        d->modeString = QString("CIE 1931 xyY");

        d->spectralLocusVbo->bind();
        d->spectralLocusVbo->allocate(d->spectralLocus.constData(), d->spectralLocus.size() * sizeof(QVector3D));
        d->spectralLocusVbo->release();

        d->resetTargetOrigin = QVector3D{d->m_whitePoint.x(), d->m_whitePoint.y(), 0.5f};
    } break;
    default: {
        const int uidx = d->pState.modeInt - maximumPlotModes - 1;
        QStringList userStringList;

        if (!d->userDefinedShaderRawText.isEmpty()) {
            const QString fromRaw = QString::fromUtf8(d->userDefinedShaderRawText);
            QStringList userStrListBuffer = fromRaw.split("\n");
            foreach (const auto &st, userStrListBuffer) {
                if (!st.trimmed().isEmpty() && !st.startsWith("//")) {
                    userStringList.append(st);
                }
            }
        }

        if (!userStringList.isEmpty() && uidx >= 0 && uidx < userStringList.size()) {
            const QStringList userDefs = userStringList.at(uidx).split("|", Qt::SkipEmptyParts);
            if (userDefs.size() > 1) {
                const QStringList userOrigin = userDefs.at(1).split(",");
                if (userOrigin.size() == 3) {
                    bool xOK = false, yOK = false, zOK = false;
                    const float X = userOrigin.at(0).toFloat(&xOK);
                    const float Y = userOrigin.at(1).toFloat(&yOK);
                    const float Z = userOrigin.at(2).toFloat(&zOK);
                    d->modeString = QString("[User %1] %2").arg(QString::number(uidx), userDefs.at(0).trimmed());
                    if (xOK && yOK && zOK) {
                        d->resetTargetOrigin = QVector3D{X, Y, Z};
                    } else {
                        d->resetTargetOrigin = QVector3D{0.0f, 0.0f, 0.5f};
                    }
                } else {
                    d->modeString = QString("[User %1] %2").arg(QString::number(uidx), userDefs.at(0).trimmed());
                    d->resetTargetOrigin = QVector3D{0.0f, 0.0f, 0.5f};
                }
            } else {
                d->modeString = QString("[User %1] %2").arg(QString::number(uidx), userDefs.at(0).trimmed());
                d->resetTargetOrigin = QVector3D{0.0f, 0.0f, 0.5f};
            }
        } else {
            d->modeString = QString("[User %1] Unnamed").arg(QString::number(uidx));
            d->resetTargetOrigin = QVector3D{0.0f, 0.0f, 0.5f};
        }
    } break;
    }

    if (changeTarget) {
        d->pState.targetPos = d->resetTargetOrigin;
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
    d->pState.useMaxBlend = false;
    d->pState.toggleOpaque = false;
    d->pState.useVariableSize = false;
    d->pState.useSmoothParticle = true;
    d->pState.useMonochrome = false;
    d->pState.useOrtho = true;
    d->pState.axisModeInt = 6;
    d->pState.minAlpha = 0.1f;
    d->pState.yawAngle = 180.0f;
    d->pState.turntableAngle = 0.0f;
    d->pState.fov = 45.0f;
    d->pState.camDistToTarget = 1.3f;
    d->pState.pitchAngle = 90.0f;
    d->pState.particleSize = 1.0f;
    d->pState.targetPos = d->resetTargetOrigin;
    d->pState.monoColor = QColor{255, 255, 255};
    d->pState.bgColor = QColor{16, 16, 16};
    makeCurrent();
    QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
    f->glClearColor(d->pState.bgColor.redF(),
                    d->pState.bgColor.greenF(),
                    d->pState.bgColor.blueF(),
                    d->pState.bgColor.alphaF());
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

    QAction setView(this);
    setView.setText("Set camera view...");
    connect(&setView, &QAction::triggered, this, [&]() {
        changeState();
    });

    menu.addAction(&copyThis);
    menu.addAction(&pasteThis);
    menu.addAction(&setView);
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
    const QColor currentBg = d->pState.bgColor;
    const QColor setBgColor =
        QColorDialog::getColor(currentBg, this, "Set background color", QColorDialog::ShowAlphaChannel);
    if (setBgColor.isValid()) {
        d->pState.bgColor = setBgColor;

        makeCurrent();
        QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
        f->glClearColor(d->pState.bgColor.redF(),
                        d->pState.bgColor.greenF(),
                        d->pState.bgColor.blueF(),
                        d->pState.bgColor.alphaF());
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
    const QColor currentBg = d->pState.monoColor;
    const QColor setMonoColor =
        QColorDialog::getColor(currentBg, this, "Set monochrome color", QColorDialog::ShowAlphaChannel);
    if (setMonoColor.isValid()) {
        d->pState.monoColor = setMonoColor;

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
    // format.setInternalTextureFormat(GL_RGB);
    format.setInternalTextureFormat(GL_RGBA32F);
    QOpenGLFramebufferObject fbo(size() * d->upscaler, format);

    fbo.bind();

    paintGL();

    QImage fboImage(fbo.toImage());
    fboImage.setColorSpace(QColorSpace::SRgb);
    fboImage.convertToColorSpace(QColorSpace::SRgbLinear);

    fbo.release();
    fbo.bindDefault();

    doneCurrent();
    d->upscaler = 1;

    return fboImage;
}

void Custom3dChart::changeState()
{
    Camera3DSettingDialog dial(d->pState, this);
    dial.exec();

    d->useDepthOrder = true;
    if (dial.result() == QDialog::Rejected) {
        doUpdate();
        return;
    }

    d->pState = dial.getSettings();
    QThread::msleep(100);
    doUpdate();
    return;
}

void Custom3dChart::copyState()
{
    QByteArray headClip{"Scatter3DClip:"};
    QByteArray toClip;

    toClip.append(QByteArray::fromRawData(reinterpret_cast<const char *>(&d->pState), sizeof(d->pState)));

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
        if (fromClip.size() != sizeof(PlotSetting3D))
            return;

        d->pState = *reinterpret_cast<const PlotSetting3D *>(fromClip.constData());

        d->useDepthOrder = true;

        cycleModes(false);
        doUpdate();
    }
}
