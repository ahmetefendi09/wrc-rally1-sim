/**
 * WRC RALLY1 HYBRID v6.1 - GCC 15 COMPATIBLE, HD TEXTURED, 4x MSAA & 3D LIT ENGINE
 * Derleme: g++ -O3 -std=c++17 wrc_rally.cpp -o wrc_rally -lX11 -lGL -lm
 */

#include <cstdint>
#include <cstddef>
#include <cmath>
#include <ctime>
#include <cstring>
#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <algorithm>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <linux/joystick.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <cerrno>

// ============================================================================
// 3D VEKTOR MATEMATIGI VE NORMAL HESABI
// ============================================================================
struct Vec3 {
    float x, y, z;
    Vec3(float _x = 0, float _y = 0, float _z = 0) : x(_x), y(_y), z(_z) {}
    Vec3 operator+(const Vec3& o) const { return Vec3(x + o.x, y + o.y, z + o.z); }
    Vec3 operator-(const Vec3& o) const { return Vec3(x - o.x, y - o.y, z - o.z); }
    Vec3 operator*(float s) const { return Vec3(x * s, y * s, z * s); }
};

inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return Vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
inline Vec3 normalize(const Vec3& v) {
    float l = std::sqrt(dot(v, v));
    return (l > 0.00001f) ? (v * (1.0f / l)) : Vec3(0, 1, 0);
}
inline float wrapAngle(float a) {
    while (a >  3.14159265f) a -= 6.2831853f;
    while (a < -3.14159265f) a += 6.2831853f;
    return a;
}

void setPerspectiveGL(float fovYDeg, float aspect, float zNear, float zFar) {
    float fH = std::tan(fovYDeg * 3.14159265f / 360.0f) * zNear;
    float fW = fH * aspect;
    glFrustum(-fW, fW, -fH, fH, zNear, zFar);
}

void setLookAtGL(const Vec3& eye, const Vec3& center, const Vec3& up) {
    Vec3 f = normalize(center - eye);
    Vec3 s = normalize(cross(f, up));
    Vec3 u = cross(s, f);
    float m[16] = {
         s.x,  u.x, -f.x, 0.0f,
         s.y,  u.y, -f.y, 0.0f,
         s.z,  u.z, -f.z, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    glMultMatrixf(m);
    glTranslatef(-eye.x, -eye.y, -eye.z);
}

// ============================================================================
// PROSEDUREL HD DOKU (TEXTURE) VE TRILINEAR MIPMAP URETICISI
// ============================================================================
struct TextureBank {
    GLuint roadGravel = 0;
    GLuint roadTarmac = 0;
    GLuint terrainGrass = 0;
    GLuint terrainDirt = 0;
    GLuint curbRedWhite = 0;
    GLuint treePine = 0;
    GLuint cityBuilding = 0;

    static std::uint32_t hashNoise(int x, int y, std::uint32_t seed) {
        std::uint32_t h = (std::uint32_t)(x * 374761393U + y * 668265263U + seed * 1442695041U);
        h = (h ^ (h >> 13)) * 1274126177U;
        return h ^ (h >> 16);
    }

    static float smoothNoise(float x, float y, int period, std::uint32_t seed) {
        int x0 = ((int)std::floor(x)) & (period - 1);
        int y0 = ((int)std::floor(y)) & (period - 1);
        int x1 = (x0 + 1) & (period - 1);
        int y1 = (y0 + 1) & (period - 1);
        float fx = x - std::floor(x);
        float fy = y - std::floor(y);
        fx = fx * fx * (3.0f - 2.0f * fx);
        fy = fy * fy * (3.0f - 2.0f * fy);

        float n00 = (hashNoise(x0, y0, seed) & 0xFFFF) / 65535.0f;
        float n10 = (hashNoise(x1, y0, seed) & 0xFFFF) / 65535.0f;
        float n01 = (hashNoise(x0, y1, seed) & 0xFFFF) / 65535.0f;
        float n11 = (hashNoise(x1, y1, seed) & 0xFFFF) / 65535.0f;
        float nx0 = n00 + (n10 - n00) * fx;
        float nx1 = n01 + (n11 - n01) * fx;
        return nx0 + (nx1 - nx0) * fy;
    }

    static GLuint uploadMipmappedRGBA(int w, int h, std::vector<std::uint8_t> data) {
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);

        int level = 0, curW = w, curH = h;
        while (curW >= 1 && curH >= 1) {
            glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, curW, curH, 0, GL_RGBA, GL_UNSIGNED_BYTE, data.data());
            if (curW == 1 && curH == 1) break;
            int nextW = std::max(1, curW / 2), nextH = std::max(1, curH / 2);
            std::vector<std::uint8_t> nextData(nextW * nextH * 4);
            for (int y = 0; y < nextH; ++y) {
                for (int x = 0; x < nextW; ++x) {
                    int x0 = std::min(curW - 1, x * 2), x1 = std::min(curW - 1, x * 2 + 1);
                    int y0 = std::min(curH - 1, y * 2), y1 = std::min(curH - 1, y * 2 + 1);
                    for (int c = 0; c < 4; ++c) {
                        int sum = data[(y0 * curW + x0) * 4 + c] + data[(y0 * curW + x1) * 4 + c] +
                                  data[(y1 * curW + x0) * 4 + c] + data[(y1 * curW + x1) * 4 + c];
                        nextData[(y * nextW + x) * 4 + c] = (std::uint8_t)(sum >> 2);
                    }
                }
            }
            data.swap(nextData);
            curW = nextW; curH = nextH; level++;
        }
        return tex;
    }

    void initAll() {
        const int S = 256;
        std::vector<std::uint8_t> buf(S * S * 4);

        // 1. FINLANDIYA CAKIL / TOPRAK YOL DOKUSU (Tekerlek oluklari + Cakil taslari)
        for (int y = 0; y < S; ++y) {
            for (int x = 0; x < S; ++x) {
                float u = (float)x / S;
                float n1 = smoothNoise(x * 0.25f, y * 0.12f, 64, 101);
                float n2 = smoothNoise(x * 0.80f, y * 0.80f, 128, 202);
                float fine = (hashNoise(x, y, 303) & 0xFF) / 255.0f;

                float rutL = std::exp(-std::pow((u - 0.30f) / 0.11f, 2.0f));
                float rutR = std::exp(-std::pow((u - 0.70f) / 0.11f, 2.0f));
                float rutDark = 1.0f - 0.24f * std::max(rutL, rutR);
                float edgeLoose = std::clamp((std::fabs(u - 0.5f) - 0.36f) * 4.0f, 0.0f, 1.0f);

                float val = (0.55f + 0.25f * n1 + 0.12f * n2 + 0.08f * fine) * rutDark * (1.0f - 0.15f * edgeLoose);
                int idx = (y * S + x) * 4;
                buf[idx]     = (std::uint8_t)std::clamp((int)(val * 215.0f), 0, 255);
                buf[idx + 1] = (std::uint8_t)std::clamp((int)(val * 168.0f), 0, 255);
                buf[idx + 2] = (std::uint8_t)std::clamp((int)(val * 112.0f), 0, 255);
                buf[idx + 3] = 255;
            }
        }
        roadGravel = uploadMipmappedRGBA(S, S, buf);

        // 2. GERCEKCI ASFALT DOKUSU
        for (int y = 0; y < S; ++y) {
            for (int x = 0; x < S; ++x) {
                float u = (float)x / S, v = (float)y / S;
                float n1 = smoothNoise(x * 0.20f, y * 0.10f, 64, 404);
                float grain = (hashNoise(x, y, 505) & 0xFF) / 255.0f;
                float rutL = std::exp(-std::pow((u - 0.30f) / 0.12f, 2.0f));
                float rutR = std::exp(-std::pow((u - 0.70f) / 0.12f, 2.0f));
                float base = (0.26f + 0.08f * n1 + 0.06f * grain) * (1.0f - 0.16f * std::max(rutL, rutR));

                int r = (int)(base * 235.0f), g = (int)(base * 242.0f), b = (int)(base * 255.0f);
                if (std::fabs(u - 0.05f) < 0.014f || std::fabs(u - 0.95f) < 0.014f) {
                    r = 225; g = 230; b = 238;
                } else if (std::fabs(u - 0.50f) < 0.010f && v < 0.55f) {
                    r = 235; g = 195; b = 45;
                }
                int idx = (y * S + x) * 4;
                buf[idx] = (std::uint8_t)r; buf[idx + 1] = (std::uint8_t)g; buf[idx + 2] = (std::uint8_t)b; buf[idx + 3] = 255;
            }
        }
        roadTarmac = uploadMipmappedRGBA(S, S, buf);

        // 3. ORMAN ZEMINI / CIMEN DOKUSU
        for (int y = 0; y < S; ++y) {
            for (int x = 0; x < S; ++x) {
                float n1 = smoothNoise(x * 0.15f, y * 0.15f, 64, 606);
                float n2 = smoothNoise(x * 0.60f, y * 0.60f, 128, 707);
                float fine = (hashNoise(x, y, 808) & 0xFF) / 255.0f;
                float v = 0.45f + 0.32f * n1 + 0.15f * n2 + 0.08f * fine;
                int idx = (y * S + x) * 4;
                buf[idx]     = (std::uint8_t)std::clamp((int)(v * 78.0f), 0, 255);
                buf[idx + 1] = (std::uint8_t)std::clamp((int)(v * 145.0f), 0, 255);
                buf[idx + 2] = (std::uint8_t)std::clamp((int)(v * 62.0f), 0, 255);
                buf[idx + 3] = 255;
            }
        }
        terrainGrass = uploadMipmappedRGBA(S, S, buf);

        // 4. TOPRAK / KAYA YAMAC DOKUSU
        for (int y = 0; y < S; ++y) {
            for (int x = 0; x < S; ++x) {
                float n1 = smoothNoise(x * 0.18f, y * 0.18f, 64, 909);
                float n2 = smoothNoise(x * 0.55f, y * 0.55f, 128, 111);
                float fine = (hashNoise(x, y, 222) & 0xFF) / 255.0f;
                float grassMix = std::clamp((n1 - 0.45f) * 3.2f, 0.0f, 1.0f);
                float v = 0.48f + 0.30f * n2 + 0.12f * fine;
                int rDirt = (int)(v * 148.0f), gDirt = (int)(v * 115.0f), bDirt = (int)(v * 74.0f);
                int rGrs  = (int)(v * 75.0f),  gGrs  = (int)(v * 135.0f), bGrs  = (int)(v * 58.0f);
                int idx = (y * S + x) * 4;
                buf[idx]     = (std::uint8_t)(rDirt + (rGrs - rDirt) * grassMix);
                buf[idx + 1] = (std::uint8_t)(gDirt + (gGrs - gDirt) * grassMix);
                buf[idx + 2] = (std::uint8_t)(bDirt + (bGrs - bDirt) * grassMix);
                buf[idx + 3] = 255;
            }
        }
        terrainDirt = uploadMipmappedRGBA(S, S, buf);

        // 5. KIRMIZI-BEYAZ BORDUR / BARIYER DOKUSU
        for (int y = 0; y < S; ++y) {
            for (int x = 0; x < S; ++x) {
                float grit = 0.88f + 0.12f * ((hashNoise(x, y, 333) & 0xFF) / 255.0f);
                bool redStripe = ((y / (S / 4)) % 2 == 0);
                int idx = (y * S + x) * 4;
                if (redStripe) {
                    buf[idx] = (std::uint8_t)(230 * grit); buf[idx + 1] = (std::uint8_t)(38 * grit); buf[idx + 2] = (std::uint8_t)(42 * grit);
                } else {
                    buf[idx] = (std::uint8_t)(240 * grit); buf[idx + 1] = (std::uint8_t)(242 * grit); buf[idx + 2] = (std::uint8_t)(246 * grit);
                }
                buf[idx + 3] = 255;
            }
        }
        curbRedWhite = uploadMipmappedRGBA(S, S, buf);

        // 6. CAM AGACI IGNESI DOKUSU
        for (int y = 0; y < S; ++y) {
            for (int x = 0; x < S; ++x) {
                float n = smoothNoise(x * 0.4f, y * 0.4f, 64, 555);
                float fine = (hashNoise(x, y, 666) & 0xFF) / 255.0f;
                float v = (0.45f + 0.35f * n + 0.20f * fine) * (0.85f + 0.15f * std::sin(x * 0.35f));
                int idx = (y * S + x) * 4;
                buf[idx]     = (std::uint8_t)std::clamp((int)(v * 42.0f), 0, 255);
                buf[idx + 1] = (std::uint8_t)std::clamp((int)(v * 128.0f), 0, 255);
                buf[idx + 2] = (std::uint8_t)std::clamp((int)(v * 52.0f), 0, 255);
                buf[idx + 3] = 255;
            }
        }
        treePine = uploadMipmappedRGBA(S, S, buf);

        // 7. SEHIR GOKDELEN PENCERELERI DOKUSU
        for (int y = 0; y < S; ++y) {
            for (int x = 0; x < S; ++x) {
                int wx = x % 32, wy = y % 32;
                bool isWindow = (wx >= 6 && wx <= 25 && wy >= 6 && wy <= 24);
                bool lit = ((hashNoise(x / 32, y / 32, 777) % 10) < 6);
                int idx = (y * S + x) * 4;
                if (isWindow && lit) { buf[idx] = 240; buf[idx + 1] = 215; buf[idx + 2] = 145; }
                else if (isWindow)   { buf[idx] = 28;  buf[idx + 1] = 38;  buf[idx + 2] = 58;  }
                else                 { buf[idx] = 36;  buf[idx + 1] = 40;  buf[idx + 2] = 54;  }
                buf[idx + 3] = 255;
            }
        }
        cityBuilding = uploadMipmappedRGBA(S, S, buf);
    }
};

// ============================================================================
// KOMPAKT 2D HUD BITMAP YAZI CIZICI
// ============================================================================
static const std::uint8_t* getGlyph(char c) {
    static const std::uint8_t DIGITS[10][7] = {
        {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14}, {14,17,1,6,8,16,31},
        {31,2,4,2,1,17,14}, {2,6,10,18,31,2,2}, {31,16,30,1,1,17,14},
        {6,8,16,30,17,17,14}, {31,1,2,4,8,8,8}, {14,17,17,14,17,17,14}, {14,17,17,15,1,2,12}
    };
    static const std::uint8_t LETTERS[26][7] = {
        {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30}, {14,17,16,16,16,17,14},
        {28,18,17,17,17,18,28}, {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
        {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17}, {14,4,4,4,4,4,14},
        {7,2,2,2,2,18,12}, {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
        {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17}, {14,17,17,17,17,17,14},
        {30,17,17,30,16,16,16}, {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
        {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4}, {17,17,17,17,17,17,14},
        {17,17,17,17,17,10,4}, {17,17,17,21,21,27,17}, {17,17,10,4,10,17,17},
        {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31}
    };
    static const std::uint8_t SYM_SPC[7] = {0,0,0,0,0,0,0};
    static const std::uint8_t SYM_DOT[7] = {0,0,0,0,0,12,12};
    static const std::uint8_t SYM_COL[7] = {0,12,12,0,12,12,0};
    static const std::uint8_t SYM_DSH[7] = {0,0,0,31,0,0,0};
    static const std::uint8_t SYM_SLS[7] = {1,2,2,4,8,8,16};
    static const std::uint8_t SYM_EXL[7] = {4,4,4,4,4,0,4};
    static const std::uint8_t SYM_LBR[7] = {14,8,8,8,8,8,14};
    static const std::uint8_t SYM_RBR[7] = {14,2,2,2,2,2,14};

    if (c >= '0' && c <= '9') return DIGITS[c - '0'];
    if (c >= 'a' && c <= 'z') c -= 32;
    if (c >= 'A' && c <= 'Z') return LETTERS[c - 'A'];
    if (c == '.') return SYM_DOT;
    if (c == ':') return SYM_COL;
    if (c == '-') return SYM_DSH;
    if (c == '/') return SYM_SLS;
    if (c == '!') return SYM_EXL;
    if (c == '[' || c == '(') return SYM_LBR;
    if (c == ']' || c == ')') return SYM_RBR;
    return SYM_SPC;
}

void drawHudText(float x, float y, float scale, const std::string& text, float r, float g, float b) {
    glColor3f(r, g, b);
    glBegin(GL_QUADS);
    float curX = x;
    for (char c : text) {
        const std::uint8_t* rows = getGlyph(c);
        for (int ry = 0; ry < 7; ++ry) {
            std::uint8_t bits = rows[ry];
            for (int rx = 0; rx < 5; ++rx) {
                if (bits & (1 << (4 - rx))) {
                    float px = curX + rx * scale, py = y + ry * scale;
                    glVertex2f(px, py); glVertex2f(px + scale, py);
                    glVertex2f(px + scale, py + scale); glVertex2f(px, py + scale);
                }
            }
        }
        curX += 6.0f * scale;
    }
    glEnd();
}

void drawHudRect(float x, float y, float w, float h, float r, float g, float b, float a = 1.0f) {
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2f(x, y); glVertex2f(x + w, y); glVertex2f(x + w, y + h); glVertex2f(x, y + h);
    glEnd();
}

// ============================================================================
// GAMEPAD / DIREKSIYON + KLAVYE GIRIS YONETICISI
// ============================================================================
struct InputManager {
    int jsFd = -1;
    std::string deviceName = "KEYBOARD";
    float axis[8] = {0}, axisInit[8] = {0};
    bool axisCalib[8] = {false}, axisMoved[8] = {false}, btn[16] = {false};
    int reconnectTicks = 0;

    float kbSteer = 0.0f, steer = 0.0f, throttle = 0.0f, brake = 0.0f;
    bool handbrake = false, boost = false, resetCar = false;
    int camMode = 0, carLivery = 0;
    bool prevC = false, prevV = false;

    void tryOpenJoystick() {
        if (jsFd >= 0) return;
        const char* devs[] = {"/dev/input/js0", "/dev/input/js1", "/dev/input/js2", "/dev/input/js3"};
        for (const char* d : devs) {
            int fd = open(d, O_RDONLY | O_NONBLOCK);
            if (fd >= 0) {
                jsFd = fd;
                char name[128] = "GAMEPAD";
                if (ioctl(jsFd, JSIOCGNAME(sizeof(name)), name) >= 0) {
                    deviceName = std::string(name);
                    if (deviceName.size() > 16) deviceName = deviceName.substr(0, 16);
                }
                for (int i = 0; i < 8; ++i) { axis[i] = 0; axisCalib[i] = false; axisMoved[i] = false; }
                break;
            }
        }
    }

    float getTrigger(int idx) {
        if (!axisMoved[idx]) return 0.0f;
        if (axisInit[idx] < -0.4f) return std::clamp((axis[idx] + 1.0f) * 0.5f, 0.0f, 1.0f);
        return std::clamp(axis[idx], 0.0f, 1.0f);
    }

    void update(Display* dpy, float dt, bool& quitFlag) {
        resetCar = false;
        if (jsFd < 0) {
            if (++reconnectTicks > 60) { reconnectTicks = 0; tryOpenJoystick(); }
        } else {
            struct js_event e;
            while (read(jsFd, &e, sizeof(e)) > 0) {
                bool init = (e.type & JS_EVENT_INIT) != 0;
                std::uint8_t t = e.type & ~JS_EVENT_INIT;
                if (t == JS_EVENT_AXIS && e.number < 8) {
                    float v = std::clamp(e.value / 32767.0f, -1.0f, 1.0f);
                    if (init || !axisCalib[e.number]) { axisInit[e.number] = v; axisCalib[e.number] = true; }
                    else if (std::fabs(v - axisInit[e.number]) > 0.07f) axisMoved[e.number] = true;
                    axis[e.number] = v;
                } else if (t == JS_EVENT_BUTTON && e.number < 16) {
                    bool p = (e.value != 0);
                    if (!init && p && !btn[e.number]) {
                        if (e.number == 6) carLivery = (carLivery + 1) % 3;
                        if (e.number == 7) camMode = (camMode + 1) % 3;
                    }
                    btn[e.number] = p;
                }
            }
            if (errno != EAGAIN && errno != 0) { close(jsFd); jsFd = -1; deviceName = "KEYBOARD"; }
        }

        float jSteer = 0.0f, jGas = 0.0f, jBrake = 0.0f;
        bool jHandbrake = false, jBoost = false;
        if (jsFd >= 0) {
            float s = axis[0];
            if (std::fabs(s) < 0.07f) s = 0.0f;
            else {
                float u = (std::fabs(s) - 0.07f) / 0.93f;
                s = (s > 0 ? 1.0f : -1.0f) * (0.45f * u + 0.55f * u * u * u);
            }
            jSteer = s;
            float trigGas = std::max(getTrigger(5), getTrigger(4));
            float stickGas = (axisMoved[1] && axis[1] < -0.15f) ? -axis[1] : 0.0f;
            jGas = std::max({trigGas, stickGas, btn[0] ? 1.0f : 0.0f});

            float trigBrk = getTrigger(2);
            float stickBrk = (axisMoved[1] && axis[1] > 0.2f) ? axis[1] : 0.0f;
            jBrake = std::max({trigBrk, stickBrk, btn[4] ? 1.0f : 0.0f});

            jHandbrake = btn[1] || btn[2];
            jBoost     = btn[3] || btn[5];
        }

        char keymap[32];
        XQueryKeymap(dpy, keymap);
        auto keyDown = [&](KeySym sym) -> bool {
            KeyCode code = XKeysymToKeycode(dpy, sym);
            return (code != 0) && ((keymap[code >> 3] & (1 << (code & 7))) != 0);
        };

        if (keyDown(XK_Escape) || keyDown(XK_q) || keyDown(XK_Q)) quitFlag = true;
        bool kGas   = keyDown(XK_w) || keyDown(XK_W) || keyDown(XK_Up);
        bool kBrake = keyDown(XK_s) || keyDown(XK_S) || keyDown(XK_Down);
        bool kLeft  = keyDown(XK_a) || keyDown(XK_A) || keyDown(XK_Left);
        bool kRight = keyDown(XK_d) || keyDown(XK_D) || keyDown(XK_Right);
        bool kHBrk  = keyDown(XK_space);
        bool kBoost = keyDown(XK_f) || keyDown(XK_F) || keyDown(XK_Shift_L);
        resetCar    = keyDown(XK_r) || keyDown(XK_R);

        bool nowC = keyDown(XK_c) || keyDown(XK_C);
        if (nowC && !prevC) camMode = (camMode + 1) % 3;
        prevC = nowC;

        bool nowV = keyDown(XK_v) || keyDown(XK_V);
        if (nowV && !prevV) carLivery = (carLivery + 1) % 3;
        prevV = nowV;

        if (kLeft)       kbSteer = std::max(-1.0f, kbSteer - 2.6f * dt);
        else if (kRight) kbSteer = std::min( 1.0f, kbSteer + 2.6f * dt);
        else {
            if (kbSteer > 0) kbSteer = std::max(0.0f, kbSteer - 4.0f * dt);
            else if (kbSteer < 0) kbSteer = std::min(0.0f, kbSteer + 4.0f * dt);
        }

        steer     = (std::fabs(jSteer) > std::fabs(kbSteer)) ? jSteer : kbSteer;
        throttle  = std::max(jGas,   kGas   ? 1.0f : 0.0f);
        brake     = std::max(jBrake, kBrake ? 1.0f : 0.0f);
        handbrake = jHandbrake || kHBrk;
        boost     = jBoost     || kBoost;
    }
};

// ============================================================================
// WRC 3D ETAP YAPISI
// ============================================================================
struct StageNode {
    Vec3 pos, forward, right;
    float heading, curvature;
    int surface, pacenoteType;
    std::string noteText;
    bool hasChicaneBale = false;
    float baleOffset = 0.0f;
};

struct SkidMark3D { Vec3 leftA, leftB, rightA, rightB; };
struct Particle3D { Vec3 pos, vel; float size, life, maxLife, r, g, b; };

// ============================================================================
// DETAYLI 360 DERECE SKYBOX PANORAMASI
// ============================================================================
void drawDetailedPanoramaSky(const Vec3& camEye, int surfaceType, float timeSec) {
    glPushMatrix();
    glTranslatef(camEye.x, camEye.y, camEye.z);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_FOG);

    float rHor, gHor, bHor, rZen, gZen, bZen;
    if (surfaceType == 0) {
        rZen = 0.03f; gZen = 0.05f; bZen = 0.12f;
        rHor = 0.16f; gHor = 0.14f; bHor = 0.28f;
    } else if (surfaceType == 1) {
        rZen = 0.09f; gZen = 0.18f; bZen = 0.36f;
        rHor = 0.56f; gHor = 0.36f; bHor = 0.38f;
    } else {
        rZen = 0.15f; gZen = 0.36f; bZen = 0.68f;
        rHor = 0.58f; gHor = 0.72f; bHor = 0.86f;
    }

    const int SLICES = 64;
    const float R = 360.0f;

    glBegin(GL_QUAD_STRIP);
    for (int i = 0; i <= SLICES; ++i) {
        float ang = (i * 2.0f * 3.14159265f) / SLICES;
        float cx = std::sin(ang) * R, cz = std::cos(ang) * R;
        glColor3f(rHor * 0.65f, gHor * 0.65f, bHor * 0.65f);
        glVertex3f(cx, -80.0f, cz);
        glColor3f(rHor, gHor, bHor);
        glVertex3f(cx, 0.0f, cz);
    }
    glEnd();

    glBegin(GL_QUAD_STRIP);
    for (int i = 0; i <= SLICES; ++i) {
        float ang = (i * 2.0f * 3.14159265f) / SLICES;
        float cx = std::sin(ang) * R, cz = std::cos(ang) * R;
        glColor3f(rHor, gHor, bHor);
        glVertex3f(cx, 0.0f, cz);
        glColor3f((rHor + rZen) * 0.5f, (gHor + gZen) * 0.5f, (bHor + bZen) * 0.5f);
        glVertex3f(cx * 0.92f, 75.0f, cz * 0.92f);
    }
    glEnd();

    glBegin(GL_QUAD_STRIP);
    for (int i = 0; i <= SLICES; ++i) {
        float ang = (i * 2.0f * 3.14159265f) / SLICES;
        float cx = std::sin(ang) * R, cz = std::cos(ang) * R;
        glColor3f((rHor + rZen) * 0.5f, (gHor + gZen) * 0.5f, (bHor + bZen) * 0.5f);
        glVertex3f(cx * 0.92f, 75.0f, cz * 0.92f);
        glColor3f(rZen, gZen, bZen);
        glVertex3f(cx * 0.45f, 220.0f, cz * 0.45f);
    }
    glEnd();

    // Puruzsuz Gunes / Ay Halesi
    float sunX = -65.0f, sunY = 72.0f, sunZ = 320.0f;
    glBegin(GL_TRIANGLE_FAN);
    glColor4f(1.0f, 0.98f, 0.92f, 0.95f);
    glVertex3f(sunX, sunY, sunZ);
    glColor4f(1.0f, 0.90f, 0.65f, 0.0f);
    for (int k = 0; k <= 32; ++k) {
        float a = k * 2.0f * 3.14159265f / 32.0f;
        glVertex3f(sunX + std::cos(a) * 38.0f, sunY + std::sin(a) * 38.0f, sunZ);
    }
    glEnd();

    glColor3f(1.0f, 0.99f, 0.92f);
    glBegin(GL_POLYGON);
    for (int k = 0; k < 28; ++k) {
        float a = k * 2.0f * 3.14159265f / 28.0f;
        glVertex3f(sunX + std::cos(a) * 7.5f, sunY + std::sin(a) * 7.5f, sunZ);
    }
    glEnd();

    // 360 Derece 3D Karli Dag Siradizileri
    const int MTN_SEGS = 120;
    glBegin(GL_TRIANGLES);
    for (int i = 0; i < MTN_SEGS; ++i) {
        float a0 = (i * 2.0f * 3.14159265f) / MTN_SEGS;
        float a1 = ((i + 1) * 2.0f * 3.14159265f) / MTN_SEGS;
        float am = (a0 + a1) * 0.5f;

        float hPeak = 24.0f + 22.0f * std::fabs(std::sin(i * 0.45f)) + 12.0f * std::cos(i * 1.15f);
        float x0 = std::sin(a0) * 340.0f, z0 = std::cos(a0) * 340.0f;
        float x1 = std::sin(a1) * 340.0f, z1 = std::cos(a1) * 340.0f;
        float xm = std::sin(am) * 340.0f, zm = std::cos(am) * 340.0f;

        glColor3f(rHor * 0.72f, gHor * 0.74f, bHor * 0.82f);
        glVertex3f(x0, -8.0f, z0); glVertex3f(xm, -8.0f, zm);
        if (hPeak > 36.0f) glColor3f(0.92f, 0.94f, 0.98f);
        else               glColor3f(rHor * 0.55f, gHor * 0.58f, bHor * 0.68f);
        glVertex3f(xm, hPeak, zm);

        glColor3f(rHor * 0.58f, gHor * 0.60f, bHor * 0.70f);
        glVertex3f(xm, -8.0f, zm); glVertex3f(x1, -8.0f, z1);
        if (hPeak > 36.0f) glColor3f(0.75f, 0.80f, 0.88f);
        else               glColor3f(rHor * 0.45f, gHor * 0.48f, bHor * 0.58f);
        glVertex3f(xm, hPeak, zm);
    }
    glEnd();

    glEnable(GL_FOG);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glPopMatrix();
}

// ============================================================================
// GERCEKCI NORMAL HESAPLI QUAD, KUTU, AGAC VE KAYA MODELLERI
// ============================================================================
void drawLitQuad(Vec3 a, Vec3 b, Vec3 c, Vec3 d) {
    Vec3 n = normalize(cross(b - a, d - a));
    glNormal3f(n.x, n.y, n.z);
    glVertex3f(a.x, a.y, a.z); glVertex3f(b.x, b.y, b.z);
    glVertex3f(c.x, c.y, c.z); glVertex3f(d.x, d.y, d.z);
}

void drawLitBox(float x0, float y0, float z0, float x1, float y1, float z1) {
    glBegin(GL_QUADS);
    glNormal3f(0, 1, 0);  glVertex3f(x0, y1, z0); glVertex3f(x0, y1, z1); glVertex3f(x1, y1, z1); glVertex3f(x1, y1, z0);
    glNormal3f(0, -1, 0); glVertex3f(x0, y0, z0); glVertex3f(x1, y0, z0); glVertex3f(x1, y0, z1); glVertex3f(x0, y0, z1);
    glNormal3f(0, 0, -1); glVertex3f(x0, y0, z0); glVertex3f(x0, y1, z0); glVertex3f(x1, y1, z0); glVertex3f(x1, y0, z0);
    glNormal3f(0, 0, 1);  glVertex3f(x0, y0, z1); glVertex3f(x1, y0, z1); glVertex3f(x1, y1, z1); glVertex3f(x0, y1, z1);
    glNormal3f(-1, 0, 0); glVertex3f(x0, y0, z0); glVertex3f(x0, y0, z1); glVertex3f(x0, y1, z1); glVertex3f(x0, y1, z0);
    glNormal3f(1, 0, 0);  glVertex3f(x1, y0, z0); glVertex3f(x1, y1, z0); glVertex3f(x1, y1, z1); glVertex3f(x1, y0, z1);
    glEnd();
}

void drawDetailedConiferTree(GLuint pineTex, float heightScale) {
    glPushMatrix();
    glScalef(heightScale, heightScale, heightScale);

    glDisable(GL_TEXTURE_2D);
    glColor3f(0.36f, 0.22f, 0.11f);
    glBegin(GL_QUAD_STRIP);
    for (int k = 0; k <= 8; ++k) {
        float a = k * 2.0f * 3.14159265f / 8.0f;
        float nx = std::cos(a), nz = std::sin(a);
        glNormal3f(nx, 0.0f, nz);
        glVertex3f(nx * 0.36f, -1.2f, nz * 0.36f);
        glVertex3f(nx * 0.22f,  2.8f, nz * 0.22f);
    }
    glEnd();

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, pineTex);
    glColor3f(0.90f, 0.95f, 0.88f);
    for (int tier = 0; tier < 5; ++tier) {
        float yBot = 1.0f + tier * 1.45f;
        float yTop = yBot + 2.55f;
        float rBot = 2.45f - tier * 0.40f;

        glBegin(GL_TRIANGLE_FAN);
        glNormal3f(0.0f, 1.0f, 0.0f);
        glTexCoord2f(0.5f, 0.5f);
        glVertex3f(0.0f, yTop, 0.0f);
        for (int k = 0; k <= 10; ++k) {
            float a = (k * 2.0f * 3.14159265f / 10.0f) + tier * 0.3f;
            float flute = (k % 2 == 0) ? 1.0f : 0.82f;
            float vx = std::cos(a) * rBot * flute, vz = std::sin(a) * rBot * flute;
            Vec3 n = normalize(Vec3(vx, rBot * 0.6f, vz));
            glNormal3f(n.x, n.y, n.z);
            glTexCoord2f(0.5f + std::cos(a) * 0.5f, 0.5f + std::sin(a) * 0.5f);
            glVertex3f(vx, yBot - (k % 2 == 0 ? 0.18f : 0.0f), vz);
        }
        glEnd();
    }
    glPopMatrix();
}

void drawRockBoulder(float radius) {
    glDisable(GL_TEXTURE_2D);
    glColor3f(0.42f, 0.43f, 0.46f);
    glPushMatrix();
    glScalef(radius, radius * 0.72f, radius * 0.88f);
    drawLitBox(-1.0f, -0.6f, -1.0f, 1.0f, 0.9f, 1.0f);
    glRotatef(35.0f, 0, 1, 0);
    glColor3f(0.36f, 0.37f, 0.40f);
    drawLitBox(-0.85f, -0.5f, -0.85f, 0.85f, 1.05f, 0.85f);
    glPopMatrix();
}

// ============================================================================
// TAM DETAYLI 3D WRC RALLY1 ARAC MODELI (ROLL-CAGE, CAM, STOPLAR, DISLI LASTIKLER)
// ============================================================================
void drawTrueWRCRally1Car(int livery, float steerAngle, float wheelSpin,
                          float bodyRoll, float bodyPitch, float suspBounce,
                          bool braking, bool handbrake, bool reversing,
                          int backfireFrames, bool hybridActive) {
    float rMain, gMain, bMain, rAcc, gAcc, bAcc, rDark, gDark, bDark;
    if (livery == 0) {
        rMain = 0.22f; gMain = 0.58f; bMain = 0.94f;
        rAcc  = 0.96f; gAcc  = 0.16f; bAcc  = 0.12f;
        rDark = 0.08f; gDark = 0.14f; bDark = 0.28f;
    } else if (livery == 1) {
        rMain = 0.95f; gMain = 0.96f; bMain = 0.98f;
        rAcc  = 0.90f; gAcc  = 0.10f; bAcc  = 0.14f;
        rDark = 0.10f; gDark = 0.10f; bDark = 0.12f;
    } else {
        rMain = 0.18f; gMain = 0.26f; bMain = 0.86f;
        rAcc  = 0.98f; gAcc  = 0.78f; bAcc  = 0.08f;
        rDark = 0.10f; gDark = 0.08f; bDark = 0.24f;
    }

    glDisable(GL_TEXTURE_2D);

    // 1. ZEMIN AMBIENT GOLGESI VE FREN LAMBASI ASFALT YANSIMASI
    glDisable(GL_LIGHTING);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.0f, -2.0f);
    glDepthMask(GL_FALSE);
    glBegin(GL_QUADS);
    glColor4f(0.01f, 0.01f, 0.02f, 0.72f);
    glVertex3f(-1.22f, 0.02f, -2.18f); glVertex3f( 1.22f, 0.02f, -2.18f);
    glVertex3f( 1.22f, 0.02f,  2.18f); glVertex3f(-1.22f, 0.02f,  2.18f);
    glEnd();

    if (braking || handbrake) {
        glBegin(GL_TRIANGLE_FAN);
        glColor4f(1.0f, 0.10f, 0.08f, 0.38f);
        glVertex3f(0.0f, 0.03f, -2.10f);
        glColor4f(1.0f, 0.05f, 0.05f, 0.0f);
        for (int k = 0; k <= 16; ++k) {
            float a = k * 2.0f * 3.14159265f / 16.0f;
            glVertex3f(std::cos(a) * 1.65f, 0.03f, -2.10f + std::sin(a) * 1.35f);
        }
        glEnd();
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glEnable(GL_LIGHTING);

    // 2. 4 ADET DISLI RALLI TEKERLEGI
    auto drawRallyWheel = [&](float wx, float wz, float steerDeg) {
        glPushMatrix();
        glTranslatef(wx, 0.36f, wz);
        glRotatef(steerDeg, 0, 1, 0);

        const int SEGS = 24;
        const float rad = 0.36f, halfW = 0.21f;

        glBegin(GL_QUAD_STRIP);
        for (int i = 0; i <= SEGS; ++i) {
            float a = (i * 2.0f * 3.14159265f / SEGS) + wheelSpin;
            float cy = std::cos(a) * rad, cz = std::sin(a) * rad;
            glNormal3f(0.0f, std::cos(a), std::sin(a));
            if (i % 2 == 0) glColor3f(0.10f, 0.10f, 0.12f);
            else            glColor3f(0.16f, 0.16f, 0.19f);
            glVertex3f(-halfW, cy, cz); glVertex3f( halfW, cy, cz);
        }
        glEnd();

        float outX = (wx < 0) ? -halfW : halfW;
        float inX  = (wx < 0) ?  halfW : -halfW;
        glNormal3f((wx < 0) ? -1.0f : 1.0f, 0.0f, 0.0f);
        glColor3f(0.12f, 0.12f, 0.14f);
        glBegin(GL_POLYGON);
        for (int i = 0; i < SEGS; ++i) {
            float a = i * 2.0f * 3.14159265f / SEGS;
            glVertex3f(outX, std::cos(a) * rad, std::sin(a) * rad);
        }
        glEnd();
        glNormal3f((wx < 0) ? 1.0f : -1.0f, 0.0f, 0.0f);
        glBegin(GL_POLYGON);
        for (int i = 0; i < SEGS; ++i) {
            float a = i * 2.0f * 3.14159265f / SEGS;
            glVertex3f(inX, std::cos(a) * rad, std::sin(a) * rad);
        }
        glEnd();

        float rimX = (wx < 0) ? outX - 0.01f : outX + 0.01f;
        glNormal3f((wx < 0) ? -1.0f : 1.0f, 0.0f, 0.0f);
        glColor3f(0.92f, 0.94f, 0.98f);
        glBegin(GL_POLYGON);
        for (int i = 0; i < 16; ++i) {
            float a = (i * 2.0f * 3.14159265f / 16.0f) + wheelSpin;
            glVertex3f(rimX, std::cos(a) * 0.23f, std::sin(a) * 0.23f);
        }
        glEnd();
        glPopMatrix();
    };

    float steerDeg = -steerAngle * 57.2958f;
    drawRallyWheel(-1.02f, -1.28f, 0.0f);
    drawRallyWheel( 1.02f, -1.28f, 0.0f);
    drawRallyWheel(-1.02f,  1.30f, steerDeg);
    drawRallyWheel( 1.02f,  1.30f, steerDeg);

    // 3. AMORTISOR UZERINDEKI SCULPTED GOVDE
    glPushMatrix();
    glTranslatef(0.0f, suspBounce, 0.0f);
    glRotatef(bodyRoll * 57.2958f,  0, 0, 1);
    glRotatef(bodyPitch * 57.2958f, 1, 0, 0);

    GLfloat specHigh[4] = { 0.65f, 0.68f, 0.75f, 1.0f };
    GLfloat specLow[4]  = { 0.05f, 0.05f, 0.05f, 1.0f };
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, specHigh);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 48.0f);

    // A) KABIN ICI DETAYLAR (Arka camdan gorunen Kirmizi Roll-Cage!)
    glColor3f(0.08f, 0.09f, 0.11f);
    drawLitBox(-0.74f, 0.42f, -1.78f, 0.74f, 0.72f, 0.55f);

    glColor3f(0.95f, 0.18f, 0.14f);
    glBegin(GL_QUADS);
    glNormal3f(0, 0.5f, -0.86f);
    glVertex3f(-0.56f, 1.22f, -1.18f); glVertex3f(-0.46f, 1.22f, -1.18f);
    glVertex3f( 0.56f, 0.74f, -1.75f); glVertex3f( 0.46f, 0.74f, -1.75f);
    glVertex3f( 0.46f, 1.22f, -1.18f); glVertex3f( 0.56f, 1.22f, -1.18f);
    glVertex3f(-0.46f, 0.74f, -1.75f); glVertex3f(-0.56f, 0.74f, -1.75f);
    glVertex3f(-0.62f, 0.86f, -1.58f); glVertex3f( 0.62f, 0.86f, -1.58f);
    glVertex3f( 0.62f, 0.92f, -1.54f); glVertex3f(-0.62f, 0.92f, -1.54f);
    glEnd();

    // B) YAN GOVDE VE WIDEBODY CAMURLUKLAR
    const float zPts[6]    = { -1.94f, -1.64f, -0.92f,  0.92f,  1.66f,  1.96f };
    const float yBotPts[5] = {  0.50f,  0.62f,  0.22f,  0.62f,  0.26f };
    const float yTopPts[6] = {  0.78f,  0.82f,  0.82f,  0.82f,  0.76f,  0.64f };

    glBegin(GL_QUADS);
    for (int s = 0; s < 5; ++s) {
        float za = zPts[s], zb = zPts[s + 1];
        float yBot = yBotPts[s], yTopA = yTopPts[s], yTopB = yTopPts[s + 1];

        glColor3f(rMain, gMain, bMain);
        drawLitQuad({-1.14f, yBot, za}, {-1.14f, yBot, zb}, {-0.98f, yTopB, zb}, {-0.98f, yTopA, za});
        drawLitQuad({ 1.14f, yBot, zb}, { 1.14f, yBot, za}, { 0.98f, yTopA, za}, { 0.98f, yTopB, zb});

        if (s == 1 || s == 3) {
            glColor3f(rAcc, gAcc, bAcc);
            drawLitQuad({-1.22f, yBot, za}, {-1.22f, yBot, zb}, {-1.12f, yTopB - 0.04f, zb}, {-1.12f, yTopA - 0.04f, za});
            drawLitQuad({ 1.22f, yBot, zb}, { 1.22f, yBot, za}, { 1.12f, yTopA - 0.04f, za}, { 1.12f, yTopB - 0.04f, zb});
        }
    }

    glColor3f(rDark, gDark, bDark);
    drawLitQuad({-1.15f, 0.24f, -0.90f}, {-1.15f, 0.24f,  0.90f}, {-1.10f, 0.48f,  0.90f}, {-1.10f, 0.48f, -0.90f});
    drawLitQuad({ 1.15f, 0.24f,  0.90f}, { 1.15f, 0.24f, -0.90f}, { 1.10f, 0.48f, -0.90f}, { 1.10f, 0.48f,  0.90f});

    glColor3f(rAcc, gAcc, bAcc);
    drawLitQuad({-1.155f, 0.48f, -0.90f}, {-1.155f, 0.48f,  0.90f}, {-1.08f, 0.64f,  0.90f}, {-1.08f, 0.64f, -0.90f});
    drawLitQuad({ 1.155f, 0.48f,  0.90f}, { 1.155f, 0.48f, -0.90f}, { 1.08f, 0.64f, -0.90f}, { 1.08f, 0.64f,  0.90f});

    // C) ON KAPUT, TAVAN, YAN CAMLAR VE C-SUTUNU
    glColor3f(rMain, gMain, bMain);
    drawLitQuad({-0.98f, 0.64f, 1.96f}, { 0.98f, 0.64f, 1.96f}, { 0.98f, 0.82f, 0.60f}, {-0.98f, 0.82f, 0.60f});
    glColor3f(0.16f, 0.24f, 0.34f);
    drawLitQuad({-0.92f, 0.82f,  0.60f}, { 0.92f, 0.82f,  0.60f}, { 0.74f, 1.30f, -0.05f}, {-0.74f, 1.30f, -0.05f});
    glColor3f(0.95f, 0.96f, 0.98f);
    drawLitQuad({-0.74f, 1.30f, -1.16f}, {-0.74f, 1.30f, -0.05f}, { 0.74f, 1.30f, -0.05f}, { 0.74f, 1.30f, -1.16f});
    glColor3f(0.14f, 0.20f, 0.30f);
    drawLitQuad({-0.98f, 0.82f, -1.78f}, {-0.98f, 0.82f,  0.55f}, {-0.74f, 1.28f, -0.06f}, {-0.74f, 1.28f, -1.14f});
    drawLitQuad({ 0.98f, 0.82f,  0.55f}, { 0.98f, 0.82f, -1.78f}, { 0.74f, 1.28f, -1.14f}, { 0.74f, 1.28f, -0.06f});

    // D) ARKA BAGAJ C-SUTUNLARI VE ARKA CAM CERCEVESI
    glColor3f(rMain, gMain, bMain);
    drawLitQuad({-0.98f, 0.78f, -1.90f}, {-0.72f, 0.78f, -1.90f}, {-0.58f, 1.30f, -1.16f}, {-0.74f, 1.30f, -1.16f});
    drawLitQuad({ 0.72f, 0.78f, -1.90f}, { 0.98f, 0.78f, -1.90f}, { 0.74f, 1.30f, -1.16f}, { 0.58f, 1.30f, -1.16f});
    glColor3f(0.10f, 0.11f, 0.14f);
    drawLitQuad({-0.72f, 0.78f, -1.91f}, { 0.72f, 0.78f, -1.91f}, { 0.68f, 0.85f, -1.82f}, {-0.68f, 0.85f, -1.82f});
    glEnd();

    // Yari-Seffaf Arka Cam
    glDepthMask(GL_FALSE);
    glBegin(GL_QUADS);
    glNormal3f(0.0f, 0.75f, -0.66f);
    glColor4f(0.15f, 0.25f, 0.38f, 0.58f);
    glVertex3f(-0.68f, 0.85f, -1.82f); glVertex3f( 0.68f, 0.85f, -1.82f);
    glColor4f(0.32f, 0.48f, 0.68f, 0.65f);
    glVertex3f( 0.56f, 1.27f, -1.19f); glVertex3f(-0.56f, 1.27f, -1.19f);
    glEnd();
    glDepthMask(GL_TRUE);

    // Arka Silecek
    glColor3f(0.08f, 0.08f, 0.10f);
    glBegin(GL_QUADS);
    glNormal3f(0, 0.7f, -0.7f);
    glVertex3f(-0.03f, 0.84f, -1.84f); glVertex3f( 0.03f, 0.84f, -1.84f);
    glVertex3f( 0.35f, 1.16f, -1.36f); glVertex3f( 0.30f, 1.16f, -1.36f);
    glEnd();

    // E) ARKA PANEL, 3D LED STOPLAR, PLAKA VE DIFUZOR
    glColor3f(rMain, gMain, bMain);
    glBegin(GL_QUADS);
    drawLitQuad({-1.02f, 0.44f, -1.94f}, { 1.02f, 0.44f, -1.94f}, { 0.98f, 0.78f, -1.90f}, {-0.98f, 0.78f, -1.90f});
    glColor3f(0.10f, 0.11f, 0.14f);
    drawLitQuad({-1.04f, 0.60f, -1.95f}, { 1.04f, 0.60f, -1.95f}, { 0.96f, 0.81f, -1.87f}, {-0.96f, 0.81f, -1.87f});
    glEnd();

    glDisable(GL_LIGHTING);
    float tailR = (braking || handbrake) ? 1.00f : 0.90f;
    float tailG = (braking || handbrake) ? 0.25f : 0.08f;
    float tailB = (braking || handbrake) ? 0.12f : 0.10f;

    glBegin(GL_QUADS);
    glColor3f(tailR, tailG, tailB);
    glVertex3f(-1.02f, 0.62f, -1.97f); glVertex3f(-0.38f, 0.62f, -1.97f);
    glVertex3f(-0.38f, 0.79f, -1.89f); glVertex3f(-0.95f, 0.79f, -1.89f);
    glColor3f(1.0f, 0.82f, 0.55f);
    glVertex3f(-0.96f, 0.68f, -1.98f); glVertex3f(-0.46f, 0.68f, -1.98f);
    glVertex3f(-0.46f, 0.73f, -1.93f); glVertex3f(-0.92f, 0.73f, -1.93f);

    glColor3f(tailR, tailG, tailB);
    glVertex3f( 0.38f, 0.62f, -1.97f); glVertex3f( 1.02f, 0.62f, -1.97f);
    glVertex3f( 0.95f, 0.79f, -1.89f); glVertex3f( 0.38f, 0.79f, -1.89f);
    glColor3f(1.0f, 0.82f, 0.55f);
    glVertex3f( 0.46f, 0.68f, -1.98f); glVertex3f( 0.96f, 0.68f, -1.98f);
    glVertex3f( 0.92f, 0.73f, -1.93f); glVertex3f( 0.46f, 0.73f, -1.93f);

    glColor3f(tailR * 0.95f, 0.08f, 0.08f);
    glVertex3f(-0.38f, 0.69f, -1.97f); glVertex3f( 0.38f, 0.69f, -1.97f);
    glVertex3f( 0.38f, 0.74f, -1.92f); glVertex3f(-0.38f, 0.74f, -1.92f);

    if (braking || handbrake) {
        glColor3f(1.0f, 0.15f, 0.12f);
        glVertex3f(-0.36f, 1.23f, -1.24f); glVertex3f( 0.36f, 1.23f, -1.24f);
        glVertex3f( 0.34f, 1.28f, -1.19f); glVertex3f(-0.34f, 1.28f, -1.19f);
    }
    if (reversing) {
        glColor3f(0.95f, 0.98f, 1.0f);
        glVertex3f(-0.65f, 0.63f, -1.99f); glVertex3f(-0.44f, 0.63f, -1.99f);
        glVertex3f(-0.44f, 0.68f, -1.96f); glVertex3f(-0.65f, 0.68f, -1.96f);
        glVertex3f( 0.44f, 0.63f, -1.99f); glVertex3f( 0.65f, 0.63f, -1.99f);
        glVertex3f( 0.65f, 0.68f, -1.96f); glVertex3f( 0.44f, 0.68f, -1.96f);
    }

    // WRC Plaka
    glColor3f(0.08f, 0.08f, 0.10f);
    glVertex3f(-0.34f, 0.45f, -1.96f); glVertex3f( 0.34f, 0.45f, -1.96f);
    glVertex3f( 0.34f, 0.59f, -1.95f); glVertex3f(-0.34f, 0.59f, -1.95f);
    glColor3f(0.98f, 0.86f, 0.12f);
    glVertex3f(-0.30f, 0.47f, -1.97f); glVertex3f( 0.30f, 0.47f, -1.97f);
    glVertex3f( 0.30f, 0.57f, -1.96f); glVertex3f(-0.30f, 0.57f, -1.96f);
    glEnd();
    glEnable(GL_LIGHTING);

    // Difuzor, Pacaliklar ve Cift Egzoz
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, specLow);
    glColor3f(0.12f, 0.13f, 0.16f);
    drawLitBox(-0.76f, 0.18f, -2.02f, 0.76f, 0.44f, -1.85f);

    glColor3f(0.24f, 0.26f, 0.32f);
    for (int f = -2; f <= 2; ++f) {
        float fx = f * 0.30f;
        drawLitBox(fx - 0.02f, 0.16f, -2.06f, fx + 0.02f, 0.42f, -1.96f);
    }

    glColor3f(rAcc, gAcc, bAcc);
    drawLitBox(-1.22f, 0.08f, -1.62f, -0.82f, 0.48f, -1.58f);
    drawLitBox( 0.82f, 0.08f, -1.62f,  1.22f, 0.48f, -1.58f);

    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, specHigh);
    float exPos[2] = { -0.46f, 0.46f };
    for (int e = 0; e < 2; ++e) {
        float ex = exPos[e];
        glColor3f(0.78f, 0.82f, 0.88f);
        drawLitBox(ex - 0.08f, 0.22f, -2.08f, ex + 0.08f, 0.34f, -1.96f);
        glColor3f(0.06f, 0.06f, 0.08f);
        drawLitBox(ex - 0.05f, 0.24f, -2.09f, ex + 0.05f, 0.32f, -2.04f);
    }

    // Tavan Scoop, Anten, Yan Aynalar ve Cift Katli WRC Spoiler
    glColor3f(rAcc, gAcc, bAcc);
    drawLitBox(-0.18f, 1.30f, -0.55f, 0.18f, 1.40f, -0.05f);
    glColor3f(0.15f, 0.15f, 0.18f);
    drawLitBox(-0.015f, 1.30f, -0.85f, 0.015f, 1.72f, -0.82f);
    glColor3f(rAcc, gAcc, bAcc);
    drawLitBox(-1.24f, 0.86f, 0.32f, -0.96f, 0.96f, 0.46f);
    drawLitBox( 0.96f, 0.86f, 0.32f,  1.24f, 0.96f, 0.46f);

    glColor3f(0.12f, 0.13f, 0.16f);
    drawLitBox(-0.62f, 0.82f, -1.86f, -0.54f, 1.40f, -1.72f);
    drawLitBox( 0.54f, 0.82f, -1.86f,  0.62f, 1.40f, -1.72f);
    glColor3f(rAcc, gAcc, bAcc);
    drawLitBox(-1.10f, 1.36f, -1.98f,  1.10f, 1.42f, -1.64f);
    glColor3f(0.14f, 0.15f, 0.18f);
    drawLitBox(-1.06f, 1.18f, -1.95f,  1.06f, 1.23f, -1.68f);
    glColor3f(rMain, gMain, bMain);
    drawLitBox(-1.12f, 1.10f, -2.00f, -1.07f, 1.46f, -1.60f);
    drawLitBox( 1.07f, 1.10f, -2.00f,  1.12f, 1.46f, -1.60f);

    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, specLow);

    if (backfireFrames > 0 || hybridActive) {
        float fLen = hybridActive ? 1.15f : 0.65f;
        glDisable(GL_LIGHTING);
        glDepthMask(GL_FALSE);
        for (int e = 0; e < 2; ++e) {
            float ex = exPos[e];
            glBegin(GL_TRIANGLE_FAN);
            glColor4f(1.0f, 0.98f, 0.85f, 0.95f);
            glVertex3f(ex, 0.28f, -2.10f);
            for (int k = 0; k <= 12; ++k) {
                float a = k * 2.0f * 3.14159f / 12.0f;
                if (hybridActive) glColor4f(0.15f, 0.75f, 1.0f, 0.0f);
                else              glColor4f(1.0f, 0.45f, 0.05f, 0.0f);
                glVertex3f(ex + std::cos(a) * 0.22f, 0.28f + std::sin(a) * 0.22f, -2.10f - fLen);
            }
            glEnd();
        }
        glDepthMask(GL_TRUE);
        glEnable(GL_LIGHTING);
    }

    glPopMatrix();
}

int main() {
    Display* dpy = XOpenDisplay(NULL);
    if (!dpy) {
        std::cerr << "HATA: X11 Ekran sunucusu acilamadi!\n";
        return 1;
    }

    Window root = DefaultRootWindow(dpy);
    GLint attMSAA[] = {
        GLX_RGBA, GLX_DEPTH_SIZE, 24, GLX_DOUBLEBUFFER,
        GLX_SAMPLE_BUFFERS, 1, GLX_SAMPLES, 4, None
    };
    XVisualInfo* vi = glXChooseVisual(dpy, 0, attMSAA);
    if (!vi) {
        GLint attStd[] = { GLX_RGBA, GLX_DEPTH_SIZE, 24, GLX_DOUBLEBUFFER, None };
        vi = glXChooseVisual(dpy, 0, attStd);
    }
    if (!vi) return 1;

    Colormap cmap = XCreateColormap(dpy, root, vi->visual, AllocNone);
    XSetWindowAttributes swa;
    swa.colormap = cmap;
    swa.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask | StructureNotifyMask;

    int winW = 1280, winH = 720;
    Window win = XCreateWindow(dpy, root, 0, 0, winW, winH, 0, vi->depth, InputOutput,
                               vi->visual, CWColormap | CWEventMask, &swa);
    XMapWindow(dpy, win);
    XStoreName(dpy, win, "WRC RALLY1 HYBRID v6.1 - HD TEXTURED, 4x MSAA & 3D LIT ENGINE");

    Atom wmDelete = XInternAtom(dpy, "WM_DELETE_WINDOW", True);
    XSetWMProtocols(dpy, win, &wmDelete, 1);

    GLXContext glc = glXCreateContext(dpy, vi, NULL, GL_TRUE);
    glXMakeCurrent(dpy, win, glc);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glShadeModel(GL_SMOOTH);
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_NORMALIZE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

    GLfloat globalAmb[4] = { 0.42f, 0.44f, 0.50f, 1.0f };
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, globalAmb);
    GLfloat lightDiff[4] = { 0.92f, 0.90f, 0.85f, 1.0f };
    GLfloat lightSpec[4] = { 0.85f, 0.85f, 0.90f, 1.0f };
    glLightfv(GL_LIGHT0, GL_DIFFUSE, lightDiff);
    glLightfv(GL_LIGHT0, GL_SPECULAR, lightSpec);

    glEnable(GL_FOG);
    GLfloat fogColor[4] = {0.58f, 0.72f, 0.86f, 1.0f};
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogfv(GL_FOG_COLOR, fogColor);
    glFogf(GL_FOG_START, 140.0f);
    glFogf(GL_FOG_END, 370.0f);

    TextureBank textures;
    textures.initAll();

    InputManager input;
    input.tryOpenJoystick();

    const int NUM_NODES = 6000;
    const float SEG_LEN = 3.0f;
    const float ROAD_HALF_W = 8.2f;
    std::vector<StageNode> stage(NUM_NODES);

    struct TargetSection {
        int durationSegs;
        float targetHeadingDeg;
        float slope;
        int pType;
        const char* pText;
    };
    const std::vector<TargetSection> sections = {
        { 35,   0.0f,  0.00f, 0, "6 STRAIGHT FLAT OUT" },
        { 45, -45.0f,  0.00f, 5, "3 LEFT INTO 3 RIGHT" },
        { 50, +48.0f,  0.00f, 6, "3 RIGHT LONG KEEP IN" },
        { 30,   0.0f,  0.00f, 0, "6 STRAIGHT" },
        { 45, -52.0f,  0.00f, 3, "2 SQUARE LEFT DONT CUT" },
        { 55, +54.0f,  0.00f, 2, "1 HAIRPIN RIGHT HANDBRAKE!" },
        { 40,   0.0f,  0.048f,7, "6 CREST UP FULL THROTTLE" },
        { 40,   0.0f, -0.048f,7, "6 OVER JUMP KEEP MIDDLE" },
        { 55, -54.0f,  0.00f, 1, "1 HAIRPIN LEFT HANDBRAKE!" },
        { 50, +50.0f,  0.00f, 4, "2 SQUARE RIGHT OPEN" },
        { 40, -32.0f,  0.00f, 5, "4 LEFT FAST SWEEPER" },
        { 35,   0.0f,  0.00f, 0, "6 STRAIGHT THROUGH ARCH" }
    };

    Vec3 curPos(0.0f, 0.0f, 0.0f);
    float curHeading = 0.0f, curSlope = 0.0f;
    int secIdx = 0, secTimer = 0;

    for (int i = 0; i < NUM_NODES; ++i) {
        const auto& sec = sections[secIdx];
        float targetRad = sec.targetHeadingDeg * (3.14159265f / 180.0f);
        float desiredC = std::clamp((targetRad - curHeading) * 0.045f, -0.020f, 0.020f);
        curHeading = std::clamp(curHeading + desiredC * SEG_LEN, -0.96f, 0.96f);
        curSlope += (sec.slope - curSlope) * 0.05f;

        Vec3 fwd(std::sin(curHeading), 0.0f, std::cos(curHeading));
        Vec3 rgt(std::cos(curHeading), 0.0f, -std::sin(curHeading));

        stage[i].pos          = curPos;
        stage[i].forward      = fwd;
        stage[i].right        = rgt;
        stage[i].heading      = curHeading;
        stage[i].curvature    = desiredC;
        stage[i].surface      = ((i / 650) + 2) % 3;
        stage[i].pacenoteType = sec.pType;
        stage[i].noteText     = sec.pText;
        stage[i].hasChicaneBale = (secTimer == 8 && (sec.pType == 1 || sec.pType == 2));
        stage[i].baleOffset   = (sec.targetHeadingDeg > 0) ? 3.6f : -3.6f;

        curPos = curPos + fwd * SEG_LEN + Vec3(0.0f, curSlope * SEG_LEN, 0.0f);
        if (++secTimer >= sec.durationSegs) { secTimer = 0; secIdx = (secIdx + 1) % sections.size(); }
    }

    for (int pass = 0; pass < 5; ++pass) {
        for (int i = 2; i < NUM_NODES - 2; ++i) {
            stage[i].pos.y = (stage[i - 1].pos.y + stage[i].pos.y * 2.0f + stage[i + 1].pos.y) * 0.25f;
        }
    }

    float trackS = 25.0f, latD = 0.0f, relYaw = 0.0f, vLong = 0.0f, vLat = 0.0f, yawRate = 0.0f;
    float frontSteerAngle = 0.0f, wheelSpin = 0.0f, airY = 0.0f, airVelY = 0.0f;
    bool airborne = false;
    float bodyRoll = 0.0f, bodyPitch = 0.0f, suspBounce = 0.0f, suspVel = 0.0f;

    int gear = 1;
    float rpm = 1200.0f, autoShiftCooldown = 0.0f, reverseHoldTimer = 0.0f;
    const float gearTopKmh[7]     = { 0.0f, 44.0f, 76.0f, 110.0f, 144.0f, 176.0f, 208.0f };
    const float gearUpKmh[7]      = { 0.0f, 40.0f, 70.0f, 102.0f, 135.0f, 166.0f, 999.0f };
    const float gearDownKmh[7]    = { 0.0f,  0.0f, 28.0f,  55.0f,  86.0f, 118.0f, 150.0f };
    const float gearTorqueMult[7] = { 0.0f, 1.38f, 1.06f,  0.84f,  0.66f,  0.52f,  0.41f };

    float hybridCharge = 1.0f;
    int backfireFrames = 0;
    float camRelYaw = 0.0f;
    Vec3 prevCarWorldPos = stage[8].pos;

    float stageTime = 0.0f;
    long driftPoints = 0;
    std::string hudAlert = "v6.1 HD TEXTURE & 3D LIGHT MOTORU AKTIF! GAZA BAS VE YANLA!";
    float alertTimer = 4.5f;

    std::vector<SkidMark3D> skidMarks;
    std::vector<Particle3D> particles;

    const char* carNames[3] = { "HYUNDAI i20 N RALLY1", "TOYOTA GR YARIS RALLY1", "FORD PUMA RALLY1" };
    const char* surfaceNames[3] = { "SUPER SPECIAL STAGE (CITY NIGHT)", "ALPINE MOUNTAIN ASPHALT", "FINLAND GRAVEL STAGE" };

    bool quit = false;
    auto lastTime = std::chrono::steady_clock::now();

    while (!quit) {
        while (XPending(dpy) > 0) {
            XEvent xev;
            XNextEvent(dpy, &xev);
            if (xev.type == ConfigureNotify) {
                winW = std::max(320, xev.xconfigure.width);
                winH = std::max(240, xev.xconfigure.height);
                glViewport(0, 0, winW, winH);
            } else if (xev.type == ClientMessage) {
                if ((Atom)xev.xclient.data.l[0] == wmDelete) quit = true;
            }
        }

        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;
        dt = std::clamp(dt, 0.004f, 0.033f);
        stageTime += dt;
        if (alertTimer > 0.0f) alertTimer = std::max(0.0f, alertTimer - dt);
        if (autoShiftCooldown > 0.0f) autoShiftCooldown = std::max(0.0f, autoShiftCooldown - dt);

        input.update(dpy, dt, quit);

        float maxStageDist = (NUM_NODES - 150) * SEG_LEN;
        if (trackS >= maxStageDist) trackS = 30.0f;
        if (trackS < 15.0f) trackS = 15.0f;

        int segIdx = (int)(trackS / SEG_LEN);
        float segFrac = (trackS / SEG_LEN) - segIdx;
        const StageNode& n0 = stage[segIdx];
        const StageNode& n1 = stage[segIdx + 1];

        Vec3 roadCenter = n0.pos * (1.0f - segFrac) + n1.pos * segFrac;
        Vec3 roadFwd    = normalize(n0.forward * (1.0f - segFrac) + n1.forward * segFrac);
        Vec3 roadRight  = normalize(n0.right * (1.0f - segFrac) + n1.right * segFrac);
        float roadCurvature = n0.curvature * (1.0f - segFrac) + n1.curvature * segFrac;

        if (input.resetCar) {
            latD = 0.0f; relYaw = 0.0f; vLong = 0.0f; vLat = 0.0f;
            yawRate = 0.0f; gear = 1; airborne = false; airY = 0.0f;
        }

        // OTOMATIK SANZIMAN (R - 1 - 2 - 3 - 4 - 5 - 6)
        float speed = std::sqrt(vLong * vLong + vLat * vLat);
        float kmh = std::fabs(vLong) * 3.6f;
        float surfaceGrip = (n0.surface == 2) ? 0.78f : 1.0f;

        if (input.brake > 0.2f || input.handbrake) hybridCharge = std::min(1.0f, hybridCharge + 0.30f * dt);
        bool hybridActive = (input.boost && hybridCharge > 0.05f && input.throttle > 0.2f && gear >= 1);
        if (hybridActive) hybridCharge = std::max(0.0f, hybridCharge - 0.28f * dt);

        if (gear >= 1) {
            if (vLong < 0.4f && input.brake > 0.5f && input.throttle < 0.1f) {
                reverseHoldTimer += dt;
                if (reverseHoldTimer > 0.35f) { gear = -1; reverseHoldTimer = 0.0f; }
            } else reverseHoldTimer = 0.0f;
        } else if (gear == -1) {
            if (input.throttle > 0.15f || (std::fabs(vLong) < 0.4f && input.brake < 0.1f)) {
                if (vLong < -0.5f) vLong = std::min(0.0f, vLong + 25.0f * dt);
                else { gear = 1; vLong = 0.0f; }
            }
        }

        if (gear >= 1 && !airborne && autoShiftCooldown <= 0.0f) {
            if (gear < 6 && kmh >= gearUpKmh[gear] && input.throttle > 0.25f) {
                gear++; autoShiftCooldown = 0.30f; backfireFrames = 7; bodyPitch = -0.035f;
            } else if (gear > 1 && kmh <= gearDownKmh[gear]) {
                gear--; autoShiftCooldown = 0.22f; backfireFrames = 6; bodyPitch = 0.030f;
            }
        }

        float engineAccel = 0.0f, brakeDecel = 0.0f;
        if (gear == -1) {
            float revRatio = std::clamp(kmh / 36.0f, 0.0f, 1.0f);
            rpm += ((1300.0f + revRatio * 5600.0f) - rpm) * 12.0f * dt;
            if (input.brake > 0.1f && kmh < 36.0f) engineAccel = -input.brake * 9.5f * surfaceGrip;
            vLong = std::clamp(vLong + (engineAccel - 0.35f * vLong) * dt, -10.0f, 0.2f);
        } else {
            float gMinKmh = (gear == 1) ? 0.0f : gearDownKmh[gear] * 0.78f;
            float gearRatio = std::clamp((kmh - gMinKmh) / std::max(12.0f, gearTopKmh[gear] - gMinKmh), 0.0f, 1.0f);
            float wheelSlipRpm = (input.handbrake || std::fabs(vLat) > 2.2f) ? (input.throttle * 850.0f) : 0.0f;
            rpm += (std::clamp(1250.0f + gearRatio * 7000.0f + wheelSlipRpm, 1200.0f, 8450.0f) - rpm) * 14.0f * dt;

            if (!airborne) {
                float basePower = (11.4f + (hybridActive ? 3.2f : 0.0f)) * gearTorqueMult[gear];
                engineAccel = input.throttle * basePower * surfaceGrip;
                brakeDecel  = (input.brake * 24.0f + (input.handbrake ? 6.5f : 0.0f)) * surfaceGrip;
            }
            float aeroDrag = 0.06f * vLong + 0.0042f * vLong * std::fabs(vLong);
            float engBrake = (input.throttle < 0.05f && !airborne) ? (1.4f * gearTorqueMult[gear]) : 0.0f;
            vLong = std::clamp(vLong + (engineAccel - brakeDecel - aeroDrag - engBrake) * dt, 0.0f, 208.0f / 3.6f);
        }

        // 4WD DRIFT FIZIGI
        float slipAngle = (speed > 1.5f) ? std::atan2(vLat, std::max(1.5f, std::fabs(vLong))) : 0.0f;
        bool isCounterSteering = (input.steer * slipAngle > 0.02f);
        float speedSteerLimit = isCounterSteering ? 0.55f : std::clamp(0.52f - (kmh / 220.0f) * 0.28f, 0.22f, 0.52f);
        frontSteerAngle += (input.steer * speedSteerLimit - frontSteerAngle) * std::min(1.0f, 9.0f * dt);

        if (!airborne) {
            float dirSign = (vLong >= -0.1f) ? 1.0f : -1.0f;
            float speedFactor = std::clamp(std::fabs(vLong) / 7.5f, 0.0f, 1.0f);
            float desiredYawRate = frontSteerAngle * 2.25f * speedFactor * dirSign;

            if (input.handbrake && speed > 4.5f) desiredYawRate += input.steer * 0.90f * speedFactor;
            else if (input.throttle > 0.65f && gear >= 1 && gear <= 3 && std::fabs(frontSteerAngle) > 0.12f)
                desiredYawRate += (frontSteerAngle > 0 ? 1.0f : -1.0f) * 0.35f * input.throttle;

            float selfAlignYaw = -slipAngle * 1.45f * surfaceGrip;
            yawRate = std::clamp(yawRate + ((desiredYawRate + selfAlignYaw - yawRate) * 6.0f * surfaceGrip) * dt, -1.75f, 1.75f);

            float rearLatGrip = 4.2f * surfaceGrip;
            if (input.handbrake) rearLatGrip = 1.05f;
            else if (input.throttle > 0.65f && std::fabs(slipAngle) > 0.12f) rearLatGrip = 2.05f * surfaceGrip;

            vLat = std::clamp(vLat + ((-vLong * yawRate * 0.52f) - (vLat * rearLatGrip)) * dt, -17.0f, 17.0f);
        } else {
            yawRate *= (1.0f - 1.0f * dt);
        }

        // FRENET HAREKETI VE DUVAR KORUMASI
        float vS = vLong * std::cos(relYaw) - vLat * std::sin(relYaw);
        float vD = vLong * std::sin(relYaw) + vLat * std::cos(relYaw);
        float dS = (vS / std::clamp(1.0f - roadCurvature * latD, 0.50f, 1.55f)) * dt;

        trackS += dS;
        latD   += vD * dt;
        relYaw = std::clamp(wrapAngle(relYaw + yawRate * dt - roadCurvature * dS), -1.75f, 1.75f);

        float carLatExtent = std::fabs(std::sin(relYaw)) * 2.02f + std::fabs(std::cos(relYaw)) * 1.15f;
        float maxLatD = std::max(1.2f, ROAD_HALF_W - carLatExtent - 0.15f);

        if (std::fabs(latD) > maxLatD) {
            float wallSign = (latD > 0.0f) ? 1.0f : -1.0f;
            latD = wallSign * maxLatD;
            if (vD * wallSign > 0.0f) { vLong *= std::max(0.80f, 1.0f - 1.6f * dt); vLat *= 0.35f; }
            relYaw *= (1.0f - 5.5f * dt); yawRate *= 0.3f;
            hudAlert = "BARIYER SURTUNMESI!"; alertTimer = 0.6f;
        }

        if (n0.hasChicaneBale && segFrac < 0.65f) {
            float distLat = latD - n0.baleOffset;
            if (std::fabs(distLat) < 2.20f) {
                latD = n0.baleOffset + ((distLat >= 0.0f) ? 2.20f : -2.20f);
                vLong *= 0.68f; vLat *= 0.40f; yawRate *= 0.4f;
                hudAlert = "SAMAN BALYASI SIKANI!"; alertTimer = 1.0f;
            }
        }

        int newSegIdx = std::clamp((int)(trackS / SEG_LEN), 2, NUM_NODES - 140);
        float newFrac = (trackS / SEG_LEN) - newSegIdx;
        const StageNode& sn0 = stage[newSegIdx];
        const StageNode& sn1 = stage[newSegIdx + 1];

        Vec3 curRoadCenter = sn0.pos * (1.0f - newFrac) + sn1.pos * newFrac;
        Vec3 curRoadFwd    = normalize(sn0.forward * (1.0f - newFrac) + sn1.forward * newFrac);
        Vec3 curRoadRight  = normalize(sn0.right * (1.0f - newFrac) + sn1.right * newFrac);
        float curRoadHead  = std::atan2(curRoadFwd.x, curRoadFwd.z);

        if (airborne) {
            airVelY -= 22.0f * dt; airY += airVelY * dt;
            if (airY <= 0.0f) {
                airY = 0.0f; airborne = false;
                suspVel = -std::min(4.2f, std::fabs(airVelY) * 0.38f); airVelY = 0.0f;
            }
        } else {
            airY = 0.0f;
            if (sn0.pacenoteType == 7 && ((sn1.pos.y - sn0.pos.y) / SEG_LEN) < -0.030f && kmh > 115.0f) {
                airborne = true; airVelY = 3.2f; airY = 0.08f;
            }
        }

        Vec3 carWorldPos = curRoadCenter + curRoadRight * latD + Vec3(0.0f, airY, 0.0f);
        float carWorldYaw = wrapAngle(curRoadHead + relYaw);
        Vec3 carFwd(std::sin(carWorldYaw), 0.0f, std::cos(carWorldYaw));
        Vec3 carRight(std::cos(carWorldYaw), 0.0f, -std::sin(carWorldYaw));

        wheelSpin += (vLong * 2.5f + (input.handbrake ? 0.0f : input.throttle * 6.0f)) * dt;
        suspVel += (-suspBounce * 65.0f - suspVel * 10.0f) * dt;
        suspBounce = std::clamp(suspBounce + suspVel * dt, -0.14f, 0.14f);
        float bumpNoise = (speed > 4.0f && !airborne) ? std::sin(trackS * 2.6f) * (sn0.surface == 2 ? 0.020f : 0.006f) : 0.0f;

        bodyRoll  += (std::clamp(vLat * 0.020f + frontSteerAngle * 0.12f, -0.16f, 0.16f) - bodyRoll) * 8.5f * dt;
        bodyPitch += (((airborne ? -0.07f : 0.0f) - engineAccel * 0.0040f + brakeDecel * 0.0030f) - bodyPitch) * 8.5f * dt;

        float slipDeg = std::fabs(slipAngle) * 57.2958f;
        bool isDrifting = (!airborne && (slipDeg > 9.0f || input.handbrake) && kmh > 25.0f);

        if (isDrifting) {
            driftPoints += (long)(slipDeg * speed * 0.18f * dt);
            Vec3 rearAxle = carWorldPos - carFwd * 1.28f + Vec3(0, 0.02f, 0);
            Vec3 prevRear = prevCarWorldPos - carFwd * 1.28f + Vec3(0, 0.02f, 0);
            if (dot(rearAxle - prevRear, rearAxle - prevRear) < 6.0f) {
                skidMarks.push_back({
                    prevRear - carRight * 1.02f, rearAxle - carRight * 1.02f,
                    prevRear + carRight * 1.02f, rearAxle + carRight * 1.02f
                });
                if (skidMarks.size() > 380) skidMarks.erase(skidMarks.begin());
            }
            for (int s = 0; s < 2; ++s) {
                Vec3 tirePos = rearAxle + carRight * (s == 0 ? -1.02f : 1.02f);
                float sr = (sn0.surface == 2) ? 0.78f : 0.88f;
                float sg = (sn0.surface == 2) ? 0.66f : 0.90f;
                float sb = (sn0.surface == 2) ? 0.48f : 0.94f;
                particles.push_back({
                    tirePos, Vec3((rand() % 20 - 10) * 0.08f, 0.9f + (rand() % 10) * 0.08f, (rand() % 20 - 10) * 0.08f),
                    0.45f, 0.52f, 0.52f, sr, sg, sb
                });
            }
        }
        prevCarWorldPos = carWorldPos;
        if (backfireFrames > 0) backfireFrames--;

        // 3D SAHNE RENDERING
        if (sn0.surface == 0)      { fogColor[0] = 0.16f; fogColor[1] = 0.14f; fogColor[2] = 0.28f; }
        else if (sn0.surface == 1) { fogColor[0] = 0.56f; fogColor[1] = 0.36f; fogColor[2] = 0.38f; }
        else                       { fogColor[0] = 0.58f; fogColor[1] = 0.72f; fogColor[2] = 0.86f; }

        glFogfv(GL_FOG_COLOR, fogColor);
        glClearColor(fogColor[0], fogColor[1], fogColor[2], 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        setPerspectiveGL(54.0f + (kmh / 210.0f) * 11.0f + (hybridActive ? 4.0f : 0.0f), (float)winW / (float)winH, 0.4f, 420.0f);

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        camRelYaw += (relYaw * 0.50f - camRelYaw) * 7.5f * dt;
        float camWorldYaw = wrapAngle(curRoadHead + camRelYaw);
        Vec3 camDir(std::sin(camWorldYaw), 0.0f, std::cos(camWorldYaw));

        Vec3 eye, lookTarget;
        if (input.camMode == 0) {
            eye        = carWorldPos - camDir * 5.2f + Vec3(0.0f, 1.92f, 0.0f);
            lookTarget = carWorldPos + camDir * 6.5f + Vec3(0.0f, 0.78f, 0.0f);
        } else if (input.camMode == 1) {
            eye        = carWorldPos - camDir * 7.0f + Vec3(0.0f, 2.55f, 0.0f);
            lookTarget = carWorldPos + camDir * 8.0f + Vec3(0.0f, 0.82f, 0.0f);
        } else {
            eye        = carWorldPos + carFwd * 0.35f + Vec3(0.0f, 1.18f + suspBounce, 0.0f);
            lookTarget = carWorldPos + carFwd * 15.0f + Vec3(0.0f, 0.95f, 0.0f);
        }

        if (input.camMode != 2) {
            Vec3 eyeRel = eye - curRoadCenter;
            float eyeLat = eyeRel.x * curRoadRight.x + eyeRel.z * curRoadRight.z;
            float maxEyeLat = ROAD_HALF_W - 0.95f;
            if (std::fabs(eyeLat) > maxEyeLat) eye = eye - curRoadRight * (eyeLat - (eyeLat > 0 ? maxEyeLat : -maxEyeLat));
        }
        setLookAtGL(eye, lookTarget, Vec3(0, 1, 0));

        GLfloat lightPos[4] = { -0.35f, 0.82f, 0.45f, 0.0f };
        glLightfv(GL_LIGHT0, GL_POSITION, lightPos);

        drawDetailedPanoramaSky(eye, sn0.surface, stageTime);

        glEnable(GL_LIGHTING);
        glEnable(GL_TEXTURE_2D);

        int startI = std::max(1, newSegIdx - 12);
        int endI   = std::min(NUM_NODES - 2, newSegIdx + 125);

        // 1. Dokulu Ana Yol
        glBindTexture(GL_TEXTURE_2D, (sn0.surface == 2) ? textures.roadGravel : textures.roadTarmac);
        glColor3f(1.0f, 1.0f, 1.0f);
        glBegin(GL_QUADS);
        for (int i = startI; i < endI; ++i) {
            const StageNode& s0 = stage[i];
            const StageNode& s1 = stage[i + 1];
            Vec3 cIn0L = s0.pos - s0.right * 7.45f, cIn1L = s1.pos - s1.right * 7.45f;
            Vec3 cIn0R = s0.pos + s0.right * 7.45f, cIn1R = s1.pos + s1.right * 7.45f;
            float v0 = i * 0.25f, v1 = (i + 1) * 0.25f;
            glNormal3f(0.0f, 1.0f, 0.0f);
            glTexCoord2f(0.0f, v0); glVertex3f(cIn0L.x, cIn0L.y, cIn0L.z);
            glTexCoord2f(1.0f, v0); glVertex3f(cIn0R.x, cIn0R.y, cIn0R.z);
            glTexCoord2f(1.0f, v1); glVertex3f(cIn1R.x, cIn1R.y, cIn1R.z);
            glTexCoord2f(0.0f, v1); glVertex3f(cIn1L.x, cIn1L.y, cIn1L.z);
        }
        glEnd();

        // 2. Dokulu Yan Arazi
        glBindTexture(GL_TEXTURE_2D, (sn0.surface == 2) ? textures.terrainDirt : textures.terrainGrass);
        glBegin(GL_QUADS);
        for (int i = startI; i < endI; ++i) {
            const StageNode& s0 = stage[i];
            const StageNode& s1 = stage[i + 1];
            Vec3 f0L = s0.pos - s0.right * 26.0f + Vec3(0, 2.4f, 0), f1L = s1.pos - s1.right * 26.0f + Vec3(0, 2.4f, 0);
            Vec3 f0R = s0.pos + s0.right * 26.0f + Vec3(0, 2.4f, 0), f1R = s1.pos + s1.right * 26.0f + Vec3(0, 2.4f, 0);
            Vec3 wOut0L = s0.pos - s0.right * 8.7f, wOut1L = s1.pos - s1.right * 8.7f;
            Vec3 wOut0R = s0.pos + s0.right * 8.7f, wOut1R = s1.pos + s1.right * 8.7f;
            float v0 = i * 0.30f, v1 = (i + 1) * 0.30f;

            glNormal3f(s0.right.x * 0.2f, 0.98f, s0.right.z * 0.2f);
            glTexCoord2f(0.0f, v0); glVertex3f(f0L.x, f0L.y, f0L.z);
            glTexCoord2f(2.5f, v0); glVertex3f(wOut0L.x, wOut0L.y, wOut0L.z);
            glTexCoord2f(2.5f, v1); glVertex3f(wOut1L.x, wOut1L.y, wOut1L.z);
            glTexCoord2f(0.0f, v1); glVertex3f(f1L.x, f1L.y, f1L.z);

            glNormal3f(-s0.right.x * 0.2f, 0.98f, -s0.right.z * 0.2f);
            glTexCoord2f(0.0f, v0); glVertex3f(wOut0R.x, wOut0R.y, wOut0R.z);
            glTexCoord2f(2.5f, v0); glVertex3f(f0R.x, f0R.y, f0R.z);
            glTexCoord2f(2.5f, v1); glVertex3f(f1R.x, f1R.y, f1R.z);
            glTexCoord2f(0.0f, v1); glVertex3f(wOut1R.x, wOut1R.y, wOut1R.z);
        }
        glEnd();

        // 3. Dokulu Bordurler ve Bariyerler
        glBindTexture(GL_TEXTURE_2D, textures.curbRedWhite);
        glBegin(GL_QUADS);
        for (int i = startI; i < endI; ++i) {
            const StageNode& s0 = stage[i];
            const StageNode& s1 = stage[i + 1];
            Vec3 wOut0L = s0.pos - s0.right * 8.7f, wOut1L = s1.pos - s1.right * 8.7f;
            Vec3 wIn0L  = s0.pos - s0.right * 8.2f, wIn1L  = s1.pos - s1.right * 8.2f;
            Vec3 cIn0L  = s0.pos - s0.right * 7.45f, cIn1L = s1.pos - s1.right * 7.45f;
            Vec3 cIn0R  = s0.pos + s0.right * 7.45f, cIn1R = s1.pos + s1.right * 7.45f;
            Vec3 wIn0R  = s0.pos + s0.right * 8.2f, wIn1R  = s1.pos + s1.right * 8.2f;
            Vec3 wOut0R = s0.pos + s0.right * 8.7f, wOut1R = s1.pos + s1.right * 8.7f;
            float v0 = i * 0.25f, v1 = (i + 1) * 0.25f;

            glNormal3f(0.0f, 1.0f, 0.0f);
            glTexCoord2f(0.0f, v0); glVertex3f(wIn0L.x, wIn0L.y, wIn0L.z);
            glTexCoord2f(1.0f, v0); glVertex3f(cIn0L.x, cIn0L.y, cIn0L.z);
            glTexCoord2f(1.0f, v1); glVertex3f(cIn1L.x, cIn1L.y, cIn1L.z);
            glTexCoord2f(0.0f, v1); glVertex3f(wIn1L.x, wIn1L.y, wIn1L.z);

            glTexCoord2f(0.0f, v0); glVertex3f(cIn0R.x, cIn0R.y, cIn0R.z);
            glTexCoord2f(1.0f, v0); glVertex3f(wIn0R.x, wIn0R.y, wIn0R.z);
            glTexCoord2f(1.0f, v1); glVertex3f(wIn1R.x, wIn1R.y, wIn1R.z);
            glTexCoord2f(0.0f, v1); glVertex3f(cIn1R.x, cIn1R.y, cIn1R.z);

            float bH = (std::fabs(s0.curvature) > 0.008f || s0.surface == 0) ? 0.82f : 0.52f;
            glNormal3f(s0.right.x, 0.2f, s0.right.z);
            glTexCoord2f(0.0f, v0); glVertex3f(wIn0L.x, wIn0L.y, wIn0L.z);
            glTexCoord2f(1.0f, v0); glVertex3f(wIn0L.x, wIn0L.y + bH, wIn0L.z);
            glTexCoord2f(1.0f, v1); glVertex3f(wIn1L.x, wIn1L.y + bH, wIn1L.z);
            glTexCoord2f(0.0f, v1); glVertex3f(wIn1L.x, wIn1L.y, wIn1L.z);

            glNormal3f(0.0f, 1.0f, 0.0f);
            glTexCoord2f(0.0f, v0); glVertex3f(wOut0L.x, wOut0L.y + bH, wOut0L.z);
            glTexCoord2f(1.0f, v0); glVertex3f(wIn0L.x, wIn0L.y + bH, wIn0L.z);
            glTexCoord2f(1.0f, v1); glVertex3f(wIn1L.x, wIn1L.y + bH, wIn1L.z);
            glTexCoord2f(0.0f, v1); glVertex3f(wOut1L.x, wOut1L.y + bH, wOut1L.z);

            glNormal3f(-s0.right.x, 0.2f, -s0.right.z);
            glTexCoord2f(0.0f, v0); glVertex3f(wIn0R.x, wIn0R.y, wIn0R.z);
            glTexCoord2f(1.0f, v0); glVertex3f(wIn0R.x, wIn0R.y + bH, wIn0R.z);
            glTexCoord2f(1.0f, v1); glVertex3f(wIn1R.x, wIn1R.y + bH, wIn1R.z);
            glTexCoord2f(0.0f, v1); glVertex3f(wIn1R.x, wIn1R.y, wIn1R.z);

            glNormal3f(0.0f, 1.0f, 0.0f);
            glTexCoord2f(0.0f, v0); glVertex3f(wIn0R.x, wIn0R.y + bH, wIn0R.z);
            glTexCoord2f(1.0f, v0); glVertex3f(wOut0R.x, wOut0R.y + bH, wOut0R.z);
            glTexCoord2f(1.0f, v1); glVertex3f(wOut1R.x, wOut1R.y + bH, wOut1R.z);
            glTexCoord2f(0.0f, v1); glVertex3f(wIn1R.x, wIn1R.y + bH, wIn1R.z);
        }
        glEnd();

        // 4. Agaclar, Kayalar ve Binalar
        int objEndI = std::min(NUM_NODES - 2, newSegIdx + 90);
        for (int i = startI; i < objEndI; ++i) {
            const StageNode& s0 = stage[i];
            if (i % 4 == 0) {
                for (int side = -1; side <= 1; side += 2) {
                    float distOut = 13.5f + ((i * 7 + (side > 0 ? 3 : 0)) % 6);
                    float yGround = ((distOut - 8.7f) / (26.0f - 8.7f)) * 2.4f;
                    Vec3 objPos = s0.pos + s0.right * (side * distOut) + Vec3(0, yGround, 0);

                    glPushMatrix();
                    glTranslatef(objPos.x, objPos.y, objPos.z);
                    glRotatef(s0.heading * 57.2958f, 0, 1, 0);

                    if (s0.surface == 0) {
                        glEnable(GL_TEXTURE_2D);
                        glBindTexture(GL_TEXTURE_2D, textures.cityBuilding);
                        glColor3f(0.90f, 0.92f, 1.0f);
                        float h = 12.0f + ((i * 17 + (side > 0 ? 7 : 0)) % 14);
                        glBegin(GL_QUADS);
                        glNormal3f(0, 0, -1);
                        glTexCoord2f(0, 0); glVertex3f(-3.5f, -1.5f, -3.5f);
                        glTexCoord2f(2, 0); glVertex3f( 3.5f, -1.5f, -3.5f);
                        glTexCoord2f(2, 4); glVertex3f( 3.5f,     h, -3.5f);
                        glTexCoord2f(0, 4); glVertex3f(-3.5f,     h, -3.5f);
                        glNormal3f(-side, 0, 0);
                        glTexCoord2f(0, 0); glVertex3f(-side * 3.5f, -1.5f, -3.5f);
                        glTexCoord2f(2, 0); glVertex3f(-side * 3.5f, -1.5f,  3.5f);
                        glTexCoord2f(2, 4); glVertex3f(-side * 3.5f,     h,  3.5f);
                        glTexCoord2f(0, 4); glVertex3f(-side * 3.5f,     h, -3.5f);
                        glEnd();
                    } else {
                        drawDetailedConiferTree(textures.treePine, 0.85f + ((i * 13 + (side > 0 ? 5 : 0)) % 5) * 0.10f);
                    }
                    glPopMatrix();
                }
            }

            if (i % 7 == 2 && s0.surface != 0) {
                float side = (i % 2 == 0) ? -1.0f : 1.0f;
                Vec3 rockPos = s0.pos + s0.right * (side * 10.6f) + Vec3(0, 0.25f, 0);
                glPushMatrix();
                glTranslatef(rockPos.x, rockPos.y, rockPos.z);
                glRotatef((i * 47) % 360, 0, 1, 0);
                drawRockBoulder(0.85f + (i % 3) * 0.25f);
                glPopMatrix();
            }

            if (s0.hasChicaneBale) {
                Vec3 bPos = s0.pos + s0.right * s0.baleOffset;
                glPushMatrix();
                glTranslatef(bPos.x, bPos.y, bPos.z);
                glEnable(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, textures.curbRedWhite);
                glColor3f(1.0f, 1.0f, 1.0f);
                glBegin(GL_QUAD_STRIP);
                for (int k = 0; k <= 16; ++k) {
                    float a = k * 2.0f * 3.14159f / 16.0f;
                    float cx = std::cos(a) * 1.05f, cz = std::sin(a) * 1.05f;
                    glNormal3f(std::cos(a), 0.0f, std::sin(a));
                    glTexCoord2f((float)k / 8.0f, 0.0f); glVertex3f(cx, 0.0f, cz);
                    glTexCoord2f((float)k / 8.0f, 1.0f); glVertex3f(cx, 1.20f, cz);
                }
                glEnd();
                glPopMatrix();
            }
        }

        glDisable(GL_TEXTURE_2D);
        glDisable(GL_LIGHTING);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-1.0f, -1.0f);
        glDepthMask(GL_FALSE);
        glColor4f(0.05f, 0.05f, 0.07f, 0.65f);
        glBegin(GL_QUADS);
        for (const auto& sm : skidMarks) {
            Vec3 dL = normalize(cross(sm.leftB - sm.leftA, Vec3(0, 1, 0))) * 0.19f;
            glVertex3f(sm.leftA.x - dL.x, sm.leftA.y, sm.leftA.z - dL.z);
            glVertex3f(sm.leftA.x + dL.x, sm.leftA.y, sm.leftA.z + dL.z);
            glVertex3f(sm.leftB.x + dL.x, sm.leftB.y, sm.leftB.z + dL.z);
            glVertex3f(sm.leftB.x - dL.x, sm.leftB.y, sm.leftB.z - dL.z);

            glVertex3f(sm.rightA.x - dL.x, sm.rightA.y, sm.rightA.z - dL.z);
            glVertex3f(sm.rightA.x + dL.x, sm.rightA.y, sm.rightA.z + dL.z);
            glVertex3f(sm.rightB.x + dL.x, sm.rightB.y, sm.rightB.z + dL.z);
            glVertex3f(sm.rightB.x - dL.x, sm.rightB.y, sm.rightB.z - dL.z);
        }
        glEnd();
        glDepthMask(GL_TRUE);
        glDisable(GL_POLYGON_OFFSET_FILL);
        glEnable(GL_LIGHTING);

        // 3D WRC ARACI CIZ
        glPushMatrix();
        glTranslatef(carWorldPos.x, carWorldPos.y, carWorldPos.z);
        glRotatef(carWorldYaw * 57.2958f, 0, 1, 0);
        drawTrueWRCRally1Car(input.carLivery, frontSteerAngle, wheelSpin,
                             bodyRoll, bodyPitch, suspBounce + bumpNoise,
                             (input.brake > 0.1f && gear >= 1), input.handbrake, (gear == -1),
                             backfireFrames, hybridActive);
        glPopMatrix();

        // DUMAN VE TOZ BULUTLARI
        glDisable(GL_LIGHTING);
        glDepthMask(GL_FALSE);
        Vec3 billRight(std::cos(camWorldYaw), 0.0f, -std::sin(camWorldYaw));
        for (std::size_t i = 0; i < particles.size();) {
            auto& sp = particles[i];
            sp.pos = sp.pos + sp.vel * dt;
            sp.size += 1.40f * dt;
            sp.life -= dt;
            if (sp.life <= 0.0f) { particles[i] = particles.back(); particles.pop_back(); continue; }
            float ratio = sp.life / sp.maxLife, rad = sp.size * 0.5f;
            glBegin(GL_TRIANGLE_FAN);
            glColor4f(sp.r, sp.g, sp.b, ratio * 0.42f);
            glVertex3f(sp.pos.x, sp.pos.y, sp.pos.z);
            glColor4f(sp.r, sp.g, sp.b, 0.0f);
            for (int k = 0; k <= 12; ++k) {
                float a = k * 2.0f * 3.14159f / 12.0f;
                Vec3 offset = billRight * (std::cos(a) * rad) + Vec3(0.0f, std::sin(a) * rad, 0.0f);
                glVertex3f(sp.pos.x + offset.x, sp.pos.y + offset.y, sp.pos.z + offset.z);
            }
            glEnd();
            ++i;
        }
        glDepthMask(GL_TRUE);

        // 2D HUD GOSTERGELERI
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_FOG);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, winW, winH, 0, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        int foundNote = 0, distToNote = 0;
        std::string foundText = "6 STRAIGHT FLAT OUT";
        for (int k = 2; k < 48; ++k) {
            int checkI = std::min(NUM_NODES - 5, newSegIdx + k);
            if (stage[checkI].pacenoteType != 0) {
                foundNote = stage[checkI].pacenoteType;
                foundText = stage[checkI].noteText;
                distToNote = (int)(k * SEG_LEN);
                break;
            }
        }

        float boxX = winW * 0.5f - 220.0f, boxY = 18.0f;
        drawHudRect(boxX, boxY, 440.0f, 74.0f, 0.06f, 0.08f, 0.12f, 0.88f);

        float nr = 0.15f, ng = 0.78f, nb = 0.30f;
        std::string badgeCode = "6";
        if (foundNote == 1 || foundNote == 2)      { nr = 0.92f; ng = 0.12f; nb = 0.15f; badgeCode = "HP"; }
        else if (foundNote == 3 || foundNote == 4) { nr = 0.95f; ng = 0.48f; nb = 0.05f; badgeCode = "2"; }
        else if (foundNote == 5 || foundNote == 6) { nr = 0.95f; ng = 0.80f; nb = 0.08f; badgeCode = "3"; }
        else if (foundNote == 7)                   { nr = 0.10f; ng = 0.65f; nb = 0.98f; badgeCode = "JP"; }

        drawHudRect(boxX + 6.0f, boxY + 6.0f, 68.0f, 62.0f, nr, ng, nb, 1.0f);
        drawHudText(boxX + 18.0f, boxY + 22.0f, 4.0f, badgeCode, 1.0f, 1.0f, 1.0f);
        drawHudText(boxX + 88.0f, boxY + 16.0f, 2.3f, foundText, 1.0f, 0.96f, 0.85f);

        std::string subLine = (alertTimer > 0.0f) ? hudAlert :
                              ((foundNote != 0) ? ("IN " + std::to_string(distToNote) + " M") : "FULL THROTTLE");
        drawHudText(boxX + 88.0f, boxY + 46.0f, 1.9f, subLine,
                    alertTimer > 0.0f ? 1.0f : 0.40f, alertTimer > 0.0f ? 0.45f : 0.90f, 0.25f);

        drawHudRect(18.0f, 18.0f, 365.0f, 92.0f, 0.06f, 0.08f, 0.12f, 0.84f);
        drawHudText(28.0f, 28.0f, 1.95f, surfaceNames[sn0.surface], 0.0f, 0.88f, 1.0f);
        char timeBuf[64], ptsBuf[64], spdBuf[32], rpmInfo[64];
        int mins = (int)(stageTime / 60.0f);
        snprintf(timeBuf, sizeof(timeBuf), "STAGE TIME: %02d:%05.2f  DIST: %dM", mins, stageTime - mins * 60.0f, (int)trackS);
        drawHudText(28.0f, 54.0f, 2.0f, timeBuf, 1.0f, 1.0f, 1.0f);

        snprintf(ptsBuf, sizeof(ptsBuf), "DRIFT SCORE: %ld  SLIP: %d DEG", driftPoints, (int)slipDeg);
        drawHudText(28.0f, 82.0f, 1.9f, ptsBuf, isDrifting ? 1.0f : 0.75f, isDrifting ? 0.85f : 0.78f, 0.20f);

        float hudX = winW - 365.0f, hudY = winH - 155.0f;
        drawHudRect(hudX, hudY, 347.0f, 137.0f, 0.06f, 0.08f, 0.12f, 0.92f);

        float rpmRatio = std::clamp(rpm / 8500.0f, 0.0f, 1.0f);
        drawHudRect(hudX + 12.0f, hudY + 10.0f, 323.0f, 14.0f, 0.18f, 0.20f, 0.25f, 1.0f);
        drawHudRect(hudX + 12.0f, hudY + 10.0f, 323.0f * rpmRatio, 14.0f,
                    (rpm > 7500.0f) ? 1.0f : 0.10f, (rpm > 7500.0f) ? 0.20f : 0.92f, 0.20f, 1.0f);

        const char* gLabels[7] = { "R", "1", "2", "3", "4", "5", "6" };
        const int   gValues[7] = { -1,   1,   2,   3,   4,   5,   6  };
        for (int g = 0; g < 7; ++g) {
            float gx = hudX + 12.0f + g * 46.5f, gy = hudY + 30.0f;
            if (gear == gValues[g]) {
                if (g == 0) drawHudRect(gx, gy, 42.0f, 24.0f, 0.95f, 0.15f, 0.18f, 1.0f);
                else        drawHudRect(gx, gy, 42.0f, 24.0f, 0.0f, 0.78f, 0.98f, 1.0f);
                drawHudText(gx + 15.0f, gy + 5.0f, 2.1f, gLabels[g], 1.0f, 1.0f, 1.0f);
            } else {
                drawHudRect(gx, gy, 42.0f, 24.0f, 0.16f, 0.18f, 0.24f, 1.0f);
                drawHudText(gx + 15.0f, gy + 5.0f, 2.0f, gLabels[g], 0.55f, 0.60f, 0.70f);
            }
        }

        std::string bigGearStr = (gear == -1) ? "R" : std::to_string(gear);
        drawHudRect(hudX + 12.0f, hudY + 60.0f, 64.0f, 66.0f,
                    (gear == -1) ? 0.88f : 0.18f, (gear == -1) ? 0.14f : 0.24f, 0.34f, 1.0f);
        drawHudText(hudX + 30.0f, hudY + 76.0f, 5.0f, bigGearStr, 1.0f, 1.0f, 1.0f);

        snprintf(spdBuf, sizeof(spdBuf), "%3d KMH", (int)kmh);
        drawHudText(hudX + 90.0f, hudY + 64.0f, 3.8f, spdBuf, 1.0f, 1.0f, 1.0f);

        snprintf(rpmInfo, sizeof(rpmInfo), "%4d RPM  [AUTO GEAR]", (int)rpm);
        drawHudText(hudX + 90.0f, hudY + 98.0f, 1.75f, rpmInfo, 0.75f, 0.90f, 1.0f);

        drawHudText(hudX + 90.0f, hudY + 116.0f, 1.6f, "HYBRID", 0.25f, 0.85f, 1.0f);
        drawHudRect(hudX + 170.0f, hudY + 114.0f, 165.0f, 12.0f, 0.15f, 0.20f, 0.30f, 1.0f);
        drawHudRect(hudX + 170.0f, hudY + 114.0f, 165.0f * hybridCharge, 12.0f, 0.0f, 0.78f, 1.0f, 1.0f);

        drawHudRect(18.0f, winH - 58.0f, 780.0f, 40.0f, 0.06f, 0.08f, 0.12f, 0.86f);
        std::string ctrlInfo = std::string(carNames[input.carLivery]) + " | [W/R2]:GAZ  [S/L2]:FREN/GERI(R)  [SPACE/B]:ELFRENI  [C]:KAMERA  [V]:ARAC";
        drawHudText(28.0f, winH - 44.0f, 1.65f, ctrlInfo, 0.82f, 0.88f, 0.95f);

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_FOG);
        glXSwapBuffers(dpy, win);

        auto frameEnd = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(frameEnd - now);
        if (elapsed.count() < 16) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16 - elapsed.count()));
        }
    }

    glXMakeCurrent(dpy, None, NULL);
    glXDestroyContext(dpy, glc);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}
