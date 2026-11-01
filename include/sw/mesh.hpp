#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace sw::gfx {

inline constexpr std::size_t kMeshHeaderSize = 14;
inline constexpr std::size_t kMeshVertexSize = 28;
inline constexpr std::uint16_t kMeshVersion = 7;
inline constexpr std::uint16_t kMeshLayoutWord = 0x13;

struct Vertex {
    float x = 0, y = 0, z = 0;
    std::uint8_t r = 255, g = 255, b = 255, a = 255;
    float nx = 0, ny = 1, nz = 0;
};

struct Submesh {
    std::uint32_t index_count = 0;
    std::uint16_t a = 0;   // meaning not known
    std::uint16_t b = 0;   // meaning not known
    float min[3] = {0, 0, 0};
    float max[3] = {0, 0, 0};
    std::string name;
    std::uint16_t c = 0;   // meaning not known
};

struct Mesh {
    std::uint16_t version = kMeshVersion;
    std::uint16_t second_word = 1;
    std::uint16_t layout_word = kMeshLayoutWord;
    std::uint16_t reserved = 0;
    std::vector<Vertex> vertices;
    std::vector<std::uint16_t> indices;
    std::vector<Submesh> submeshes;
    std::string source;
};

// Puts one submesh over the whole index array, with the bounding box of the
// vertices and the name "ID1". Use it for a mesh that was built by hand.
void add_submesh(Mesh& mesh);

// Reads a mesh file. Checks the magic, the sizes and that every index is below
// the vertex count.
bool read_mesh(const std::string& path, Mesh& out, std::string& error);

// Writes a NEW mesh file; refuses the file the mesh was read from.
bool write_mesh(const std::string& path, const Mesh& mesh, std::string& error);

// The bounding box of the vertices. False for an empty mesh.
bool bounds(const Mesh& mesh, float min[3], float max[3]);

// Moves every vertex.
void translate(Mesh& mesh, float dx, float dy, float dz);

// Scales every vertex around the origin (the normals stay unit length for a
// uniform scale; the caller fixes them for another one).
void scale(Mesh& mesh, float sx, float sy, float sz);

}  // namespace sw::gfx
