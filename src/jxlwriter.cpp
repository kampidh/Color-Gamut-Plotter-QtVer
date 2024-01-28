#include "jxlwriter.h"

#include <jxl/color_encoding.h>
#include <jxl/encode_cxx.h>
#include <jxl/resizable_parallel_runner_cxx.h>

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QColorSpace>

#include <algorithm>

JxlWriter::JxlWriter()
{
}

bool JxlWriter::convert(QImage *img, const QString &filename, const int encEffort)
{
    if (img->format() == QImage::Format_ARGB32 || img->format() == QImage::Format_ARGB32_Premultiplied
        || img->format() == QImage::Format_RGB32 || img->format() == QImage::Format_BGR888) {
        img->convertTo(QImage::Format_RGBA8888);
    }

    const QRect bounds = img->rect();

    auto enc = JxlEncoderMake(nullptr);
    auto runner = JxlResizableParallelRunnerMake(nullptr);
    if (JXL_ENC_SUCCESS != JxlEncoderSetParallelRunner(enc.get(), JxlResizableParallelRunner, runner.get())) {
        qDebug() << "JxlEncoderSetParallelRunner failed";
        return false;
    }

    JxlResizableParallelRunnerSetThreads(
        runner.get(),
        JxlResizableParallelRunnerSuggestThreads(static_cast<uint64_t>(bounds.width()),
                                                 static_cast<uint64_t>(bounds.height())));

    const JxlPixelFormat pixelFormat = [&]() {
        JxlPixelFormat pixelFormat{};
        switch (img->format()) {
        case QImage::Format_RGBA8888:
        case QImage::Format_RGBA8888_Premultiplied:
        case QImage::Format_ARGB32:
        case QImage::Format_ARGB32_Premultiplied:
            pixelFormat.data_type = JXL_TYPE_UINT8;
            break;
        case QImage::Format_RGBA64:
        case QImage::Format_RGBA64_Premultiplied:
            pixelFormat.data_type = JXL_TYPE_UINT16;
            break;
#if QT_VERSION >= QT_VERSION_CHECK(6, 2, 0)
        case QImage::Format_RGBA16FPx4:
        case QImage::Format_RGBA16FPx4_Premultiplied:
            pixelFormat.data_type = JXL_TYPE_FLOAT16;
            break;
        case QImage::Format_RGBA32FPx4:
        case QImage::Format_RGBA32FPx4_Premultiplied:
            pixelFormat.data_type = JXL_TYPE_FLOAT;
            break;
#endif
        default:
            pixelFormat.data_type = JXL_TYPE_UINT8;
            break;
        }
        pixelFormat.num_channels = 4;
        return pixelFormat;
    }();

    const auto basicInfo = [&]() {
        auto info{std::make_unique<JxlBasicInfo>()};
        JxlEncoderInitBasicInfo(info.get());
        info->xsize = static_cast<uint32_t>(bounds.width());
        info->ysize = static_cast<uint32_t>(bounds.height());
        {
            if (pixelFormat.data_type == JXL_TYPE_UINT8) {
                info->bits_per_sample = 8;
                info->exponent_bits_per_sample = 0;
                info->alpha_bits = 8;
                info->alpha_exponent_bits = 0;
            } else if (pixelFormat.data_type == JXL_TYPE_UINT16) {
                info->bits_per_sample = 16;
                info->exponent_bits_per_sample = 0;
                info->alpha_bits = 16;
                info->alpha_exponent_bits = 0;
            } else if (pixelFormat.data_type == JXL_TYPE_FLOAT16) {
                info->bits_per_sample = 16;
                info->exponent_bits_per_sample = 5;
                info->alpha_bits = 16;
                info->alpha_exponent_bits = 5;
            } else if (pixelFormat.data_type == JXL_TYPE_FLOAT) {
                info->bits_per_sample = 32;
                info->exponent_bits_per_sample = 8;
                info->alpha_bits = 32;
                info->alpha_exponent_bits = 8;
            }
        }

        info->num_color_channels = 3;
        info->num_extra_channels = 1;

        info->uses_original_profile = JXL_TRUE;
        info->have_animation = JXL_FALSE;
        return info;
    }();

    if (JXL_ENC_SUCCESS != JxlEncoderSetBasicInfo(enc.get(), basicInfo.get())) {
        qDebug() << "JxlEncoderSetBasicInfo failed";
        return false;
    }

    JxlColorEncoding cicpDescription{};
    {
        switch (img->format()) {
        case QImage::Format_RGBA8888:
        case QImage::Format_RGBA8888_Premultiplied:
        case QImage::Format_ARGB32:
        case QImage::Format_ARGB32_Premultiplied:
        case QImage::Format_RGBA64:
        case QImage::Format_RGBA64_Premultiplied:
            cicpDescription.transfer_function = JXL_TRANSFER_FUNCTION_SRGB;
            break;
#if QT_VERSION >= QT_VERSION_CHECK(6, 2, 0)
        case QImage::Format_RGBA16FPx4:
        case QImage::Format_RGBA16FPx4_Premultiplied:
        case QImage::Format_RGBA32FPx4:
        case QImage::Format_RGBA32FPx4_Premultiplied:
            cicpDescription.transfer_function = JXL_TRANSFER_FUNCTION_LINEAR;
            break;
#endif
        default:
            cicpDescription.transfer_function = JXL_TRANSFER_FUNCTION_SRGB;
            break;
        }

        cicpDescription.primaries = JXL_PRIMARIES_SRGB;
        cicpDescription.white_point = JXL_WHITE_POINT_D65;

        if (JXL_ENC_SUCCESS != JxlEncoderSetColorEncoding(enc.get(), &cicpDescription)) {
            qDebug() << "JxlEncoderSetColorEncoding failed";
            return false;
        }
    }

    auto *frameSettings = JxlEncoderFrameSettingsCreate(enc.get(), nullptr);
    {
        const auto setFrameLossless = [&](bool v) {
            if (JxlEncoderSetFrameLossless(frameSettings, v ? JXL_TRUE : JXL_FALSE) != JXL_ENC_SUCCESS) {
                qDebug() << "JxlEncoderSetFrameLossless failed";
                return false;
            }
            return true;
        };

        const auto setSetting = [&](JxlEncoderFrameSettingId id, int v) {
            // https://github.com/libjxl/libjxl/issues/1210
            if (id == JXL_ENC_FRAME_SETTING_RESAMPLING && v == -1)
                return true;
            if (JxlEncoderFrameSettingsSetOption(frameSettings, id, v) != JXL_ENC_SUCCESS) {
                qDebug() << "JxlEncoderFrameSettingsSetOption failed";
                return false;
            }
            return true;
        };

        const int effort = [&]() {
            const quint64 imgPxSize = bounds.width() * bounds.height();
            if (encEffort > 0) return std::min(std::max(encEffort, 1), 9);
            if (imgPxSize > 5000000) {
                return 1;
            }
            if (pixelFormat.data_type == JXL_TYPE_UINT8 || pixelFormat.data_type == JXL_TYPE_UINT16) {
                return 7;
            }
            return 3;
        }();

        [[maybe_unused]]
        const auto setSettingFloat = [&](JxlEncoderFrameSettingId id, float v) {
            if (JxlEncoderFrameSettingsSetFloatOption(frameSettings, id, v) != JXL_ENC_SUCCESS) {
                qDebug() << "JxlEncoderFrameSettingsSetFloatOption failed";
                return false;
            }
            return true;
        };

        if (!setFrameLossless(true)
            || !setSetting(JXL_ENC_FRAME_SETTING_EFFORT, effort)
            || !setSetting(JXL_ENC_FRAME_SETTING_RESPONSIVE, 0)
            || !setSetting(JXL_ENC_FRAME_SETTING_DECODING_SPEED, 0)) {
            return false;
        }
    }

    if (JxlEncoderAddImageFrame(frameSettings,
                                &pixelFormat,
                                img->constBits(),
                                static_cast<size_t>(img->sizeInBytes()))
        != JXL_ENC_SUCCESS) {
        qDebug() << "JxlEncoderAddImageFrame failed";
        return false;
    }

    JxlEncoderCloseInput(enc.get());

    QFile outF(filename);
    outF.open(QIODevice::WriteOnly);
    if (!outF.isWritable()) {
        qDebug() << "Cannot write to file";
        outF.close();
        return false;
    }

    QByteArray compressed(16384, 0x0);
    auto *nextOut = reinterpret_cast<uint8_t *>(compressed.data());
    auto availOut = static_cast<size_t>(compressed.size());
    auto result = JXL_ENC_NEED_MORE_OUTPUT;
    while (result == JXL_ENC_NEED_MORE_OUTPUT) {
        result = JxlEncoderProcessOutput(enc.get(), &nextOut, &availOut);
        if (result != JXL_ENC_ERROR) {
            outF.write(compressed.data(), compressed.size() - static_cast<int>(availOut));
        }
        if (result == JXL_ENC_NEED_MORE_OUTPUT) {
            compressed.resize(compressed.size() * 2);
            nextOut = reinterpret_cast<uint8_t *>(compressed.data());
            availOut = static_cast<size_t>(compressed.size());
        }
    }
    if (JXL_ENC_SUCCESS != result) {
        qDebug() << "JxlEncoderProcessOutput failed";
        outF.close();
        return false;
    }
    outF.close();

    return true;
}
