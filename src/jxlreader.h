#ifndef JXLREADER_H
#define JXLREADER_H

#include <QSize>
#include <QString>

#include "imageformats.h"

class JxlReader
{
public:
    JxlReader(const QString &filename);
    ~JxlReader();

    bool processJxl();
    QByteArray getRawImage() const;
    QByteArray getRawICC() const;
    QSize getImageDimension();
    ImageColorDepthID getImageColorDepth();

private:
    class Private;
    Private *const d{nullptr};
};

#endif // JXLREADER_H
