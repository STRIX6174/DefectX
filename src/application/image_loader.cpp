#include "application/image_loader.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <filesystem>
#include <fstream>
#include <vector>
#include <iterator>
#include <cstdint>
#include <cstring>

namespace qf {
namespace application {

namespace {

// ---------------------------------------------------------------------------
// MD5（RFC 1321）——仅用于文件内容哈希，不依赖任何第三方库。
// ---------------------------------------------------------------------------
inline uint32_t rotl32(uint32_t x, uint32_t n) {
    return (x << n) | (x >> (32 - n));
}

const uint32_t kMd5K[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
    0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
    0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
    0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
    0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
    0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
};

const uint32_t kMd5S[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
};

std::string md5Hex(const uint8_t* data, size_t len) {
    const uint64_t bitLen = static_cast<uint64_t>(len) * 8;

    // 补位：0x80 + 0x00…，直到长度 mod 64 == 56，再追加 8 字节小端 bitLen。
    size_t paddedLen = len + 1;
    while (paddedLen % 64 != 56) ++paddedLen;
    paddedLen += 8;

    std::vector<uint8_t> buf(paddedLen, 0);
    if (len > 0) std::memcpy(buf.data(), data, len);
    buf[len] = 0x80;
    for (int i = 0; i < 8; ++i) {
        buf[paddedLen - 8 + i] = static_cast<uint8_t>((bitLen >> (8 * i)) & 0xFF);
    }

    uint32_t a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;

    for (size_t off = 0; off < paddedLen; off += 64) {
        uint32_t M[16];
        for (int i = 0; i < 16; ++i) {
            M[i] = static_cast<uint32_t>(buf[off + i * 4])
                 | (static_cast<uint32_t>(buf[off + i * 4 + 1]) << 8)
                 | (static_cast<uint32_t>(buf[off + i * 4 + 2]) << 16)
                 | (static_cast<uint32_t>(buf[off + i * 4 + 3]) << 24);
        }

        uint32_t A = a0, B = b0, C = c0, D = d0;
        for (int i = 0; i < 64; ++i) {
            uint32_t F;
            uint32_t g;
            if (i < 16) {
                F = (B & C) | (~B & D);
                g = i;
            } else if (i < 32) {
                F = (D & B) | (~D & C);
                g = (5 * i + 1) % 16;
            } else if (i < 48) {
                F = B ^ C ^ D;
                g = (3 * i + 5) % 16;
            } else {
                F = C ^ (B | ~D);
                g = (7 * i) % 16;
            }

            uint32_t tmp = D;
            D = C;
            C = B;
            B = B + rotl32(A + F + kMd5K[i] + M[g], kMd5S[i]);
            A = tmp;
        }

        a0 += A; b0 += B; c0 += C; d0 += D;
    }

    static const char* hex = "0123456789abcdef";
    std::string out(32, '0');
    const uint32_t words[4] = {a0, b0, c0, d0};
    for (int w = 0; w < 4; ++w) {
        for (int i = 0; i < 4; ++i) {
            const uint8_t byte = static_cast<uint8_t>((words[w] >> (8 * i)) & 0xFF);
            out[w * 8 + i * 2]     = hex[byte >> 4];
            out[w * 8 + i * 2 + 1] = hex[byte & 0x0F];
        }
    }
    return out;
}

std::vector<uint8_t> readFileBytes(const std::string& filePath) {
    std::ifstream f(filePath, std::ios::binary);
    if (!f.is_open()) return {};
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
}

}  // anonymous namespace

std::pair<cv::Mat, model::ErrorCode> ImageLoader::load(const std::string& filePath) {
    cv::Mat img = cv::imread(filePath, cv::IMREAD_GRAYSCALE);
    if (img.empty()) {
        return {img, model::ErrorCode::DecodeFailed};
    }
    return {img, model::ErrorCode::None};
}

model::ImageEntry ImageLoader::buildEntry(const std::string& filePath, const cv::Mat& img) {
    model::ImageEntry entry;
    entry.filePath = filePath;
    entry.width    = img.cols;
    entry.height   = img.rows;
    entry.status   = model::ImageStatus::Pending;
    return entry;
}

std::string ImageLoader::computeMd5(const std::string& filePath) const {
    auto bytes = readFileBytes(filePath);
    if (bytes.empty()) return {};
    return md5Hex(bytes.data(), bytes.size());
}

std::string ImageLoader::relativePath(const std::string& filePath, const std::string& root) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path file = fs::absolute(filePath, ec);
    fs::path base = fs::absolute(root, ec);

    if (!ec && !file.empty()) {
        auto fit = file.begin();
        auto bit = base.begin();
        for (; fit != file.end() && bit != base.end(); ++fit, ++bit) {
            if (*fit != *bit) break;
        }
        if (fit != file.end()) {
            std::string rel;
            for (auto it = fit; it != file.end(); ++it) {
                if (!rel.empty()) rel += "/";
                rel += it->string();
            }
            return rel;
        }
    }
    return fs::path(filePath).filename().string();
}

}  // namespace application
}  // namespace qf