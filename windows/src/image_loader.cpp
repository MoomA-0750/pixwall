#include "image_loader.h"

#include <propidl.h>
#include <wrl/client.h>

#include "core.h"

using Microsoft::WRL::ComPtr;

namespace pixwall {

namespace {

// メタデータの数（VT_UI1 / VT_UI2 / VT_UI4）を読む。無ければ fallback
unsigned ReadUInt(IWICMetadataQueryReader* reader, const wchar_t* name, unsigned fallback) {
    if (!reader) return fallback;
    PROPVARIANT v;
    PropVariantInit(&v);
    unsigned result = fallback;
    if (SUCCEEDED(reader->GetMetadataByName(name, &v))) {
        switch (v.vt) {
            case VT_UI1: result = v.bVal; break;
            case VT_UI2: result = v.uiVal; break;
            case VT_UI4: result = v.ulVal; break;
            default: break;
        }
    }
    PropVariantClear(&v);
    return result;
}

}  // namespace

bool LoadImageFile(IWICImagingFactory* wic, const std::wstring& path, LoadedImage& out) {
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                              WICDecodeMetadataCacheOnDemand, &decoder))) {
        return false;
    }
    UINT count = 0;
    if (FAILED(decoder->GetFrameCount(&count)) || count == 0) return false;

    GUID format{};
    decoder->GetContainerFormat(&format);
    const bool isGif = format == GUID_ContainerFormatGif;

    ComPtr<IWICMetadataQueryReader> globalMeta;
    decoder->GetMetadataQueryReader(&globalMeta);

    // GIF は論理画面の大きさ。ほかの形式は1枚目の大きさ
    ComPtr<IWICBitmapFrameDecode> first;
    if (FAILED(decoder->GetFrame(0, &first))) return false;
    UINT fw = 0;
    UINT fh = 0;
    first->GetSize(&fw, &fh);
    int width = static_cast<int>(isGif ? ReadUInt(globalMeta.Get(), L"/logscrdesc/Width", fw) : fw);
    int height = static_cast<int>(isGif ? ReadUInt(globalMeta.Get(), L"/logscrdesc/Height", fh) : fh);
    if (width <= 0 || height <= 0) return false;

    GifCompositor compositor(width, height);
    out = {};
    out.width = width;
    out.height = height;

    for (UINT i = 0; i < count; ++i) {
        ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(decoder->GetFrame(i, &frame))) continue;
        UINT w = 0;
        UINT h = 0;
        frame->GetSize(&w, &h);

        ComPtr<IWICBitmapSource> converted;
        if (FAILED(WICConvertBitmapSource(GUID_WICPixelFormat32bppPBGRA, frame.Get(), &converted))) continue;
        std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4);
        if (FAILED(converted->CopyPixels(nullptr, w * 4, static_cast<UINT>(pixels.size()), pixels.data()))) {
            continue;
        }

        ComPtr<IWICMetadataQueryReader> meta;
        frame->GetMetadataQueryReader(&meta);

        GifCompositor::SubFrame sub;
        sub.width = static_cast<int>(w);
        sub.height = static_cast<int>(h);
        sub.pixels = pixels.data();
        double delay = 0;
        if (isGif) {
            sub.left = static_cast<int>(ReadUInt(meta.Get(), L"/imgdesc/Left", 0));
            sub.top = static_cast<int>(ReadUInt(meta.Get(), L"/imgdesc/Top", 0));
            sub.disposal = static_cast<GifCompositor::Disposal>(ReadUInt(meta.Get(), L"/grctlext/Disposal", 0) & 7);
            // 1/100 秒で書かれている
            delay = ReadUInt(meta.Get(), L"/grctlext/Delay", 0) / 100.0;
        } else {
            // GIF 以外は1枚ずつ完成した絵として扱い、前のフレームに重ねない
            sub.disposal = GifCompositor::Disposal::Background;
        }
        out.frames.push_back(compositor.Add(sub));
        out.delays.push_back(delay);
    }
    return !out.frames.empty();
}

}  // namespace pixwall
