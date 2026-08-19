#include <Geode/Geode.hpp>
#include <Geode/modify/GJAccountManager.hpp>

using namespace geode::prelude;

class $modify(GJAccountManager) {
    void onGetAccountBackupURLCompleted(gd::string response, gd::string tag) {
        auto GM = GameManager::sharedState();
        auto var = GM->getGameVariable(GameVar::SaveGauntlets);
        GM->setGameVariable(GameVar::SaveGauntlets, 0);

        auto GLM = GameLevelManager::get();
        for(auto [_, level] : GLM->m_dailyLevels->asExt<gd::string, GJGameLevel*>()) {
            level->m_levelString = "";

            if(level->m_dailyID != GLM->m_activeDailyID && level->m_dailyID != GLM->m_activeWeeklyID && level->m_dailyID != GLM->m_activeEventID) {
                level->m_levelNotDownloaded = true;
            }
        }

        GJAccountManager::onGetAccountBackupURLCompleted(response, tag);
        m_backupDelegate->backupAccountFailed(BackupAccountError::GenericError, -1);

        GM->setGameVariable(GameVar::SaveGauntlets, var);
    }
};