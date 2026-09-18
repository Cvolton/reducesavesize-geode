#include <Geode/Geode.hpp>
#include <semaphore>
#include "utils.hpp"

using namespace geode::prelude;

void forceRenderFrame() {
    auto director = CCDirector::sharedDirector();
    auto ogPaused = director->m_bPaused;
    director->m_bPaused = true;
    director->drawScene();
    director->m_bPaused = ogPaused;
}

bool shouldSkipLocalLevels() {
    if(auto mod = Loader::get()->getInstalledMod("cvolton.ignore_created_levels_when_cloud_saving")) {
        if(auto scene = CCScene::get()) {
            if(auto ignoreCheck = typeinfo_cast<CCMenuItemToggler*>(scene->getChildByIDRecursive("cvolton.ignore_created_levels_when_cloud_saving/include-levels-check"))) {
                return !ignoreCheck->isOn();
            }
        }
    }
    return false;
}

constexpr const float TO_MB = 9.536743e-07;

#include <Geode/modify/AccountLayer.hpp>
class $modify(RSSAccountLayer, AccountLayer) {
    void customSetup() {
        AccountLayer::customSetup();

        constexpr float globalOffset = 16; //- 10.f;

        createBar("account-backup-prepare-bar"_spr, 73.f - globalOffset, "Preparing:");
        createBar("account-backup-progress-bar"_spr, 93.f - globalOffset, "Uploading:");

        auto label = Label::create("Saving account data", "goldFont.fnt");
        label->setPosition({ (this->getContentWidth() / 2), (this->getContentHeight() / 2) + 80.f });
        label->setScale(.8f);
        label->setVisible(false);
        label->setID("account-backup-label"_spr);
        m_mainLayer->addChild(label, 110);

        auto loadingCircle = LoadingSpinner::create(65.f);
        loadingCircle->setPosition({ (this->getContentWidth() / 2), (this->getContentHeight() / 2) + 8.f });
        loadingCircle->setID("account-backup-loading-circle"_spr);
        loadingCircle->setVisible(false);
        m_mainLayer->addChild(loadingCircle, 110);

        auto cclayercolor = CCLayerColor::create(ccc4(0, 0, 0, 75));
        cclayercolor->setVisible(false);
        cclayercolor->setID("account-backup-cclayercolor"_spr);
        m_mainLayer->addChild(cclayercolor, 108);

        /*auto bg = NineSlice::create("square02_001.png");
        bg->setContentSize({ 320.f, 50.f });
        bg->setAnchorPoint({ 0.5f, 0.5f });
        bg->setPosition({ (this->getContentWidth() / 2), (this->getContentHeight() / 2) - 83.f  + globalOffset });
        bg->setOpacity(230);
        m_mainLayer->addChild(bg, 109);*/
    }

    void createBar(ZStringView id, float offset, ZStringView text) {
        auto bar = ProgressBar::create();
        bar->setAnchorPoint({0.5f, 0.5f});
        bar->setPosition({ (this->getContentWidth() / 2) + 40, (this->getContentHeight() / 2) - offset });
        bar->setID(id);
        bar->setVisible(false);
        m_mainLayer->addChild(bar, 110);

        auto label = Label::create(text, "bigFont.fnt");
        label->setAnchorPoint({1, 0.5f});
        label->setPosition({-3, 9});
        label->setScale(.45f);
        bar->addChild(label);
    }
};

#include <Geode/modify/GJAccountManager.hpp>
class $modify(GJAccountManager) {
    struct Fields {
        TaskHolder<web::WebResponse> m_listener;
    };

    bool backupAccount(gd::string url) {
        if(GJAccountManager::isDLActive("bak_account")) return 0;

        auto prepareBar = Ref(typeinfo_cast<ProgressBar*>(CCScene::get()->getChildByIDRecursive("account-backup-prepare-bar"_spr)));
        prepareBar->setVisible(true);

        auto uploadBar = Ref(typeinfo_cast<ProgressBar*>(CCScene::get()->getChildByIDRecursive("account-backup-progress-bar"_spr)));
        uploadBar->setVisible(true);

        auto label = Ref(typeinfo_cast<Label*>(CCScene::get()->getChildByIDRecursive("account-backup-label"_spr)));
        label->setVisible(true);

        auto loadingCircle = Ref(typeinfo_cast<LoadingSpinner*>(CCScene::get()->getChildByIDRecursive("account-backup-loading-circle"_spr)));
        loadingCircle->setVisible(true);

        auto cclayercolor = Ref(typeinfo_cast<CCLayerColor*>(CCScene::get()->getChildByIDRecursive("account-backup-cclayercolor"_spr)));
        cclayercolor->setVisible(true);

        Ref<AccountLayer> accLayer;
        if(auto parent = prepareBar->getParent()) {
            if((accLayer = typeinfo_cast<AccountLayer*>(parent->getParent())) != nullptr) {
                accLayer->m_linkedAccountTitle->setVisible(false);
                accLayer->m_buttonMenu->setVisible(false);
                accLayer->m_loadingCircle->setVisible(false);
            }
        }

        std::function<void()> hideCustomUI = [uploadBar, prepareBar, accLayer, label, loadingCircle, cclayercolor]() {
            if(uploadBar) {
                uploadBar->setVisible(false);
            }
            if(prepareBar) {
                prepareBar->setVisible(false);
            }
            if(accLayer) {
                accLayer->m_linkedAccountTitle->setVisible(true);
                accLayer->m_buttonMenu->setVisible(true);
            }
            if(label) {
                label->setVisible(false);
            }
            if(loadingCircle) {
                loadingCircle->setVisible(false);
            }
            if(cclayercolor) {
                cclayercolor->setVisible(false);
            }
        };

        auto instant = asp::Instant::now();

        prepareBar->updateProgress(0.f);
        uploadBar->updateProgress(0.f);
        forceRenderFrame();

        GJAccountManager::addDLToActive("bak_account");

        auto gjp2 = m_GJP2;
        m_GJP2 = "";

        auto GM = GameManager::sharedState();
        GM->m_quickSave = true;

        log::info("Backing up account to {}...", url);
        log::info("Current time: {}", instant.elapsed());

        std::counting_semaphore<> sem{0};

        std::string gmString;
        std::string llmString;

        // Prepare
        MusicDownloadManager::sharedState()->clearUnusedSongs();
        prepareBar->updateProgress(20.f);
        forceRenderFrame();

        // LLM saving
        bool shouldSkip = shouldSkipLocalLevels();
        auto LLM = LocalLevelManager::get();
        LLM->updateLevelOrder();
        llmString = LLM->getSaveString();
        
        prepareBar->updateProgress(40.f);
        forceRenderFrame();

        if(!shouldSkip) {
            async::runtime().spawnBlocking<void>([&sem, this, instant, &llmString]{
                llmString = ReduceSaveSize::compressWithLibdeflate(llmString);
                log::info("Compressed local level manager string, {}", instant.elapsed());
                sem.release();
            });
        }

        // GM saving
        // clearUnusedSongs needs to be outside of the blocking thread because it creates CCStrings
        // and therefore interacts with the autorelease pool

        gmString = GM->getSaveString();

        async::runtime().spawnBlocking<void>([&sem, this, instant, GM, &gmString]{
            gmString = ReduceSaveSize::compressWithLibdeflate(gmString);
            log::info("Compressed save string, {}", instant.elapsed());
            
            sem.release();
        });

        // waiting for GM
        prepareBar->updateProgress(60.f);
        forceRenderFrame();

        sem.acquire();
        prepareBar->updateProgress(80.f);
        forceRenderFrame();

        sem.acquire();
        prepareBar->updateProgress(95.f);
        forceRenderFrame();

        m_localLevelsSize = llmString.size();
        m_gameManagerSize = gmString.size();

        m_GJP2 = gjp2;

        float fullSaveSize = (m_gameManagerSize * TO_MB) + (m_localLevelsSize * TO_MB);
        log::info("Full save size: {} MB (GM: {} MB, LLM: {} MB)", fullSaveSize, m_gameManagerSize * TO_MB, m_localLevelsSize * TO_MB);

        std::function<void()> doSave = [this, hideCustomUI, gmString = std::move(gmString), llmString = std::move(llmString), uploadBar, prepareBar, GM, url, instant, accLayer]() mutable {
            std::string postString = fmt::format("{}&saveData={};{}&secret=Wmfv3899gc9", GameLevelManager::sharedState()->getBasePostString(), gmString, llmString);

            if(accLayer) {
                accLayer->m_textArea->setString("");
            }

            auto req = web::WebRequest();
            req.onProgress([uploadBar, prepareBar](web::WebProgress const& p) mutable {
                if(uploadBar) {
                    uploadBar->updateProgress(p.uploadProgress().value_or(0.f));
                }
            });
            req.bodyString(postString).userAgent("");

            m_fields->m_listener.spawn(
                req.post(url),
                [this, hideCustomUI = std::move(hideCustomUI)](web::WebResponse res) mutable {
                    hideCustomUI();
                    GJAccountManager::handleIt(!res.error(), res.string().unwrapOrDefault(), "bak_account", GJHttpType::BackupAccount);
                }
            );

            GM->m_quickSave = false;

            log::info("Backup request sent, {}", instant.elapsed());
            prepareBar->updateProgress(100.f);
            forceRenderFrame();
        };

        /*if(fullSaveSize > 32.f) {
            createQuickPopup(
                "Save file too large", 
                fmt::format("Your save file is <cr>too large</c> to be saved.\n<cy>Size: {:.2f}/32MB</c> <co>(Profile: {:.2f}, Levels: {:.2f})</c>\n<cg>Try anyway?</c> <cr>(it will fail)</c>", fullSaveSize, m_gameManagerSize * TO_MB, m_localLevelsSize * TO_MB), 
                "No", "Yes", [this, doSave, hideCustomUI](FLAlertLayer *alert, bool btn2) mutable {
                    if(btn2) {
                        doSave();
                    } else {
                        hideCustomUI();
                        GJAccountManager::handleIt(false, "-1", "bak_account", GJHttpType::BackupAccount);
                    }
                }
            );
            return false;
        }*/

        doSave();

        return true;
    }
};