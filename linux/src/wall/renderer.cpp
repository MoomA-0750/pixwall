#include "renderer.h"

#include <EGL/eglext.h>

#include <cstdio>

namespace pixwall {

namespace {

// 頂点は 0〜1 の四角。rect（x, y, 幅, 高さ。-1〜1 の画面の座標）に広げる
const char* const kVertex =
    "attribute vec2 corner;\n"
    "uniform vec4 rect;\n"
    "varying vec2 uv;\n"
    "void main() {\n"
    "  uv = corner;\n"
    "  gl_Position = vec4(rect.x + corner.x * rect.z, rect.y - corner.y * rect.w, 0.0, 1.0);\n"
    "}\n";

const char* const kFragment =
    "precision mediump float;\n"
    "uniform sampler2D image;\n"
    "varying vec2 uv;\n"
    "void main() { gl_FragColor = texture2D(image, uv); }\n";

GLuint Compile(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::fprintf(stderr, "pixwall: shader: %s\n", log);
    }
    return shader;
}

}  // namespace

bool Renderer::Init(wl_display* display) {
    auto getPlatformDisplay =
        reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
    display_ = getPlatformDisplay ? getPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, display, nullptr)
                                  : eglGetDisplay(reinterpret_cast<EGLNativeDisplayType>(display));
    if (display_ == EGL_NO_DISPLAY || !eglInitialize(display_, nullptr, nullptr)) return false;
    eglBindAPI(EGL_OPENGL_ES_API);
    // 窓は不透明に出す（後ろを透かさない）ので、アルファの無い形を選ぶ
    const EGLint attributes[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                                 EGL_RED_SIZE,     8,              EGL_GREEN_SIZE,      8,
                                 EGL_BLUE_SIZE,    8,              EGL_ALPHA_SIZE,      0,
                                 EGL_NONE};
    EGLint count = 0;
    if (!eglChooseConfig(display_, attributes, &config_, 1, &count) || count == 0) return false;
    const EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    context_ = eglCreateContext(display_, config_, EGL_NO_CONTEXT, contextAttributes);
    if (context_ == EGL_NO_CONTEXT) return false;
    // 窓がまだ無いうちに GL の準備をするため、窓なしで文脈を当てる（Mesa は EGL_KHR_surfaceless_context を持つ）
    if (!eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, context_)) return false;

    program_ = glCreateProgram();
    glAttachShader(program_, Compile(GL_VERTEX_SHADER, kVertex));
    glAttachShader(program_, Compile(GL_FRAGMENT_SHADER, kFragment));
    glBindAttribLocation(program_, 0, "corner");
    glLinkProgram(program_);
    rectLocation_ = glGetUniformLocation(program_, "rect");
    const GLfloat corners[] = {0, 0, 1, 0, 0, 1, 1, 1};
    glGenBuffers(1, &buffer_);
    glBindBuffer(GL_ARRAY_BUFFER, buffer_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(corners), corners, GL_STATIC_DRAW);
    return true;
}

Renderer::~Renderer() {
    if (display_ == EGL_NO_DISPLAY) return;
    if (!textures_.empty()) glDeleteTextures(static_cast<GLsizei>(textures_.size()), textures_.data());
    eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (context_ != EGL_NO_CONTEXT) eglDestroyContext(display_, context_);
    eglTerminate(display_);
}

void Renderer::Upload(const LoadedImage& image) {
    eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, context_);
    if (!textures_.empty()) glDeleteTextures(static_cast<GLsizei>(textures_.size()), textures_.data());
    textures_.assign(image.frames.size(), 0);
    width_ = image.width;
    height_ = image.height;
    if (textures_.empty()) return;
    glGenTextures(static_cast<GLsizei>(textures_.size()), textures_.data());
    std::vector<uint8_t> rgba(static_cast<size_t>(width_) * height_ * 4);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (size_t i = 0; i < textures_.size(); ++i) {
        const auto& f = image.frames[i];
        for (size_t p = 0; p + 3 < rgba.size(); p += 4) {
            rgba[p] = f[p + 2];
            rgba[p + 1] = f[p + 1];
            rgba[p + 2] = f[p];
            rgba[p + 3] = f[p + 3];
        }
        glBindTexture(GL_TEXTURE_2D, textures_[i]);
        // ドットをぼかさない
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    }
    // GPU に渡し終えるまで待ち、こちらの写しを手放せるようにする
    glFinish();
}

EGLSurface Renderer::CreateSurface(EGLNativeWindowType window) {
    EGLSurface surface = eglCreateWindowSurface(display_, config_, window, nullptr);
    if (surface == EGL_NO_SURFACE) return surface;
    eglMakeCurrent(display_, surface, surface, context_);
    // 隠れた窓で eglSwapBuffers が止まらないよう、合成を待たない（描く時刻はこちらのタイマーで決める）
    eglSwapInterval(display_, 0);
    return surface;
}

void Renderer::DestroySurface(EGLSurface surface) {
    if (surface == EGL_NO_SURFACE) return;
    eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, context_);
    eglDestroySurface(display_, surface);
}

void Renderer::Draw(EGLSurface surface, int width, int height, size_t frame, const Placement& placement, int rgb) {
    if (!eglMakeCurrent(display_, surface, surface, context_)) return;
    glViewport(0, 0, width, height);
    glClearColor(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!textures_.empty() && width > 0 && height > 0) {
        const PixelRect r = Layout(width_, height_, width, height, placement);
        glUseProgram(program_);
        glUniform4f(rectLocation_, -1 + 2.0f * r.x / width, 1 - 2.0f * r.y / height, 2.0f * r.width / width,
                    2.0f * r.height / height);
        glBindBuffer(GL_ARRAY_BUFFER, buffer_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textures_[frame < textures_.size() ? frame : 0]);
        // premultiplied の重ね合わせ。透明なところは余白の色が見える
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    eglSwapBuffers(display_, surface);
}

}  // namespace pixwall
