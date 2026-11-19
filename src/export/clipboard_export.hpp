#pragma once

#include "export/wic_exporter.hpp"

#include <windows.h>

#include <cstdint>
#include <vector>

namespace saycheese::export_img {

std::vector<uint8_t> BuildDibV5Payload(
    uint32_t width,
    uint32_t height,
    uint32_t srcStride,
    const uint8_t* bgraPixels);

std::vector<uint8_t> BuildDibPayload(
    uint32_t width,
    uint32_t height,
    uint32_t srcStride,
    const uint8_t* bgraPixels);

bool CopyImageToClipboard(HWND ownerHwnd, const ExportedImage& image);

} // namespace saycheese::export_img
