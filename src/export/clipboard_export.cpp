#include "export/clipboard_export.hpp"

#include "platform/win32_raii.hpp"

#include <cstring>

namespace saycheese::export_img {

namespace {

void PackBottomUpOpaqueBgra(
    uint8_t* dstPixels,
    uint32_t width,
    uint32_t height,
    uint32_t srcStride,
    const uint8_t* srcPixels) noexcept {
    const uint32_t dstStride = width * 4u;
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* srcRow = srcPixels + static_cast<size_t>(y) * srcStride;
        uint8_t* dstRow = dstPixels + static_cast<size_t>(height - 1u - y) * dstStride;
        for (uint32_t x = 0; x < width; ++x) {
            const size_t i = static_cast<size_t>(x) * 4u;
            dstRow[i + 0] = srcRow[i + 0];
            dstRow[i + 1] = srcRow[i + 1];
            dstRow[i + 2] = srcRow[i + 2];
            dstRow[i + 3] = 255;
        }
    }
}

bool SetClipboardGlobalPayload(UINT format, const std::vector<uint8_t>& payload) {
    if (payload.empty()) {
        return false;
    }
    platform::UniqueGlobalMem mem(::GlobalAlloc(GMEM_MOVEABLE, payload.size()));
    if (!mem) {
        return false;
    }
    void* locked = ::GlobalLock(mem.Get());
    if (locked == nullptr) {
        return false;
    }
    std::memcpy(locked, payload.data(), payload.size());
    ::GlobalUnlock(mem.Get());

    if (::SetClipboardData(format, mem.Get()) == nullptr) {
        return false;
    }
    // Clipboard owns the global handle once SetClipboardData succeeds.
    (void)mem.Release();
    return true;
}

} // namespace

std::vector<uint8_t> BuildDibV5Payload(
    uint32_t width,
    uint32_t height,
    uint32_t srcStride,
    const uint8_t* bgraPixels) {
    std::vector<uint8_t> out;
    if (width == 0 || height == 0 || srcStride < width * 4u || bgraPixels == nullptr) {
        return out;
    }

    const size_t pixelBytes = static_cast<size_t>(width) * height * 4u;
    out.resize(sizeof(BITMAPV5HEADER) + pixelBytes);

    BITMAPV5HEADER hdr{};
    hdr.bV5Size = sizeof(BITMAPV5HEADER);
    hdr.bV5Width = static_cast<LONG>(width);
    hdr.bV5Height = static_cast<LONG>(height); // Bottom-up DIB
    hdr.bV5Planes = 1;
    hdr.bV5BitCount = 32;
    hdr.bV5Compression = BI_BITFIELDS;
    hdr.bV5SizeImage = static_cast<DWORD>(pixelBytes);
    hdr.bV5RedMask = 0x00FF0000;
    hdr.bV5GreenMask = 0x0000FF00;
    hdr.bV5BlueMask = 0x000000FF;
    hdr.bV5AlphaMask = 0xFF000000;
    hdr.bV5CSType = LCS_sRGB;
    hdr.bV5Intent = LCS_GM_IMAGES;

    std::memcpy(out.data(), &hdr, sizeof(hdr));
    PackBottomUpOpaqueBgra(out.data() + sizeof(BITMAPV5HEADER), width, height, srcStride, bgraPixels);
    return out;
}

std::vector<uint8_t> BuildDibPayload(
    uint32_t width,
    uint32_t height,
    uint32_t srcStride,
    const uint8_t* bgraPixels) {
    std::vector<uint8_t> out;
    if (width == 0 || height == 0 || srcStride < width * 4u || bgraPixels == nullptr) {
        return out;
    }

    const size_t pixelBytes = static_cast<size_t>(width) * height * 4u;
    out.resize(sizeof(BITMAPINFOHEADER) + pixelBytes);

    BITMAPINFOHEADER hdr{};
    hdr.biSize = sizeof(BITMAPINFOHEADER);
    hdr.biWidth = static_cast<LONG>(width);
    hdr.biHeight = static_cast<LONG>(height); // Bottom-up DIB
    hdr.biPlanes = 1;
    hdr.biBitCount = 32;
    hdr.biCompression = BI_RGB;
    hdr.biSizeImage = static_cast<DWORD>(pixelBytes);

    std::memcpy(out.data(), &hdr, sizeof(hdr));
    PackBottomUpOpaqueBgra(out.data() + sizeof(BITMAPINFOHEADER), width, height, srcStride, bgraPixels);
    return out;
}

bool CopyImageToClipboard(HWND ownerHwnd, const ExportedImage& image) {
    if (!image.IsValid()) {
        return false;
    }

    const std::vector<uint8_t> dibV5 = BuildDibV5Payload(
        image.width, image.height, image.stride, image.bgraPixels.data());
    const std::vector<uint8_t> dib = BuildDibPayload(
        image.width, image.height, image.stride, image.bgraPixels.data());
    if (dibV5.empty() || dib.empty()) {
        return false;
    }

    platform::ClipboardGuard guard(ownerHwnd);
    if (!guard.IsOpen()) {
        return false;
    }
    if (!::EmptyClipboard()) {
        return false;
    }

    const bool okV5 = SetClipboardGlobalPayload(CF_DIBV5, dibV5);
    const bool okDib = SetClipboardGlobalPayload(CF_DIB, dib);
    return okV5 || okDib;
}

} // namespace saycheese::export_img
