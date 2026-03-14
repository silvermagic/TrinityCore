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

/* ScriptData
Name: quest_commandscript
%Complete: 100
Comment: All quest related commands
Category: commandscripts
EndScriptData */

/**
 * @file cs_quest.cpp
 * @brief 任务管理命令模块
 *
 * 本模块提供了一系列GM命令，用于管理玩家的任务状态。
 * 主要功能包括：
 * - 添加任务：将指定任务添加到玩家的任务日志
 * - 完成任务：将指定任务标记为完成状态
 * - 移除任务：从玩家的任务日志中删除指定任务
 * - 奖励任务：给予玩家任务奖励并完成任务
 *
 * 这些命令主要用于游戏测试、任务调试和玩家支持。
 * 所有命令都需要相应的RBAC权限才能执行。
 */

#include "ScriptMgr.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "DisableMgr.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "RBAC.h"
#include "ReputationMgr.h"
#include "World.h"

#if TRINITY_COMPILER == TRINITY_COMPILER_GNU
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

/**
 * @class quest_commandscript
 * @brief 任务命令脚本类
 *
 * 该类继承自CommandScript，提供所有与任务管理相关的GM命令。
 * 命令包括：添加任务、完成任务、移除任务、给予任务奖励。
 * 这些命令对于测试任务流程、调试任务问题和处理玩家任务卡住情况非常有用。
 */
class quest_commandscript : public CommandScript
{
public:
    /**
     * @brief 构造函数
     *
     * 初始化任务命令脚本，注册命令名称为"quest_commandscript"
     */
    quest_commandscript() : CommandScript("quest_commandscript") { }

    /**
     * @brief 获取所有任务命令的命令表
     * @return 返回命令表向量，包含所有任务相关命令的定义
     *
     * 该函数注册了以下任务命令：
     * - quest add: 添加任务到玩家的任务日志
     * - quest complete: 将任务标记为完成状态
     * - quest remove: 从任务日志中移除任务
     * - quest reward: 给予任务奖励并完成任务
     *
     * 所有命令都需要相应的RBAC权限
     */
    std::vector<ChatCommand> GetCommands() const override
    {
        // 任务命令子表
        static std::vector<ChatCommand> questCommandTable =
        {
            { "add",      rbac::RBAC_PERM_COMMAND_QUEST_ADD,      false, &HandleQuestAdd,      "" },
            { "complete", rbac::RBAC_PERM_COMMAND_QUEST_COMPLETE, false, &HandleQuestComplete, "" },
            { "remove",   rbac::RBAC_PERM_COMMAND_QUEST_REMOVE,   false, &HandleQuestRemove,   "" },
            { "reward",   rbac::RBAC_PERM_COMMAND_QUEST_REWARD,   false, &HandleQuestReward,   "" },
        };
        static std::vector<ChatCommand> commandTable =
        {
            { "quest", rbac::RBAC_PERM_COMMAND_QUEST,  false, nullptr, "", questCommandTable },
        };
        return commandTable;
    }

    /**
     * @brief 添加任务命令处理函数
     * @param handler 聊天处理器指针
     * @param quest 任务对象指针（从命令参数解析）
     * @return 命令执行成功返回true，失败返回false
     *
     * .quest add <任务ID> - 将指定任务添加到选中玩家或自己的任务日志
     *
     * 执行流程：
     * 1. 获取目标玩家（选中的玩家或自己）
     * 2. 检查任务是否被禁用
     * 3. 检查是否是物品触发的任务（这类任务需要物品才能正常工作）
     * 4. 检查玩家是否已有该任务
     * 5. 验证玩家是否可以接受该任务
     * 6. 添加任务并检查是否自动完成
     *
     * @note 物品触发的任务不能通过此命令添加，因为缺少启动物品
     *       任务的接受条件仍会被检查（前置任务、等级要求等）
     */
    static bool HandleQuestAdd(ChatHandler* handler, Quest const* quest)
    {
        Player* player = handler->getSelectedPlayerOrSelf();
        if (!player)
        {
            handler->SendSysMessage(LANG_NO_CHAR_SELECTED);
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (DisableMgr::IsDisabledFor(DISABLE_TYPE_QUEST, quest->GetQuestId(), nullptr))
        {
            handler->PSendSysMessage(LANG_COMMAND_QUEST_NOTFOUND, quest->GetQuestId());
            handler->SetSentErrorMessage(true);
            return false;
        }

        // check item starting quest (it can work incorrectly if added without item in inventory)
        ItemTemplateContainer const& itc = sObjectMgr->GetItemTemplateStore();
        auto itr = std::find_if(std::begin(itc), std::end(itc), [quest](ItemTemplateContainer::value_type const& value)
        {
            return value.second.StartQuest == quest->GetQuestId();
        });

        if (itr != std::end(itc))
        {
            handler->PSendSysMessage(LANG_COMMAND_QUEST_STARTFROMITEM, quest->GetQuestId(), itr->first);
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (player->IsActiveQuest(quest->GetQuestId()))
            return false;

        // ok, normal (creature/GO starting) quest
        if (player->CanAddQuest(quest, true))
            player->AddQuestAndCheckCompletion(quest, nullptr);

        return true;
    }

    /**
     * @brief 移除任务命令处理函数
     * @param handler 聊天处理器指针
     * @param quest 任务对象指针
     * @return 命令执行成功返回true，失败返回false
     *
     * .quest remove <任务ID> - 从选中玩家的任务日志中移除指定任务
     *
     * 执行流程：
     * 1. 获取目标玩家（必须是选中的玩家）
     * 2. 验证任务是否存在
     * 3. 检查玩家是否接了该任务
     * 4. 从任务日志的所有槽位中清除该任务
     * 5. 移除任务源物品（但保留已装备的任务物品）
     * 6. 如果任务有PvP标志，更新玩家的PvP状态
     * 7. 从活跃任务和已完成任务列表中移除
     * 8. 触发任务状态改变脚本事件
     *
     * @note 这会完全移除任务，包括任务历史记录
     *       玩家可以重新接受该任务
     */
    static bool HandleQuestRemove(ChatHandler* handler, Quest const* quest)
    {
        Player* player = handler->getSelectedPlayer();
        if (!player)
        {
            handler->SendSysMessage(LANG_NO_CHAR_SELECTED);
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (!quest)
        {
            handler->PSendSysMessage(LANG_COMMAND_QUEST_NOTFOUND, quest->GetQuestId());
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (player->GetQuestStatus(quest->GetQuestId()) != QUEST_STATUS_NONE)
        {
            // remove all quest entries for 'entry' from quest log
            for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
            {
                uint32 logQuest = player->GetQuestSlotQuestId(slot);
                if (logQuest == quest->GetQuestId())
                {
                    player->SetQuestSlot(slot, 0);

                    // we ignore unequippable quest items in this case, its' still be equipped
                    player->TakeQuestSourceItem(logQuest, false);

                    if (quest->HasFlag(QUEST_FLAGS_FLAGS_PVP))
                    {
                        player->pvpInfo.IsHostile = player->pvpInfo.IsInHostileArea || player->HasPvPForcingQuest();
                        player->UpdatePvPState();
                    }
                }
            }
            player->RemoveActiveQuest(quest->GetQuestId(), false);
            player->RemoveRewardedQuest(quest->GetQuestId());

            sScriptMgr->OnQuestStatusChange(player, quest->GetQuestId());

            handler->SendSysMessage(LANG_COMMAND_QUEST_REMOVED);
            return true;
        }
        else
        {
            handler->PSendSysMessage(LANG_COMMAND_QUEST_NOTFOUND, quest->GetQuestId());
            handler->SetSentErrorMessage(true);
            return false;
        }
    }

    /**
     * @brief 完成任务命令处理函数
     * @param handler 聊天处理器指针
     * @param quest 任务对象指针
     * @return 命令执行成功返回true，失败返回false
     *
     * .quest complete <任务ID> - 完成选中玩家或自己的指定任务
     *
     * 该命令会自动满足任务的所有要求：
     * 1. 添加任务所需的物品到玩家背包
     * 2. 模拟击杀所需生物/游戏对象
     * 3. 模拟击杀所需玩家（如果是PvP任务）
     * 4. 设置所需的声望值
     * 5. 添加所需的金币（如果任务需要金币才能完成）
     * 6. 记录GM完成任务到任务追踪系统（如果启用）
     * 7. 标记任务为完成状态
     *
     * 执行流程：
     * - 验证玩家是否接了该任务
     * - 自动填充所有任务目标
     * - 更新任务追踪数据库（用于统计分析）
     * - 将任务标记为完成（但不领取奖励）
     *
     * @note 使用此命令完成任务不会获得经验值奖励
     *       需要使用.quest reward命令领取奖励
     */
    static bool HandleQuestComplete(ChatHandler* handler, Quest const* quest)
    {
        Player* player = handler->getSelectedPlayerOrSelf();
        if (!player)
        {
            handler->SendSysMessage(LANG_NO_CHAR_SELECTED);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // If player doesn't have the quest
        if (player->GetQuestStatus(quest->GetQuestId()) == QUEST_STATUS_NONE
            || DisableMgr::IsDisabledFor(DISABLE_TYPE_QUEST, quest->GetQuestId(), nullptr))
        {
            handler->PSendSysMessage(LANG_COMMAND_QUEST_NOTFOUND, quest->GetQuestId());
            handler->SetSentErrorMessage(true);
            return false;
        }

        // Add quest items for quests that require items
        for (uint8 x = 0; x < QUEST_ITEM_OBJECTIVES_COUNT; ++x)
        {
            uint32 id = quest->RequiredItemId[x];
            uint32 count = quest->RequiredItemCount[x];
            if (!id || !count)
                continue;

            uint32 curItemCount = player->GetItemCount(id, true);

            ItemPosCountVec dest;
            uint8 msg = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, id, count-curItemCount);
            if (msg == EQUIP_ERR_OK)
            {
                Item* item = player->StoreNewItem(dest, id, true);
                player->SendNewItem(item, count-curItemCount, true, false);
            }
        }

        // All creature/GO slain/cast (not required, but otherwise it will display "Creature slain 0/10")
        for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
        {
            int32 creature = quest->RequiredNpcOrGo[i];
            uint32 creatureCount = quest->RequiredNpcOrGoCount[i];

            if (creature > 0)
            {
                if (CreatureTemplate const* creatureInfo = sObjectMgr->GetCreatureTemplate(creature))
                    for (uint16 z = 0; z < creatureCount; ++z)
                        player->KilledMonster(creatureInfo, ObjectGuid::Empty);
            }
            else if (creature < 0)
                for (uint16 z = 0; z < creatureCount; ++z)
                    player->KillCreditGO(creature);
        }

        // player kills
        if (quest->HasSpecialFlag(QUEST_SPECIAL_FLAGS_PLAYER_KILL))
            if (uint32 reqPlayers = quest->GetPlayersSlain())
                player->KilledPlayerCreditForQuest(reqPlayers, quest);

        // If the quest requires reputation to complete
        if (uint32 repFaction = quest->GetRepObjectiveFaction())
        {
            uint32 repValue = quest->GetRepObjectiveValue();
            uint32 curRep = player->GetReputationMgr().GetReputation(repFaction);
            if (curRep < repValue)
                if (FactionEntry const* factionEntry = sFactionStore.LookupEntry(repFaction))
                    player->GetReputationMgr().SetReputation(factionEntry, repValue);
        }

        // If the quest requires a SECOND reputation to complete
        if (uint32 repFaction = quest->GetRepObjectiveFaction2())
        {
            uint32 repValue2 = quest->GetRepObjectiveValue2();
            uint32 curRep = player->GetReputationMgr().GetReputation(repFaction);
            if (curRep < repValue2)
                if (FactionEntry const* factionEntry = sFactionStore.LookupEntry(repFaction))
                    player->GetReputationMgr().SetReputation(factionEntry, repValue2);
        }

        // If the quest requires money
        int32 ReqOrRewMoney = quest->GetRewOrReqMoney(player);
        if (ReqOrRewMoney < 0)
            player->ModifyMoney(-ReqOrRewMoney);

        if (sWorld->getBoolConfig(CONFIG_QUEST_ENABLE_QUEST_TRACKER)) // check if Quest Tracker is enabled
        {
            // prepare Quest Tracker datas
            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_QUEST_TRACK_GM_COMPLETE);
            stmt->setUInt32(0, quest->GetQuestId());
            stmt->setUInt32(1, player->GetGUID().GetCounter());

            // add to Quest Tracker
            CharacterDatabase.Execute(stmt);
        }

        player->CompleteQuest(quest->GetQuestId());
        return true;
    }

    /**
     * @brief 领取任务奖励命令处理函数
     * @param handler 聊天处理器指针
     * @param quest 任务对象指针
     * @return 命令执行成功返回true，失败返回false
     *
     * .quest reward <任务ID> - 给予选中玩家指定任务的奖励
     *
     * 执行流程：
     * 1. 获取目标玩家（必须是选中的玩家）
     * 2. 验证任务是否处于完成状态
     * 3. 检查任务是否被禁用
     * 4. 给予任务奖励（物品、金币、经验等）
     * 5. 从任务日志中移除任务
     * 6. 触发任务完成脚本
     *
     * @note 任务必须处于完成状态才能领取奖励
     *       此命令会触发正常的任务完成流程
     */
    static bool HandleQuestReward(ChatHandler* handler, Quest const* quest)
    {
        Player* player = handler->getSelectedPlayer();
        if (!player)
        {
            handler->SendSysMessage(LANG_NO_CHAR_SELECTED);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // If player doesn't have the quest
        if (player->GetQuestStatus(quest->GetQuestId()) != QUEST_STATUS_COMPLETE
            || DisableMgr::IsDisabledFor(DISABLE_TYPE_QUEST, quest->GetQuestId(), nullptr))
        {
            handler->PSendSysMessage(LANG_COMMAND_QUEST_NOTFOUND, quest->GetQuestId());
            handler->SetSentErrorMessage(true);
            return false;
        }

        player->RewardQuest(quest, 0, player);
        return true;
    }
};

void AddSC_quest_commandscript()
{
    new quest_commandscript();
}
