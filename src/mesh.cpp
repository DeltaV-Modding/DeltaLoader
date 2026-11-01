#include "sw/mesh.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace sw::gfx {

namespace {

std::uint16_t u16(const std::vector<std::uint8_t>& b, std::size_t at) {
    return static_cast<std::uint16_t>(b[at] | (b[at + 1] << 8));
}
std::uint32_t u32(const std::vector<std::uint8_t>& b, std::size_t at) {
    return static_cast<std::uint32_t>(b[at]) | (static_cast<std::uint32_t>(b[at + 1]) << 8) |
           (static_cast<std::uint32_t>(b[at + 2]) << 16) | (static_cast<std::uint32_t>(b[at + 3]) << 24);
}
float f32(const std::vector<std::uint8_t>& b, std::size_t at) {
    const std::uint32_t v = u32(b, at);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}
void put16(std::vector<std::uint8_t>& b, std::uint16_t v) {
    b.push_back(static_cast<std::uint8_t>(v & 255));
    b.push_back(static_cast<std::uint8_t>(v >> 8));
}
void put32(std::vector<std::uint8_t>& b, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 255));
}
void putf(std::vector<std::uint8_t>& b, float f) {
    std::uint32_t v;
    std::memcpy(&v, &f, 4);
    put32(b, v);
}

}  // namespace

bool read_mesh(const std::string& path, Mesh& out, std::string& error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        error = "cannot open " + path;
        return false;
    }
    std::vector<std::uint8_t> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (b.size() < kMeshHeaderSize + 8 || std::memcmp(b.data(), "mesh", 4) != 0) {
        error = "not a mesh file";
        return false;
    }
    Mesh m;
    m.version = u16(b, 4);
    m.second_word = u16(b, 6);
    const std::size_t vc = u16(b, 8);
    m.layout_word = u16(b, 10);
    m.reserved = u16(b, 12);
    if (m.layout_word != kMeshLayoutWord) {
        error = "layout word " + std::to_string(m.layout_word) + " is not supported (only 19)";
        return false;
    }
    std::size_t p = kMeshHeaderSize;
    if (p + vc * kMeshVertexSize + 4 > b.size()) {
        error = "the file is shorter than its vertices";
        return false;
    }
    m.vertices.resize(vc);
    for (std::size_t i = 0; i < vc; ++i, p += kMeshVertexSize) {
        Vertex& v = m.vertices[i];
        v.x = f32(b, p);
        v.y = f32(b, p + 4);
        v.z = f32(b, p + 8);
        v.r = b[p + 12];
        v.g = b[p + 13];
        v.b = b[p + 14];
        v.a = b[p + 15];
        v.nx = f32(b, p + 16);
        v.ny = f32(b, p + 20);
        v.nz = f32(b, p + 24);
    }
    const std::size_t ic = u32(b, p);
    p += 4;
    if (p + ic * 2 + 4 > b.size()) {
        error = "the file is shorter than its indices";
        return false;
    }
    m.indices.resize(ic);
    for (std::size_t i = 0; i < ic; ++i, p += 2) {
        m.indices[i] = u16(b, p);
        if (m.indices[i] >= vc) {
            error = "an index is outside of the vertices";
            return false;
        }
    }
    const std::size_t sc = u32(b, p);
    p += 4;
    std::size_t covered = 0;
    for (std::size_t i = 0; i < sc; ++i) {
        if (p + 34 + 4 > b.size()) {
            error = "the file ends inside the submeshes";
            return false;
        }
        Submesh s;
        s.index_count = u32(b, p + 2);
        s.a = u16(b, p + 6);
        s.b = u16(b, p + 8);
        for (int k = 0; k < 3; ++k) {
            s.min[k] = f32(b, p + 10 + static_cast<std::size_t>(k) * 4);
            s.max[k] = f32(b, p + 22 + static_cast<std::size_t>(k) * 4);
        }
        p += 34;
        const std::size_t nl = u16(b, p + 2);
        p += 4;
        if (p + nl + 14 > b.size()) {
            error = "the file ends inside a submesh name";
            return false;
        }
        s.name.assign(reinterpret_cast<const char*>(&b[p]), nl);
        p += nl + 12;   // the name and the three floats (1, 1, 1)
        s.c = u16(b, p);
        p += 2;
        covered += s.index_count;
        m.submeshes.push_back(std::move(s));
    }
    if (p != b.size() || covered != ic) {
        error = "the submeshes do not use the file and the index array exactly";
        return false;
    }
    m.source = path;
    out = std::move(m);
    return true;
}

bool write_mesh(const std::string& path, const Mesh& m, std::string& error) {
    std::error_code ec;
    if (!m.source.empty() && std::filesystem::exists(path, ec) && std::filesystem::equivalent(path, m.source, ec)) {
        error = "refused: the output is the file that was read";
        return false;
    }
    if (m.vertices.size() > 65535) {
        error = "a mesh has at most 65535 vertices";
        return false;
    }
    for (std::uint16_t i : m.indices) {
        if (i >= m.vertices.size()) {
            error = "an index is outside of the vertices";
            return false;
        }
    }
    std::vector<std::uint8_t> b;
    b.insert(b.end(), {'m', 'e', 's', 'h'});
    put16(b, m.version);
    put16(b, m.second_word);
    put16(b, static_cast<std::uint16_t>(m.vertices.size()));
    put16(b, m.layout_word);
    put16(b, m.reserved);
    for (const Vertex& v : m.vertices) {
        putf(b, v.x);
        putf(b, v.y);
        putf(b, v.z);
        b.push_back(v.r);
        b.push_back(v.g);
        b.push_back(v.b);
        b.push_back(v.a);
        putf(b, v.nx);
        putf(b, v.ny);
        putf(b, v.nz);
    }
    put32(b, static_cast<std::uint32_t>(m.indices.size()));
    for (std::uint16_t i : m.indices) put16(b, i);
    put32(b, static_cast<std::uint32_t>(m.submeshes.size()));
    for (const Submesh& s : m.submeshes) {
        put16(b, 0);
        put32(b, s.index_count);
        put16(b, s.a);
        put16(b, s.b);
        for (int k = 0; k < 3; ++k) putf(b, s.min[k]);
        for (int k = 0; k < 3; ++k) putf(b, s.max[k]);
        put16(b, 0);
        put16(b, static_cast<std::uint16_t>(s.name.size()));
        b.insert(b.end(), s.name.begin(), s.name.end());
        putf(b, 1.0f);
        putf(b, 1.0f);
        putf(b, 1.0f);
        put16(b, s.c);
    }
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        error = "cannot write " + path;
        return false;
    }
    f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    if (!f) {
        error = "write failed";
        return false;
    }
    return true;
}

bool bounds(const Mesh& m, float mn[3], float mx[3]) {
    if (m.vertices.empty()) return false;
    mn[0] = mx[0] = m.vertices[0].x;
    mn[1] = mx[1] = m.vertices[0].y;
    mn[2] = mx[2] = m.vertices[0].z;
    for (const Vertex& v : m.vertices) {
        mn[0] = std::min(mn[0], v.x);
        mn[1] = std::min(mn[1], v.y);
        mn[2] = std::min(mn[2], v.z);
        mx[0] = std::max(mx[0], v.x);
        mx[1] = std::max(mx[1], v.y);
        mx[2] = std::max(mx[2], v.z);
    }
    return true;
}

void add_submesh(Mesh& m) {
    Submesh s;
    s.index_count = static_cast<std::uint32_t>(m.indices.size());
    s.name = "ID1";
    float mn[3] = {0, 0, 0}, mx[3] = {0, 0, 0};
    if (bounds(m, mn, mx)) {
        for (int k = 0; k < 3; ++k) {
            s.min[k] = mn[k];
            s.max[k] = mx[k];
        }
    }
    m.submeshes.assign(1, s);
}

void translate(Mesh& m, float dx, float dy, float dz) {
    for (Vertex& v : m.vertices) {
        v.x += dx;
        v.y += dy;
        v.z += dz;
    }
}

void scale(Mesh& m, float sx, float sy, float sz) {
    for (Vertex& v : m.vertices) {
        v.x *= sx;
        v.y *= sy;
        v.z *= sz;
    }
}

}  // namespace sw::gfx
