#include <Geode/Geode.hpp>
#include <Geode/utils/base64.hpp>
#include <Geode/utils/hash.hpp>

#include <thread>
#include <vector>
#include <atomic>
#include <algorithm>
#include <semaphore>

#include "utils.hpp"

using namespace geode::prelude;

static bool g_initialized = false;
static bool g_isCompressing = false;

bool isRecompressed(ZStringView input) {
    if(input.empty()) return true;

    auto decompressed = cocos2d::ZipUtils::decompressString(input, false, 0);
    auto recompressed = cocos2d::ZipUtils::compressString(decompressed, false, 0);

    float ratio = static_cast<float>(input.size()) / static_cast<float>(recompressed.size());
    return ratio < 0.9f;
}

void runCompression() {
    g_initialized = false;

    asp::Instant start = asp::Instant::now();
    auto LLM = LocalLevelManager::get();

    std::vector<Ref<GJGameLevel>> levels;
    for(auto level : LLM->m_localLevels->asExt<GJGameLevel>()) {
        levels.push_back(level);
    }

    auto hashes = Mod::get()->getSavedValue<std::unordered_set<std::string>>("hashes");

    std::thread([LLM, levels = std::move(levels), start = start, hashes = std::move(hashes)]() mutable {
        size_t totalLevels = levels.size();

        std::atomic<size_t> currentIdx{0};
        std::atomic<size_t> completedCount{0};
        std::atomic<size_t> totalOldSize{0};
        std::atomic<size_t> totalNewSize{0};

        std::unordered_set<std::string> newHashes;

        auto insertHashThreadSafe = [&newHashes](std::string const& hash) {
            static std::mutex mtx;
            std::lock_guard lock(mtx);
            newHashes.insert(hash);
        };

        auto worker = [&]() {
            auto newHashes = std::unordered_set<std::string>();

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

                auto hash = sha256(originalStr).toString();
                insertHashThreadSafe(hash);
                if(hashes.contains(hash)) {
                    log::trace("Level {} is already hashed, skipping...", i + 1);
                    completedCount.fetch_add(1);
                    continue;
                }

                if(isRecompressed(originalStr)) {
                    log::trace("Level {} is already recompressed, skipping...", i + 1);
                    completedCount.fetch_add(1);
                    continue;
                }

                size_t ogLength = originalStr.size();
                
                std::string decompressed = cocos2d::ZipUtils::decompressString(originalStr, false, 0);
                std::string compressed = ReduceSaveSize::compressWithLibdeflate(decompressed);
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

                log::debug("Compressed level {} / {} ({} bytes to {} bytes)", 
                        currentCompleted, totalLevels, ogLength, newLength);
            }
        };

        unsigned int hardwareThreads = std::thread::hardware_concurrency();
        unsigned int numThreads = hardwareThreads == 0 ? 4 : std::max(1u, hardwareThreads - 2);

        log::debug("Starting re-compression on {} threads for {} levels...", numThreads, totalLevels);

        std::vector<std::thread> threads;
        for (unsigned int i = 0; i < numThreads; ++i) {
            threads.emplace_back(worker);
        }

        for (auto& t : threads) {
            t.join();
        }

        size_t finalOld = totalOldSize.load();
        size_t finalNew = totalNewSize.load();

        Loader::get()->queueInMainThread([finalOld, finalNew, levels = std::move(levels), start, LLM, hashes = std::move(newHashes)]() mutable {
            log::debug("Total size reduced from {} bytes to {} bytes ({}% reduction, {} levels processed)", 
                finalOld, finalNew, 100.0f * (finalOld - finalNew) / finalOld, levels.size());

            log::debug("Compression took {}", start.elapsed());

            Mod::get()->setSavedValue("hashes", std::move(hashes));
            if (start.elapsed().seconds() > 20) {
                LLM->save();
                (void) Mod::get()->saveData();
            }

            g_initialized = true;
            g_isCompressing = false;
        });
    }).detach();
}

void scheduleRunCompression() {
    if(g_isCompressing) return;
    g_isCompressing = true;

    Loader::get()->queueInMainThread([]() {
        runCompression();
    });
}

$on_game(Loaded) {
    scheduleRunCompression();
}

/*#include <Geode/modify/GManager.hpp>
class $modify(GManager) {
    gd::string getCompressedSaveString() {
        return compressWithLibdeflate(GManager::getSaveString());
    }
};*/

static bool g_isSavingLevel = false;

#include <Geode/modify/ZipUtils.hpp>
class $modify(ZipUtils) {
    static void onModify(auto& self) {
        (void)self.setHookPriority("ZipUtils::compressString", Priority::VeryLate);
    }

    static gd::string compressString(gd::string const& data, bool encrypt, int encryptionKey) {
        if(g_isSavingLevel && !encrypt && g_initialized) {
            scheduleRunCompression();
            return ReduceSaveSize::compressWithLibdeflate(data, 0);
        }

        return ZipUtils::compressString(data, encrypt, encryptionKey);
    }
};

#include <Geode/modify/EditorPauseLayer.hpp>
class $modify(EditorPauseLayer) {
    void saveLevel() {
        g_isSavingLevel = true;
        EditorPauseLayer::saveLevel();
        g_isSavingLevel = false;
    }
};