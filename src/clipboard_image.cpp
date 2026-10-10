// Win32 clipboard image reading: CF_DIB / CF_DIBV5 -> cv::Mat (BGR).
//
// The clipboard stores a device-independent bitmap as a BITMAPINFOHEADER (or
// BITMAPV5HEADER) immediately followed by the color table (if any) and the
// pixel bits. We parse that block into a BGR cv::Mat, handling:
//   - 24-bit and 32-bit DIBs (the common screenshot formats)
//   - bottom-up (positive biHeight) and top-down (negative) row order
//   - 4-byte row stride alignment
//
// Returns an empty Mat on any failure (no image, unsupported format, etc.).

#include <opencv2/core.hpp>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

cv::Mat read_clipboard_image() {
    if (!IsClipboardFormatAvailable(CF_DIB) &&
        !IsClipboardFormatAvailable(CF_DIBV5)) {
        return {};
    }
    if (!OpenClipboard(nullptr)) return {};

    cv::Mat result;

    // Prefer CF_DIB; fall back to CF_DIBV5.
    UINT fmt = IsClipboardFormatAvailable(CF_DIB) ? CF_DIB : CF_DIBV5;
    HANDLE h = GetClipboardData(fmt);
    if (h) {
        const void* p = GlobalLock(h);
        SIZE_T total = GlobalSize(h);
        if (p && total >= sizeof(BITMAPINFOHEADER)) {
            const BITMAPINFOHEADER* bih = reinterpret_cast<const BITMAPINFOHEADER*>(p);
            const int width  = bih->biWidth;
            const int height = bih->biHeight;           // may be negative (top-down)
            const int bpp    = bih->biBitCount;
            const int abs_h  = std::abs(height);

            // Only handle uncompressed 24/32-bit DIBs (screenshot norm).
            const bool ok_compression =
                (bih->biCompression == BI_RGB) ||
                (bih->biCompression == BI_BITFIELDS);
            if (width > 0 && abs_h > 0 && (bpp == 24 || bpp == 32) && ok_compression) {
                // Compute offset to pixel data:
                //   header size + color masks (BI_BITFIELDS adds 3 DWORDs for
                //   the 16/32-bit masks) + palette (none for 24/32-bit BI_RGB).
                size_t header_size = bih->biSize;  // BITMAPINFOHEADER(40) or V5(124)
                size_t masks = 0;
                if (bih->biCompression == BI_BITFIELDS && bih->biSize == sizeof(BITMAPINFOHEADER))
                    masks = 3 * sizeof(DWORD);
                const uint8_t* bits =
                    reinterpret_cast<const uint8_t*>(p) + header_size + masks;

                const int channels = bpp / 8;
                const size_t stride = (static_cast<size_t>(width) * channels + 3) & ~size_t(3);

                // Bounds check against the buffer.
                const uint8_t* end = reinterpret_cast<const uint8_t*>(p) + total;
                if (bits + stride * abs_h <= end) {
                    cv::Mat img(abs_h, width, CV_8UC3);
                    const bool top_down = (height < 0);
                    for (int y = 0; y < abs_h; ++y) {
                        // Bottom-up DIB: first row in memory is the bottom row.
                        const int src_row = top_down ? y : (abs_h - 1 - y);
                        const uint8_t* srow = bits + static_cast<size_t>(src_row) * stride;
                        cv::Vec3b* drow = img.ptr<cv::Vec3b>(y);
                        for (int x = 0; x < width; ++x) {
                            const uint8_t* px = srow + static_cast<size_t>(x) * channels;
                            // DIB is BGR(A); cv::Mat is BGR — copy first 3 bytes.
                            drow[x] = cv::Vec3b(px[0], px[1], px[2]);
                        }
                    }
                    result = img;
                }
            }
        }
        if (p) GlobalUnlock(h);
    }

    CloseClipboard();
    return result;
}

#else  // non-Windows fallback (feature is Windows-only per spec)
cv::Mat read_clipboard_image() { return {}; }
#endif
