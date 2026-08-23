#include "utils.hpp"
#include <libdeflate.h>

#include <Geode/Geode.hpp>
#include <Geode/utils/base64.hpp>

using namespace geode::prelude;

std::vector<uint8_t> gzip_compress(const uint8_t* data, size_t size, int level = 12) {
    libdeflate_compressor* c = libdeflate_alloc_compressor(level);
    if (!c) return {};

    size_t bound = libdeflate_gzip_compress_bound(c, size);
    std::vector<uint8_t> out(bound);

    size_t actual = libdeflate_gzip_compress(c, data, size, out.data(), bound);
    libdeflate_free_compressor(c);

    if (actual == 0) return {};
    out.resize(actual);
    return out;
}

std::string ReduceSaveSize::compressWithLibdeflate(const std::string& input, int level) {
    auto compressedData = gzip_compress(reinterpret_cast<const uint8_t*>(input.data()), input.size(), level);
    return geode::utils::base64::encode(compressedData, geode::utils::base64::Base64Variant::UrlWithPad);
}