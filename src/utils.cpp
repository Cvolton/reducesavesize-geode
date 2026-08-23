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

std::vector<uint8_t> gzip_compress_parallel(const uint8_t* data, size_t size, int level = 12) {
    unsigned nThreads = std::max(1u, std::thread::hardware_concurrency() - 2);
    size_t chunkSize = (size + nThreads - 1) / nThreads;
    chunkSize = std::max<size_t>(chunkSize, 1024 * 1024);

    size_t chunkCount = (size + chunkSize - 1) / chunkSize;
    std::vector<std::vector<uint8_t>> results(chunkCount);
    std::vector<std::thread> threads;

    for (size_t i = 0; i < chunkCount; ++i) {
        threads.emplace_back([&results, chunkSize, size, level, data, i] {
            size_t offset = i * chunkSize;
            size_t len = std::min(chunkSize, size - offset);

            libdeflate_compressor* c = libdeflate_alloc_compressor(level);
            size_t bound = libdeflate_gzip_compress_bound(c, len);
            std::vector<uint8_t> out(bound);
            size_t actual = libdeflate_gzip_compress(c, data + offset, len, out.data(), bound);
            libdeflate_free_compressor(c);

            out.resize(actual);
            results[i] = std::move(out);
        });
    }
    for (auto& t : threads) t.join();

    std::vector<uint8_t> combined;
    combined.reserve(size / 3);
    for (auto& r : results) {
        combined.insert(combined.end(), r.begin(), r.end());
    }

    return combined;
}

std::string ReduceSaveSize::compressWithLibdeflate(const std::string& input, int level) {
    auto compressedData = gzip_compress(reinterpret_cast<const uint8_t*>(input.data()), input.size(), level);
    return geode::utils::base64::encode(compressedData, geode::utils::base64::Base64Variant::UrlWithPad);
}

std::string ReduceSaveSize::compressWithLibdeflateParallel(const std::string& input, int level) {
    auto compressedData = gzip_compress_parallel(reinterpret_cast<const uint8_t*>(input.data()), input.size(), level);
    return geode::utils::base64::encode(compressedData, geode::utils::base64::Base64Variant::UrlWithPad);
}