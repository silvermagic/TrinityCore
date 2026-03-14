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
 * @file cs_reload.cpp
 * @brief 重载命令脚本模块
 *
 * 本文件实现了所有与数据库表重载相关的GM命令，是TrinityCore中命令数量最多的模块之一。
 * 重载命令允许在不重启服务器的情况下从数据库重新加载配置数据。
 *
 * 主要功能分类：
 * 1. 批量重载命令（.reload all xxx）
 * 2. 单表重载命令（.reload xxx）
 * 3. 语言包重载命令（.reload xxx_locale）
 * 4. 脚本重载命令（.reload xxx_scripts）
 * 5. 战利品表重载命令（.reload xxx_loot_template）
 *
 * 使用场景：
 * - 数据库配置更新后实时生效
 * - 测试新配置而无需重启服务器
 * - 修复运行时的配置问题
 *
 * @note 部分重载操作可能影响正在执行的游戏逻辑，请谨慎使用
 */

/* ScriptData
Name: reload_commandscript
%Complete: 100
Comment: All reload related commands
Category: commandscripts
EndScriptData */

#include "ScriptMgr.h"
#include "AccountMgr.h"
#include "AchievementMgr.h"
#include "AuctionHouseMgr.h"
#include "BattlegroundMgr.h"
#include "Chat.h"
#include "CreatureTextMgr.h"
#include "DatabaseEnv.h"
#include "DisableMgr.h"
#include "ItemEnchantmentMgr.h"
#include "Language.h"
#include "LFGMgr.h"
#include "Log.h"
#include "LootMgr.h"
#include "MapManager.h"
#include "ObjectMgr.h"
#include "SkillDiscovery.h"
#include "SkillExtraItems.h"
#include "SmartAI.h"
#include "SpellMgr.h"
#include "StringConvert.h"
#include "TicketMgr.h"
#include "WaypointManager.h"
#include "World.h"

#if TRINITY_COMPILER == TRINITY_COMPILER_GNU
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

/**
 * @class reload_commandscript
 * @brief 重载命令脚本类
 *
 * 实现所有与数据库表重载相关的GM命令。
 * 该类继承自CommandScript，提供命令注册和处理接口。
 *
 * 重载命令分为以下几类：
 * 1. 批量重载命令 - 一次性重载相关的一组表
 * 2. 单表重载命令 - 重载特定的数据库表
 * 3. 语言包重载命令 - 重载多语言支持相关的表
 *
 * 性能考虑：
 * - 重载操作通常涉及数据库查询和内存更新
 * - 部分重载可能需要遍历所有在线玩家
 * - 建议在低峰期执行大规模重载操作
 */
class reload_commandscript : public CommandScript
{
public:
    /**
     * @brief 构造函数
     * 初始化命令脚本，设置脚本名称为"reload_commandscript"
     */
    reload_commandscript() : CommandScript("reload_commandscript") { }

    /**
     * @brief 获取命令表
     * @return 返回重载命令的命令表结构
     *
     * 注册以下命令层次结构：
     * - .reload
     *   - .all            - 重载所有数据
     *   - .all achievement - 重载所有成就相关数据
     *   - .all area       - 重载所有区域相关数据
     *   - .all gossips    - 重载所有对话相关数据
     *   - .all item       - 重载所有物品相关数据
     *   - .all locales    - 重载所有语言包
     *   - .all loot       - 重载所有战利品表
     *   - .all npc        - 重载所有NPC相关数据
     *   - .all quest      - 重载所有任务相关数据
     *   - .all scripts    - 重载所有脚本
     *   - .all spell      - 重载所有法术相关数据
     *   - .auctions       - 重载拍卖行数据
     *   - .config         - 重载配置文件
     *   - ... (以及其他大量单表重载命令)
     */
    std::vector<ChatCommand> GetCommands() const override
    {
        // 批量重载命令表
        static std::vector<ChatCommand> reloadAllCommandTable =
        {
            { "achievement",                   rbac::RBAC_PERM_COMMAND_RELOAD_ALL_ACHIEVEMENT,                  true,  &HandleReloadAllAchievementCommand,              "" },
            { "area",                          rbac::RBAC_PERM_COMMAND_RELOAD_ALL_AREA,                         true,  &HandleReloadAllAreaCommand,                     "" },
            { "gossips",                       rbac::RBAC_PERM_COMMAND_RELOAD_ALL_GOSSIP,                       true,  &HandleReloadAllGossipsCommand,                  "" },
            { "item",                          rbac::RBAC_PERM_COMMAND_RELOAD_ALL_ITEM,                         true,  &HandleReloadAllItemCommand,                     "" },
            { "locales",                       rbac::RBAC_PERM_COMMAND_RELOAD_ALL_LOCALES,                      true,  &HandleReloadAllLocalesCommand,                  "" },
            { "loot",                          rbac::RBAC_PERM_COMMAND_RELOAD_ALL_LOOT,                         true,  &HandleReloadAllLootCommand,                     "" },
            { "npc",                           rbac::RBAC_PERM_COMMAND_RELOAD_ALL_NPC,                          true,  &HandleReloadAllNpcCommand,                      "" },
            { "quest",                         rbac::RBAC_PERM_COMMAND_RELOAD_ALL_QUEST,                        true,  &HandleReloadAllQuestCommand,                    "" },
            { "scripts",                       rbac::RBAC_PERM_COMMAND_RELOAD_ALL_SCRIPTS,                      true,  &HandleReloadAllScriptsCommand,                  "" },
            { "spell",                         rbac::RBAC_PERM_COMMAND_RELOAD_ALL_SPELL,                        true,  &HandleReloadAllSpellCommand,                    "" },
            { "",                              rbac::RBAC_PERM_COMMAND_RELOAD_ALL,                              true,  &HandleReloadAllCommand,                         "" },
        };
        // 单表重载命令表
        static std::vector<ChatCommand> reloadCommandTable =
        {
            { "auctions",                      rbac::RBAC_PERM_COMMAND_RELOAD_AUCTIONS,                         true,  &HandleReloadAuctionsCommand,                   "" },
            { "access_requirement",            rbac::RBAC_PERM_COMMAND_RELOAD_ACCESS_REQUIREMENT,               true,  &HandleReloadAccessRequirementCommand,          "" },
            { "achievement_criteria_data",     rbac::RBAC_PERM_COMMAND_RELOAD_ACHIEVEMENT_CRITERIA_DATA,        true,  &HandleReloadAchievementCriteriaDataCommand,    "" },
            { "achievement_reward",            rbac::RBAC_PERM_COMMAND_RELOAD_ACHIEVEMENT_REWARD,               true,  &HandleReloadAchievementRewardCommand,          "" },
            { "all",                           rbac::RBAC_PERM_COMMAND_RELOAD_ALL,                              true,  nullptr,                                           "", reloadAllCommandTable },
            { "areatrigger_involvedrelation",  rbac::RBAC_PERM_COMMAND_RELOAD_AREATRIGGER_INVOLVEDRELATION,     true,  &HandleReloadQuestAreaTriggersCommand,          "" },
            { "areatrigger_tavern",            rbac::RBAC_PERM_COMMAND_RELOAD_AREATRIGGER_TAVERN,               true,  &HandleReloadAreaTriggerTavernCommand,          "" },
            { "areatrigger_teleport",          rbac::RBAC_PERM_COMMAND_RELOAD_AREATRIGGER_TELEPORT,             true,  &HandleReloadAreaTriggerTeleportCommand,        "" },
            { "autobroadcast",                 rbac::RBAC_PERM_COMMAND_RELOAD_AUTOBROADCAST,                    true,  &HandleReloadAutobroadcastCommand,              "" },
            { "battleground_template",         rbac::RBAC_PERM_COMMAND_RELOAD_BATTLEGROUND_TEMPLATE,            true,  &HandleReloadBattlegroundTemplate,              "" },
            { "broadcast_text",                rbac::RBAC_PERM_COMMAND_RELOAD_BROADCAST_TEXT,                   true,  &HandleReloadBroadcastTextCommand,              "" },
            { "conditions",                    rbac::RBAC_PERM_COMMAND_RELOAD_CONDITIONS,                       true,  &HandleReloadConditions,                        "" },
            { "config",                        rbac::RBAC_PERM_COMMAND_RELOAD_CONFIG,                           true,  &HandleReloadConfigCommand,                     "" },
            { "creature_text",                 rbac::RBAC_PERM_COMMAND_RELOAD_CREATURE_TEXT,                    true,  &HandleReloadCreatureText,                      "" },
            { "creature_questender",           rbac::RBAC_PERM_COMMAND_RELOAD_CREATURE_QUESTENDER,              true,  &HandleReloadCreatureQuestEnderCommand,         "" },
            { "creature_linked_respawn",       rbac::RBAC_PERM_COMMAND_RELOAD_CREATURE_LINKED_RESPAWN,          true,  &HandleReloadLinkedRespawnCommand,              "" },
            { "creature_loot_template",        rbac::RBAC_PERM_COMMAND_RELOAD_CREATURE_LOOT_TEMPLATE,           true,  &HandleReloadLootTemplatesCreatureCommand,      "" },
            { "creature_movement_override",    rbac::RBAC_PERM_COMMAND_RELOAD_CREATURE_MOVEMENT_OVERRIDE,       true,  &HandleReloadCreatureMovementOverrideCommand,   "" },
            { "creature_onkill_reputation",    rbac::RBAC_PERM_COMMAND_RELOAD_CREATURE_ONKILL_REPUTATION,       true,  &HandleReloadOnKillReputationCommand,           "" },
            { "creature_queststarter",         rbac::RBAC_PERM_COMMAND_RELOAD_CREATURE_QUESTSTARTER,            true,  &HandleReloadCreatureQuestStarterCommand,       "" },
            { "creature_summon_groups",        rbac::RBAC_PERM_COMMAND_RELOAD_CREATURE_SUMMON_GROUPS,           true,  &HandleReloadCreatureSummonGroupsCommand,       "" },
            { "creature_template",             rbac::RBAC_PERM_COMMAND_RELOAD_CREATURE_TEMPLATE,                true,  &HandleReloadCreatureTemplateCommand,           "" },
            { "disables",                      rbac::RBAC_PERM_COMMAND_RELOAD_DISABLES,                         true,  &HandleReloadDisablesCommand,                   "" },
            { "disenchant_loot_template",      rbac::RBAC_PERM_COMMAND_RELOAD_DISENCHANT_LOOT_TEMPLATE,         true,  &HandleReloadLootTemplatesDisenchantCommand,    "" },
            { "event_scripts",                 rbac::RBAC_PERM_COMMAND_RELOAD_EVENT_SCRIPTS,                    true,  &HandleReloadEventScriptsCommand,               "" },
            { "fishing_loot_template",         rbac::RBAC_PERM_COMMAND_RELOAD_FISHING_LOOT_TEMPLATE,            true,  &HandleReloadLootTemplatesFishingCommand,       "" },
            { "graveyard_zone",                rbac::RBAC_PERM_COMMAND_RELOAD_GRAVEYARD_ZONE,                   true,  &HandleReloadGameGraveyardZoneCommand,          "" },
            { "game_tele",                     rbac::RBAC_PERM_COMMAND_RELOAD_GAME_TELE,                        true,  &HandleReloadGameTeleCommand,                   "" },
            { "gameobject_questender",         rbac::RBAC_PERM_COMMAND_RELOAD_GAMEOBJECT_QUESTENDER,            true,  &HandleReloadGOQuestEnderCommand,               "" },
            { "gameobject_loot_template",      rbac::RBAC_PERM_COMMAND_RELOAD_GAMEOBJECT_QUEST_LOOT_TEMPLATE,   true,  &HandleReloadLootTemplatesGameobjectCommand,    "" },
            { "gameobject_queststarter",       rbac::RBAC_PERM_COMMAND_RELOAD_GAMEOBJECT_QUESTSTARTER,          true,  &HandleReloadGOQuestStarterCommand,             "" },
            { "gm_tickets",                    rbac::RBAC_PERM_COMMAND_RELOAD_GM_TICKETS,                       true,  &HandleReloadGMTicketsCommand,                  "" },
            { "gossip_menu",                   rbac::RBAC_PERM_COMMAND_RELOAD_GOSSIP_MENU,                      true,  &HandleReloadGossipMenuCommand,                 "" },
            { "gossip_menu_option",            rbac::RBAC_PERM_COMMAND_RELOAD_GOSSIP_MENU_OPTION,               true,  &HandleReloadGossipMenuOptionCommand,           "" },
            { "item_enchantment_template",     rbac::RBAC_PERM_COMMAND_RELOAD_ITEM_ENCHANTMENT_TEMPLATE,        true,  &HandleReloadItemEnchantementsCommand,          "" },
            { "item_loot_template",            rbac::RBAC_PERM_COMMAND_RELOAD_ITEM_LOOT_TEMPLATE,               true,  &HandleReloadLootTemplatesItemCommand,          "" },
            { "item_set_names",                rbac::RBAC_PERM_COMMAND_RELOAD_ITEM_SET_NAMES,                   true,  &HandleReloadItemSetNamesCommand,               "" },
            { "lfg_dungeon_rewards",           rbac::RBAC_PERM_COMMAND_RELOAD_LFG_DUNGEON_REWARDS,              true,  &HandleReloadLfgRewardsCommand,                 "" },
            // 语言包重载命令
            { "achievement_reward_locale",     rbac::RBAC_PERM_COMMAND_RELOAD_ACHIEVEMENT_REWARD_LOCALE,        true,  &HandleReloadLocalesAchievementRewardCommand,   "" },
            { "creature_template_locale",      rbac::RBAC_PERM_COMMAND_RELOAD_CRETURE_TEMPLATE_LOCALE,          true,  &HandleReloadLocalesCreatureCommand,            "" },
            { "creature_text_locale",          rbac::RBAC_PERM_COMMAND_RELOAD_CRETURE_TEXT_LOCALE,              true,  &HandleReloadLocalesCreatureTextCommand,        "" },
            { "gameobject_template_locale",    rbac::RBAC_PERM_COMMAND_RELOAD_GAMEOBJECT_TEMPLATE_LOCALE,       true,  &HandleReloadLocalesGameobjectCommand,          "" },
            { "gossip_menu_option_locale",     rbac::RBAC_PERM_COMMAND_RELOAD_GOSSIP_MENU_OPTION_LOCALE,        true,  &HandleReloadLocalesGossipMenuOptionCommand,    "" },
            { "item_template_locale",          rbac::RBAC_PERM_COMMAND_RELOAD_ITEM_TEMPLATE_LOCALE,             true,  &HandleReloadLocalesItemCommand,                "" },
            { "item_set_name_locale",          rbac::RBAC_PERM_COMMAND_RELOAD_ITEM_SET_NAME_LOCALE,             true,  &HandleReloadLocalesItemSetNameCommand,         "" },
            { "npc_text_locale",               rbac::RBAC_PERM_COMMAND_RELOAD_NPC_TEXT_LOCALE,                  true,  &HandleReloadLocalesNpcTextCommand,             "" },
            { "page_text_locale",              rbac::RBAC_PERM_COMMAND_RELOAD_PAGE_TEXT_LOCALE,                 true,  &HandleReloadLocalesPageTextCommand,            "" },
            { "points_of_interest_locale",     rbac::RBAC_PERM_COMMAND_RELOAD_POINTS_OF_INTEREST_LOCALE,        true,  &HandleReloadLocalesPointsOfInterestCommand,    "" },
            { "quest_template_locale",         rbac::RBAC_PERM_COMMAND_RELOAD_QUEST_TEMPLATE_LOCALE,            true,  &HandleReloadLocalesQuestCommand,               "" },
            { "mail_level_reward",             rbac::RBAC_PERM_COMMAND_RELOAD_MAIL_LEVEL_REWARD,                true,  &HandleReloadMailLevelRewardCommand,            "" },
            { "mail_loot_template",            rbac::RBAC_PERM_COMMAND_RELOAD_MAIL_LOOT_TEMPLATE,               true,  &HandleReloadLootTemplatesMailCommand,          "" },
            { "milling_loot_template",         rbac::RBAC_PERM_COMMAND_RELOAD_MILLING_LOOT_TEMPLATE,            true,  &HandleReloadLootTemplatesMillingCommand,       "" },
            { "npc_spellclick_spells",         rbac::RBAC_PERM_COMMAND_RELOAD_NPC_SPELLCLICK_SPELLS,            true,  &HandleReloadSpellClickSpellsCommand,           "" },
            { "npc_vendor",                    rbac::RBAC_PERM_COMMAND_RELOAD_NPC_VENDOR,                       true,  &HandleReloadNpcVendorCommand,                  "" },
            { "page_text",                     rbac::RBAC_PERM_COMMAND_RELOAD_PAGE_TEXT,                        true,  &HandleReloadPageTextsCommand,                  "" },
            { "pickpocketing_loot_template",   rbac::RBAC_PERM_COMMAND_RELOAD_PICKPOCKETING_LOOT_TEMPLATE,      true,  &HandleReloadLootTemplatesPickpocketingCommand, "" },
            { "points_of_interest",            rbac::RBAC_PERM_COMMAND_RELOAD_POINTS_OF_INTEREST,               true,  &HandleReloadPointsOfInterestCommand,           "" },
            { "prospecting_loot_template",     rbac::RBAC_PERM_COMMAND_RELOAD_PROSPECTING_LOOT_TEMPLATE,        true,  &HandleReloadLootTemplatesProspectingCommand,   "" },
            { "quest_greeting",                rbac::RBAC_PERM_COMMAND_RELOAD_QUEST_GREETING,                   true,  &HandleReloadQuestGreetingCommand,              "" },
            { "quest_greeting_locale",         rbac::RBAC_PERM_COMMAND_RELOAD_QUEST_GREETING_LOCALE,            true,  &HandleReloadLocalesQuestGreetingCommand,       "" },
            { "quest_poi",                     rbac::RBAC_PERM_COMMAND_RELOAD_QUEST_POI,                        true,  &HandleReloadQuestPOICommand,                   "" },
            { "quest_template",                rbac::RBAC_PERM_COMMAND_RELOAD_QUEST_TEMPLATE,                   true,  &HandleReloadQuestTemplateCommand,              "" },
            { "rbac",                          rbac::RBAC_PERM_COMMAND_RELOAD_RBAC,                             true,  &HandleReloadRBACCommand,                       "" },
            { "reference_loot_template",       rbac::RBAC_PERM_COMMAND_RELOAD_REFERENCE_LOOT_TEMPLATE,          true,  &HandleReloadLootTemplatesReferenceCommand,     "" },
            { "reserved_name",                 rbac::RBAC_PERM_COMMAND_RELOAD_RESERVED_NAME,                    true,  &HandleReloadReservedNameCommand,               "" },
            { "reputation_reward_rate",        rbac::RBAC_PERM_COMMAND_RELOAD_REPUTATION_REWARD_RATE,           true,  &HandleReloadReputationRewardRateCommand,       "" },
            { "reputation_spillover_template", rbac::RBAC_PERM_COMMAND_RELOAD_SPILLOVER_TEMPLATE,               true,  &HandleReloadReputationRewardRateCommand,       "" },
            { "skill_discovery_template",      rbac::RBAC_PERM_COMMAND_RELOAD_SKILL_DISCOVERY_TEMPLATE,         true,  &HandleReloadSkillDiscoveryTemplateCommand,     "" },
            { "skill_extra_item_template",     rbac::RBAC_PERM_COMMAND_RELOAD_SKILL_EXTRA_ITEM_TEMPLATE,        true,  &HandleReloadSkillExtraItemTemplateCommand,     "" },
            { "skill_fishing_base_level",      rbac::RBAC_PERM_COMMAND_RELOAD_SKILL_FISHING_BASE_LEVEL,         true,  &HandleReloadSkillFishingBaseLevelCommand,      "" },
            { "skinning_loot_template",        rbac::RBAC_PERM_COMMAND_RELOAD_SKINNING_LOOT_TEMPLATE,           true,  &HandleReloadLootTemplatesSkinningCommand,      "" },
            { "smart_scripts",                 rbac::RBAC_PERM_COMMAND_RELOAD_SMART_SCRIPTS,                    true,  &HandleReloadSmartScripts,                      "" },
            { "spell_required",                rbac::RBAC_PERM_COMMAND_RELOAD_SPELL_REQUIRED,                   true,  &HandleReloadSpellRequiredCommand,              "" },
            { "spell_area",                    rbac::RBAC_PERM_COMMAND_RELOAD_SPELL_AREA,                       true,  &HandleReloadSpellAreaCommand,                  "" },
            { "spell_bonus_data",              rbac::RBAC_PERM_COMMAND_RELOAD_SPELL_BONUS_DATA,                 true,  &HandleReloadSpellBonusesCommand,               "" },
            { "spell_group",                   rbac::RBAC_PERM_COMMAND_RELOAD_SPELL_GROUP,                      true,  &HandleReloadSpellGroupsCommand,                "" },
            { "spell_learn_spell",             rbac::RBAC_PERM_COMMAND_RELOAD_SPELL_LEARN_SPELL,                true,  &HandleReloadSpellLearnSpellCommand,            "" },
            { "spell_loot_template",           rbac::RBAC_PERM_COMMAND_RELOAD_SPELL_LOOT_TEMPLATE,              true,  &HandleReloadLootTemplatesSpellCommand,         "" },
            { "spell_linked_spell",            rbac::RBAC_PERM_COMMAND_RELOAD_SPELL_LINKED_SPELL,               true,  &HandleReloadSpellLinkedSpellCommand,           "" },
            { "spell_pet_auras",               rbac::RBAC_PERM_COMMAND_RELOAD_SPELL_PET_AURAS,                  true,  &HandleReloadSpellPetAurasCommand,              "" },
            { "spell_proc",                    rbac::RBAC_PERM_COMMAND_RELOAD_SPELL_PROC,                       true,  &HandleReloadSpellProcsCommand,                 "" },
            { "spell_scripts",                 rbac::RBAC_PERM_COMMAND_RELOAD_SPELL_SCRIPTS,                    true,  &HandleReloadSpellScriptsCommand,               "" },
            { "spell_target_position",         rbac::RBAC_PERM_COMMAND_RELOAD_SPELL_TARGET_POSITION,            true,  &HandleReloadSpellTargetPositionCommand,        "" },
            { "spell_threats",                 rbac::RBAC_PERM_COMMAND_RELOAD_SPELL_THREATS,                    true,  &HandleReloadSpellThreatsCommand,               "" },
            { "spell_group_stack_rules",       rbac::RBAC_PERM_COMMAND_RELOAD_SPELL_GROUP_STACK_RULES,          true,  &HandleReloadSpellGroupStackRulesCommand,       "" },
            { "trainer",                       rbac::RBAC_PERM_COMMAND_RELOAD_TRAINER,                          true,  &HandleReloadTrainerCommand,                    "" },
            { "trinity_string",                rbac::RBAC_PERM_COMMAND_RELOAD_TRINITY_STRING,                   true,  &HandleReloadTrinityStringCommand,              "" },
            { "waypoint_scripts",              rbac::RBAC_PERM_COMMAND_RELOAD_WAYPOINT_SCRIPTS,                 true,  &HandleReloadWpScriptsCommand,                  "" },
            { "waypoint_data",                 rbac::RBAC_PERM_COMMAND_RELOAD_WAYPOINT_DATA,                    true,  &HandleReloadWpCommand,                         "" },
            { "vehicle_template",              rbac::RBAC_PERM_COMMAND_RELOAD_VEHICLE_TEMPLATE,                 true,  &HandleReloadVehicleTemplateCommand,            "" },
            { "vehicle_accessory",             rbac::RBAC_PERM_COMMAND_RELOAD_VEHICLE_ACCESORY,                 true,  &HandleReloadVehicleAccessoryCommand,           "" },
            { "vehicle_template_accessory",    rbac::RBAC_PERM_COMMAND_RELOAD_VEHICLE_TEMPLATE_ACCESSORY,       true,  &HandleReloadVehicleTemplateAccessoryCommand,   "" },
        };
        static std::vector<ChatCommand> commandTable =
        {
            { "reload",                        rbac::RBAC_PERM_COMMAND_RELOAD,                                  true,  nullptr,                                           "", reloadCommandTable },
        };
        return commandTable;
    }

    // ==================== 批量重载命令 ====================

    /**
     * @brief 处理重载GM工单命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（未使用）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reload gm_tickets
     *
     * 功能：从数据库重新加载所有GM工单数据。
     * 包括所有未处理的玩家工单和支持请求。
     */
    static bool HandleReloadGMTicketsCommand(ChatHandler* /*handler*/, char const* /*args*/)
    {
        sTicketMgr->LoadTickets();
        return true;
    }

    /**
     * @brief 处理重载所有数据命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（未使用）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reload all
     *
     * 功能：一次性重载所有可重载的数据库表。
     * 这是一个综合性命令，会依次调用所有分组的批量重载命令：
     * - 技能钓鱼等级
     * - 所有成就数据
     * - 所有区域数据
     * - 所有战利品表
     * - 所有NPC数据
     * - 所有任务数据
     * - 所有法术数据
     * - 所有物品数据
     * - 所有对话数据
     * - 所有语言包
     * - 访问要求
     * - 邮件等级奖励
     * - 保留名称
     * - Trinity字符串
     * - 传送点
     * - 生物移动覆盖
     * - 生物召唤组
     * - 载具附件
     * - 自动广播
     * - 战场模板
     *
     * @warning 此命令会触发大量数据库查询，可能影响服务器性能
     * @note 建议在服务器低峰期使用
     */
    static bool HandleReloadAllCommand(ChatHandler* handler, char const* /*args*/)
    {
        // 重载钓鱼技能基础等级
        HandleReloadSkillFishingBaseLevelCommand(handler, "");

        // 依次重载各分组数据
        HandleReloadAllAchievementCommand(handler, "");
        HandleReloadAllAreaCommand(handler, "");
        HandleReloadAllLootCommand(handler, "");
        HandleReloadAllNpcCommand(handler, "");
        HandleReloadAllQuestCommand(handler, "");
        HandleReloadAllSpellCommand(handler, "");
        HandleReloadAllItemCommand(handler, "");
        HandleReloadAllGossipsCommand(handler, "");
        HandleReloadAllLocalesCommand(handler, "");

        // 重载其他杂项数据
        HandleReloadAccessRequirementCommand(handler, "");
        HandleReloadMailLevelRewardCommand(handler, "");
        HandleReloadReservedNameCommand(handler, "");
        HandleReloadTrinityStringCommand(handler, "");
        HandleReloadGameTeleCommand(handler, "");

        HandleReloadCreatureMovementOverrideCommand(handler, "");
        HandleReloadCreatureSummonGroupsCommand(handler);

        HandleReloadVehicleAccessoryCommand(handler, "");
        HandleReloadVehicleTemplateAccessoryCommand(handler, "");

        HandleReloadAutobroadcastCommand(handler, "");
        HandleReloadBattlegroundTemplate(handler, "");
        return true;
    }

    /**
     * @brief 处理重载所有成就数据命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（未使用）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reload all achievement
     *
     * 功能：重载所有成就相关的数据表：
     * - achievement_criteria_data：成就条件数据
     * - achievement_reward：成就奖励数据
     */
    static bool HandleReloadAllAchievementCommand(ChatHandler* handler, char const* /*args*/)
    {
        HandleReloadAchievementCriteriaDataCommand(handler, "");
        HandleReloadAchievementRewardCommand(handler, "");
        return true;
    }

    /**
     * @brief 处理重载所有区域数据命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（未使用）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reload all area
     *
     * 功能：重载所有区域相关的数据表：
     * - areatrigger_teleport：区域触发传送点
     * - areatrigger_tavern：旅店区域触发点
     * - game_graveyard_zone：墓地区域关联
     *
     * @note 任务区域触发器会在all quest命令中重载
     */
    static bool HandleReloadAllAreaCommand(ChatHandler* handler, char const* /*args*/)
    {
        // 任务区域触发器在all quest命令中重载
        HandleReloadAreaTriggerTeleportCommand(handler, "");
        HandleReloadAreaTriggerTavernCommand(handler, "");
        HandleReloadGameGraveyardZoneCommand(handler, "");
        return true;
    }

    /**
     * @brief 处理重载所有战利品表命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（未使用）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reload all loot
     *
     * 功能：重载所有战利品表数据。
     * 包括所有类型的战利品模板表：
     * - creature_loot_template：生物掉落
     * - gameobject_loot_template：游戏物体掉落
     * - item_loot_template：物品开出
     * - disenchant_loot_template：分解掉落
     * - fishing_loot_template：钓鱼掉落
     * - mail_loot_template：邮件奖励
     * - milling_loot_template：研磨产物
     * - pickpocketing_loot_template：偷窃掉落
     * - prospecting_loot_template：选矿产物
     * - skinning_loot_template：剥皮掉落
     * - spell_loot_template：法术产物
     * - reference_loot_template：引用掉落
     *
     * @note 同时会重载条件数据，确保战利品条件正确应用
     */
    static bool HandleReloadAllLootCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Loot Tables...");
        // 加载所有战利品表
        LoadLootTables();
        handler->SendGlobalGMSysMessage("DB tables `*_loot_template` reloaded.");
        // 重新加载条件数据
        sConditionMgr->LoadConditions(true);
        return true;
    }

    /**
     * @brief 处理重载所有NPC数据命令
     * @param handler 聊天命令处理器
     * @param args 命令参数
     * @return 命令执行成功返回true
     *
     * 命令格式: .reload all npc
     *
     * 功能：重载所有NPC相关的数据表：
     * - trainer：训练师数据（如果不是从all_gossips调用）
     * - npc_vendor：NPC商人出售物品
     * - points_of_interest：兴趣点
     * - npc_spellclick_spells：NPC点击法术
     *
     * @param args 参数不为'a'时才重载trainer，防止与其他批量命令重复
     */
    static bool HandleReloadAllNpcCommand(ChatHandler* handler, char const* args)
    {
        // 如果不是从all_gossips调用，才重载trainer
        if (*args != 'a')
        HandleReloadTrainerCommand(handler, "a");
        HandleReloadNpcVendorCommand(handler, "a");
        HandleReloadPointsOfInterestCommand(handler, "a");
        HandleReloadSpellClickSpellsCommand(handler, "a");
        return true;
    }

    /**
     * @brief 处理重载所有任务数据命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（未使用）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reload all quest
     *
     * 功能：重载所有任务相关的数据表：
     * - quest_greeting：任务问候语
     * - areatrigger_involvedrelation：任务区域触发
     * - quest_poi：任务兴趣点
     * - quest_template：任务模板
     * - 任务起始者和结束者关系
     *
     * @note 这会重新建立所有任务与NPC/游戏物体的关系
     */
    static bool HandleReloadAllQuestCommand(ChatHandler* handler, char const* /*args*/)
    {
        HandleReloadQuestGreetingCommand(handler, "");
        HandleReloadQuestAreaTriggersCommand(handler, "a");
        HandleReloadQuestPOICommand(handler, "a");
        HandleReloadQuestTemplateCommand(handler, "a");

        // 重新加载任务起始者和结束者关系
        TC_LOG_INFO("misc", "Re-Loading Quests Relations...");
        sObjectMgr->LoadQuestStartersAndEnders();
        handler->SendGlobalGMSysMessage("DB tables `*_queststarter` and `*_questender` reloaded.");
        return true;
    }

    /**
     * @brief 处理重载所有脚本命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（未使用）
     * @return 命令执行成功返回true，脚本正在执行中返回false
     *
     * 命令格式: .reload all scripts
     *
     * 功能：重载所有脚本相关的数据表：
     * - event_scripts：事件脚本
     * - spell_scripts：法术脚本
     * - waypoint_scripts：路径点脚本
     * - waypoint_data：路径点数据
     *
     * @warning 如果有脚本正在执行中，命令将失败，需要稍后重试
     * @note 这是安全机制，防止在脚本执行过程中重载导致数据不一致
     */
    static bool HandleReloadAllScriptsCommand(ChatHandler* handler, char const* /*args*/)
    {
        // 检查是否有脚本正在执行
        if (sMapMgr->IsScriptScheduled())
        {
            handler->PSendSysMessage("DB scripts used currently, please attempt reload later.");
            handler->SetSentErrorMessage(true);
            return false;
        }

        TC_LOG_INFO("misc", "Re-Loading Scripts...");
        // 依次重载各类脚本
        HandleReloadEventScriptsCommand(handler, "a");
        HandleReloadSpellScriptsCommand(handler, "a");
        handler->SendGlobalGMSysMessage("DB tables `*_scripts` reloaded.");
        HandleReloadWpScriptsCommand(handler, "a");
        HandleReloadWpCommand(handler, "a");
        return true;
    }

    /**
     * @brief 处理重载所有法术数据命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（未使用）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reload all spell
     *
     * 功能：重载所有法术相关的数据表：
     * - skill_discovery_template：技能发现模板
     * - skill_extra_item_template：技能额外物品模板
     * - spell_required：法术需求
     * - spell_area：法术区域
     * - spell_group：法术组
     * - spell_learn_spell：法术学习法术
     * - spell_linked_spell：法术链接法术
     * - spell_proc：法术触发
     * - spell_bonus_data：法术加成数据
     * - spell_target_position：法术目标位置
     * - spell_threats：法术威胁
     * - spell_group_stack_rules：法术组堆叠规则
     * - spell_pet_auras：法术宠物光环
     */
    static bool HandleReloadAllSpellCommand(ChatHandler* handler, char const* /*args*/)
    {
        HandleReloadSkillDiscoveryTemplateCommand(handler, "a");
        HandleReloadSkillExtraItemTemplateCommand(handler, "a");
        HandleReloadSpellRequiredCommand(handler, "a");
        HandleReloadSpellAreaCommand(handler, "a");
        HandleReloadSpellGroupsCommand(handler, "a");
        HandleReloadSpellLearnSpellCommand(handler, "a");
        HandleReloadSpellLinkedSpellCommand(handler, "a");
        HandleReloadSpellProcsCommand(handler, "a");
        HandleReloadSpellBonusesCommand(handler, "a");
        HandleReloadSpellTargetPositionCommand(handler, "a");
        HandleReloadSpellThreatsCommand(handler, "a");
        HandleReloadSpellGroupStackRulesCommand(handler, "a");
        HandleReloadSpellPetAurasCommand(handler, "a");
        return true;
    }

    /**
     * @brief 处理重载所有对话数据命令
     * @param handler 聊天命令处理器
     * @param args 命令参数
     * @return 命令执行成功返回true
     *
     * 命令格式: .reload all gossips
     *
     * 功能：重载所有对话相关的数据表：
     * - gossip_menu：对话菜单
     * - gossip_menu_option：对话菜单选项
     * - points_of_interest：兴趣点（如果不是从all_scripts调用）
     *
     * @param args 参数不为'a'时才重载兴趣点，防止与其他批量命令重复
     */
    static bool HandleReloadAllGossipsCommand(ChatHandler* handler, char const* args)
    {
        HandleReloadGossipMenuCommand(handler, "a");
        HandleReloadGossipMenuOptionCommand(handler, "a");
        // 如果不是从all_scripts调用，才重载兴趣点
        if (*args != 'a')
        HandleReloadPointsOfInterestCommand(handler, "a");
        return true;
    }

    /**
     * @brief 处理重载所有物品数据命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（未使用）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reload all item
     *
     * 功能：重载所有物品相关的数据表：
     * - page_text：页面文本（物品说明等）
     * - item_enchantment_template：物品附魔模板
     */
    static bool HandleReloadAllItemCommand(ChatHandler* handler, char const* /*args*/)
    {
        HandleReloadPageTextsCommand(handler, "a");
        HandleReloadItemEnchantementsCommand(handler, "a");
        return true;
    }

    /**
     * @brief 处理重载所有语言包命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（未使用）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reload all locales
     *
     * 功能：重载所有本地化/语言包数据表：
     * - achievement_reward_locale：成就奖励本地化
     * - creature_template_locale：生物模板本地化
     * - creature_text_locale：生物文本本地化
     * - gameobject_template_locale：游戏物体模板本地化
     * - gossip_menu_option_locale：对话菜单选项本地化
     * - item_template_locale：物品模板本地化
     * - npc_text_locale：NPC文本本地化
     * - page_text_locale：页面文本本地化
     * - points_of_interest_locale：兴趣点本地化
     * - quest_template_locale：任务模板本地化
     * - quest_offer_reward_locale：任务奖励文本本地化
     * - quest_request_item_locale：任务需求物品文本本地化
     * - quest_greeting_locale：任务问候语本地化
     *
     * @note 重载后所有支持多语言的内容将更新为新数据
     */
    static bool HandleReloadAllLocalesCommand(ChatHandler* handler, char const* /*args*/)
    {
        HandleReloadLocalesAchievementRewardCommand(handler, "a");
        HandleReloadLocalesCreatureCommand(handler, "a");
        HandleReloadLocalesCreatureTextCommand(handler, "a");
        HandleReloadLocalesGameobjectCommand(handler, "a");
        HandleReloadLocalesGossipMenuOptionCommand(handler, "a");
        HandleReloadLocalesItemCommand(handler, "a");
        HandleReloadLocalesNpcTextCommand(handler, "a");
        HandleReloadLocalesPageTextCommand(handler, "a");
        HandleReloadLocalesPointsOfInterestCommand(handler, "a");
        HandleReloadLocalesQuestCommand(handler, "a");
        HandleReloadLocalesQuestOfferRewardCommand(handler, "a");
        HandleReloadLocalesQuestRequestItemsCommand(handler, "a");
        HandleReloadLocalesQuestGreetingCommand(handler, "");
        return true;
    }

    // ==================== 单表重载命令 ====================

    /**
     * @brief 处理重载配置文件命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（未使用）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reload config
     *
     * 功能：重新加载服务器配置文件（worldserver.conf）。
     * 包括所有运行时可修改的配置项。
     *
     * @note 某些配置项需要重启服务器才能生效
     * @note 同时会重新初始化视野距离信息
     */
    static bool HandleReloadConfigCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading config settings...");
        // 重新加载配置设置
        sWorld->LoadConfigSettings(true);
        // 重新初始化视野距离信息
        sMapMgr->InitializeVisibilityDistanceInfo();
        handler->SendGlobalGMSysMessage("World config settings reloaded.");
        return true;
    }

    /**
     * @brief 处理重载访问要求命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（未使用）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reload access_requirement
     *
     * 功能：重载副本/地图访问要求配置。
     * 包括进入特定地图所需的等级、物品、任务等条件。
     */
    static bool HandleReloadAccessRequirementCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Access Requirement definitions...");
        sObjectMgr->LoadAccessRequirements();
        handler->SendGlobalGMSysMessage("DB table `access_requirement` reloaded.");
        return true;
    }

    /**
     * @brief 处理重载成就条件数据命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（未使用）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reload achievement_criteria_data
     *
     * 功能：重载成就条件数据。
     * 包括成就完成所需的特定条件和进度追踪方式。
     */
    static bool HandleReloadAchievementCriteriaDataCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Additional Achievement Criteria Data...");
        sAchievementMgr->LoadAchievementCriteriaData();
        handler->SendGlobalGMSysMessage("DB table `achievement_criteria_data` reloaded.");
        return true;
    }

    /**
     * @brief 处理重载成就奖励命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（未使用）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reload achievement_reward
     *
     * 功能：重载成就奖励数据。
     * 包括完成成就后获得的物品、头衔、法术等奖励。
     */
    static bool HandleReloadAchievementRewardCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Achievement Reward Data...");
        sAchievementMgr->LoadRewards();
        handler->SendGlobalGMSysMessage("DB table `achievement_reward` reloaded.");
        return true;
    }

    /**
     * @brief 处理重载旅店区域触发点命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（未使用）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reload areatrigger_tavern
     *
     * 功能：重载旅店区域触发点数据。
     * 这些触发点定义了玩家可以休息和绑定炉石的位置。
     */
    static bool HandleReloadAreaTriggerTavernCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Tavern Area Triggers...");
        sObjectMgr->LoadTavernAreaTriggers();
        handler->SendGlobalGMSysMessage("DB table `areatrigger_tavern` reloaded.");
        return true;
    }

    /**
     * @brief 处理重载区域触发传送点命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（未使用）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reload areatrigger_teleport
     *
     * 功能：重载区域触发传送点数据。
     * 这些触发点定义了玩家进入特定区域时的传送目标位置。
     */
    static bool HandleReloadAreaTriggerTeleportCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Area Trigger Teleports definitions...");
        sObjectMgr->LoadAreaTriggerTeleports();
        handler->SendGlobalGMSysMessage("DB table `areatrigger_teleport` reloaded.");
        return true;
    }

    /**
     * @brief 处理重载自动广播命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（未使用）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reload autobroadcast
     *
     * 功能：重载自动广播消息配置。
     * 这些消息会按照配置的时间间隔自动向所有玩家广播。
     */
    static bool HandleReloadAutobroadcastCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Autobroadcasts...");
        sWorld->LoadAutobroadcasts();
        handler->SendGlobalGMSysMessage("DB table `autobroadcast` reloaded.");
        return true;
    }

    /**
     * @brief 处理重载战场模板命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（未使用）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reload battleground_template
     *
     * 功能：重载战场模板数据。
     * 包括战场的基本配置、队伍规模、等级范围等信息。
     */
    static bool HandleReloadBattlegroundTemplate(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Battleground Templates...");
        sBattlegroundMgr->LoadBattlegroundTemplates();
        handler->SendGlobalGMSysMessage("DB table `battleground_template` reloaded.");
        return true;
    }

    /**
     * @brief 处理重载广播文本命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（未使用）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reload broadcast_text
     *
     * 功能：重载广播文本数据及其本地化版本。
     * 包括游戏中使用的各种广播消息文本。
     */
    static bool HandleReloadBroadcastTextCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Broadcast texts...");
        sObjectMgr->LoadBroadcastTexts();
        sObjectMgr->LoadBroadcastTextLocales();
        handler->SendGlobalGMSysMessage("DB table `broadcast_text` reloaded.");
        return true;
    }

    /**
     * @brief 处理重载击杀声望命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（未使用）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reload creature_onkill_reputation
     *
     * 功能：重载生物击杀声望奖励数据。
     * 定义玩家击杀特定生物时获得的声望值。
     */
    static bool HandleReloadOnKillReputationCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading creature award reputation definitions...");
        sObjectMgr->LoadReputationOnKill();
        handler->SendGlobalGMSysMessage("DB table `creature_onkill_reputation` reloaded.");
        return true;
    }

    /**
     * @brief 处理重载生物召唤组命令
     * @param handler 聊天命令处理器
     * @return 命令执行成功返回true
     *
     * 命令格式: .reload creature_summon_groups
     *
     * 功能：重载生物召唤组数据。
     * 定义了特定条件下召唤的生物组信息。
     */
    static bool HandleReloadCreatureSummonGroupsCommand(ChatHandler* handler)
    {
        TC_LOG_INFO("misc", "Reloading creature summon groups...");
        sObjectMgr->LoadTempSummons();
        handler->SendGlobalGMSysMessage("DB table `creature_summon_groups` reloaded.");
        return true;
    }

    /**
     * @brief 处理重载生物模板命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（生物ID列表，空格分隔）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reload creature_template #entry1 [#entry2 ...]
     *
     * 功能：重载指定生物的模板数据。
     * 支持同时重载多个生物，每个生物ID用空格分隔。
     *
     * @note 此命令只会重载指定ID的生物，不会影响其他生物
     * @note 重载后会自动验证生物模板数据的有效性
     */
    static bool HandleReloadCreatureTemplateCommand(ChatHandler* handler, char const* args)
    {
        // 必须提供生物ID参数
        if (!*args)
            return false;

        // 遍历所有提供的生物ID
        for (std::string_view entryStr : Trinity::Tokenize(args, ' ', false))
        {
            uint32 entry = Trinity::StringTo<uint32>(entryStr).value_or(0);

            // 从数据库查询生物模板数据
            WorldDatabasePreparedStatement* stmt = WorldDatabase.GetPreparedStatement(WORLD_SEL_CREATURE_TEMPLATE);
            stmt->setUInt32(0, entry);
            PreparedQueryResult result = WorldDatabase.Query(stmt);

            if (!result)
            {
                // 生物模板在数据库中不存在
                handler->PSendSysMessage(LANG_COMMAND_CREATURETEMPLATE_NOTFOUND, entry);
                continue;
            }

            // 检查内存中是否存在该生物模板
            CreatureTemplate const* cInfo = sObjectMgr->GetCreatureTemplate(entry);
            if (!cInfo)
            {
                handler->PSendSysMessage(LANG_COMMAND_CREATURESTORAGE_NOTFOUND, entry);
                continue;
            }

            TC_LOG_INFO("misc", "Reloading creature template entry {}", entry);

            // 加载并验证生物模板
            Field* fields = result->Fetch();
            sObjectMgr->LoadCreatureTemplate(fields);
            sObjectMgr->CheckCreatureTemplate(cInfo);
        }

        // 初始化查询数据
        sObjectMgr->InitializeQueriesData(QUERY_DATA_CREATURES);
        handler->SendGlobalGMSysMessage("Creature template reloaded.");
        return true;
    }

    /**
     * @brief 处理重载生物任务起始者命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（未使用）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reload creature_queststarter
     *
     * 功能：重载生物与任务起始关系数据。
     * 定义哪些生物可以发放哪些任务。
     */
    static bool HandleReloadCreatureQuestStarterCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Loading Quests Relations... (`creature_queststarter`)");
        sObjectMgr->LoadCreatureQuestStarters();
        handler->SendGlobalGMSysMessage("DB table `creature_queststarter` reloaded.");
        return true;
    }

    /**
     * @brief 处理重载链接重生命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（未使用）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reload creature_linked_respawn
     *
     * 功能：重载生物链接重生数据。
     * 定义了生物之间的重生依赖关系，当主生物被击杀后，
     * 链接的从生物也会随之重生。
     */
    static bool HandleReloadLinkedRespawnCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Loading Linked Respawns... (`creature_linked_respawn`)");
        sObjectMgr->LoadLinkedRespawn();
        handler->SendGlobalGMSysMessage("DB table `creature_linked_respawn` (creature linked respawns) reloaded.");
        return true;
    }

    /**
     * @brief 处理重载生物任务结束者命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（未使用）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reload creature_questender
     *
     * 功能：重载生物与任务结束关系数据。
     * 定义哪些生物可以接收哪些任务的提交。
     */
    static bool HandleReloadCreatureQuestEnderCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Loading Quests Relations... (`creature_questender`)");
        sObjectMgr->LoadCreatureQuestEnders();
        handler->SendGlobalGMSysMessage("DB table `creature_questender` reloaded.");
        return true;
    }

    /**
     * @brief 处理重载对话菜单命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（未使用）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reload gossip_menu
     *
     * 功能：重载对话菜单数据。
     * 定义NPC与玩家交互时显示的对话菜单内容。
     *
     * @note 同时会重载条件数据，确保对话菜单条件正确应用
     */
    static bool HandleReloadGossipMenuCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading `gossip_menu` Table!");
        sObjectMgr->LoadGossipMenu();
        handler->SendGlobalGMSysMessage("DB table `gossip_menu` reloaded.");
        // 重载条件数据
        sConditionMgr->LoadConditions(true);
        return true;
    }

    /**
     * @brief 处理重载对话菜单选项命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（未使用）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reload gossip_menu_option
     *
     * 功能：重载对话菜单选项数据。
     * 定义对话菜单中每个选项的文本、图标和行为。
     *
     * @note 同时会重载条件数据，确保选项显示条件正确应用
     */
    static bool HandleReloadGossipMenuOptionCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading `gossip_menu_option` Table!");
        sObjectMgr->LoadGossipMenuItems();
        handler->SendGlobalGMSysMessage("DB table `gossip_menu_option` reloaded.");
        // 重载条件数据
        sConditionMgr->LoadConditions(true);
        return true;
    }

    /**
     * @brief 处理重载游戏物体任务起始者命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（未使用）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reload gameobject_queststarter
     *
     * 功能：重载游戏物体与任务起始关系数据。
     * 定义哪些游戏物体（如旗帜、任务板等）可以发放任务。
     */
    static bool HandleReloadGOQuestStarterCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Loading Quests Relations... (`gameobject_queststarter`)");
        sObjectMgr->LoadGameobjectQuestStarters();
        handler->SendGlobalGMSysMessage("DB table `gameobject_queststarter` reloaded.");
        return true;
    }

    /**
     * @brief 处理重载游戏物体任务结束者命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（未使用）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reload gameobject_questender
     *
     * 功能：重载游戏物体与任务结束关系数据。
     * 定义哪些游戏物体可以接收任务提交。
     */
    static bool HandleReloadGOQuestEnderCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Loading Quests Relations... (`gameobject_questender`)");
        sObjectMgr->LoadGameobjectQuestEnders();
        handler->SendGlobalGMSysMessage("DB table `gameobject_questender` reloaded.");
        return true;
    }

    /**
     * @brief 处理重载任务区域触发器命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（未使用）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reload areatrigger_involvedrelation
     *
     * 功能：重载任务区域触发器数据。
     * 定义玩家进入特定区域时触发任务进度的关系。
     */
    static bool HandleReloadQuestAreaTriggersCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Quest Area Triggers...");
        sObjectMgr->LoadQuestAreaTriggers();
        handler->SendGlobalGMSysMessage("DB table `areatrigger_involvedrelation` (quest area triggers) reloaded.");
        return true;
    }

    /**
     * @brief 处理重载任务问候语命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（未使用）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reload quest_greeting
     *
     * 功能：重载任务问候语数据。
     * 定义NPC在发放任务时显示的问候语。
     */
    static bool HandleReloadQuestGreetingCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Quest Greeting ...");
        sObjectMgr->LoadQuestGreetings();
        handler->SendGlobalGMSysMessage("DB table `quest_greeting` reloaded.");
        return true;
    }

    /**
     * @brief 处理重载任务问候语本地化命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（未使用）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reload quest_greeting_locale
     *
     * 功能：重载任务问候语的本地化数据。
     * 提供不同语言版本的任务问候语。
     */
    static bool HandleReloadLocalesQuestGreetingCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Quest Greeting locales...");
        sObjectMgr->LoadQuestGreetingLocales();
        handler->SendGlobalGMSysMessage("DB table `quest_greeting_locale` reloaded.");
        return true;
    }

    /**
     * @brief 处理重载任务模板命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（未使用）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reload quest_template
     *
     * 功能：重载任务模板数据。
     * 包括所有任务的基本信息、目标、奖励等。
     *
     * @note 同时会重载与任务相关的游戏物体数据
     */
    static bool HandleReloadQuestTemplateCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Quest Templates...");
        // 加载任务模板
        sObjectMgr->LoadQuests();
        // 初始化查询数据
        sObjectMgr->InitializeQueriesData(QUERY_DATA_QUESTS);
        handler->SendGlobalGMSysMessage("DB table `quest_template` (quest definitions) reloaded.");

        // 加载与任务相关的游戏物体
        TC_LOG_INFO("misc", "Re-Loading GameObjects for quests...");
        sObjectMgr->LoadGameObjectForQuests();
        handler->SendGlobalGMSysMessage("Data GameObjects for quests reloaded.");
        return true;
    }

    /**
     * @brief 处理重载生物战利品模板命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（未使用）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reload creature_loot_template
     *
     * 功能：重载生物战利品掉落表。
     * 定义生物被击杀后可能掉落的物品。
     *
     * @note 同时会重载条件数据，确保掉落条件正确应用
     */
    static bool HandleReloadLootTemplatesCreatureCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Loot Tables... (`creature_loot_template`)");
        // 加载生物战利品模板
        LoadLootTemplates_Creature();
        // 检查战利品引用有效性
        LootTemplates_Creature.CheckLootRefs();
        handler->SendGlobalGMSysMessage("DB table `creature_loot_template` reloaded.");
        // 重载条件数据
        sConditionMgr->LoadConditions(true);
        return true;
    }

    /**
     * @brief 处理重载生物移动覆盖命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（未使用）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reload creature_movement_override
     *
     * 功能：重载生物移动覆盖配置。
     * 定义特定生物的特殊移动参数覆盖。
     */
    static bool HandleReloadCreatureMovementOverrideCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Creature movement overrides...");
        sObjectMgr->LoadCreatureMovementOverrides();
        handler->SendGlobalGMSysMessage("DB table `creature_movement_override` reloaded.");
        return true;
    }

    /**
     * @brief 处理重载分解战利品模板命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（未使用）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reload disenchant_loot_template
     *
     * 功能：重载附魔分解战利品掉落表。
     * 定义物品被分解时可能产生的材料。
     */
    static bool HandleReloadLootTemplatesDisenchantCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Loot Tables... (`disenchant_loot_template`)");
        LoadLootTemplates_Disenchant();
        LootTemplates_Disenchant.CheckLootRefs();
        handler->SendGlobalGMSysMessage("DB table `disenchant_loot_template` reloaded.");
        sConditionMgr->LoadConditions(true);
        return true;
    }

    /**
     * @brief 处理重载钓鱼战利品模板命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（未使用）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reload fishing_loot_template
     *
     * 功能：重载钓鱼战利品掉落表。
     * 定义在不同区域钓鱼可能获得的物品。
     */
    static bool HandleReloadLootTemplatesFishingCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Loot Tables... (`fishing_loot_template`)");
        LoadLootTemplates_Fishing();
        LootTemplates_Fishing.CheckLootRefs();
        handler->SendGlobalGMSysMessage("DB table `fishing_loot_template` reloaded.");
        sConditionMgr->LoadConditions(true);
        return true;
    }

    /**
     * @brief 处理重载游戏物体战利品模板命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（未使用）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reload gameobject_loot_template
     *
     * 功能：重载游戏物体（如宝箱、矿石等）战利品掉落表。
     */
    static bool HandleReloadLootTemplatesGameobjectCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Loot Tables... (`gameobject_loot_template`)");
        LoadLootTemplates_Gameobject();
        LootTemplates_Gameobject.CheckLootRefs();
        handler->SendGlobalGMSysMessage("DB table `gameobject_loot_template` reloaded.");
        sConditionMgr->LoadConditions(true);
        return true;
    }

    static bool HandleReloadLootTemplatesItemCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Loot Tables... (`item_loot_template`)");
        LoadLootTemplates_Item();
        LootTemplates_Item.CheckLootRefs();
        handler->SendGlobalGMSysMessage("DB table `item_loot_template` reloaded.");
        sConditionMgr->LoadConditions(true);
        return true;
    }

    static bool HandleReloadLootTemplatesMillingCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Loot Tables... (`milling_loot_template`)");
        LoadLootTemplates_Milling();
        LootTemplates_Milling.CheckLootRefs();
        handler->SendGlobalGMSysMessage("DB table `milling_loot_template` reloaded.");
        sConditionMgr->LoadConditions(true);
        return true;
    }

    static bool HandleReloadLootTemplatesPickpocketingCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Loot Tables... (`pickpocketing_loot_template`)");
        LoadLootTemplates_Pickpocketing();
        LootTemplates_Pickpocketing.CheckLootRefs();
        handler->SendGlobalGMSysMessage("DB table `pickpocketing_loot_template` reloaded.");
        sConditionMgr->LoadConditions(true);
        return true;
    }

    static bool HandleReloadLootTemplatesProspectingCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Loot Tables... (`prospecting_loot_template`)");
        LoadLootTemplates_Prospecting();
        LootTemplates_Prospecting.CheckLootRefs();
        handler->SendGlobalGMSysMessage("DB table `prospecting_loot_template` reloaded.");
        sConditionMgr->LoadConditions(true);
        return true;
    }

    static bool HandleReloadLootTemplatesMailCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Loot Tables... (`mail_loot_template`)");
        LoadLootTemplates_Mail();
        LootTemplates_Mail.CheckLootRefs();
        handler->SendGlobalGMSysMessage("DB table `mail_loot_template` reloaded.");
        sConditionMgr->LoadConditions(true);
        return true;
    }

    static bool HandleReloadLootTemplatesReferenceCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Loot Tables... (`reference_loot_template`)");
        LoadLootTemplates_Reference();
        handler->SendGlobalGMSysMessage("DB table `reference_loot_template` reloaded.");
        sConditionMgr->LoadConditions(true);
        return true;
    }

    static bool HandleReloadLootTemplatesSkinningCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Loot Tables... (`skinning_loot_template`)");
        LoadLootTemplates_Skinning();
        LootTemplates_Skinning.CheckLootRefs();
        handler->SendGlobalGMSysMessage("DB table `skinning_loot_template` reloaded.");
        sConditionMgr->LoadConditions(true);
        return true;
    }

    static bool HandleReloadLootTemplatesSpellCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Loot Tables... (`spell_loot_template`)");
        LoadLootTemplates_Spell();
        LootTemplates_Spell.CheckLootRefs();
        handler->SendGlobalGMSysMessage("DB table `spell_loot_template` reloaded.");
        sConditionMgr->LoadConditions(true);
        return true;
    }

    static bool HandleReloadTrinityStringCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading trinity_string Table!");
        sObjectMgr->LoadTrinityStrings();
        handler->SendGlobalGMSysMessage("DB table `trinity_string` reloaded.");
        return true;
    }

    static bool HandleReloadTrainerCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading `trainer` Table!");
        sObjectMgr->LoadTrainers();
        sObjectMgr->LoadCreatureDefaultTrainers();
        handler->SendGlobalGMSysMessage("DB table `trainer` reloaded.");
        handler->SendGlobalGMSysMessage("DB table `trainer_locale` reloaded.");
        handler->SendGlobalGMSysMessage("DB table `trainer_spell` reloaded.");
        handler->SendGlobalGMSysMessage("DB table `creature_default_trainer` reloaded.");
        return true;
    }

    static bool HandleReloadNpcVendorCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading `npc_vendor` Table!");
        sObjectMgr->LoadVendors();
        handler->SendGlobalGMSysMessage("DB table `npc_vendor` reloaded.");
        return true;
    }

    static bool HandleReloadPointsOfInterestCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading `points_of_interest` Table!");
        sObjectMgr->LoadPointsOfInterest();
        handler->SendGlobalGMSysMessage("DB table `points_of_interest` reloaded.");
        return true;
    }

    static bool HandleReloadQuestPOICommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Quest POI ..." );
        sObjectMgr->LoadQuestPOI();
        sObjectMgr->InitializeQueriesData(QUERY_DATA_POIS);
        handler->SendGlobalGMSysMessage("DB Table `quest_poi` and `quest_poi_points` reloaded.");
        return true;
    }

    static bool HandleReloadSpellClickSpellsCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading `npc_spellclick_spells` Table!");
        sObjectMgr->LoadNPCSpellClickSpells();
        handler->SendGlobalGMSysMessage("DB table `npc_spellclick_spells` reloaded.");
        return true;
    }

    static bool HandleReloadReservedNameCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Loading ReservedNames... (`reserved_name`)");
        sObjectMgr->LoadReservedPlayersNames();
        handler->SendGlobalGMSysMessage("DB table `reserved_name` (player reserved names) reloaded.");
        return true;
    }

    static bool HandleReloadReputationRewardRateCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading `reputation_reward_rate` Table!" );
        sObjectMgr->LoadReputationRewardRate();
        handler->SendGlobalSysMessage("DB table `reputation_reward_rate` reloaded.");
        return true;
    }

    static bool HandleReloadReputationSpilloverTemplateCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading `reputation_spillover_template` Table!" );
        sObjectMgr->LoadReputationSpilloverTemplate();
        handler->SendGlobalSysMessage("DB table `reputation_spillover_template` reloaded.");
        return true;
    }

    static bool HandleReloadSkillDiscoveryTemplateCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Skill Discovery Table...");
        LoadSkillDiscoveryTable();
        handler->SendGlobalGMSysMessage("DB table `skill_discovery_template` (recipes discovered at crafting) reloaded.");
        return true;
    }

    static bool HandleReloadSkillPerfectItemTemplateCommand(ChatHandler* handler, char const* /*args*/)
    { // latched onto HandleReloadSkillExtraItemTemplateCommand as it's part of that table group (and i don't want to chance all the command IDs)
        TC_LOG_INFO("misc", "Re-Loading Skill Perfection Data Table...");
        LoadSkillPerfectItemTable();
        handler->SendGlobalGMSysMessage("DB table `skill_perfect_item_template` (perfect item procs when crafting) reloaded.");
        return true;
    }

    static bool HandleReloadSkillExtraItemTemplateCommand(ChatHandler* handler, char const* args)
    {
        TC_LOG_INFO("misc", "Re-Loading Skill Extra Item Table...");
        LoadSkillExtraItemTable();
        handler->SendGlobalGMSysMessage("DB table `skill_extra_item_template` (extra item creation when crafting) reloaded.");

        return HandleReloadSkillPerfectItemTemplateCommand(handler, args);
    }

    static bool HandleReloadSkillFishingBaseLevelCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Skill Fishing base level requirements...");
        sObjectMgr->LoadFishingBaseSkillLevel();
        handler->SendGlobalGMSysMessage("DB table `skill_fishing_base_level` (fishing base level for zone/subzone) reloaded.");
        return true;
    }

    static bool HandleReloadSpellAreaCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading SpellArea Data...");
        sSpellMgr->LoadSpellAreas();
        handler->SendGlobalGMSysMessage("DB table `spell_area` (spell dependences from area/quest/auras state) reloaded.");
        return true;
    }

    static bool HandleReloadSpellRequiredCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Spell Required Data... ");
        sSpellMgr->LoadSpellRequired();
        handler->SendGlobalGMSysMessage("DB table `spell_required` reloaded.");
        return true;
    }

    static bool HandleReloadSpellGroupsCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Spell Groups...");
        sSpellMgr->LoadSpellGroups();
        handler->SendGlobalGMSysMessage("DB table `spell_group` (spell groups) reloaded.");
        return true;
    }

    static bool HandleReloadSpellLearnSpellCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Spell Learn Spells...");
        sSpellMgr->LoadSpellLearnSpells();
        handler->SendGlobalGMSysMessage("DB table `spell_learn_spell` reloaded.");
        return true;
    }

    static bool HandleReloadSpellLinkedSpellCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Spell Linked Spells...");
        sSpellMgr->LoadSpellLinked();
        handler->SendGlobalGMSysMessage("DB table `spell_linked_spell` reloaded.");
        return true;
    }

    static bool HandleReloadSpellProcsCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Spell Proc conditions and data...");
        sSpellMgr->LoadSpellProcs();
        handler->SendGlobalGMSysMessage("DB table `spell_proc` (spell proc conditions and data) reloaded.");
        return true;
    }

    static bool HandleReloadSpellBonusesCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Spell Bonus Data...");
        sSpellMgr->LoadSpellBonuses();
        handler->SendGlobalGMSysMessage("DB table `spell_bonus_data` (spell damage/healing coefficients) reloaded.");
        return true;
    }

    static bool HandleReloadSpellTargetPositionCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Spell target coordinates...");
        sSpellMgr->LoadSpellTargetPositions();
        handler->SendGlobalGMSysMessage("DB table `spell_target_position` (destination coordinates for spell targets) reloaded.");
        return true;
    }

    static bool HandleReloadSpellThreatsCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Aggro Spells Definitions...");
        sSpellMgr->LoadSpellThreats();
        handler->SendGlobalGMSysMessage("DB table `spell_threat` (spell aggro definitions) reloaded.");
        return true;
    }

    static bool HandleReloadSpellGroupStackRulesCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Spell Group Stack Rules...");
        sSpellMgr->LoadSpellGroupStackRules();
        handler->SendGlobalGMSysMessage("DB table `spell_group_stack_rules` (spell stacking definitions) reloaded.");
        return true;
    }

    static bool HandleReloadSpellPetAurasCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Spell pet auras...");
        sSpellMgr->LoadSpellPetAuras();
        handler->SendGlobalGMSysMessage("DB table `spell_pet_auras` reloaded.");
        return true;
    }

    static bool HandleReloadPageTextsCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Page Text...");
        sObjectMgr->LoadPageTexts();
        handler->SendGlobalGMSysMessage("DB table `page_text` reloaded.");
        return true;
    }

    static bool HandleReloadItemEnchantementsCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Item Random Enchantments Table...");
        LoadRandomEnchantmentsTable();
        handler->SendGlobalGMSysMessage("DB table `item_enchantment_template` reloaded.");
        return true;
    }

    static bool HandleReloadItemSetNamesCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Item set names...");
        sObjectMgr->LoadItemSetNames();
        handler->SendGlobalGMSysMessage("DB table `item_set_names` reloaded.");
        return true;
    }

    /**
     * @brief 处理重载事件脚本命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（'a'表示批量调用，不输出消息）
     * @return 命令执行成功返回true，脚本正在执行中返回false
     *
     * 命令格式: .reload event_scripts
     *
     * 功能：重载事件脚本数据。
     * 定义游戏中各种事件触发时执行的脚本动作。
     *
     * @warning 如果有脚本正在执行中，命令将失败
     */
    static bool HandleReloadEventScriptsCommand(ChatHandler* handler, char const* args)
    {
        if (sMapMgr->IsScriptScheduled())
        {
            handler->SendSysMessage("DB scripts used currently, please attempt reload later.");
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (*args != 'a')
            TC_LOG_INFO("misc", "Re-Loading Scripts from `event_scripts`...");

        sObjectMgr->LoadEventScripts();

        if (*args != 'a')
            handler->SendGlobalGMSysMessage("DB table `event_scripts` reloaded.");

        return true;
    }

    /**
     * @brief 处理重载路径点脚本命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（'a'表示批量调用，不输出消息）
     * @return 命令执行成功返回true，脚本正在执行中返回false
     *
     * 命令格式: .reload waypoint_scripts
     *
     * 功能：重载路径点脚本数据。
     * 定义生物在路径点移动时执行的脚本动作。
     */
    static bool HandleReloadWpScriptsCommand(ChatHandler* handler, char const* args)
    {
        if (sMapMgr->IsScriptScheduled())
        {
            handler->SendSysMessage("DB scripts used currently, please attempt reload later.");
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (*args != 'a')
            TC_LOG_INFO("misc", "Re-Loading Scripts from `waypoint_scripts`...");

        sObjectMgr->LoadWaypointScripts();

        if (*args != 'a')
            handler->SendGlobalGMSysMessage("DB table `waypoint_scripts` reloaded.");

        return true;
    }

    /**
     * @brief 处理重载路径点数据命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（'a'表示批量调用，不输出消息）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reload waypoint_data
     *
     * 功能：重载路径点数据。
     * 定义生物巡逻移动的路径点坐标和移动参数。
     */
    static bool HandleReloadWpCommand(ChatHandler* handler, char const* args)
    {
        if (*args != 'a')
            TC_LOG_INFO("misc", "Re-Loading Waypoints data from 'waypoints_data'");

        sWaypointMgr->Load();

        if (*args != 'a')
            handler->SendGlobalGMSysMessage("DB Table 'waypoint_data' reloaded.");

        return true;
    }

    /**
     * @brief 处理重载法术脚本命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（'a'表示批量调用，不输出消息）
     * @return 命令执行成功返回true，脚本正在执行中返回false
     *
     * 命令格式: .reload spell_scripts
     *
     * 功能：重载法术脚本数据。
     * 定义法术施放时执行的脚本动作。
     */
    static bool HandleReloadSpellScriptsCommand(ChatHandler* handler, char const* args)
    {
        if (sMapMgr->IsScriptScheduled())
        {
            handler->SendSysMessage("DB scripts used currently, please attempt reload later.");
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (*args != 'a')
            TC_LOG_INFO("misc", "Re-Loading Scripts from `spell_scripts`...");

        sObjectMgr->LoadSpellScripts();

        if (*args != 'a')
            handler->SendGlobalGMSysMessage("DB table `spell_scripts` reloaded.");

        return true;
    }

    static bool HandleReloadGameGraveyardZoneCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Graveyard-zone links...");

        sObjectMgr->LoadGraveyardZones();

        handler->SendGlobalGMSysMessage("DB table `game_graveyard_zone` reloaded.");

        return true;
    }

    static bool HandleReloadGameTeleCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Game Tele coordinates...");

        sObjectMgr->LoadGameTele();

        handler->SendGlobalGMSysMessage("DB table `game_tele` reloaded.");

        return true;
    }

    static bool HandleReloadDisablesCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading disables table...");
        DisableMgr::LoadDisables();
        TC_LOG_INFO("misc", "Checking quest disables...");
        DisableMgr::CheckQuestDisables();
        handler->SendGlobalGMSysMessage("DB table `disables` reloaded.");
        return true;
    }

    static bool HandleReloadLocalesAchievementRewardCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Achievement Reward Data Locale...");
        sAchievementMgr->LoadRewardLocales();
        handler->SendGlobalGMSysMessage("DB table `achievement_reward_locale` reloaded.");
        return true;
    }

    static bool HandleReloadLfgRewardsCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading lfg dungeon rewards...");
        sLFGMgr->LoadRewards();
        handler->SendGlobalGMSysMessage("DB table `lfg_dungeon_rewards` reloaded.");
        return true;
    }

    static bool HandleReloadLocalesCreatureCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Creature Template Locale...");
        sObjectMgr->LoadCreatureLocales();
        handler->SendGlobalGMSysMessage("DB table `creature_template_locale` reloaded.");
        return true;
    }

    static bool HandleReloadLocalesCreatureTextCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Creature Texts Locale...");
        sCreatureTextMgr->LoadCreatureTextLocales();
        handler->SendGlobalGMSysMessage("DB table `creature_text_locale` reloaded.");
        return true;
    }

    static bool HandleReloadLocalesGameobjectCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Gameobject Template Locale... ");
        sObjectMgr->LoadGameObjectLocales();
        handler->SendGlobalGMSysMessage("DB table `gameobject_template_locale` reloaded.");
        return true;
    }

    static bool HandleReloadLocalesGossipMenuOptionCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Gossip Menu Option Locale... ");
        sObjectMgr->LoadGossipMenuItemsLocales();
        handler->SendGlobalGMSysMessage("DB table `gossip_menu_option_locale` reloaded.");
        return true;
    }

    static bool HandleReloadLocalesItemCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Item Template Locale... ");
        sObjectMgr->LoadItemLocales();
        handler->SendGlobalGMSysMessage("DB table `item_template_locale` reloaded.");
        return true;
    }

    static bool HandleReloadLocalesItemSetNameCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Item set name Locale... ");
        sObjectMgr->LoadItemSetNameLocales();
        handler->SendGlobalGMSysMessage("DB table `item_set_name_locale` reloaded.");
        return true;
    }

    static bool HandleReloadLocalesNpcTextCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading NPC Text Locale... ");
        sObjectMgr->LoadNpcTextLocales();
        handler->SendGlobalGMSysMessage("DB table `npc_text_locale` reloaded.");
        return true;
    }

    static bool HandleReloadLocalesPageTextCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Page Text Locale... ");
        sObjectMgr->LoadPageTextLocales();
        handler->SendGlobalGMSysMessage("DB table `page_text_locale` reloaded.");
        return true;
    }

    static bool HandleReloadLocalesPointsOfInterestCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Points Of Interest Locale... ");
        sObjectMgr->LoadPointOfInterestLocales();
        handler->SendGlobalGMSysMessage("DB table `points_of_interest_locale` reloaded.");
        return true;
    }

    static bool HandleReloadLocalesQuestCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Quest Template Locale... ");
        sObjectMgr->LoadQuestLocales();
        handler->SendGlobalGMSysMessage("DB table `quest_template_locale` reloaded.");
        return true;
    }

    static bool HandleReloadLocalesQuestOfferRewardCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Quest Offer Reward Locale... ");
        sObjectMgr->LoadQuestOfferRewardLocale();
        handler->SendGlobalGMSysMessage("DB table `quest_offer_reward_locale` reloaded.");
        return true;
    }

    static bool HandleReloadLocalesQuestRequestItemsCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Quest Request Item Locale... ");
        sObjectMgr->LoadQuestRequestItemsLocale();
        handler->SendGlobalGMSysMessage("DB table `quest_request_item_locale` reloaded.");
        return true;
    }

    static bool HandleReloadMailLevelRewardCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Player level dependent mail rewards...");
        sObjectMgr->LoadMailLevelRewards();
        handler->SendGlobalGMSysMessage("DB table `mail_level_reward` reloaded.");
        return true;
    }

    /**
     * @brief 处理重载拍卖行命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（未使用）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reload auctions
     *
     * 功能：重新加载拍卖行数据。
     * 包括所有拍卖物品和拍卖信息。
     *
     * @note 这会重新加载所有动态拍卖数据，可能耗时较长
     */
    static bool HandleReloadAuctionsCommand(ChatHandler* handler, char const* /*args*/)
    {
        ///- Reload dynamic data tables from the database
        TC_LOG_INFO("misc", "Re-Loading Auctions...");
        sAuctionMgr->LoadAuctionItems();
        sAuctionMgr->LoadAuctions();
        handler->SendGlobalGMSysMessage("Auctions reloaded.");
        return true;
    }

    /**
     * @brief 处理重载条件命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（未使用）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reload conditions
     *
     * 功能：重载条件数据。
     * 条件系统用于控制游戏中的各种触发条件，
     * 如任务完成条件、物品使用条件、对话显示条件等。
     */
    static bool HandleReloadConditions(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Conditions...");
        sConditionMgr->LoadConditions(true);
        handler->SendGlobalGMSysMessage("Conditions reloaded.");
        return true;
    }

    /**
     * @brief 处理重载生物文本命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（未使用）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reload creature_text
     *
     * 功能：重载生物文本数据。
     * 定义生物在特定情况下说的话（ yell, say, emote等）。
     */
    static bool HandleReloadCreatureText(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Creature Texts...");
        sCreatureTextMgr->LoadCreatureTexts();
        handler->SendGlobalGMSysMessage("Creature Texts reloaded.");
        return true;
    }

    /**
     * @brief 处理重载Smart脚本命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（未使用）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reload smart_scripts
     *
     * 功能：重载SmartAI脚本数据。
     * SmartAI是一种灵活的AI脚本系统，通过数据库配置实现复杂的AI行为。
     */
    static bool HandleReloadSmartScripts(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Re-Loading Smart Scripts...");
        sSmartScriptMgr->LoadSmartAIFromDB();
        handler->SendGlobalGMSysMessage("Smart Scripts reloaded.");
        return true;
    }

    static bool HandleReloadVehicleTemplateCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Reloading vehicle_template table...");
        sObjectMgr->LoadVehicleTemplate();
        handler->SendGlobalGMSysMessage("Vehicle templates reloaded.");
        return true;
    }

    static bool HandleReloadVehicleAccessoryCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Reloading vehicle_accessory table...");
        sObjectMgr->LoadVehicleAccessories();
        handler->SendGlobalGMSysMessage("Vehicle accessories reloaded.");
        return true;
    }

    static bool HandleReloadVehicleTemplateAccessoryCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Reloading vehicle_template_accessory table...");
        sObjectMgr->LoadVehicleTemplateAccessories();
        handler->SendGlobalGMSysMessage("Vehicle template accessories reloaded.");
        return true;
    }

    /**
     * @brief 处理重载RBAC命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（未使用）
     * @return 命令执行成功返回true
     *
     * 命令格式: .reload rbac
     *
     * 功能：重载基于角色的访问控制(RBAC)数据。
     * 包括权限定义、角色权限关联等。
     * 重载后会更新所有在线玩家的RBAC数据。
     */
    static bool HandleReloadRBACCommand(ChatHandler* handler, char const* /*args*/)
    {
        TC_LOG_INFO("misc", "Reloading RBAC tables...");
        // 加载RBAC数据
        sAccountMgr->LoadRBAC();
        // 重新加载世界的RBAC数据
        sWorld->ReloadRBAC();
        handler->SendGlobalGMSysMessage("RBAC data reloaded.");
        return true;
    }
};

/**
 * @brief 注册重载命令脚本
 *
 * 此函数在脚本系统初始化时被调用，
 * 创建reload_commandscript实例并注册到命令脚本管理器中。
 */
void AddSC_reload_commandscript()
{
    new reload_commandscript();
}
