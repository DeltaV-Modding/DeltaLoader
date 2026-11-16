// sw::render::load_image - a picture file into RGBA8 for texture_uniform (REPORT 3.193).
// PNG, JPEG, BMP, GIF, TIFF through the Windows Imaging Component; .txtr of the game through sw/textures.hpp (uncompressed
// RGBA, or BC3 decoded). Works without a game and on any thread (COM is initialised for the call when it is not yet).

#include "sw/render.hpp"
#include "sw/textures.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincodec.h>

#include <cwctype>
#include <string>
#include <vector>

namespace sw::render {

namespace {

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n > 0 ? n - 1 : 0), '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}

bool ends_with_txtr(const std::wstring& path) {
    if (path.size() < 5) return false;
    std::wstring tail = path.substr(path.size() - 5);
    for (auto& c : tail) c = static_cast<wchar_t>(std::towlower(c));
    return tail == L".txtr";
}

template <class T>
void release(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

}  // namespace

bool load_image(const std::wstring& path, std::vector<std::uint8_t>& rgba, int& width, int& height, std::string& error) {
    rgba.clear();
    width = height = 0;
    if (ends_with_txtr(path)) {
        const std::string p = narrow(path);
        sw::gfx::Texture t;
        std::string why;
        if (sw::gfx::read(p, t, why)) {
            rgba = std::move(t.rgba);
            width = t.width;
            height = t.height;
            return true;
        }
        sw::gfx::TextureFile file;
        sw::gfx::Image image;
        if (!sw::gfx::read_file(p, file, error) || !sw::gfx::decode(file, 0, image, error)) return false;
        rgba = std::move(image.rgba);
        width = image.width;
        height = image.height;
        return true;
    }

    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninit = SUCCEEDED(co);   // S_OK or S_FALSE; RPC_E_CHANGED_MODE: COM is already up in another mode, fine
    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    bool ok = false;
    UINT w = 0, h = 0;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) {
        error = "the Windows Imaging Component is not available";
    } else if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder))) {
        error = "cannot open or decode " + narrow(path);
    } else if (FAILED(decoder->GetFrame(0, &frame)) || FAILED(factory->CreateFormatConverter(&converter)) ||
               FAILED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                            WICBitmapPaletteTypeCustom)) ||
               FAILED(converter->GetSize(&w, &h))) {
        error = "cannot convert " + narrow(path) + " to RGBA";
    } else if (w == 0 || h == 0 || w > 16384 || h > 16384) {
        error = "the picture is empty or larger than 16384 pixels";
    } else {
        rgba.resize(static_cast<std::size_t>(w) * h * 4u);
        if (FAILED(converter->CopyPixels(nullptr, w * 4u, static_cast<UINT>(rgba.size()), rgba.data()))) {
            error = "cannot read the pixels of " + narrow(path);
            rgba.clear();
        } else {
            width = static_cast<int>(w);
            height = static_cast<int>(h);
            ok = true;
        }
    }
    release(converter);
    release(frame);
    release(decoder);
    release(factory);
    if (uninit) CoUninitialize();
    return ok;
}

}  // namespace sw::render
