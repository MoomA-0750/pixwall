// EGL と OpenGL ES 2 で、フレームを GPU に置いて最近傍で広げて描く。全部の画面の窓で1つの文脈を使う
#pragma once

// EGLNativeWindowType を wl_egl_window* にそろえるため、EGL より先に読む
#include <wayland-client.h>
#include <wayland-egl.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include <vector>

#include "core.h"
#include "image_loader.h"

namespace pixwall {

class Renderer {
public:
    bool Init(wl_display* display);
    ~Renderer();

    // 画像のフレームを全部 GPU に置く（前のものは手放す）。画像が無ければ空にする
    void Upload(const LoadedImage& image);
    bool HasImage() const { return !textures_.empty(); }
    int imageWidth() const { return width_; }
    int imageHeight() const { return height_; }
    size_t frameCount() const { return textures_.size(); }

    EGLSurface CreateSurface(EGLNativeWindowType window);
    void DestroySurface(EGLSurface surface);

    // surface（物理ピクセルで width × height）に frame 枚目を描いて出す。余白は rgb（0xRRGGBB）
    void Draw(EGLSurface surface, int width, int height, size_t frame, const Placement& placement, int rgb);

private:
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLConfig config_ = nullptr;
    EGLContext context_ = EGL_NO_CONTEXT;
    GLuint program_ = 0;
    GLint rectLocation_ = -1;
    GLuint buffer_ = 0;
    std::vector<GLuint> textures_;
    int width_ = 0;
    int height_ = 0;
};

}  // namespace pixwall
