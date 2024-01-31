#ifndef SHADERS_GL_H
#define SHADERS_GL_H

static constexpr char monoVertShader[] = {
    R"(#version 430 core
layout (location = 0) in vec3 aPosition;
out vec4 vertexColor;

uniform mat4 mView;
uniform vec4 vColor;

void main()
{
    gl_Position = mView * vec4(aPosition, 1.0f);
    vertexColor = vColor;
}
)"};

static constexpr char monoFragShader[] = {
    R"(#version 430 core
out vec4 FragColor;
in vec4 vertexColor;

void main()
{
    FragColor = vertexColor;
}
)"};

static constexpr char vertShader[] = {
    R"(#version 430 core
layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec4 aColor;
layout (location = 2) in vec4 aPositionSub;

out vec4 vertexColor;

uniform int bVarPointSize;
uniform float fVarPointSizeK;
uniform float fVarPointSizeDepth;
uniform mat4 mView;
uniform float fPointSize;
uniform int bUsePrecalc;

uniform mat4 mModel;
uniform mat3 mXyzRgb;
uniform vec2 vWhite;

void main()
{
    // vec4 modelTrans = mModel * vec4(aPosition, 1.0f);
    // gl_Position = mView * modelTrans;
    // gl_Position = aPositionSub;

    if (bUsePrecalc == 0) {
        gl_Position = mView * vec4(aPosition, 1.0f);
    } else {
        gl_Position = aPositionSub;
    }
    if (bVarPointSize != 0) {
        float calcSize = (fPointSize * fVarPointSizeK) / (fVarPointSizeDepth * gl_Position.z + 0.01f);
        gl_PointSize = max(calcSize, 0.1f);
    } else {
        gl_PointSize = max(fPointSize, 0.1f);
    }

    vertexColor = aColor;
}
)"};

static constexpr char fragShader[] = {
    R"(#version 430 core
out vec4 FragColor;

in vec4 vertexColor;

uniform int monoMode;
uniform vec3 monoColor;
uniform int maxMode;
uniform float minAlpha;

float toSRGB(float inst)
{
    if (inst > 0.0031308) {
        return float((1.055 * pow(inst, 1.0 / 2.4)) - 0.055);
    } else {
        return float(12.92 * inst);
    }
}

void main()
{
    if (maxMode != 0) {
        float alphaV = ((toSRGB(vertexColor.a) * (1.0f - minAlpha)) + max(minAlpha, 0.15));
        vec4 absCol;
        if (monoMode == 0) {
            absCol = vec4(vertexColor.r * alphaV, vertexColor.g * alphaV, vertexColor.b * alphaV, 1.0f);
        } else {
            absCol = vec4(monoColor.r * alphaV, monoColor.g * alphaV, monoColor.b * alphaV, 1.0f);
        }
        FragColor = absCol;
    } else {
        if (monoMode == 0) {
            FragColor = vec4(vertexColor.r, vertexColor.g, vertexColor.b, max(vertexColor.a, minAlpha));
        } else {
            FragColor = vec4(monoColor.r, monoColor.g, monoColor.b, max(0.005f, minAlpha));
        }
    }
}
)"};

static constexpr char compShader[] = {
    R"(#version 430 core
layout(local_size_x = 32) in;

struct QtVect3D{
    float x;
    float y;
    float z;
};

struct QtVect4D{
    float x;
    float y;
    float z;
    float w;
};

layout(std430, binding = 0) buffer posIn {
    QtVect3D ins[];
};
layout(std430, binding = 1) buffer posOut {
    QtVect4D outs[];
};
layout(std430, binding = 2) buffer posOutZ {
    float outZ[];
};

uniform int arraySize;
uniform mat4 transformMatrix;

void main ()
{
    if (gl_GlobalInvocationID.x >= arraySize) {
        return;
    }

    vec4 tfd = transformMatrix * vec4(ins[gl_GlobalInvocationID.x].x, ins[gl_GlobalInvocationID.x].y, ins[gl_GlobalInvocationID.x].z, 1.0);
    outs[gl_GlobalInvocationID.x] = QtVect4D(tfd.x, tfd.y, tfd.z, tfd.w);
    outZ[gl_GlobalInvocationID.x] = tfd.z;
}
)"};

static constexpr char conversionShader[] = {
    R"(#version 430 core
layout(local_size_x = 32) in;

// F32 3D vector compatibility
// vec3 has a different ordering and/or offset it seems.
struct QtVect3D{
    float x;
    float y;
    float z;
};

// input buffer, as F32 xyY array format
layout(std430, binding = 3) buffer colorIn {
    QtVect3D ins[];
};

// output buffer, as F32 to space XYZ
//
// from initial view (ortho):
// horizontal axis = +X up, -X down
// vertical axis = +Y right, -Y left
// depth axis = +Z back/dot, -Z front/cross
layout(std430, binding = 4) buffer colorOut {
    QtVect3D outs[];
};

// max array size defined from app as a barrier
uniform int arraySize;

// colorspace whitepoint
uniform vec3 vWhite;

// mode selector
uniform int iMode = 0;

const mat3 D50toD65Bradford = mat3(
    0.9555766, -0.0282895, 0.0122982,
    -0.0230393, 1.0099416, -0.0204830,
    0.0631636, 0.0210077, 1.3299098
);

const mat3 D65toD50Bradford = mat3(
    1.0478112, 0.0295424, -0.0092345,
    0.0228866, 0.9904844, 0.0150436,
    -0.0501270, -0.0170491, 0.7521316
);

const mat3 xyz2srgb = mat3(
    3.2404542, -0.9692660, 0.0556434,
    -1.5371385, 1.8760108, -0.2040259,
    -0.4985314, 0.0415560, 1.0572252
);

vec3 xyyToXyz(const vec3 v)
{
    vec3 iXYZ;
    if (v.y != 0) {
        float X = (v.x * v.z) / v.y;
        float Y = v.z;
        float Z = ((1.0 - v.x - v.y) * v.z) / v.y;
        iXYZ = vec3(X, Y, Z);
    } else {
        iXYZ = vec3(0, 0, 0);
    }
    return iXYZ;
}

float toSRGB(const float v)
{
    if (v > 0.0031308) {
        return float((1.055 * pow(v, 1.0 / 2.4)) - 0.055);
    } else {
        return float(12.92 * v);
    }
}

vec3 toSRGB(const vec3 v)
{
    return vec3(toSRGB(v.x), toSRGB(v.y), toSRGB(v.z));
}

float fromSRGB(const float v)
{
    if (v > 0.04045) {
        return float(pow((v + 0.055) / 1.055, 2.4));
    } else {
        return float(v / 12.92);
    }
}

vec3 fromSRGB(const vec3 v)
{
    return vec3(fromSRGB(v.x), fromSRGB(v.y), fromSRGB(v.z));
}

float cbroot(const float v)
{
    return float(sign(v) * pow(abs(v), 1.0 / 3.0));
}

float safeInv(const float v, const float g)
{
    if (v > 0) {
        return float(pow(v, 1.0 / g));
    } else {
        return 0.0;
    }
}

vec3 safeInv(const vec3 v, const float g)
{
    return vec3(safeInv(v.x, g), safeInv(v.y, g), safeInv(v.z, g));
}

float specInvG18(const float v)
{
    if (v > 0.018) {
        return float(1.099 * pow(v, 0.45) - 0.099);
    } else {
        return float(4.5 * v);
    }
}

vec3 specInvG18(const vec3 v)
{
    return vec3(specInvG18(v.x), specInvG18(v.y), specInvG18(v.z));
}

vec3 userShader(const vec3 iXYZ, const vec3 wXYZ, const int iMode);

void main()
{
    // compute unit barrier
    if (gl_GlobalInvocationID.x >= arraySize) {
        return;
    }

    const vec3 ixyY = vec3(ins[gl_GlobalInvocationID.x].x, ins[gl_GlobalInvocationID.x].y, ins[gl_GlobalInvocationID.x].z);
    const vec3 iXYZ = xyyToXyz(ixyY);

    switch (iMode) {
    case 0:
    case 1:
    case 2: {
        // CIE Luv / Lu'v'
        const float e = 0.008856;
        const float k = 903.3;

        const vec3 wXYZ = xyyToXyz(vWhite);

        const float yt = iXYZ.y / wXYZ.y;

        const float ur = (4.0 * iXYZ.x) / (iXYZ.x + (15.0 * iXYZ.y) + (3.0 * iXYZ.z));
        const float vr = (9.0 * iXYZ.y) / (iXYZ.x + (15.0 * iXYZ.y) + (3.0 * iXYZ.z));

        const float urt = (4.0 * wXYZ.x) / (wXYZ.x + (15.0 * wXYZ.y) + (3.0 * wXYZ.z));
        const float vrt = (9.0 * wXYZ.y) / (wXYZ.x + (15.0 * wXYZ.y) + (3.0 * wXYZ.z));

        float L;

        if (yt > e) {
            L = (116.0 * cbroot(yt)) - 16.0;
        } else {
            L = k * yt;
        }

        const float u = 13.0 * L * (ur - urt);
        const float v = 13.0 * L * (vr - vrt);

        vec3 oLuv;
        if (iMode == 0) {
            // UCS 1960 uv
            // "The CIE 1960 UCS does not define a luminance or lightness component"
            // - Wikipedia on CIE 1960 color space
            // errrr.... let's use Y as 1931 does, Yuv it is.
            oLuv = vec3(ur, vr * (2.0 / 3.0), iXYZ.y);
        } else if (iMode == 1) {
            // UCS 1976 Yu'v'
            oLuv = vec3(ur, vr, iXYZ.y);
        } else if (iMode == 2) {
            // CIE 1976 L*u*v*
            oLuv = vec3(u, v, L) / 100.0;
        }

        const QtVect3D outLuv = QtVect3D(oLuv.x, oLuv.y, oLuv.z);
        outs[gl_GlobalInvocationID.x] = outLuv;

        } break;

    case 3: {
        // CIE Lab
        const float e = 0.008856;
        const float k = 903.3;

        const vec3 wXYZ = xyyToXyz(vWhite);

        const float xr = iXYZ.x / wXYZ.x;
        const float yr = iXYZ.y / wXYZ.y;
        const float zr = iXYZ.z / wXYZ.z;

        float fx, fy, fz;

        if (xr > e) {
            fx = cbroot(xr);
        } else {
            fx = ((k * xr) + 16.0) / 116.0;
        }

        if (yr > e) {
            fy = cbroot(yr);
        } else {
            fy = ((k * yr) + 16.0) / 116.0;
        }

        if (zr > e) {
            fz = cbroot(zr);
        } else {
            fz = ((k * zr) + 16.0) / 116.0;
        }

        const float L = (116.0 * fy) - 16.0;
        const float a = 500.0 * (fx - fy);
        const float b = 200.0 * (fy - fz);

        const vec3 oLab = vec3(a, b, L) / 100.0;

        const QtVect3D outLab = QtVect3D(oLab.x, oLab.y, oLab.z);
        outs[gl_GlobalInvocationID.x] = outLab;
        } break;

    case 4: {
        // OKLab
        const mat3 oklM1 = mat3(
            0.8189330101, 0.0329845436, 0.0482003018,
            0.3618667424, 0.9293118715, 0.2643662691,
            -0.1288597137, 0.0361456387, 0.6338517070
        );

        const mat3 oklM2 = mat3(
            0.2104542553, 1.9779984951, 0.0259040371,
            0.7936177850, -2.4285922050, 0.7827717662,
            -0.0040720468, 0.4505937099, -0.8086757660
        );

        const vec3 wXYZ = xyyToXyz(vWhite);

        const vec3 lms = oklM1 * iXYZ;

        float lmsL, lmsM, lmsS;

        lmsL = cbroot(lms.x);
        lmsM = cbroot(lms.y);
        lmsS = cbroot(lms.z);

        const vec3 lmsa = vec3(lmsL, lmsM, lmsS);
        const vec3 oLab = oklM2 * lmsa;

        // make double sure no NaNs are passed
        if (oLab.x != oLab.x || oLab.y != oLab.y || oLab.z != oLab.z) {
            const QtVect3D outLab = QtVect3D(0.0, 0.0, 0.0);
            outs[gl_GlobalInvocationID.x] = outLab;
        } else {
            const QtVect3D outLab = QtVect3D(oLab.y, oLab.z, oLab.x);
            outs[gl_GlobalInvocationID.x] = outLab;
        }
        } break;

    case 5: {
        // XYZ
        const QtVect3D outXyz = QtVect3D(iXYZ.x, iXYZ.y, iXYZ.z);
        outs[gl_GlobalInvocationID.x] = outXyz;
        } break;

    case 6: {
        // RGB Linear
        const vec3 iRGB = xyz2srgb * iXYZ;

        const QtVect3D outRgb = QtVect3D(iRGB.x, iRGB.y, iRGB.z);
        outs[gl_GlobalInvocationID.x] = outRgb;
        } break;

    case 7: {
        // RGB 2.2
        const vec3 iRGB = safeInv(xyz2srgb * iXYZ, 2.2);

        const QtVect3D outRgb = QtVect3D(iRGB.x, iRGB.y, iRGB.z);
        outs[gl_GlobalInvocationID.x] = outRgb;
        } break;

    case 8: {
        // RGB sRGB
        const vec3 iRGB = toSRGB(xyz2srgb * iXYZ);

        const QtVect3D outRgb = QtVect3D(iRGB.x, iRGB.y, iRGB.z);
        outs[gl_GlobalInvocationID.x] = outRgb;
        } break;

    case 9:
    case 10:
    case 11:
    case 12: {
        // LMS

        // CAT02
        const mat3 xyz2cat02lms = mat3(
            0.7328, -0.7036, 0.0030,
            0.4296, 1.6975, 0.0136,
            -0.1624, 0.0061, 0.9834
        );

        // Equal energy
        const mat3 xyz2elms = mat3(
            0.38971, -0.22981, 0.0,
            0.68898, 1.18340, 0.0,
            -0.07868, 0.04641, 1.0
        );

        // D65 norm
        const mat3 xyz2d65lms = mat3(
            0.4002, -0.2263, 0.0,
            0.7076, 1.1653, 0.0,
            -0.0808, 0.0457, 0.9182
        );

        // physiological CMFs
        const mat3 xyz2cmflms = mat3(
            0.210576, -0.417076, 0.0,
            0.855098, 1.177260, 0.0,
            -0.0396983, 0.0786283, 0.5168350
        );

        // JXL's XYB
        // this one from wikipedia, was it wrong?
        const mat3 lms2xyb = mat3(
            1.0, 1.0, 0.0,
            -1.0, 1.0, 0.0,
            0.0, 0.0, 1.0
        );

        vec3 lms;

        if (iMode == 9) {
            lms = xyz2cat02lms * iXYZ;
        } else if (iMode == 10) {
            lms = xyz2elms * iXYZ;
        } else if (iMode == 11) {
            lms = xyz2d65lms * iXYZ;
        } else if (iMode == 12) {
            lms = xyz2cmflms * iXYZ;
        } else if (iMode == 13) {
            // unused then
            lms = (lms2xyb * (xyz2d65lms * iXYZ));
            lms = vec3(lms.x, lms.z, lms.y) * 0.5;
        }

        const QtVect3D outRgb = QtVect3D(lms.x, lms.y, lms.z);
        outs[gl_GlobalInvocationID.x] = outRgb;
        } break;

    case 13: {
        // XYB from sRGB Linear
        // this one from spec paper
        const vec3 iRGB = xyz2srgb * iXYZ;

        // the opsin bias and matrix seems to match with libjxl one as per 0.9.x
        // alrighty then~
        const float bias = -0.00379307325527544933;
        const float Lmix = (0.3 * iRGB.r) + (0.622 * iRGB.g) + (0.078 * iRGB.b) - bias;
        const float Mmix = (0.23 * iRGB.r) + (0.692 * iRGB.g) + (0.078 * iRGB.b) - bias;
        const float Smix = (0.24342268924547819 * iRGB.r) + (0.20476744424496821 * iRGB.g) + (0.55180986650955360 * iRGB.b) - bias;

        const float Lgamma = cbroot(Lmix) + cbroot(bias);
        const float Mgamma = cbroot(Mmix) + cbroot(bias);
        const float Sgamma = cbroot(Smix) + cbroot(bias);
        const float X = (Lgamma - Mgamma) / 2.0;
        const float Y = (Lgamma + Mgamma) / 2.0;
        const float B = Sgamma;

        const QtVect3D outRgb = QtVect3D(X, B, Y);
        outs[gl_GlobalInvocationID.x] = outRgb;
        } break;

    case 14: {
        // ITU.BT-601 Y'CbCr
        const vec3 iRGB = safeInv(xyz2srgb * iXYZ, 2.2);
        const float R = iRGB.r;
        const float G = iRGB.g;
        const float B = iRGB.b;

        const float Y = 0.299 * R + 0.587 * G + 0.114 * B;
        const float Cb = -0.169 * R - 0.331 * G + 0.500 * B;
        const float Cr = 0.500 * R - 0.419 * G - 0.081 * B;

        const QtVect3D outRgb = QtVect3D(Cb, Cr, Y);
        outs[gl_GlobalInvocationID.x] = outRgb;
        } break;

    case 15: {
        // ITU.BT-709 Y'CbCr
        const vec3 iRGB = safeInv(xyz2srgb * iXYZ, 2.2);
        const float R = iRGB.r;
        const float G = iRGB.g;
        const float B = iRGB.b;

        const float Y = 0.2215 * R + 0.7154 * G + 0.0721 * B;
        const float Cb = -0.1145 * R - 0.3855 * G + 0.5000 * B;
        const float Cr = 0.5016 * R - 0.4556 * G - 0.0459 * B;

        const QtVect3D outRgb = QtVect3D(Cb, Cr, Y);
        outs[gl_GlobalInvocationID.x] = outRgb;
        } break;

    case 16: {
        // SMPTE-240M Y’PbPr
        const vec3 iRGB = safeInv(xyz2srgb * iXYZ, 2.2);
        const float R = iRGB.r;
        const float G = iRGB.g;
        const float B = iRGB.b;

        const float Y = 0.2122 * R + 0.7013 * G + 0.0865 * B;
        const float Pb = -0.1162 * R - 0.3838 * G + 0.5000 * B;
        const float Pr = 0.5000 * R - 0.4451 * G - 0.0549 * B;

        const QtVect3D outRgb = QtVect3D(Pb, Pr, Y);
        outs[gl_GlobalInvocationID.x] = outRgb;
        } break;

    case 17: {
        // Kodak YCC g1.8
        const vec3 iRGB = specInvG18(xyz2srgb * iXYZ);
        const float R = iRGB.r;
        const float G = iRGB.g;
        const float B = iRGB.b;

        const float Y = 0.299 * R + 0.587 * G + 0.114 * B;
        const float C1 = -0.299 * R - 0.587 * G + 0.886 * B;
        const float C2 = 0.701 * R - 0.587 * G - 0.114 * B;

        const QtVect3D outRgb = QtVect3D(C1, C2, Y);
        outs[gl_GlobalInvocationID.x] = outRgb;
        } break;

    case 18:
    case 19: {
        // Dolby ICtCp

        const mat3 xyz2lms = mat3(
            0.3592, -0.1922, 0.0070,
            0.6976, 1.1004, 0.0749,
            -0.0358, 0.0755, 0.8434
        );

        const mat3 lms2ictcp = mat3(
            0.5, 1.6137, 4.3781,
            0.5, -3.3234, -4.2455,
            0.0, 1.7097, -0.1325
        );

        const float m1 = 2610.0 / 16384.0;
        const float m2 = 2523.0 / 4096.0 * 128.0;
        const float c1 = 3424.0 / 4096.0;
        const float c2 = 2413.0 / 4096.0 * 32.0;
        const float c3 = 2392.0 / 4096.0 * 32.0;

        vec3 LMS;
        if (iMode == 18) {
            LMS = (xyz2lms * iXYZ);
        } else if (iMode == 19) {
            LMS = (xyz2lms * iXYZ) / 10000.0;
        }

        const float L = pow(((c1 + (c2 * pow(LMS.x, m1))) / (1.0 + (c3 * pow(LMS.x, m1)))), m2);
        const float M = pow(((c1 + (c2 * pow(LMS.y, m1))) / (1.0 + (c3 * pow(LMS.y, m1)))), m2);
        const float S = pow(((c1 + (c2 * pow(LMS.z, m1))) / (1.0 + (c3 * pow(LMS.z, m1)))), m2);

        const vec3 ICtCp = lms2ictcp * vec3(L, M, S);

        const QtVect3D outRgb = QtVect3D(ICtCp.y, ICtCp.z, ICtCp.x);
            outs[gl_GlobalInvocationID.x] = outRgb;
        } break;

    case -1:
        // Passtrough / copy buffer xyY
        outs[gl_GlobalInvocationID.x] = ins[gl_GlobalInvocationID.x];
        break;

    default: {
        // User defined
        vec3 userOut = userShader(iXYZ, vWhite, iMode - 20); //iMode - max plot modes
        const QtVect3D outXyz = QtVect3D(userOut.x, userOut.y, userOut.z);
        outs[gl_GlobalInvocationID.x] = outXyz;
        } break;
    }
}
)"};

static constexpr char compatabilityShader[] = {
    R"(#version 430 core

vec3 xyyToXyz(const vec3 v);

vec3 userShader(const vec3 iXYZ, const vec3 wXYZ, const int iMode)
{
    return iXYZ;
}
)"};

#endif // SHADERS_GL_H
