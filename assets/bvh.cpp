#include <cstdint>
#include <algorithm>
#include <limits>
#include <vector>
#include <numeric>
#include <cassert>
#include <array>
#include <stack>
#include <utility>
#include <tuple>
#include <cmath>
#include <cctype>
#include <optional>
#include <fstream>
#include <iostream>

struct Vec3 {
    float values[3];

    Vec3() = default;
    Vec3(float x, float y, float z) : values { x, y, z } {}
    explicit Vec3(float x) : Vec3(x, x, x) {}

    float& operator [] (int i) { return values[i]; }
    float operator [] (int i) const { return values[i]; }
};

inline Vec3 operator + (const Vec3& a, const Vec3& b) {
    return Vec3(a[0] + b[0], a[1] + b[1], a[2] + b[2]);
}

inline Vec3 operator - (const Vec3& a, const Vec3& b) {
    return Vec3(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
}

inline Vec3 operator * (const Vec3& a, const Vec3& b) {
    return Vec3(a[0] * b[0], a[1] * b[1], a[2] * b[2]);
}

inline Vec3 operator * (const Vec3& a, float b) {
    return Vec3(a[0] * b, a[1] * b, a[2] * b);
}

inline Vec3 operator * (float a, const Vec3& b) {
    return b * a;
}

inline Vec3 min(const Vec3& a, const Vec3& b) {
    return Vec3(std::min(a[0], b[0]), std::min(a[1], b[1]), std::min(a[2], b[2]));
}

inline Vec3 max(const Vec3& a, const Vec3& b) {
    return Vec3(std::max(a[0], b[0]), std::max(a[1], b[1]), std::max(a[2], b[2]));
}

inline float dot(const Vec3& a, const Vec3& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

inline float length(const Vec3& a) {
    return std::sqrt(dot(a, a));
}

inline Vec3 normalize(const Vec3& a) {
    return a * (1.0f / length(a));
}

inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return Vec3(
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0]);
}

struct BBox {
    Vec3 min, max;

    BBox() = default;
    BBox(const Vec3& min, const Vec3& max) : min(min), max(max) {}
    explicit BBox(const Vec3& point) : BBox(point, point) {}

    BBox& extend(const Vec3& point) {
        return extend(BBox(point));
    }

    BBox& extend(const BBox& other) {
        min = ::min(min, other.min);
        max = ::max(max, other.max);
        return *this;
    }

    Vec3 diagonal() const {
        return max - min;
    }

    int largest_axis() const {
        auto d = diagonal();
        int axis = 0;
        if (d[axis] < d[1]) axis = 1;
        if (d[axis] < d[2]) axis = 2;
        return axis;
    }

    float half_area() const {
        auto d = diagonal();
        return (d[0] + d[1]) * d[2] + d[0] * d[1];
    }

    static BBox empty() {
        return BBox(
            Vec3(-std::numeric_limits<float>::max()),
            Vec3(+std::numeric_limits<float>::max()));
    }
};

inline float robust_min(float a, float b) { return a < b ? a : b; }
inline float robust_max(float a, float b) { return a > b ? a : b; }
inline float safe_inverse(float x) {
    return std::fabs(x) <= std::numeric_limits<float>::epsilon()
        ? std::copysign(1.0f / std::numeric_limits<float>::epsilon(), x)
        : 1.0f / x;
}

struct Ray {
    Vec3 org, dir;
    float tmin, tmax;

    Vec3 inv_dir() const {
        return Vec3(safe_inverse(dir[0]), safe_inverse(dir[1]), safe_inverse(dir[2]));
    }
};

struct Hit {
    uint32_t prim_index;

    operator bool () const { return prim_index != static_cast<uint32_t>(-1); }
    static Hit none() { return Hit { static_cast<uint32_t>(-1) }; }
};

struct Node {
    BBox bbox;
    uint32_t prim_count;
    uint32_t first_index;

    Node() = default;
    Node(const BBox& bbox, uint32_t prim_count, uint32_t first_index)
        : bbox(bbox), prim_count(prim_count), first_index(first_index)
    {}

    bool is_leaf() const { return prim_count != 0; }

    struct Intersection {
        float tmin;
        float tmax;
        operator bool () const { return tmin <= tmax; }
    };

    Intersection intersect(const Ray& ray) const {
        auto inv_dir = ray.inv_dir();
        auto tmin = (bbox.min - ray.org) * inv_dir;
        auto tmax = (bbox.max - ray.org) * inv_dir;
        std::tie(tmin, tmax) = std::make_pair(min(tmin, tmax), max(tmin, tmax));
        return Intersection {
            robust_max(tmin[0], robust_max(tmin[1], robust_max(tmin[2], ray.tmin))),
            robust_min(tmax[0], robust_min(tmax[1], robust_min(tmax[2], ray.tmax))) };
    }
};

struct Bvh {
    std::vector<Node> nodes;
    std::vector<size_t> prim_indices;

    Bvh() = default;

    static Bvh build(const BBox* bboxes, const Vec3* centers, size_t prim_count);

    size_t depth(size_t node_index = 0) const {
        auto& node = nodes[node_index];
        return node.is_leaf() ? 1 : 1 + std::max(depth(node.first_index), depth(node.first_index + 1));
    }

    template <typename Prim>
    Hit traverse(Ray& ray, const std::vector<Prim>& prims) const;
};

struct BuildConfig {
    size_t min_prims;
    size_t max_prims;
    float traversal_cost;
};

static constexpr BuildConfig build_config = { 2, 8, 1.0f };

struct Bin {
    BBox bbox = BBox::empty();
    size_t prim_count = 0;

    Bin& extend(const Bin& other) {
        bbox.extend(other.bbox);
        prim_count += other.prim_count;
        return *this;
    }

    float cost() const { return bbox.half_area() * prim_count; }
};

static constexpr size_t bin_count = 16;

static size_t bin_index(int axis, const BBox& bbox, const Vec3& center) {
    int index = (center[axis] - bbox.min[axis]) * (bin_count / (bbox.max[axis] - bbox.min[axis]));
    return std::min(bin_count - 1, static_cast<size_t>(std::max(0, index)));
}

struct Split {
    int axis = 0;
    float cost = std::numeric_limits<float>::max();
    size_t right_bin = 0;

    operator bool () const { return right_bin != 0; }
    bool operator < (const Split& other) const {
        return *this && cost < other.cost;
    }
};

static Split find_best_split(
    int axis,
    const Bvh& bvh,
    const Node& node,
    const BBox* bboxes,
    const Vec3* centers)
{
    std::array<Bin, bin_count> bins;
    for (size_t i = 0; i < node.prim_count; ++i) {
        auto prim_index = bvh.prim_indices[i];
        auto& bin = bins[bin_index(axis, node.bbox, centers[prim_index])];
        bin.bbox.extend(bboxes[prim_index]);
        bin.prim_count++;
    }
    std::array<float, bin_count> right_cost;
    Bin left_accum, right_accum;
    for (size_t i = bin_count - 1; i > 0; --i) {
        right_accum.extend(bins[i]);
        right_cost[i] = right_accum.cost();
    }
    Split split { axis };
    for (size_t i = 0; i < bin_count - 1; ++i) {
        left_accum.extend(bins[i]);
        float cost = left_accum.cost() + right_cost[i + 1];
        if (cost < split.cost) {
            split.cost  = cost;
            split.right_bin = i + 1;
        }
    }
    return split;
}

static void build_recursive(
    Bvh& bvh,
    size_t node_index,
    size_t& node_count,
    const BBox* bboxes,
    const Vec3* centers)
{
    auto& node = bvh.nodes[node_index];    
    assert(node.is_leaf());

    node.bbox = BBox::empty();
    for (size_t i = 0; i < node.prim_count; ++i)
        node.bbox.extend(bboxes[bvh.prim_indices[i]]);

    if (node.prim_count < build_config.min_prims)
        return;

    Split min_split;
    for (int axis = 0; axis < 3; ++axis)
        min_split = std::min(min_split, find_best_split(axis, bvh, node, bboxes, centers));

    float leaf_cost = node.bbox.half_area() * (node.prim_count - build_config.traversal_cost);
    size_t first_right; // Index of the first primitive in the right child
    if (!min_split || min_split.cost > leaf_cost) {
        if (node.prim_count > build_config.max_prims) {
            // Fall back solution: The node has too many primitives, we use the median split
            int axis = node.bbox.largest_axis();
            std::sort(
                bvh.prim_indices.begin() + node.first_index,
                bvh.prim_indices.begin() + node.first_index + node.prim_count,
                [&] (size_t i, size_t j) { return centers[i][axis] < centers[j][axis]; });
            first_right = node.first_index + node.prim_count / 2;
        } else
            // Terminate with a leaf
            return;
    } else {
        // The split was good, we need to partition the primitives
        first_right = std::partition(
            bvh.prim_indices.begin() + node.first_index,
            bvh.prim_indices.begin() + node.first_index + node.prim_count,
            [&] (size_t i) { return bin_index(min_split.axis, node.bbox, centers[i]) < min_split.right_bin; })
            - bvh.prim_indices.begin();
    }

    auto first_child = node_count;
    auto& left  = bvh.nodes[first_child];
    auto& right = bvh.nodes[first_child + 1];
    node_count += 2;

    left .prim_count  = first_right - node.first_index;
    right.prim_count  = node.prim_count - left.prim_count;
    left .first_index = node.first_index;
    right.first_index = first_right;

    node.first_index = first_child;
    node.prim_count  = 0;

    build_recursive(bvh, first_child, node_count, bboxes, centers);
    build_recursive(bvh, first_child + 1, node_count, bboxes, centers);
}

Bvh Bvh::build(const BBox* bboxes, const Vec3* centers, size_t prim_count) {
    Bvh bvh;

    bvh.prim_indices.resize(prim_count);
    std::iota(bvh.prim_indices.begin(), bvh.prim_indices.end(), 0);

    bvh.nodes.resize(2 * prim_count - 1);
    bvh.nodes[0].prim_count = prim_count;
    bvh.nodes[0].first_index = 0;

    size_t node_count = 1;
    build_recursive(bvh, 0, node_count, bboxes, centers);
    bvh.nodes.resize(node_count);
    return bvh;
}

struct Triangle {
    Vec3 p0, p1, p2;

    Triangle() = default;
    Triangle(const Vec3& p0, const Vec3& p1, const Vec3& p2)
        : p0(p0), p1(p1), p2(p2)
    {}

    bool intersect(Ray& ray) const;
};

bool Triangle::intersect(Ray& ray) const {
    auto e1 = p0 - p1;
    auto e2 = p2 - p0;
    auto n = cross(e1, e2);

    auto c = p0 - ray.org;
    auto r = cross(ray.dir, c);
    auto inv_det = 1.0f / dot(n, ray.dir);

    auto u = dot(r, e2) * inv_det;
    auto v = dot(r, e1) * inv_det;
    auto w = 1.0f - u - v;

    // These comparisons are designed to return false
    // when one of t, u, or v is a NaN
    if (u >= 0 && v >= 0 && w >= 0) {
        auto t = dot(n, c) * inv_det;
        if (t >= ray.tmin && t <= ray.tmax) {
            ray.tmax = t;
            return true;
        }
    }

    return false;
}

template <typename Prim>
Hit Bvh::traverse(Ray& ray, const std::vector<Prim>& prims) const {
    auto hit = Hit::none();
    std::stack<uint32_t> stack;
    stack.push(0);
    while (!stack.empty()) {
        auto& node = nodes[stack.top()];
        stack.pop();
        if (!node.intersect(ray))
            continue;

        if (node.is_leaf()) {
            for (size_t i = 0; i < node.prim_count; ++i) {
                auto prim_index = prim_indices[node.first_index + i];
                if (prims[prim_index].intersect(ray))
                    hit.prim_index = prim_index;
            }
        } else {
            stack.push(node.first_index);
            stack.push(node.first_index + 1);
        }
    }
    return hit;
}

namespace obj {

inline void remove_eol(char* ptr) {
    int i = 0;
    while (ptr[i]) i++;
    i--;
    while (i > 0 && std::isspace(ptr[i])) {
        ptr[i] = '\0';
        i--;
    }
}

inline char* strip_spaces(char* ptr) {
    while (std::isspace(*ptr)) ptr++;
    return ptr;
}

inline std::optional<int> read_index(char** ptr) {
    char* base = *ptr;

    // Detect end of line (negative indices are supported) 
    base = strip_spaces(base);
    if (!std::isdigit(*base) && *base != '-')
        return std::nullopt;

    int index = std::strtol(base, &base, 10);
    base = strip_spaces(base);

    if (*base == '/') {
        base++;

        // Handle the case when there is no texture coordinate
        if (*base != '/')
            std::strtol(base, &base, 10);

        base = strip_spaces(base);

        if (*base == '/') {
            base++;
            std::strtol(base, &base, 10);
        }
    }

    *ptr = base;
    return std::make_optional(index);
}

inline std::vector<Triangle> load_from_stream(std::istream& is) {
    static constexpr size_t max_line = 1024;
    char line[max_line];

    std::vector<Vec3> vertices;
    std::vector<Triangle> triangles;

    while (is.getline(line, max_line)) {
        char* ptr = strip_spaces(line);
        if (*ptr == '\0' || *ptr == '#')
            continue;
        remove_eol(ptr);
        if (*ptr == 'v' && std::isspace(ptr[1])) {
            auto x = std::strtof(ptr + 1, &ptr);
            auto y = std::strtof(ptr, &ptr);
            auto z = std::strtof(ptr, &ptr);
            vertices.emplace_back(x, y, z);
        } else if (*ptr == 'f' && std::isspace(ptr[1])) {
            Vec3 points[2];
            ptr += 2;
            for (size_t i = 0; ; ++i) {
                if (auto index = read_index(&ptr)) {
                    size_t j = *index < 0 ? vertices.size() + *index : *index - 1;
                    assert(j < vertices.size());
                    auto v = vertices[j];
                    if (i >= 2) {
                        triangles.emplace_back(points[0], points[1], v);
                        points[1] = v;
                    } else {
                        points[i] = v;
                    }
                } else {
                    break;
                }
            }
        }
    }

    return triangles;
}

inline std::vector<Triangle> load_from_file(const std::string& file) {
    std::ifstream is(file);
    if (is)
        return load_from_stream(is);
    return std::vector<Triangle>();
}

} // namespace obj

static const size_t width = 1024;
static const size_t height = 1024;
static const auto output_file = "out.ppm";

int main(int argc, char** argv) {
    Vec3 eye(0, 1, 3);
    Vec3 dir(0, 0, -1);
    Vec3 up(0, 1, 0);

    if (argc < 2) {
        std::cerr << "Missing input file" << std::endl;
        return 1;
    }
    auto tris = obj::load_from_file(argv[1]);
    if (tris.empty()) {
        std::cerr << "No triangle was found in input OBJ file" << std::endl;
        return 1;
    }
    std::cout << "Loaded file with " << tris.size() << " triangle(s)" << std::endl;

    std::vector<BBox> bboxes(tris.size());
    std::vector<Vec3> centers(tris.size());
    for (size_t i = 0; i < tris.size(); ++i) {
        bboxes[i] = BBox(tris[i].p0).extend(tris[i].p1).extend(tris[i].p2);
        centers[i] = (tris[i].p0 + tris[i].p1 + tris[i].p2) * (1.0f / 3.0f);
    }
    auto bvh = Bvh::build(bboxes.data(), centers.data(), tris.size());
    std::cout << "Built BVH with " << bvh.nodes.size() << " node(s), depth " << bvh.depth() << std::endl;

    dir = normalize(dir);
    auto right = normalize(cross(dir, up));
    up = cross(right, dir);

    std::vector<uint8_t> image(width * height * 3);
    size_t intersections = 0;
    std::cout << "Rendering";
    for (size_t y = 0; y < height; ++y) {
        for (size_t x = 0; x < width; ++x) {
            auto u = 2.0f * static_cast<float>(x)/static_cast<float>(width) - 1.0f;
            auto v = 2.0f * static_cast<float>(y)/static_cast<float>(height) - 1.0f;
            Ray ray;
            ray.org = eye;
            ray.dir = dir + u * right + v * up;
            ray.tmin = 0;
            ray.tmax = std::numeric_limits<float>::max();
            auto hit = bvh.traverse(ray, tris);
            if (hit)
                intersections++;
            auto pixel = 3 * (y * width + x);
            image[pixel + 0] = hit.prim_index * 37;
            image[pixel + 1] = hit.prim_index * 91;
            image[pixel + 2] = hit.prim_index * 51;
        }
        if (y % (height / 10) == 0)
            std::cout << "." << std::flush;
    }
    std::cout << "\n" << intersections << " intersection(s) found" << std::endl;

    std::ofstream out(output_file, std::ofstream::binary);
    out << "P6 " << width << " " << height << " " << 255 << "\n";
    for(size_t j = height; j > 0; --j)
        out.write(reinterpret_cast<char*>(image.data() + (j - 1) * 3 * width), sizeof(uint8_t) * 3 * width);
    std::cout << "Image saved as " << output_file << std::endl;
}
