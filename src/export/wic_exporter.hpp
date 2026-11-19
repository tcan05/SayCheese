#pragma once

#include "capture/captured_frame.hpp"
#include "render/d2d_context.hpp"
#include "render/scene_renderer.hpp"
#include "tools/annotation.hpp"
#include "tools/geometry.hpp"

#include <wincodec.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <vector>

namespace saycheese::export_img {

struct ExportedImage {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;
    std::vector<uint8_t> bgraPixels;
    Microsoft::WRL::ComPtr<IWICBitmap> wicBitmap;

    bool IsValid() const noexcept {
        return width > 0 && height > 0 && stride >= width * 4 &&
               bgraPixels.size() >= static_cast<size_t>(stride) * height &&
               wicBitmap != nullptr;
    }
};

enum class ImageFileFormat {
    Png = 0,
    Jpeg
};

ExportedImage RenderCropToWicBitmap(
    render::D2DContext& ctx,
    render::SceneRenderer& renderer,
    const capture::CapturedFrame& sourceFrame,
    tools::RectF cropRect,
    const std::vector<tools::Annotation>& annotations);

bool SaveWicBitmapToFile(
    IWICImagingFactory* wicFactory,
    IWICBitmap* bitmap,
    const std::wstring& filePath,
    ImageFileFormat format);

std::wstring BuildDefaultFilename();

std::pair<std::wstring, ImageFileFormat> ResolveSavePathAndFormat(
    const std::wstring& chosenPath,
    DWORD filterIndex);

bool PromptAndSaveImage(
    HWND ownerHwnd,
    IWICImagingFactory* wicFactory,
    IWICBitmap* bitmap,
    std::wstring& inOutLastDirectory,
    std::wstring* outSavedPath = nullptr);

} // namespace saycheese::export_img
