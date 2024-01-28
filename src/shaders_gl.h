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

struct QtVect3D{
    float x;
    float y;
    float z;
};

layout(std430, binding = 3) buffer colorIn {
    QtVect3D ins[];
};
layout(std430, binding = 4) buffer colorOut {
    QtVect3D outs[];
};

uniform int arraySize;

uniform vec3 vWhite;
uniform int iMode = 0;

mat3 D50toD65Bradford = mat3(
    0.9555766, -0.0282895, 0.0122982,
    -0.0230393, 1.0099416, -0.0204830,
    0.0631636, 0.0210077, 1.3299098
);

mat3 D65toD50Bradford = mat3(
    1.0478112, 0.0295424, -0.0092345,
    0.0228866, 0.9904844, 0.0150436,
    -0.0501270, -0.0170491, 0.7521316
);

mat3 xyz2srgb = mat3(
    3.2404542, -0.9692660, 0.0556434,
    -1.5371385, 1.8760108, -0.2040259,
    -0.4985314, 0.0415560, 1.0572252
);

vec3 xyyToXyz(vec3 inst)
{
    vec3 iXYZ;
    if (inst.y != 0) {
        float X = (inst.x * inst.z) / inst.y;
        float Y = inst.z;
        float Z = ((1.0 - inst.x - inst.y) * inst.z) / inst.y;
        iXYZ = vec3(X, Y, Z);
    } else {
        iXYZ = vec3(0, 0, 0);
    }
    return iXYZ;
}

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
    if (gl_GlobalInvocationID.x >= arraySize) {
        return;
    }

    vec3 inC;

    if (iMode >= 0 && iMode < 3) {
        // CIE Luv / Lu'v'
        float e = 0.008856;
        float k = 903.3;

        inC = vec3(ins[gl_GlobalInvocationID.x].x + vWhite.x, ins[gl_GlobalInvocationID.x].y + vWhite.y, ins[gl_GlobalInvocationID.x].z);
        vec3 iXYZ = xyyToXyz(inC);
        vec3 wXYZ = xyyToXyz(vWhite);

        float yt = iXYZ.y / wXYZ.y;

        float ur = (4.0 * iXYZ.x) / (iXYZ.x + (15.0 * iXYZ.y) + (3.0 * iXYZ.z));
        float vr = (9.0 * iXYZ.y) / (iXYZ.x + (15.0 * iXYZ.y) + (3.0 * iXYZ.z));

        float urt = (4.0 * wXYZ.x) / (wXYZ.x + (15.0 * wXYZ.y) + (3.0 * wXYZ.z));
        float vrt = (9.0 * wXYZ.y) / (wXYZ.x + (15.0 * wXYZ.y) + (3.0 * wXYZ.z));

        float L;

        if (yt > e) {
            L = (116.0 * pow(yt, 1.0 / 3.0)) - 16.0;
        } else {
            L = k * yt;
        }

        float u = 13.0 * L * (ur - urt);
        float v = 13.0 * L * (vr - vrt);

        vec3 oLuv;
        if (iMode == 0) {
            // UCS 1960 uv
            // "The CIE 1960 UCS does not define a luminance or lightness component"
            // - Wikipedia on CIE 1960 color space
            // errrr.... let's use Y as 1931 does, Yuv it is.
            oLuv = vec3(ur, vr * (2.0 / 3.0), iXYZ.y);
        } else if (iMode == 1) {
            // UCS 1976 L'u'v'
            oLuv = vec3(ur, vr, L / 100.0);
        } else if (iMode == 2) {
            // CIE 1976 L*u*v*
            oLuv = vec3(u, v, L) / 100.0;
        }

        QtVect3D outLuv = QtVect3D(oLuv.x, oLuv.y, oLuv.z);
        outs[gl_GlobalInvocationID.x] = outLuv;

    } else if (iMode == 3) {
        // CIE Lab
        float e = 0.008856;
        float k = 903.3;

        inC = vec3(ins[gl_GlobalInvocationID.x].x + vWhite.x, ins[gl_GlobalInvocationID.x].y + vWhite.y, ins[gl_GlobalInvocationID.x].z);
        vec3 iXYZ = xyyToXyz(inC);
        vec3 wXYZ = xyyToXyz(vWhite);

        float xr = iXYZ.x / wXYZ.x;
        float yr = iXYZ.y / wXYZ.y;
        float zr = iXYZ.z / wXYZ.z;

        float fx, fy, fz;

        if (xr > e) {
            fx = pow(xr, 1.0 / 3.0);
        } else {
            fx = ((k * xr) + 16.0) / 116.0;
        }

        if (yr > e) {
            fy = pow(yr, 1.0 / 3.0);
        } else {
            fy = ((k * yr) + 16.0) / 116.0;
        }

        if (zr > e) {
            fz = pow(zr, 1.0 / 3.0);
        } else {
            fz = ((k * zr) + 16.0) / 116.0;
        }

        float L = (116.0 * fy) - 16.0;
        float a = 500.0 * (fx - fy);
        float b = 200.0 * (fy - fz);

        vec3 oLab = vec3(a, b, L) / 100.0;

        QtVect3D outLab = QtVect3D(oLab.x, oLab.y, oLab.z);
        outs[gl_GlobalInvocationID.x] = outLab;

    } else if (iMode == 4) {
        // OKLab
        mat3 oklM1 = mat3(
            0.8189330101, 0.0329845436, 0.0482003018,
            0.3618667424, 0.9293118715, 0.2643662691,
            -0.1288597137, 0.0361456387, 0.6338517070
        );

        mat3 oklM2 = mat3(
            0.2104542553, 1.9779984951, 0.0259040371,
            0.7936177850, -2.4285922050, 0.7827717662,
            -0.0040720468, 0.4505937099, -0.8086757660
        );

        inC = vec3(ins[gl_GlobalInvocationID.x].x + vWhite.x, ins[gl_GlobalInvocationID.x].y + vWhite.y, ins[gl_GlobalInvocationID.x].z);
        vec3 iXYZ = xyyToXyz(inC);
        vec3 wXYZ = xyyToXyz(vWhite);

        vec3 lms = oklM1 * iXYZ;

        float lmsL, lmsM, lmsS;
        // I'm not sure how oklab handle negative values...
        if (lms.x > 0) {
            lmsL = pow(lms.x, 1.0 / 3.0);
        } else {
            lmsL = 0.0;
        }
        if (lms.y > 0) {
            lmsM = pow(lms.y, 1.0 / 3.0);
        } else {
            lmsM = 0.0;
        }
        if (lms.z > 0) {
            lmsS = pow(lms.z, 1.0 / 3.0);
        } else {
            lmsS = 0.0;
        }
        vec3 lmsa = vec3(lmsL, lmsM, lmsS);
        vec3 oLab = oklM2 * lmsa;

        // make double sure no NaNs are passed
        if (oLab.x != oLab.x || oLab.y != oLab.y || oLab.z != oLab.z) {
            QtVect3D outLab = QtVect3D(0.0, 0.0, 0.0);
            outs[gl_GlobalInvocationID.x] = outLab;
        } else {
            QtVect3D outLab = QtVect3D(oLab.y, oLab.z, oLab.x);
            outs[gl_GlobalInvocationID.x] = outLab;
        }

    } else if (iMode == 5) {
        // XYZ
        inC = vec3(ins[gl_GlobalInvocationID.x].x + vWhite.x, ins[gl_GlobalInvocationID.x].y + vWhite.y, ins[gl_GlobalInvocationID.x].z);
        vec3 iXYZ = xyyToXyz(inC);

        QtVect3D outXyz = QtVect3D(iXYZ.x, iXYZ.y, iXYZ.z);
        outs[gl_GlobalInvocationID.x] = outXyz;

    } else if (iMode == 6) {
        // RGB Linear
        inC = vec3(ins[gl_GlobalInvocationID.x].x + vWhite.x, ins[gl_GlobalInvocationID.x].y + vWhite.y, ins[gl_GlobalInvocationID.x].z);
        vec3 iXYZ = xyyToXyz(inC);
        vec3 iRGB = xyz2srgb * iXYZ;

        QtVect3D outRgb = QtVect3D(iRGB.x, iRGB.y, iRGB.z);
        outs[gl_GlobalInvocationID.x] = outRgb;

    } else if (iMode == 7) {
        // RGB 2.2
        inC = vec3(ins[gl_GlobalInvocationID.x].x + vWhite.x, ins[gl_GlobalInvocationID.x].y + vWhite.y, ins[gl_GlobalInvocationID.x].z);
        vec3 iXYZ = xyyToXyz(inC);
        vec3 iRGB = xyz2srgb * iXYZ;

        float gamma = 1.0 / 2.2;

        if (iRGB.x > 0) {
            iRGB.x = pow(iRGB.x, gamma);
        }

        if (iRGB.y > 0) {
            iRGB.y = pow(iRGB.y, gamma);
        }

        if (iRGB.z > 0) {
            iRGB.z = pow(iRGB.z, gamma);
        }

        QtVect3D outRgb = QtVect3D(iRGB.x, iRGB.y, iRGB.z);
        outs[gl_GlobalInvocationID.x] = outRgb;

    } else if (iMode == 8) {
        // RGB sRGB
        inC = vec3(ins[gl_GlobalInvocationID.x].x + vWhite.x, ins[gl_GlobalInvocationID.x].y + vWhite.y, ins[gl_GlobalInvocationID.x].z);
        vec3 iXYZ = xyyToXyz(inC);
        vec3 iRGB = xyz2srgb * iXYZ;

        QtVect3D outRgb = QtVect3D(toSRGB(iRGB.x), toSRGB(iRGB.y), toSRGB(iRGB.z));
        outs[gl_GlobalInvocationID.x] = outRgb;

    } else if (iMode >= 9 && iMode < 14) {
        // LMS
        inC = vec3(ins[gl_GlobalInvocationID.x].x + vWhite.x, ins[gl_GlobalInvocationID.x].y + vWhite.y, ins[gl_GlobalInvocationID.x].z);
        vec3 iXYZ = xyyToXyz(inC);

        // CAT02
        mat3 xyz2cat02lms = mat3(
            0.7328, -0.7036, 0.0030,
            0.4296, 1.6975, 0.0136,
            -0.1624, 0.0061, 0.9834
        );

        // Equal energy
        mat3 xyz2elms = mat3(
            0.38971, -0.22981, 0.0,
            0.68898, 1.18340, 0.0,
            -0.07868, 0.04641, 1.0
        );

        // D65 norm
        mat3 xyz2d65lms = mat3(
            0.4002, -0.2263, 0.0,
            0.7076, 1.1653, 0.0,
            -0.0808, 0.0457, 0.9182
        );

        // physiological CMFs
        mat3 xyz2cmflms = mat3(
            0.210576, -0.417076, 0.0,
            0.855098, 1.177260, 0.0,
            -0.0396983, 0.0786283, 0.5168350
        );

        // JXL's XYB
        mat3 lms2xyb = mat3(
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
            lms = (lms2xyb * (xyz2d65lms * iXYZ)) * 0.5;
        }

        QtVect3D outRgb = QtVect3D(lms.x, lms.y, lms.z);
        outs[gl_GlobalInvocationID.x] = outRgb;

    } else {
        // Passtrough / copy buffer;
        outs[gl_GlobalInvocationID.x] = ins[gl_GlobalInvocationID.x];
    }
}
)"};

#endif // SHADERS_GL_H
