#include "jxlreader.h"

#include <jxl/decode_cxx.h>
#include <jxl/resizable_parallel_runner_cxx.h>
#include <jxl/types.h>

#include <QDebug>
#include <QFile>
#include <QIODevice>

class Q_DECL_HIDDEN JxlReader::Private
{
public:
    bool isCMYK{false};
    QString m_filename{};

    QByteArray m_iccProfile{};
    QByteArray m_rawData{};

    ImageColorDepthID m_depthID{};
    ImageColorModelID m_colorID{};

    JxlBasicInfo m_info{};
    JxlExtraChannelInfo m_extra{};
    JxlPixelFormat m_pixelFormat{};
    JxlFrameHeader m_header{};
};

JxlReader::JxlReader(const QString &filename)
    : d(new Private)
{
    d->m_filename = filename;
}

JxlReader::~JxlReader()
{
    delete d;
}

bool JxlReader::processJxl()
{
    QFile fileIn(d->m_filename);

    fileIn.open(QIODevice::ReadOnly);

    if (!fileIn.isReadable()) {
        qWarning() << "Cannot read file!";
        fileIn.close();
        return false;
    }

    auto runner = JxlResizableParallelRunnerMake(nullptr);
    auto dec = JxlDecoderMake(nullptr);

    if (!runner || !dec) {
        qWarning() << "Failed to init runner and decoder!";
        return false;
    }

    if (JXL_DEC_SUCCESS
        != JxlDecoderSubscribeEvents(dec.get(), JXL_DEC_BASIC_INFO | JXL_DEC_COLOR_ENCODING | JXL_DEC_FULL_IMAGE)) {
        qWarning() << "JxlDecoderSubscribeEvents failed";
        return false;
    }

    if (JXL_DEC_SUCCESS != JxlDecoderSetParallelRunner(dec.get(), JxlResizableParallelRunner, runner.get())) {
        qWarning() << "JxlDecoderSetParallelRunner failed";
        return false;
    }

    const auto data = fileIn.readAll();

    fileIn.close();

    const auto validation =
        JxlSignatureCheck(reinterpret_cast<const uint8_t *>(data.constData()), static_cast<size_t>(data.size()));

    switch (validation) {
    case JXL_SIG_NOT_ENOUGH_BYTES:
        qWarning() << "Failed magic byte validation, not enough data";
        return false;
    case JXL_SIG_INVALID:
        qWarning() << "Failed magic byte validation, incorrect format";
        return false;
    default:
        break;
    }

    if (JXL_DEC_SUCCESS
        != JxlDecoderSetInput(dec.get(),
                              reinterpret_cast<const uint8_t *>(data.constData()),
                              static_cast<size_t>(data.size()))) {
        qWarning() << "JxlDecoderSetInput failed";
        return false;
    };
    JxlDecoderCloseInput(dec.get());
    if (JXL_DEC_SUCCESS != JxlDecoderSetDecompressBoxes(dec.get(), JXL_TRUE)) {
        qWarning() << "JxlDecoderSetDecompressBoxes failed";
        return false;
    };

    for (;;) {
        JxlDecoderStatus status = JxlDecoderProcessInput(dec.get());
        qDebug() << status;
        if (status == JXL_DEC_ERROR) {
            qWarning() << "Decoder error";
            return false;
        } else if (status == JXL_DEC_NEED_MORE_INPUT) {
            qWarning() << "Error, already provided all input";
            return false;
        } else if (status == JXL_DEC_BASIC_INFO) {
            if (JXL_DEC_SUCCESS != JxlDecoderGetBasicInfo(dec.get(), &d->m_info)) {
                qWarning() << "JxlDecoderGetBasicInfo failed";
                return false;
            }

            for (uint32_t i = 0; i < d->m_info.num_extra_channels; i++) {
                if (JXL_DEC_SUCCESS != JxlDecoderGetExtraChannelInfo(dec.get(), i, &d->m_extra)) {
                    qWarning() << "JxlDecoderGetExtraChannelInfo failed";
                    break;
                }
                if (d->m_extra.type == JXL_CHANNEL_BLACK) {
                    d->isCMYK = true;
                }
            }

            qDebug() << "Info";
            qDebug() << "Size:" << d->m_info.xsize << "x" << d->m_info.ysize;
            qDebug() << "Depth:" << d->m_info.bits_per_sample << d->m_info.exponent_bits_per_sample;
            qDebug() << "Number of channels:" << d->m_info.num_color_channels;
            qDebug() << "Has alpha" << d->m_info.num_extra_channels << d->m_info.alpha_bits
                     << d->m_info.alpha_exponent_bits;
            qDebug() << "Has animation:" << d->m_info.have_animation << "loops:" << d->m_info.animation.num_loops
                     << "tick:" << d->m_info.animation.tps_numerator << d->m_info.animation.tps_denominator;
            JxlResizableParallelRunnerSetThreads(
                runner.get(),
                JxlResizableParallelRunnerSuggestThreads(d->m_info.xsize, d->m_info.ysize));

            // normally, we call this on Krita to preserve original bitdepth
            /*
            if (d->m_info.exponent_bits_per_sample != 0) {
                if (d->m_info.bits_per_sample <= 16) {
                    d->m_pixelFormat.data_type = JXL_TYPE_FLOAT16;
                    d->m_depthID = Float16BitsColorDepthID;
                } else if (d->m_info.bits_per_sample <= 32) {
                    d->m_pixelFormat.data_type = JXL_TYPE_FLOAT;
                    d->m_depthID = Float32BitsColorDepthID;
                } else {
                    qWarning() << "Unsupported JPEG-XL input depth" << d->m_info.bits_per_sample
                            << d->m_info.exponent_bits_per_sample;
                    return false;
                }
            } else if (d->m_info.bits_per_sample <= 8) {
                d->m_pixelFormat.data_type = JXL_TYPE_UINT8;
                d->m_depthID = Integer8BitsColorDepthID;
            } else if (d->m_info.bits_per_sample <= 16) {
                d->m_pixelFormat.data_type = JXL_TYPE_UINT16;
                d->m_depthID = Integer16BitsColorDepthID;
            } else {
                qWarning() << "Unsupported JPEG-XL input depth" << d->m_info.bits_per_sample
                        << d->m_info.exponent_bits_per_sample;
                return false;
            } */

            // but here, let's always ask for float output
            d->m_pixelFormat.data_type = JXL_TYPE_FLOAT;
            d->m_depthID = Float32BitsColorDepthID;

            if (d->m_info.num_color_channels == 1) {
                // Grayscale
                d->m_pixelFormat.num_channels = 2;
                d->m_colorID = GrayAColorModelID;
            } else if (d->m_info.num_color_channels == 3 && !d->isCMYK) {
                // RGBA
                d->m_pixelFormat.num_channels = 4;
                d->m_colorID = RGBAColorModelID;
            } else if (d->m_info.num_color_channels == 3 && d->isCMYK) {
                // CMYKA
                d->m_pixelFormat.num_channels = 4;
                d->m_colorID = CMYKAColorModelID;
            } else {
                qWarning() << "Forcing a RGBA conversion, unknown color space";
                d->m_pixelFormat.num_channels = 4;
                d->m_colorID = RGBAColorModelID;
            }
            qDebug() << "Basic info get";
        } else if (status == JXL_DEC_COLOR_ENCODING) {
            size_t iccSize = 0;
            if (JXL_DEC_SUCCESS
                != JxlDecoderGetICCProfileSize(dec.get(), nullptr, JXL_COLOR_PROFILE_TARGET_DATA, &iccSize)) {
                qWarning() << "ICC profile size retrieval failed";
                return false;
            }
            d->m_iccProfile.resize(static_cast<int>(iccSize));
            if (JXL_DEC_SUCCESS
                != JxlDecoderGetColorAsICCProfile(dec.get(),
                                                  nullptr,
                                                  JXL_COLOR_PROFILE_TARGET_DATA,
                                                  reinterpret_cast<uint8_t *>(d->m_iccProfile.data()),
                                                  static_cast<size_t>(d->m_iccProfile.size()))) {
                return false;
            }
            qDebug() << "ICC profile get";
        } else if (status == JXL_DEC_NEED_IMAGE_OUT_BUFFER) {
            size_t rawSize = 0;
            if (JXL_DEC_SUCCESS != JxlDecoderImageOutBufferSize(dec.get(), &d->m_pixelFormat, &rawSize)) {
                qWarning() << "JxlDecoderImageOutBufferSize failed";
                return false;
            }
            d->m_rawData.resize(static_cast<int>(rawSize));
            if (JXL_DEC_SUCCESS
                != JxlDecoderSetImageOutBuffer(dec.get(),
                                               &d->m_pixelFormat,
                                               reinterpret_cast<uint8_t *>(d->m_rawData.data()),
                                               static_cast<size_t>(d->m_rawData.size()))) {
                qWarning() << "JxlDecoderSetImageOutBuffer failed";
                return false;
            }
            qDebug() << "Image out buffer set";
        } else if (status == JXL_DEC_FULL_IMAGE) {
            qDebug() << "Full image loaded";
        } else if (status == JXL_DEC_SUCCESS) {
            qDebug() << "JXL decoding success";
            JxlDecoderReleaseInput(dec.get());
            break;
        }
    }

    //    qDebug() << d->m_rawData.size();
    //    qDebug() << "Pixel count:" << d->m_info.xsize * d->m_info.ysize;
    //    qDebug() << "Raw size / depth / channel:" << (d->m_rawData.size() / 4) / 4;

    if (d->m_colorID != RGBAColorModelID) {
        qWarning() << "Only RGB/A that is supported";
        return false;
    }

    return true;
}

QByteArray JxlReader::getRawImage() const
{
    return d->m_rawData;
}

QByteArray JxlReader::getRawICC() const
{
    return d->m_iccProfile;
}

QSize JxlReader::getImageDimension()
{
    return QSize(static_cast<int>(d->m_info.xsize), static_cast<int>(d->m_info.ysize));
}

ImageColorDepthID JxlReader::getImageColorDepth()
{
    return d->m_depthID;
}
