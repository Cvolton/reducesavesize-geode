#include <Geode/Geode.hpp>
#include <Geode/utils/base64.hpp>

#include "zopfli/zopfli.h"
#include "zopfli/zlib_container.h"

#include <thread>
#include <vector>
#include <atomic>
#include <algorithm>

using namespace geode::prelude;

std::string compressWithZopfli(const std::string& input) {
    ZopfliOptions options;
    ZopfliInitOptions(&options);
    options.numiterations = 15;

    unsigned char* out = nullptr;
    size_t outsize = 0;

    ZopfliZlibCompress(&options, 
                        reinterpret_cast<const unsigned char*>(input.data()), 
                        input.size(), 
                        &out, 
                        &outsize);

    std::string result(reinterpret_cast<char*>(out), outsize);
    free(out); 
    return geode::utils::base64::encode(result, geode::utils::base64::Base64Variant::UrlWithPad);
}

bool isZopfliCompressed(ZStringView input) {
    auto decompressed = cocos2d::ZipUtils::decompressString(input, false, 0);
    auto recompressed = cocos2d::ZipUtils::compressString(decompressed, false, 0);

    float ratio = static_cast<float>(input.size()) / static_cast<float>(recompressed.size());
    return ratio < 0.9f;
}

$on_game(Loaded) {
    auto LLM = LocalLevelManager::get();

    std::vector<Ref<GJGameLevel>> levels;
    for(auto level : LLM->m_localLevels->asExt<GJGameLevel>()) {
        levels.push_back(level);
    }
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
            
            std::string originalStr = level->m_levelString;

            if(isZopfliCompressed(originalStr)) {
                log::info("Level {} is already compressed, skipping...", i + 1);
                completedCount.fetch_add(1);
                continue;
            }

            size_t ogLength = originalStr.size();
            
            std::string decompressed = cocos2d::ZipUtils::decompressString(originalStr, false, 0);
            std::string compressed = compressWithZopfli(decompressed);
            size_t newLength = compressed.size();

            level->m_levelString = compressed;

            totalOldSize.fetch_add(ogLength);
            totalNewSize.fetch_add(newLength);
            
            size_t currentCompleted = completedCount.fetch_add(1) + 1;

            log::info("Compressed level {} / {} ({} bytes to {} bytes)", 
                    currentCompleted, totalLevels, ogLength, newLength);
        }
    };

    unsigned int hardwareThreads = std::thread::hardware_concurrency();
    unsigned int numThreads = hardwareThreads == 0 ? 4 : std::max(1u, hardwareThreads - 1);

    log::info("Starting Zopfli compression on {} threads for {} levels...", numThreads, totalLevels);

    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < numThreads; ++i) {
        threads.emplace_back(worker);
    }

    for (auto& t : threads) {
        t.join();
    }

    size_t finalOld = totalOldSize.load();
    size_t finalNew = totalNewSize.load();
    log::info("Total size reduced from {} bytes to {} bytes ({}% reduction)", 
            finalOld, finalNew, 100.0f * (finalOld - finalNew) / finalOld);
}