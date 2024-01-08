#ifndef JXLWRITER_H
#define JXLWRITER_H

#include <QImage>

class JxlWriter
{
public:
    JxlWriter();

    bool convert(QImage *img, const QString &filename, const int encEffort = -1);
};

#endif // JXLWRITER_H
