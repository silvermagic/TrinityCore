/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file cs_reset.cpp
 * @brief 重置命令脚本模块
 *
 * 本文件实现了所有与玩家数据重置相关的GM命令，包括：
 * - 重置成就
 * - 重置荣誉点数
 * - 重置等级
 * - 重置法术
 * - 重置属性
 * - 重置天赋
 * - 批量重置所有玩家的特定数据
 *
 * 这些命令主要用于测试、调试或处理特定游戏问题。
 */

/* ScriptData
Name: reset_commandscript
%Complete: 100
Comment: All reset related commands
Category: commandscripts
EndScriptData */

#include "ScriptMgr.h"
#include "AchievementMgr.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "Language.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Pet.h"
#include "Player.h"
#include "RBAC.h"
#include "World.h"
#include "WorldSession.h"

#if TRINITY_COMPILER == TRINITY_COMPILER_GNU
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

/**
 * @class reset_commandscript
 * @brief 重置命令脚本类
 *
 * 实现所有与玩家数据重置相关的GM命令。
 * 该类继承自CommandScript，提供命令注册和处理接口。
 */
class reset_commandscript : public CommandScript
{
public:
    /**
     * @brief 构造函数
     * 初始化命令脚本，设置脚本名称为"reset_commandscript"
     */
    reset_commandscript() : CommandScript("reset_commandscript") { }

    /**
     * @brief 获取命令表
     * @return 返回重置命令的命令表结构
     *
     * 注册以下命令层次结构：
     * - .reset
     *   - .achievements - 重置成就数据
     *   - .honor        - 重置荣誉点数
     *   - .level        - 重置等级
     *   - .spells       - 重置法术
     *   - .stats        - 重置属性
     *   - .talents      - 重置天赋
     *   - .all          - 批量重置所有玩家数据
     */
    std::vector<ChatCommand> GetCommands() const override
    {
        static std::vector<ChatCommand> resetCommandTable =
        {
            { "achievements", rbac::RBAC_PERM_COMMAND_RESET_ACHIEVEMENTS, true, &HandleResetAchievementsCommand, "" },
            { "honor",        rbac::RBAC_PERM_COMMAND_RESET_HONOR,        true, &HandleResetHonorCommand,        "" },
            { "level",        rbac::RBAC_PERM_COMMAND_RESET_LEVEL,        true, &HandleResetLevelCommand,        "" },
            { "spells",       rbac::RBAC_PERM_COMMAND_RESET_SPELLS,       true, &HandleResetSpellsCommand,       "" },
            { "stats",        rbac::RBAC_PERM_COMMAND_RESET_STATS,        true, &HandleResetStatsCommand,        "" },
            { "talents",      rbac::RBAC_PERM_COMMAND_RESET_TALENTS,      true, &HandleResetTalentsCommand,      "" },
            { "all",          rbac::RBAC_PERM_COMMAND_RESET_ALL,          true, &HandleResetAllCommand,          "" },
        };
        static std::vector<ChatCommand> commandTable =
        {
            { "reset", rbac::RBAC_PERM_COMMAND_RESET, true, nullptr, "", resetCommandTable },
        };
        return commandTable;
    }

    /**
     * @brief 处理重置成就命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（玩家名称，可选）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reset achievements [playername]
     *
     * 功能：重置指定玩家的所有成就数据。
     * - 如果玩家在线：直接清空成就数据
     * - 如果玩家离线：从数据库中删除成就数据
     *
     * @warning 此操作不可逆，将删除所有成就进度
     */
    static bool HandleResetAchievementsCommand(ChatHandler* handler, char const* args)
    {
        Player* target;
        ObjectGuid targetGuid;
        // 提取目标玩家（可以是在线或离线玩家）
        if (!handler->extractPlayerTarget((char*)args, &target, &targetGuid))
            return false;

        if (target)
            // 玩家在线，直接重置成就
            target->ResetAchievements();
        else
            // 玩家离线，从数据库删除成就数据
            AchievementMgr::DeleteFromDB(targetGuid);

        return true;
    }

    /**
     * @brief 处理重置荣誉命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（玩家名称，可选）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reset honor [playername]
     *
     * 功能：重置指定玩家的所有荣誉相关数据，包括：
     * - 荣誉点数归零
     * - 今日击杀数归零
     * - 昨日击杀数归零
     * - 总荣誉击杀数归零
     * - 总击杀数归零
     *
     * @note 只能对在线玩家使用
     * @warning 此操作不可逆
     */
    static bool HandleResetHonorCommand(ChatHandler* handler, char const* args)
    {
        Player* target;
        // 提取目标玩家（仅支持在线玩家）
        if (!handler->extractPlayerTarget((char*)args, &target))
            return false;

        // 重置所有荣誉相关字段
        target->SetHonorPoints(0);                                     // 荣誉点数归零
        target->SetUInt32Value(PLAYER_FIELD_KILLS, 0);                 // 总击杀数归零
        target->SetUInt32Value(PLAYER_FIELD_LIFETIME_HONORABLE_KILLS, 0); // 总荣誉击杀数归零
        target->SetUInt32Value(PLAYER_FIELD_TODAY_CONTRIBUTION, 0);    // 今日贡献归零
        target->SetUInt32Value(PLAYER_FIELD_YESTERDAY_CONTRIBUTION, 0); // 昨日贡献归零
        // 更新成就进度
        target->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_EARN_HONORABLE_KILL);

        return true;
    }

    /**
     * @brief 重置属性或等级的辅助函数
     * @param player 目标玩家指针
     * @return 操作成功返回true，失败返回false
     *
     * 此函数是重置等级和重置属性命令的共享辅助函数，
     * 负责重置玩家的基础属性设置，包括：
     * - 种族阵营
     * - 能量类型
     * - 外观模型
     * - PVP标志
     * - 单位标志
     *
     * @note 此函数不会修改等级、经验和属性值，这些由调用者处理
     */
    static bool HandleResetStatsOrLevelHelper(Player* player)
    {
        // 从DBC获取职业信息
        ChrClassesEntry const* classEntry = sChrClassesStore.LookupEntry(player->GetClass());
        if (!classEntry)
        {
            TC_LOG_ERROR("misc", "Class {} not found in DBC (Wrong DBC files?)", player->GetClass());
            return false;
        }

        // 获取职业的默认能量类型
        uint8 powerType = classEntry->DisplayPower;

        // 如果没有变形光环，重置形态
        if (!player->HasAuraType(SPELL_AURA_MOD_SHAPESHIFT))
            player->SetShapeshiftForm(FORM_NONE);

        // 重置种族阵营
        player->SetFactionForRace(player->GetRace());

        // 设置能量类型
        player->SetPowerType(Powers(powerType), false);

        // 仅当玩家未处于变形状态时重置外观模型
        if (player->GetShapeshiftForm() == FORM_NONE)
            player->InitDisplayIds();

        // 设置PVP标志
        player->ReplaceAllPvpFlags(UNIT_BYTE2_FLAG_PVP);

        // 设置玩家控制标志
        player->ReplaceAllUnitFlags(UNIT_FLAG_PLAYER_CONTROLLED);

        // 重置监视阵营索引为默认值（-1表示无监视）
        player->SetUInt32Value(PLAYER_FIELD_WATCHED_FACTION_INDEX, uint32(-1));
        return true;
    }

    /**
     * @brief 处理重置等级命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（玩家名称，可选）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reset level [playername]
     *
     * 功能：将玩家等级重置为初始等级：
     * - 普通职业：重置为 CONFIG_START_PLAYER_LEVEL 配置值
     * - 死亡骑士：重置为 CONFIG_START_DEATH_KNIGHT_PLAYER_LEVEL 配置值
     *
     * 同时会重置：
     * - 经验值归零
     * - 基础属性（生命值、法力值等）
     * - 技能点（符文）
     * - 飞行点
     * - 雕文
     * - 天赋
     *
     * @note 只能对在线玩家使用
     * @warning 此操作不可逆，宠物等级也会同步重置
     */
    static bool HandleResetLevelCommand(ChatHandler* handler, char const* args)
    {
        Player* target;
        // 提取目标玩家（仅支持在线玩家）
        if (!handler->extractPlayerTarget((char*)args, &target))
            return false;

        // 调用辅助函数重置基础属性
        if (!HandleResetStatsOrLevelHelper(target))
            return false;

        // 保存旧等级用于事件通知
        uint8 oldLevel = target->GetLevel();

        // 根据职业确定初始等级
        uint32 startLevel = target->GetClass() != CLASS_DEATH_KNIGHT
            ? sWorld->getIntConfig(CONFIG_START_PLAYER_LEVEL)
            : sWorld->getIntConfig(CONFIG_START_DEATH_KNIGHT_PLAYER_LEVEL);

        // 移除等级相关的装备属性加成
        target->_ApplyAllLevelScaleItemMods(false);
        // 设置新等级
        target->SetLevel(startLevel);
        // 初始化符文（死亡骑士）
        target->InitRunes();
        // 根据新等级初始化属性
        target->InitStatsForLevel(true);
        // 初始化飞行点
        target->InitTaxiNodesForLevel();
        // 初始化雕文
        target->InitGlyphsForLevel();
        // 初始化天赋
        target->InitTalentForLevel();
        // 经验值归零
        target->SetXP(0);

        // 重新应用等级相关的装备属性加成
        target->_ApplyAllLevelScaleItemMods(true);

        // 同步宠物等级
        if (Pet* pet = target->GetPet())
            pet->SynchronizeLevelWithOwner();

        // 触发等级变更脚本事件
        sScriptMgr->OnPlayerLevelChanged(target, oldLevel);

        return true;
    }

    /**
     * @brief 处理重置法术命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（玩家名称，可选）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reset spells [playername]
     *
     * 功能：重置玩家的法术列表，恢复为该职业/种族的初始法术。
     * - 玩家在线：立即重置法术
     * - 玩家离线：设置登录标志，下次登录时自动重置
     *
     * @note 此命令会删除玩家已学习的额外法术，仅保留职业/种族默认法术
     */
    static bool HandleResetSpellsCommand(ChatHandler* handler, char const* args)
    {
        Player* target;
        ObjectGuid targetGuid;
        std::string targetName;
        // 提取目标玩家（支持在线和离线玩家）
        if (!handler->extractPlayerTarget((char*)args, &target, &targetGuid, &targetName))
            return false;

        if (target)
        {
            // 玩家在线，直接重置法术
            target->ResetSpells(/* bool myClassOnly */);

            // 发送消息通知玩家
            ChatHandler(target->GetSession()).SendSysMessage(LANG_RESET_SPELLS);
            // 如果不是自己重置自己，通知GM操作结果
            if (!handler->GetSession() || handler->GetSession()->GetPlayer() != target)
                handler->PSendSysMessage(LANG_RESET_SPELLS_ONLINE, handler->GetNameLink(target).c_str());
        }
        else
        {
            // 玩家离线，设置登录时重置标志
            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_ADD_AT_LOGIN_FLAG);
            stmt->setUInt16(0, uint16(AT_LOGIN_RESET_SPELLS));
            stmt->setUInt32(1, targetGuid.GetCounter());
            CharacterDatabase.Execute(stmt);

            // 通知GM操作结果
            handler->PSendSysMessage(LANG_RESET_SPELLS_OFFLINE, targetName.c_str());
        }

        return true;
    }

    /**
     * @brief 处理重置属性命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（玩家名称，可选）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reset stats [playername]
     *
     * 功能：重置玩家的基础属性，保持当前等级不变，重置内容包括：
     * - 基础属性（力量、敏捷、耐力、智力、精神）
     * - 生命值和法力值
     * - 技能点（符文）
     * - 飞行点
     * - 雕文
     * - 天赋
     *
     * @note 只能对在线玩家使用
     * @note 此命令会重新计算玩家的所有基础属性
     */
    static bool HandleResetStatsCommand(ChatHandler* handler, char const* args)
    {
        Player* target;
        // 提取目标玩家（仅支持在线玩家）
        if (!handler->extractPlayerTarget((char*)args, &target))
            return false;

        // 调用辅助函数重置基础属性
        if (!HandleResetStatsOrLevelHelper(target))
            return false;

        // 初始化符文（死亡骑士）
        target->InitRunes();
        // 根据当前等级重新初始化属性
        target->InitStatsForLevel(true);
        // 初始化飞行点
        target->InitTaxiNodesForLevel();
        // 初始化雕文
        target->InitGlyphsForLevel();
        // 初始化天赋
        target->InitTalentForLevel();

        return true;
    }

    /**
     * @brief 处理重置天赋命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（玩家名称，可选）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reset talents [playername]
     *
     * 功能：重置玩家的天赋配置。
     * - 玩家在线：立即重置天赋
     * - 玩家离线：设置登录标志，下次登录时自动重置
     *
     * 特殊功能：
     * - 如果目标是猎人的宠物，则重置宠物天赋
     * - 重置玩家天赋时，同时重置所有已召唤宠物的天赋
     *
     * @note 此命令不会返还天赋点，天赋点会根据等级自动计算
     */
    static bool HandleResetTalentsCommand(ChatHandler* handler, char const* args)
    {
        Player* target;
        ObjectGuid targetGuid;
        std::string targetName;
        // 尝试提取目标玩家
        if (!handler->extractPlayerTarget((char*)args, &target, &targetGuid, &targetName))
        {
            // 如果没有找到玩家，尝试重置选中宠物（猎人宠物）的天赋
            Creature* creature = handler->getSelectedCreature();
            if (!*args && creature && creature->IsPet())
            {
                Unit* owner = creature->GetOwner();
                // 检查是否是永久宠物（猎人宠物）
                if (owner && owner->GetTypeId() == TYPEID_PLAYER && creature->ToPet()->IsPermanentPetFor(owner->ToPlayer()))
                {
                    // 重置宠物天赋
                    creature->ToPet()->resetTalents(true);
                    // 发送更新数据给主人
                    owner->ToPlayer()->SendTalentsInfoData(true);

                    // 通知GM操作结果
                    if (!handler->GetSession() || handler->GetSession()->GetPlayer() != owner->ToPlayer())
                        handler->PSendSysMessage(LANG_RESET_PET_TALENTS_ONLINE, handler->GetNameLink(owner->ToPlayer()).c_str());
                }
                return true;
            }

            // 没有有效的目标
            handler->SendSysMessage(LANG_NO_CHAR_SELECTED);
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (target)
        {
            // 玩家在线，直接重置天赋
            target->ResetTalents(true);
            target->SendTalentsInfoData(false);
            // 通知GM操作结果
            if (!handler->GetSession() || handler->GetSession()->GetPlayer() != target)
                handler->PSendSysMessage(LANG_RESET_TALENTS_ONLINE, handler->GetNameLink(target).c_str());

            // 同时重置所有宠物的天赋
            Pet* pet = target->GetPet();
            Pet::resetTalentsForAllPetsOf(target, pet, true);
            // 如果有当前宠物，发送更新数据
            if (pet)
                target->SendTalentsInfoData(true);
            return true;
        }
        else if (targetGuid)
        {
            // 玩家离线，设置登录时重置天赋和宠物天赋的标志
            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_ADD_AT_LOGIN_FLAG);
            stmt->setUInt16(0, uint16(AT_LOGIN_NONE | AT_LOGIN_RESET_PET_TALENTS));
            stmt->setUInt32(1, targetGuid.GetCounter());
            CharacterDatabase.Execute(stmt);

            // 通知GM操作结果
            std::string nameLink = handler->playerLink(targetName);
            handler->PSendSysMessage(LANG_RESET_TALENTS_OFFLINE, nameLink.c_str());
            return true;
        }

        handler->SendSysMessage(LANG_NO_CHAR_SELECTED);
        handler->SetSentErrorMessage(true);
        return false;
    }

    /**
     * @brief 处理批量重置所有玩家数据命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（重置类型：spells 或 talents）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reset all <spells|talents>
     *
     * 功能：为所有玩家设置重置标志，下次登录时自动重置：
     * - spells: 重置所有玩家的法术列表
     * - talents: 重置所有玩家的天赋和宠物天赋
     *
     * @note 此命令仅设置登录标志，不会立即生效
     * @note 在线玩家会立即获得登录标志，离线玩家在数据库中设置标志
     * @warning 此命令影响范围大，请谨慎使用
     */
    static bool HandleResetAllCommand(ChatHandler* handler, char const* args)
    {
        // 必须指定重置类型
        if (!*args)
            return false;

        std::string caseName = args;

        AtLoginFlags atLogin;

        // 根据参数确定重置类型（必须完整匹配，防止误操作）
        if (caseName == "spells")
        {
            // 重置所有玩家的法术
            atLogin = AT_LOGIN_RESET_SPELLS;
            // 向全服广播通知
            sWorld->SendWorldText(LANG_RESETALL_SPELLS);
            // 如果是控制台执行，也输出到控制台
            if (!handler->GetSession())
                handler->SendSysMessage(LANG_RESETALL_SPELLS);
        }
        else if (caseName == "talents")
        {
            // 重置所有玩家的天赋和宠物天赋
            atLogin = AtLoginFlags(AT_LOGIN_RESET_TALENTS | AT_LOGIN_RESET_PET_TALENTS);
            // 向全服广播通知
            sWorld->SendWorldText(LANG_RESETALL_TALENTS);
            // 如果是控制台执行，也输出到控制台
            if (!handler->GetSession())
               handler->SendSysMessage(LANG_RESETALL_TALENTS);
        }
        else
        {
            // 未知的重置类型
            handler->PSendSysMessage(LANG_RESETALL_UNKNOWN_CASE, args);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 在数据库中为所有角色设置登录标志
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_ALL_AT_LOGIN_FLAGS);
        stmt->setUInt16(0, uint16(atLogin));
        CharacterDatabase.Execute(stmt);

        // 为所有在线玩家设置登录标志
        std::shared_lock<std::shared_mutex> lock(*HashMapHolder<Player>::GetLock());
        HashMapHolder<Player>::MapType const& plist = ObjectAccessor::GetPlayers();
        for (HashMapHolder<Player>::MapType::const_iterator itr = plist.begin(); itr != plist.end(); ++itr)
            itr->second->SetAtLoginFlag(atLogin);

        return true;
    }
};

/**
 * @brief 注册重置命令脚本
 *
 * 此函数在脚本系统初始化时被调用，
 * 创建reset_commandscript实例并注册到命令脚本管理器中。
 */
void AddSC_reset_commandscript()
{
    new reset_commandscript();
}
