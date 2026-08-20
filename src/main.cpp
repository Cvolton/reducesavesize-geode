#include <Geode/Geode.hpp>
#include <Geode/utils/base64.hpp>

#include <thread>
#include <vector>
#include <atomic>
#include <algorithm>
#include <semaphore>

#include <libdeflate.h>

using namespace geode::prelude;

static bool g_initialized = false;

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

std::string compressWithLibdeflate(const std::string& input, int level = 12) {
    auto compressedData = gzip_compress(reinterpret_cast<const uint8_t*>(input.data()), input.size(), level);
    return geode::utils::base64::encode(compressedData, geode::utils::base64::Base64Variant::UrlWithPad);
}

bool isRecompressed(ZStringView input) {
    auto decompressed = cocos2d::ZipUtils::decompressString(input, false, 0);
    auto recompressed = cocos2d::ZipUtils::compressString(decompressed, false, 0);

    float ratio = static_cast<float>(input.size()) / static_cast<float>(recompressed.size());
    return ratio < 0.9f;
}

$on_game(Loaded) {
    asp::Instant start = asp::Instant::now();
    auto LLM = LocalLevelManager::get();

    std::vector<Ref<GJGameLevel>> levels;
    for(auto level : LLM->m_localLevels->asExt<GJGameLevel>()) {
        levels.push_back(level);
    }

    std::thread([LLM, levels = std::move(levels), start = start]() mutable {
        size_t totalLevels = levels.size();

        std::atomic<size_t> currentIdx{0};
        std::atomic<size_t> completedCount{0};
        std::atomic<size_t> totalOldSize{0};
        std::atomic<size_t> totalNewSize{0};

        auto worker = [&]() {
            while (true) {
                size_t i = currentIdx.fetch_add(1);
                if (i >= totalLevels) {
                    break;
                }

                auto level = levels[i];
                
                std::string originalStr;
                std::binary_semaphore sem{0};
                Loader::get()->queueInMainThread([&originalStr, level, &sem]() {
                    originalStr = level->m_levelString;
                    sem.release();
                });
                sem.acquire();

                if(isRecompressed(originalStr)) {
                    log::info("Level {} is already recompressed, skipping...", i + 1);
                    completedCount.fetch_add(1);
                    continue;
                }

                size_t ogLength = originalStr.size();
                
                std::string decompressed = cocos2d::ZipUtils::decompressString(originalStr, false, 0);
                std::string compressed = compressWithLibdeflate(decompressed);
                size_t newLength = compressed.size();

                Loader::get()->queueInMainThread([level, compressed = std::move(compressed), &sem, originalStr = std::move(originalStr)]() mutable {
                    if(level->m_levelString != originalStr) {
                        log::warn("Level string changed during compression, skipping update for level {}", level->m_levelID.value());
                    } else {
                        level->m_levelString = compressed;
                    }
                    sem.release();
                });
                sem.acquire();

                totalOldSize.fetch_add(ogLength);
                totalNewSize.fetch_add(newLength);
                
                size_t currentCompleted = completedCount.fetch_add(1) + 1;

                log::info("Compressed level {} / {} ({} bytes to {} bytes)", 
                        currentCompleted, totalLevels, ogLength, newLength);
            }
        };

        unsigned int hardwareThreads = std::thread::hardware_concurrency();
        unsigned int numThreads = hardwareThreads == 0 ? 4 : std::max(1u, hardwareThreads - 2);

        log::info("Starting re-compression on {} threads for {} levels...", numThreads, totalLevels);

        std::vector<std::thread> threads;
        for (unsigned int i = 0; i < numThreads; ++i) {
            threads.emplace_back(worker);
        }

        for (auto& t : threads) {
            t.join();
        }

        size_t finalOld = totalOldSize.load();
        size_t finalNew = totalNewSize.load();

        Loader::get()->queueInMainThread([finalOld, finalNew, levels = std::move(levels), start, LLM]() mutable {
            log::info("Total size reduced from {} bytes to {} bytes ({}% reduction, {} levels processed)", 
                finalOld, finalNew, 100.0f * (finalOld - finalNew) / finalOld, levels.size());

            log::info("Compression took {}", start.elapsed());
            if (start.elapsed().seconds() > 60) {
                LLM->save();
            }

            g_initialized = true;
        });
    }).detach();
}

#include <Geode/modify/GManager.hpp>
class $modify(GManager) {
    gd::string getCompressedSaveString() {
        return compressWithLibdeflate(GManager::getSaveString());
    }
};

#include <Geode/modify/ZipUtils.hpp>
class $modify(ZipUtils) {
    static gd::string compressString(gd::string const& data, bool encrypt, int encryptionKey) {
        if(encrypt || !g_initialized) return ZipUtils::compressString(data, encrypt, encryptionKey);

        return compressWithLibdeflate(data);
    }
};