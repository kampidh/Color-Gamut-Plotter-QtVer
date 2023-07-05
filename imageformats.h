#ifndef IMAGEFORMATS_H
#define IMAGEFORMATS_H

typedef enum {
    Integer8BitsColorDepthID,
    Integer16BitsColorDepthID,
    Float16BitsColorDepthID,
    Float32BitsColorDepthID
} ImageColorDepthID;

typedef enum {
    GrayAColorModelID,
    RGBAColorModelID,
    CMYKAColorModelID
} ImageColorModelID;

#endif // IMAGEFORMATS_H
