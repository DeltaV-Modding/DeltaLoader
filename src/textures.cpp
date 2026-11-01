#include "sw/textures.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace sw::gfx {

namespace {

std::uint32_t u32(const std::vector<std::uint8_t>& b, std::size_t at) {
    return static_cast<std::uint32_t>(b[at]) | (static_cast<std::uint32_t>(b[at + 1]) << 8) |
           (static_cast<std::uint32_t>(b[at + 2]) << 16) | (static_cast<std::uint32_t>(b[at + 3]) << 24);
}

std::size_t at(const Texture& t, int x, int y) {
    return (static_cast<std::size_t>(y) * static_cast<std::size_t>(t.width) + static_cast<std::size_t>(x)) * 4;
}

}  // namespace

bool read(const std::string& path, Texture& out, std::string& error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        error = "cannot open " + path;
        return false;
    }
    std::vector<std::uint8_t> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (b.size() < kTxtrHeaderSize + kTxtrTrailerSize || std::memcmp(b.data(), "TXTR", 4) != 0) {
        error = "not a TXTR file";
        return false;
    }
    if (u32(b, 4) != kTxtrVersion || u32(b, 8) != kTxtrFormatRgba) {
        error = "version " + std::to_string(u32(b, 4)) + ", format " + std::to_string(u32(b, 8)) +
                " is not supported (only 3 / 2)";
        return false;
    }
    const std::size_t pixels = b.size() - kTxtrHeaderSize - kTxtrTrailerSize;
    const auto side = static_cast<std::size_t>(std::llround(std::sqrt(static_cast<double>(pixels) / 4.0)));
    if (side == 0 || side * side * 4 != pixels) {
        error = "the pixel area is not square";
        return false;
    }
    out = Texture{};
    out.width = static_cast<int>(side);
    out.height = static_cast<int>(side);
    std::copy(b.begin(), b.begin() + kTxtrHeaderSize, out.header.begin());
    out.rgba.assign(b.begin() + kTxtrHeaderSize, b.begin() + kTxtrHeaderSize + pixels);
    std::copy(b.end() - kTxtrTrailerSize, b.end(), out.trailer.begin());
    out.source = path;
    return true;
}

bool write(const std::string& path, const Texture& texture, std::string& error) {
    std::error_code ec;
    if (!texture.source.empty() && std::filesystem::exists(path, ec) &&
        std::filesystem::equivalent(path, texture.source, ec)) {
        error = "refused: the output is the file that was read";
        return false;
    }
    if (texture.rgba.size() != static_cast<std::size_t>(texture.width) * static_cast<std::size_t>(texture.height) * 4) {
        error = "the pixel buffer does not match the size";
        return false;
    }
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        error = "cannot write " + path;
        return false;
    }
    f.write(reinterpret_cast<const char*>(texture.header.data()), static_cast<std::streamsize>(texture.header.size()));
    f.write(reinterpret_cast<const char*>(texture.rgba.data()), static_cast<std::streamsize>(texture.rgba.size()));
    f.write(reinterpret_cast<const char*>(texture.trailer.data()), static_cast<std::streamsize>(texture.trailer.size()));
    if (!f) {
        error = "write failed";
        return false;
    }
    return true;
}

bool is_empty(const Texture& t, int x, int y, int width, int height) {
    if (x < 0 || y < 0 || width <= 0 || height <= 0 || x + width > t.width || y + height > t.height) return false;
    for (int yy = y; yy < y + height; ++yy)
        for (int xx = x; xx < x + width; ++xx)
            if (t.rgba[at(t, xx, yy) + 3] != 0) return false;
    return true;
}

bool find_free(const Texture& t, int width, int height, int& x, int& y) {
    if (width <= 0 || height <= 0) return false;
    for (int yy = 0; yy + height <= t.height; yy += height) {
        for (int xx = 0; xx + width <= t.width; xx += width) {
            if (is_empty(t, xx, yy, width, height)) {
                x = xx;
                y = yy;
                return true;
            }
        }
    }
    return false;
}

std::vector<std::pair<int, int>> free_cells(const Texture& t, int width, int height) {
    std::vector<std::pair<int, int>> out;
    if (width <= 0 || height <= 0) return out;
    for (int yy = 0; yy + height <= t.height; yy += height)
        for (int xx = 0; xx + width <= t.width; xx += width)
            if (is_empty(t, xx, yy, width, height)) out.emplace_back(xx, yy);
    return out;
}

bool put(Texture& t, const Image& image, int x, int y, std::string& error) {
    if (image.width <= 0 || image.height <= 0 ||
        image.rgba.size() != static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4) {
        error = "the picture is empty or its buffer does not match its size";
        return false;
    }
    if (x < 0 || y < 0 || x + image.width > t.width || y + image.height > t.height) {
        error = "the picture does not fit";
        return false;
    }
    if (!is_empty(t, x, y, image.width, image.height)) {
        error = "refused: the place is not empty, nothing is replaced";
        return false;
    }
    for (int yy = 0; yy < image.height; ++yy) {
        for (int xx = 0; xx < image.width; ++xx) {
            const std::size_t s = (static_cast<std::size_t>(yy) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(xx)) * 4;
            const std::size_t d = at(t, x + xx, y + yy);
            t.rgba[d] = image.rgba[s];
            t.rgba[d + 1] = image.rgba[s + 1];
            t.rgba[d + 2] = image.rgba[s + 2];
            t.rgba[d + 3] = image.rgba[s + 3] == 0 ? 1 : image.rgba[s + 3];
        }
    }
    return true;
}

bool crop(const Texture& t, int x, int y, int width, int height, Image& out) {
    if (x < 0 || y < 0 || width <= 0 || height <= 0 || x + width > t.width || y + height > t.height) return false;
    out = Image{};
    out.width = width;
    out.height = height;
    out.rgba.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    for (int yy = 0; yy < height; ++yy)
        std::memcpy(&out.rgba[static_cast<std::size_t>(yy) * static_cast<std::size_t>(width) * 4], &t.rgba[at(t, x, y + yy)],
                    static_cast<std::size_t>(width) * 4);
    return true;
}


namespace {

std::uint16_t read16(const std::vector<std::uint8_t>& b, std::size_t at) {
    return static_cast<std::uint16_t>(b[at] | (b[at + 1] << 8));
}

void rgb565(std::uint16_t v, int rgb[3]) {
    rgb[0] = ((v >> 11) & 31) * 255 / 31;
    rgb[1] = ((v >> 5) & 63) * 255 / 63;
    rgb[2] = (v & 31) * 255 / 31;
}

}  // namespace

bool read_file(const std::string& path, TextureFile& out, std::string& error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        error = "cannot open " + path;
        return false;
    }
    std::vector<std::uint8_t> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (b.size() < 19 + 2 || std::memcmp(b.data(), "TXTR", 4) != 0) {
        error = "not a TXTR file";
        return false;
    }
    if (u32(b, 4) != kTxtrVersion) {
        error = "version " + std::to_string(u32(b, 4)) + " is not supported (only 3)";
        return false;
    }
    TextureFile t;
    t.format = u32(b, 8);
    t.flag = b[12] == 1;
    t.width = read16(b, 13);
    t.height = read16(b, 15);
    const int mips = b[17];
    std::size_t p = 19;
    for (int i = 0; i < mips; ++i) {
        if (p + 4 > b.size()) {
            error = "the file ends inside the table of levels";
            return false;
        }
        const std::size_t size = u32(b, p);
        p += 4;
        if (p + size > b.size()) {
            error = "a level is larger than the file";
            return false;
        }
        t.levels.emplace_back(b.begin() + static_cast<std::ptrdiff_t>(p), b.begin() + static_cast<std::ptrdiff_t>(p + size));
        p += size;
    }
    if (b.size() - p != kTxtrTrailerSize) {
        error = "the levels do not use the whole file";
        return false;
    }
    t.trailer = {b[p], b[p + 1]};
    t.source = path;
    out = std::move(t);
    return true;
}

bool write_file(const std::string& path, const TextureFile& t, std::string& error) {
    std::error_code ec;
    if (!t.source.empty() && std::filesystem::exists(path, ec) && std::filesystem::equivalent(path, t.source, ec)) {
        error = "refused: the output is the file that was read";
        return false;
    }
    std::vector<std::uint8_t> b = {'T', 'X', 'T', 'R'};
    auto put32 = [&b](std::uint32_t v) {
        for (int i = 0; i < 4; ++i) b.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 255));
    };
    put32(kTxtrVersion);
    put32(t.format);
    b.push_back(t.flag ? 1 : 0);
    b.push_back(static_cast<std::uint8_t>(t.width & 255));
    b.push_back(static_cast<std::uint8_t>(t.width >> 8));
    b.push_back(static_cast<std::uint8_t>(t.height & 255));
    b.push_back(static_cast<std::uint8_t>(t.height >> 8));
    b.push_back(static_cast<std::uint8_t>(t.levels.size()));
    b.push_back(0);
    for (const auto& level : t.levels) {
        put32(static_cast<std::uint32_t>(level.size()));
        b.insert(b.end(), level.begin(), level.end());
    }
    b.insert(b.end(), t.trailer.begin(), t.trailer.end());
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        error = "cannot write " + path;
        return false;
    }
    f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    return static_cast<bool>(f);
}

bool decode_bc3(const std::vector<std::uint8_t>& d, int w, int h, Image& out) {
    const int bw = std::max(1, (w + 3) / 4), bh = std::max(1, (h + 3) / 4);
    if (w <= 0 || h <= 0 || d.size() < static_cast<std::size_t>(bw) * static_cast<std::size_t>(bh) * 16) return false;
    out = Image{};
    out.width = w;
    out.height = h;
    out.rgba.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 0);
    for (int by = 0; by < bh; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            const std::size_t p = (static_cast<std::size_t>(by) * static_cast<std::size_t>(bw) + static_cast<std::size_t>(bx)) * 16;
            int alpha[8];
            alpha[0] = d[p];
            alpha[1] = d[p + 1];
            if (alpha[0] > alpha[1]) {
                for (int i = 1; i < 7; ++i) alpha[i + 1] = ((7 - i) * alpha[0] + i * alpha[1]) / 7;
            } else {
                for (int i = 1; i < 5; ++i) alpha[i + 1] = ((5 - i) * alpha[0] + i * alpha[1]) / 5;
                alpha[6] = 0;
                alpha[7] = 255;
            }
            std::uint64_t abits = 0;
            for (int i = 0; i < 6; ++i) abits |= static_cast<std::uint64_t>(d[p + 2 + i]) << (8 * i);
            const std::uint16_t c0 = static_cast<std::uint16_t>(d[p + 8] | (d[p + 9] << 8));
            const std::uint16_t c1 = static_cast<std::uint16_t>(d[p + 10] | (d[p + 11] << 8));
            int col[4][3];
            rgb565(c0, col[0]);
            rgb565(c1, col[1]);
            for (int k = 0; k < 3; ++k) {
                col[2][k] = (2 * col[0][k] + col[1][k]) / 3;
                col[3][k] = (col[0][k] + 2 * col[1][k]) / 3;
            }
            const std::uint32_t cb = static_cast<std::uint32_t>(d[p + 12]) | (static_cast<std::uint32_t>(d[p + 13]) << 8) |
                                     (static_cast<std::uint32_t>(d[p + 14]) << 16) | (static_cast<std::uint32_t>(d[p + 15]) << 24);
            for (int i = 0; i < 16; ++i) {
                const int x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
                if (x >= w || y >= h) continue;
                const int* c = col[(cb >> (2 * i)) & 3];
                std::uint8_t* o = &out.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 4];
                o[0] = static_cast<std::uint8_t>(c[0]);
                o[1] = static_cast<std::uint8_t>(c[1]);
                o[2] = static_cast<std::uint8_t>(c[2]);
                o[3] = static_cast<std::uint8_t>(alpha[(abits >> (3 * i)) & 7]);
            }
        }
    }
    return true;
}

namespace {

std::uint16_t to565(const int rgb[3]) {
    return static_cast<std::uint16_t>(((rgb[0] * 31 + 127) / 255) << 11 | ((rgb[1] * 63 + 127) / 255) << 5 |
                                      ((rgb[2] * 31 + 127) / 255));
}

Image halve(const Image& in) {
    Image out;
    out.width = std::max(1, in.width / 2);
    out.height = std::max(1, in.height / 2);
    out.rgba.resize(static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height) * 4);
    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            for (int c = 0; c < 4; ++c) {
                int sum = 0;
                for (int dy = 0; dy < 2; ++dy)
                    for (int dx = 0; dx < 2; ++dx) {
                        const int sx = std::min(in.width - 1, x * 2 + dx), sy = std::min(in.height - 1, y * 2 + dy);
                        sum += in.rgba[(static_cast<std::size_t>(sy) * static_cast<std::size_t>(in.width) + static_cast<std::size_t>(sx)) * 4 +
                                       static_cast<std::size_t>(c)];
                    }
                out.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(out.width) + static_cast<std::size_t>(x)) * 4 +
                         static_cast<std::size_t>(c)] = static_cast<std::uint8_t>((sum + 2) / 4);
            }
        }
    }
    return out;
}

}  // namespace

bool encode_bc3(const Image& img, std::vector<std::uint8_t>& out) {
    if (img.width <= 0 || img.height <= 0 ||
        img.rgba.size() != static_cast<std::size_t>(img.width) * static_cast<std::size_t>(img.height) * 4)
        return false;
    const int bw = std::max(1, (img.width + 3) / 4), bh = std::max(1, (img.height + 3) / 4);
    out.assign(static_cast<std::size_t>(bw) * static_cast<std::size_t>(bh) * 16, 0);
    for (int by = 0; by < bh; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            int px[16][4];
            int lo[3] = {255, 255, 255}, hi[3] = {0, 0, 0}, amin = 255, amax = 0;
            for (int i = 0; i < 16; ++i) {
                const int x = std::min(img.width - 1, bx * 4 + (i & 3)), y = std::min(img.height - 1, by * 4 + (i >> 2));
                const std::uint8_t* s = &img.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(img.width) + static_cast<std::size_t>(x)) * 4];
                for (int c = 0; c < 4; ++c) px[i][c] = s[c];
                for (int c = 0; c < 3; ++c) {
                    lo[c] = std::min(lo[c], px[i][c]);
                    hi[c] = std::max(hi[c], px[i][c]);
                }
                amin = std::min(amin, px[i][3]);
                amax = std::max(amax, px[i][3]);
            }
            std::uint8_t* d = &out[(static_cast<std::size_t>(by) * static_cast<std::size_t>(bw) + static_cast<std::size_t>(bx)) * 16];
            // alpha: a0 > a1 selects the mode with eight steps
            const int a0 = amax, a1 = amin;
            int alpha[8];
            alpha[0] = a0;
            alpha[1] = a1;
            for (int i = 1; i < 7; ++i) alpha[i + 1] = ((7 - i) * a0 + i * a1) / 7;
            d[0] = static_cast<std::uint8_t>(a0);
            d[1] = static_cast<std::uint8_t>(a1);
            std::uint64_t abits = 0;
            // colour endpoints: c0 = hi, c1 = lo
            std::uint16_t c0 = to565(hi), c1 = to565(lo);
            int pal[4][3];
            rgb565(c0, pal[0]);
            rgb565(c1, pal[1]);
            for (int k = 0; k < 3; ++k) {
                pal[2][k] = (2 * pal[0][k] + pal[1][k]) / 3;
                pal[3][k] = (pal[0][k] + 2 * pal[1][k]) / 3;
            }
            std::uint32_t cbits = 0;
            for (int i = 0; i < 16; ++i) {
                int best = 0, bestd = 1 << 30;
                for (int j = 0; j < 8; ++j) {
                    const int e = std::abs(px[i][3] - alpha[j]);
                    if (e < bestd) {
                        bestd = e;
                        best = j;
                    }
                }
                abits |= static_cast<std::uint64_t>(best) << (3 * i);
                best = 0;
                bestd = 1 << 30;
                for (int j = 0; j < 4; ++j) {
                    int e = 0;
                    for (int k = 0; k < 3; ++k) e += (px[i][k] - pal[j][k]) * (px[i][k] - pal[j][k]);
                    if (e < bestd) {
                        bestd = e;
                        best = j;
                    }
                }
                cbits |= static_cast<std::uint32_t>(best) << (2 * i);
            }
            for (int i = 0; i < 6; ++i) d[2 + i] = static_cast<std::uint8_t>((abits >> (8 * i)) & 255);
            d[8] = static_cast<std::uint8_t>(c0 & 255);
            d[9] = static_cast<std::uint8_t>(c0 >> 8);
            d[10] = static_cast<std::uint8_t>(c1 & 255);
            d[11] = static_cast<std::uint8_t>(c1 >> 8);
            for (int i = 0; i < 4; ++i) d[12 + i] = static_cast<std::uint8_t>((cbits >> (8 * i)) & 255);
        }
    }
    return true;
}

bool make_texture(const Image& image, std::uint32_t format, TextureFile& out, std::string& error) {
    const int n = image.width;
    if (n != image.height || n < 4 || n > 4096 || (n & (n - 1)) != 0) {
        error = "the picture has to be square and a power of two between 4 and 4096";
        return false;
    }
    if (image.rgba.size() != static_cast<std::size_t>(n) * static_cast<std::size_t>(n) * 4) {
        error = "the pixel buffer does not match the size";
        return false;
    }
    if (format != kTxtrFormatRgba && format != kTxtrFormatBc3) {
        error = "only format 2 and 3 can be made";
        return false;
    }
    TextureFile t;
    t.format = format;
    t.width = n;
    t.height = n;
    if (format == kTxtrFormatRgba) {
        t.levels.push_back(image.rgba);
    } else {
        Image level = image;
        for (;;) {
            std::vector<std::uint8_t> data;
            if (!encode_bc3(level, data)) {
                error = "encoding failed";
                return false;
            }
            t.levels.push_back(std::move(data));
            if (level.width == 1) break;
            level = halve(level);
        }
    }
    out = std::move(t);
    return true;
}

bool decode(const TextureFile& t, int level, Image& out, std::string& error) {
    if (level < 0 || level >= static_cast<int>(t.levels.size())) {
        error = "no such level";
        return false;
    }
    const int w = std::max(1, t.width >> level), h = std::max(1, t.height >> level);
    const auto& data = t.levels[static_cast<std::size_t>(level)];
    if (t.format == kTxtrFormatRgba) {
        if (data.size() != static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4) {
            error = "the level does not have 4 bytes per pixel";
            return false;
        }
        out = Image{};
        out.width = w;
        out.height = h;
        out.rgba = data;
        return true;
    }
    if (t.format == kTxtrFormatBc3) {
        if (!decode_bc3(data, w, h, out)) {
            error = "the level is too small for BC3";
            return false;
        }
        return true;
    }
    error = "format " + std::to_string(t.format) + " cannot be decoded";
    return false;
}

}  // namespace sw::gfx
