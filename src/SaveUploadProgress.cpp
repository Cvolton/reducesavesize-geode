#include <Geode/Geode.hpp>

using namespace geode::prelude;

void forceRenderFrame() {
    auto director = CCDirector::sharedDirector();
    auto ogPaused = director->m_bPaused;
    director->m_bPaused = true;
    director->drawScene();
    director->m_bPaused = ogPaused;
}

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

        MusicDownloadManager::sharedState()->clearUnusedSongs();

        prepareBar->updateProgress(25.f);
        forceRenderFrame();

        log::info("Cleared unused songs, starting backup... {}", instant.elapsed());

        auto gmString = GM->getCompressedSaveString();
        log::info("Compressed save string, {}", instant.elapsed());

        prepareBar->updateProgress(50.f);
        forceRenderFrame();

        m_gameManagerSize = gmString.size();

        auto LLM = LocalLevelManager::get();
        LLM->updateLevelOrder();
        auto llmString = LLM->getCompressedSaveString();
        log::info("Compressed local level manager string, {}", instant.elapsed());

        prepareBar->updateProgress(75.f);
        forceRenderFrame();

        m_localLevelsSize = llmString.size();

        m_GJP2 = gjp2;

        std::string postString = GameLevelManager::sharedState()->getBasePostString() + "&saveData=" + gmString + ";" + llmString + "&secret=Wmfv3899gc9";

        auto req = web::WebRequest();
        req.onProgress([uploadBar, prepareBar](web::WebProgress const& p) mutable {
            if(uploadBar) {
                uploadBar->updateProgress(p.uploadProgress().value_or(0.f));
            }

            log::info("download progress: {}", p.downloadProgress().value_or(0.f));
            log::info("upload progress: {}", p.uploadProgress().value_or(0.f));
        });
        req.bodyString(postString).userAgent("");

        m_fields->m_listener.spawn(
            req.post(url),
            [this, uploadBar, prepareBar, accLayer, label, loadingCircle, cclayercolor](web::WebResponse res) {
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
                /*if(res.error() || res.string().unwrapOrDefault() != "1") {
                    log::error("Failed to backup account: {}", res.string().unwrapOrDefault());
                    m_backupDelegate->backupAccountFailed(BackupAccountError::GenericError, -1);
                }
                else {
                    log::info("Successfully backed up account");
                    m_backupDelegate->backupAccountFinished();
                }*/
                GJAccountManager::handleIt(res.error(), res.string().unwrapOrDefault(), "bak_account", GJHttpType::BackupAccount);
            }
        );

        log::info("Backup request sent, {}", instant.elapsed());
        prepareBar->updateProgress(100.f);
        forceRenderFrame();

        return true;
    }
};