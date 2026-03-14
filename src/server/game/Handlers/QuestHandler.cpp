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
 * @file QuestHandler.cpp
 * @brief 任务系统处理模块
 *
 * @details 本模块负责处理玩家与任务系统的所有交互,包括:
 * - 任务状态查询：查询任务给予者的状态（可接、可交、进行中等）
 * - 任务接受：接受新任务，包括NPC任务、物品任务和任务分享
 * - 任务查询：查询任务详细信息
 * - 任务完成：提交任务并选择奖励
 * - 任务放弃：放弃已接受的任务
 * - 任务分享：向队伍成员分享任务
 * - 任务日志管理：交换任务日志位置、查询已完成任务等
 *
 * 任务系统核心概念：
 * - 任务给予者（QuestGiver）：NPC、游戏对象或物品，可以提供任务
 * - 任务状态：未接、进行中、已完成、已奖励
 * - 任务目标：击杀怪物、收集物品、与NPC对话等
 * - 任务奖励：经验、金币、物品、声望等
 *
 * @note 本模块与QuestDef、ObjectMgr、Player等模块密切协作
 * @note 支持任务链、任务分享、组队任务、每日任务等功能
 */

#include "WorldSession.h"
#include "Battleground.h"
#include "Common.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "DatabaseEnv.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "GossipDef.h"
#include "Group.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "QuestPackets.h"
#include "ScriptMgr.h"
#include "World.h"
#include "WorldPacket.h"

/**
 * @brief 处理任务给予者状态查询请求
 *
 * @brief 职责：
 * 处理客户端发送的CMSG_QUESTGIVER_STATUS_QUERY消息，查询指定任务给予者的任务状态。
 * 返回该NPC或游戏对象相对于玩家的任务对话状态（如是否有可接任务、可交任务等）。
 *
 * @param recvData 接收的网络数据包，包含任务给予者的GUID
 *
 * @return 无返回值。通过SendQuestGiverStatus向客户端发送任务状态响应。
 *
 * @brief 主要流程：
 * 1. 从数据包中读取任务给予者的GUID
 * 2. 根据GUID查找任务给予者对象（NPC或游戏对象）
 * 3. 根据对象类型获取对应的任务对话状态：
 *    - NPC类型：检查是否敌对，非敌对则获取状态
 *    - 游戏对象类型：直接获取状态
 * 4. 向客户端发送任务给予者的状态信息
 */
void WorldSession::HandleQuestgiverStatusQueryOpcode(WorldPacket& recvData)
{
    ObjectGuid guid;
    recvData >> guid;
    uint32 questStatus = DIALOG_STATUS_NONE;

    // 根据GUID查找任务给予者对象（支持NPC和游戏对象）
    Object* questGiver = ObjectAccessor::GetObjectByTypeMask(*_player, guid, TYPEMASK_UNIT | TYPEMASK_GAMEOBJECT);
    if (!questGiver)
    {
        TC_LOG_INFO("network", "Error in CMSG_QUESTGIVER_STATUS_QUERY, called for non-existing questgiver ({})", guid.ToString());
        return;
    }

    // 根据任务给予者类型获取任务状态
    switch (questGiver->GetTypeId())
    {
        case TYPEID_UNIT:
        {
            TC_LOG_DEBUG("network", "WORLD: Received CMSG_QUESTGIVER_STATUS_QUERY for npc {}", questGiver->GetGUID().ToString());
            // 不向敌对单位显示任务状态
            if (!questGiver->ToCreature()->IsHostileTo(_player))
                questStatus = _player->GetQuestDialogStatus(questGiver);
            break;
        }
        case TYPEID_GAMEOBJECT:
        {
            TC_LOG_DEBUG("network", "WORLD: Received CMSG_QUESTGIVER_STATUS_QUERY for GameObject {}", questGiver->GetGUID().ToString());
            questStatus = _player->GetQuestDialogStatus(questGiver);
            break;
        }
        default:
            TC_LOG_ERROR("network", "QuestGiver called for unexpected type {}", questGiver->GetTypeId());
            break;
    }

    // 向客户端发送任务给予者的状态
    _player->PlayerTalkClass->SendQuestGiverStatus(uint8(questStatus), guid);
}

/**
 * @brief 处理任务给予者打招呼交互请求
 *
 * @brief 职责：
 * 处理客户端发送的CMSG_QUESTGIVER_HELLO消息，玩家与NPC任务给予者进行交互时触发。
 * 初始化NPC的交互状态，准备并显示任务相关的对话菜单。
 *
 * @param recvData 接收的网络数据包，包含NPC的GUID
 *
 * @return 无返回值。向客户端发送对话菜单或任务列表。
 *
 * @brief 主要流程：
 * 1. 从数据包中读取NPC的GUID
 * 2. 验证玩家是否可以与该NPC交互（具有QUESTGIVER标志）
 * 3. 移除玩家的假死状态
 * 4. 暂停NPC的移动并设置其回家位置
 * 5. 清空玩家的对话菜单
 * 6. 调用NPC AI的OnGossipHello事件（可被脚本拦截）
 * 7. 准备并发送NPC的对话菜单给客户端
 */
void WorldSession::HandleQuestgiverHelloOpcode(WorldPacket& recvData)
{
    ObjectGuid guid;
    recvData >> guid;

    TC_LOG_DEBUG("network", "WORLD: Received CMSG_QUESTGIVER_HELLO {}", guid.ToString());

    // 获取可交互的NPC，验证其具有任务给予者标志
    Creature* creature = GetPlayer()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_QUESTGIVER);
    if (!creature)
    {
        TC_LOG_DEBUG("network", "WORLD: HandleQuestgiverHelloOpcode - {} not found or you can't interact with him.",
            guid.ToString());
        return;
    }

    // 移除玩家的假死状态
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    // 暂停NPC的移动，设置交互暂停时间
    if (uint32 pause = creature->GetMovementTemplate().GetInteractionPauseTimer())
        creature->PauseMovement(pause);
    // 设置NPC的回家位置为当前位置
    creature->SetHomePosition(creature->GetPosition());

    // 清空玩家的对话菜单
    _player->PlayerTalkClass->ClearMenus();
    // 调用NPC AI的问候事件，如果脚本返回true则终止后续处理
    if (creature->AI()->OnGossipHello(_player))
        return;

    // 准备并发送NPC的对话菜单
    _player->PrepareGossipMenu(creature, creature->GetCreatureTemplate()->GossipMenuId, true);
    _player->SendPreparedGossip(creature);
}

/**
 * @brief 处理接受任务请求
 *
 * @brief 职责：
 * 处理客户端发送的CMSG_QUESTGIVER_ACCEPT_QUEST消息，玩家接受任务时触发。
 * 支持从NPC、游戏对象、物品或玩家（任务分享）接受任务。
 *
 * @param recvData 接收的网络数据包，包含任务给予者GUID、任务ID和启动作弊标志
 *
 * @return 无返回值。成功时将任务添加到玩家的任务日志，失败时关闭对话窗口。
 *
 * @brief 主要流程：
 * 1. 从数据包中读取GUID、任务ID和启动作弊标志
 * 2. 根据GUID类型获取对应的对象（NPC、游戏对象、物品或玩家）
 * 3. 验证任务来源的有效性：
 *    - 玩家分享：验证分享者是否在同一团队且可分享该任务
 *    - 其他对象：验证对象是否拥有该任务
 * 4. 验证玩家是否可以与任务给予者交互
 * 5. 验证玩家是否满足接受任务的条件
 * 6. 处理任务分享的响应通知
 * 7. 将任务添加到玩家任务日志
 * 8. 如果任务有组队接受标志，向同组玩家发送确认请求
 * 9. 如果任务有关联源技能，施放该技能
 */
void WorldSession::HandleQuestgiverAcceptQuestOpcode(WorldPacket& recvData)
{
    ObjectGuid guid;
    uint32 questId;
    uint32 startCheat;
    recvData >> guid >> questId >> startCheat;

    TC_LOG_DEBUG("network", "WORLD: Received CMSG_QUESTGIVER_ACCEPT_QUEST {}, quest = {}, startCheat = {}", guid.ToString(), questId, startCheat);

    // 根据GUID类型获取对象
    Object* object;
    if (!guid.IsPlayer())
        object = ObjectAccessor::GetObjectByTypeMask(*_player, guid, TYPEMASK_UNIT | TYPEMASK_GAMEOBJECT | TYPEMASK_ITEM);
    else
        object = ObjectAccessor::FindPlayer(guid);

    // 定义关闭对话窗口并清除任务分享信息的lambda函数
    auto CLOSE_GOSSIP_CLEAR_SHARING_INFO = ([this]()
    {
        _player->PlayerTalkClass->SendCloseGossip();
        _player->ClearQuestSharingInfo();
    });

    // 验证任务给予者是否存在
    if (!object)
    {
        CLOSE_GOSSIP_CLEAR_SHARING_INFO();
        return;
    }

    // 处理玩家分享任务的情况
    if (Player* playerQuestObject = object->ToPlayer())
    {
        // 验证分享者是否有效且可分享该任务
        if ((_player->GetPlayerSharingQuest() && _player->GetPlayerSharingQuest() != guid) || !playerQuestObject->CanShareQuest(questId))
        {
            CLOSE_GOSSIP_CLEAR_SHARING_INFO();
            return;
        }
        // 验证是否在同一团队
        if (!_player->IsInSameRaidWith(playerQuestObject))
        {
            CLOSE_GOSSIP_CLEAR_SHARING_INFO();
            return;
        }
    }
    else
    {
        // 验证对象是否拥有该任务
        if (!object->hasQuest(questId))
        {
            CLOSE_GOSSIP_CLEAR_SHARING_INFO();
            return;
        }
    }

    // WPE保护：验证玩家是否可以与任务给予者交互
    if (!_player->CanInteractWithQuestGiver(object))
    {
        CLOSE_GOSSIP_CLEAR_SHARING_INFO();
        return;
    }

    // 获取任务模板并进行处理
    if (Quest const* quest = sObjectMgr->GetQuestTemplate(questId))
    {
        // 防止作弊：验证玩家是否满足接受任务的条件
        if (!GetPlayer()->CanTakeQuest(quest, true))
        {
            CLOSE_GOSSIP_CLEAR_SHARING_INFO();
            return;
        }

        // 处理任务分享响应
        if (!_player->GetPlayerSharingQuest().IsEmpty())
        {
            Player* player = ObjectAccessor::FindPlayer(_player->GetPlayerSharingQuest());
            if (player)
            {
                player->SendPushToPartyResponse(_player, QUEST_PARTY_MSG_ACCEPT_QUEST);
                _player->ClearQuestSharingInfo();
            }
        }

        // 验证玩家是否可以添加任务到任务日志
        if (_player->CanAddQuest(quest, true))
        {
            // 将任务添加到玩家任务日志并检查是否立即完成
            _player->AddQuestAndCheckCompletion(quest, object);

            // 如果任务有组队接受标志，向同组玩家发送确认请求
            if (quest->HasFlag(QUEST_FLAGS_PARTY_ACCEPT))
            {
                if (Group* group = _player->GetGroup())
                {
                    for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
                    {
                        Player* player = itr->GetSource();

                        // 跳过自己和不在此地图的玩家
                        if (!player || player == _player || !player->IsInMap(_player))
                            continue;

                        // 检查该玩家是否可以接受此任务
                        if (player->CanTakeQuest(quest, true))
                        {
                            // 设置任务分享信息
                            player->SetQuestSharingInfo(_player->GetGUID(), questId);

                            // 关闭任何可能打开的对话窗口
                            player->PlayerTalkClass->SendCloseGossip();

                            // 发送任务确认接受请求
                            _player->SendQuestConfirmAccept(quest, player);
                        }
                    }
                }
            }

            // 关闭对话窗口
            _player->PlayerTalkClass->SendCloseGossip();

            // 如果任务有关联的源技能，施放该技能
            if (quest->GetSrcSpell() > 0)
                _player->CastSpell(_player, quest->GetSrcSpell(), true);

            return;
        }
    }

    CLOSE_GOSSIP_CLEAR_SHARING_INFO();
}

/**
 * @brief 处理查询任务详情请求
 *
 * @brief 职责：
 * 处理客户端发送的CMSG_QUESTGIVER_QUERY_QUEST消息，查询特定任务的详细信息。
 * 返回任务的详细内容，包括任务描述、目标、奖励等。
 *
 * @param recvData 接收的网络数据包，包含任务给予者GUID、任务ID和未知参数
 *
 * @return 无返回值。向客户端发送任务详细信息或关闭对话窗口。
 *
 * @brief 主要流程：
 * 1. 从数据包中读取GUID、任务ID和未知参数
 * 2. 验证任务给予者是否有效且拥有或参与该任务
 * 3. 获取任务模板
 * 4. 验证玩家是否可以接受该任务
 * 5. 如果任务可自动接受，则自动添加到任务日志
 * 6. 根据任务标志发送相应的任务信息：
 *    - 自动完成任务：发送任务物品请求界面
 *    - 普通任务：发送任务详情界面
 */
void WorldSession::HandleQuestgiverQueryQuestOpcode(WorldPacket& recvData)
{
    ObjectGuid guid;
    uint32 questId;
    uint8 unk1;
    recvData >> guid >> questId >> unk1;
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_QUESTGIVER_QUERY_QUEST npc = {}, quest = {}, unk1 = {}", guid.ToString(), questId, unk1);

    // 验证GUID有效且是任务给予者或任务参与者
    Object* object = ObjectAccessor::GetObjectByTypeMask(*_player, guid, TYPEMASK_UNIT | TYPEMASK_GAMEOBJECT | TYPEMASK_ITEM);
    if (!object || (!object->hasQuest(questId) && !object->hasInvolvedQuest(questId)))
    {
        _player->PlayerTalkClass->SendCloseGossip();
        return;
    }

    // 获取任务模板并处理
    if (Quest const* quest = sObjectMgr->GetQuestTemplate(questId))
    {
        // 验证玩家是否可以接受该任务
        // 注意：物品启动的任务不会有QUEST_FLAGS_AUTOCOMPLETE标志
        if (!_player->CanTakeQuest(quest, true))
            return;

        // 如果任务可自动接受且玩家可以添加任务，则自动添加
        if (quest->IsAutoAccept() && _player->CanAddQuest(quest, true))
            _player->AddQuestAndCheckCompletion(quest, object);

        // 根据任务标志发送相应的界面
        if (quest->HasFlag(QUEST_FLAGS_AUTOCOMPLETE))
            // 自动完成任务：发送任务物品请求界面
            _player->PlayerTalkClass->SendQuestGiverRequestItems(quest, object->GetGUID(), _player->CanCompleteQuest(quest->GetQuestId()), true);
        else
            // 普通任务：发送任务详情界面
            _player->PlayerTalkClass->SendQuestGiverQuestDetails(quest, object->GetGUID(), true);
    }
}

/**
 * @brief 处理查询任务信息请求
 *
 * @brief 职责：
 * 处理客户端发送的CMSG_QUEST_QUERY消息，查询任务的详细信息。
 * 返回任务的完整信息，包括名称、描述、目标、奖励等详细数据。
 *
 * @param query 接收的任务查询数据包，包含任务ID
 *
 * @return 无返回值。向客户端发送任务查询响应。
 *
 * @brief 主要流程：
 * 1. 从数据包中获取任务ID
 * 2. 根据任务ID获取任务模板
 * 3. 向客户端发送任务查询响应
 */
void WorldSession::HandleQuestQueryOpcode(WorldPackets::Quest::QueryQuestInfo& query)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_QUEST_QUERY quest = {}", query.QuestID);

    // 获取任务模板并发送查询响应
    if (Quest const* quest = sObjectMgr->GetQuestTemplate(query.QuestID))
        _player->PlayerTalkClass->SendQuestQueryResponse(quest);
}

/**
 * @brief 处理选择任务奖励请求
 *
 * @brief 职责：
 * 处理客户端发送的CMSG_QUESTGIVER_CHOOSE_REWARD消息，玩家完成任务并选择奖励时触发。
 * 验证任务的完成状态，发放奖励物品，并处理后续任务链。
 *
 * @param recvData 接收的网络数据包，包含任务给予者GUID、任务ID和奖励选择索引
 *
 * @return 无返回值。成功时发放任务奖励，可能触发后续任务。
 *
 * @brief 主要流程：
 * 1. 从数据包中读取GUID、任务ID和奖励选择索引
 * 2. 验证奖励索引是否有效（防止数据包作弊）
 * 3. 获取任务给予者对象并验证其参与该任务
 * 4. 验证玩家是否可以与任务给予者交互
 * 5. 获取任务模板并验证玩家的任务状态
 * 6. 验证玩家是否可以提交任务（所有目标完成）
 * 7. 验证玩家是否可以接收奖励物品（背包空间等）
 * 8. 发放任务奖励
 * 9. 如果有后续任务链，发送下一个任务的详情
 * 10. 调用任务给予者AI的OnQuestReward事件
 */
void WorldSession::HandleQuestgiverChooseRewardOpcode(WorldPacket& recvData)
{
    uint32 questId, reward;
    ObjectGuid guid;
    recvData >> guid >> questId >> reward;

    // 验证奖励索引是否有效，防止数据包作弊
    if (reward >= QUEST_REWARD_CHOICES_COUNT)
    {
        TC_LOG_ERROR("entities.player.cheat", "Error in CMSG_QUESTGIVER_CHOOSE_REWARD: player {} {} tried to get invalid reward ({}) (possible packet-hacking detected)", _player->GetName(), _player->GetGUID().ToString(), reward);
        return;
    }

    TC_LOG_DEBUG("network", "WORLD: Received CMSG_QUESTGIVER_CHOOSE_REWARD npc = {}, quest = {}, reward = {}", guid.ToString(), questId, reward);

    // 获取任务给予者对象并验证其参与该任务
    Object* object = ObjectAccessor::GetObjectByTypeMask(*_player, guid, TYPEMASK_UNIT | TYPEMASK_GAMEOBJECT);
    if (!object || !object->hasInvolvedQuest(questId))
        return;

    // WPE保护：验证玩家是否可以与任务给予者交互
    if (!_player->CanInteractWithQuestGiver(object))
        return;

    // 获取任务模板并处理
    if (Quest const* quest = sObjectMgr->GetQuestTemplate(questId))
    {
        // 验证玩家的任务状态
        if ((!_player->CanSeeStartQuest(quest) &&  _player->GetQuestStatus(questId) == QUEST_STATUS_NONE) ||
            (_player->GetQuestStatus(questId) != QUEST_STATUS_COMPLETE && !quest->IsAutoComplete()))
        {
            TC_LOG_ERROR("entities.player.cheat", "Error in QUEST_STATUS_COMPLETE: player {} {} tried to complete quest {}, but is not allowed to do so (possible packet-hacking or high latency)",
                           _player->GetName(), _player->GetGUID().ToString(), questId);
            return;
        }

        // 首先验证玩家是否可以提交任务（所有目标已完成）
        if (_player->CanRewardQuest(quest, true))
        {
            // 然后验证玩家是否可以接收奖励物品（背包空间、唯一物品数量等）
            if (_player->CanRewardQuest(quest, reward, true))
            {
                // 发放任务奖励
                _player->RewardQuest(quest, reward, object);

                // 根据任务给予者类型处理后续逻辑
                switch (object->GetTypeId())
                {
                    case TYPEID_UNIT:
                    {
                        Creature* questgiver = object->ToCreature();
                        // 获取并发送下一个任务（任务链）
                        if (Quest const* nextQuest = _player->GetNextQuest(guid, quest))
                        {
                            // 只有当玩家满足条件时才发送任务
                            if (_player->CanTakeQuest(nextQuest, false))
                            {
                                // 如果下一任务可自动接受，则自动添加
                                if (nextQuest->IsAutoAccept() && _player->CanAddQuest(nextQuest, true))
                                    _player->AddQuestAndCheckCompletion(nextQuest, object);

                                // 发送任务详情
                                _player->PlayerTalkClass->SendQuestGiverQuestDetails(nextQuest, guid, true);
                            }
                        }

                        // 清空对话菜单并调用NPC AI的任务奖励事件
                        _player->PlayerTalkClass->ClearMenus();
                        questgiver->AI()->OnQuestReward(_player, quest, reward);
                        break;
                    }
                    case TYPEID_GAMEOBJECT:
                    {
                        GameObject* questGiver = object->ToGameObject();
                        // 获取并发送下一个任务（任务链）
                        if (Quest const* nextQuest = _player->GetNextQuest(guid, quest))
                        {
                            // 只有当玩家满足条件时才发送任务
                            if (_player->CanTakeQuest(nextQuest, false))
                            {
                                // 如果下一任务可自动接受，则自动添加
                                if (nextQuest->IsAutoAccept() && _player->CanAddQuest(nextQuest, true))
                                    _player->AddQuestAndCheckCompletion(nextQuest, object);

                                // 发送任务详情
                                _player->PlayerTalkClass->SendQuestGiverQuestDetails(nextQuest, guid, true);
                            }
                        }

                        // 清空对话菜单并调用游戏对象AI的任务奖励事件
                        _player->PlayerTalkClass->ClearMenus();
                        questGiver->AI()->OnQuestReward(_player, quest, reward);
                        break;
                    }
                    default:
                        break;
                }
            }
        }
        else
            // 如果玩家还不能提交任务，发送奖励选择界面
            _player->PlayerTalkClass->SendQuestGiverOfferReward(quest, guid, true);
    }
}

/**
 * @brief 处理请求任务奖励界面
 *
 * @brief 职责：
 * 处理客户端发送的CMSG_QUESTGIVER_REQUEST_REWARD消息，玩家请求查看任务奖励界面时触发。
 * 验证任务完成状态，如果任务可完成则标记为完成，并发送奖励选择界面。
 *
 * @param recvData 接收的网络数据包，包含任务给予者GUID和任务ID
 *
 * @return 无返回值。向客户端发送任务奖励界面或终止处理。
 *
 * @brief 主要流程：
 * 1. 从数据包中读取GUID和任务ID
 * 2. 获取任务给予者对象并验证其参与该任务
 * 3. 验证玩家是否可以与任务给予者交互
 * 4. 如果任务可完成，则将任务标记为完成
 * 5. 验证任务状态是否为已完成
 * 6. 发送任务奖励选择界面
 */
void WorldSession::HandleQuestgiverRequestRewardOpcode(WorldPacket& recvData)
{
    uint32 questId;
    ObjectGuid guid;
    recvData >> guid >> questId;

    TC_LOG_DEBUG("network", "WORLD: Received CMSG_QUESTGIVER_REQUEST_REWARD npc = {}, quest = {}", guid.ToString(), questId);

    // 获取任务给予者对象并验证其参与该任务
    Object* object = ObjectAccessor::GetObjectByTypeMask(*_player, guid, TYPEMASK_UNIT | TYPEMASK_GAMEOBJECT);
    if (!object || !object->hasInvolvedQuest(questId))
        return;

    // WPE保护：验证玩家是否可以与任务给予者交互
    if (!_player->CanInteractWithQuestGiver(object))
        return;

    // 如果任务可完成，则标记为完成
    if (_player->CanCompleteQuest(questId))
        _player->CompleteQuest(questId);

    // 验证任务状态是否为已完成
    if (_player->GetQuestStatus(questId) != QUEST_STATUS_COMPLETE)
        return;

    // 发送任务奖励选择界面
    if (Quest const* quest = sObjectMgr->GetQuestTemplate(questId))
        _player->PlayerTalkClass->SendQuestGiverOfferReward(quest, guid, true);
}

/**
 * @brief 处理取消任务对话请求
 *
 * @brief 职责：
 * 处理客户端发送的CMSG_QUESTGIVER_CANCEL消息，玩家取消任务对话时触发。
 * 关闭当前打开的任务对话窗口。
 *
 * @param recvData 接收的网络数据包（未使用）
 *
 * @return 无返回值。关闭玩家的对话窗口。
 *
 * @brief 主要流程：
 * 1. 记录调试日志
 * 2. 关闭玩家的对话窗口
 */
void WorldSession::HandleQuestgiverCancel(WorldPacket& /*recvData*/)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_QUESTGIVER_CANCEL");

    // 关闭对话窗口
    _player->PlayerTalkClass->SendCloseGossip();
}

/**
 * @brief 处理交换任务日志位置请求
 *
 * @brief 职责：
 * 处理客户端发送的CMSG_QUESTLOG_SWAP_QUEST消息，玩家交换任务日志中两个任务的位置时触发。
 * 允许玩家重新排列任务日志中任务的显示顺序。
 *
 * @param recvData 接收的网络数据包，包含两个任务日志槽位索引
 *
 * @return 无返回值。交换成功时更新玩家的任务日志槽位。
 *
 * @brief 主要流程：
 * 1. 从数据包中读取两个槽位索引
 * 2. 验证槽位索引是否有效（不相同且不超过最大值）
 * 3. 交换玩家任务日志中的两个槽位
 */
void WorldSession::HandleQuestLogSwapQuest(WorldPacket& recvData)
{
    uint8 slot1, slot2;
    recvData >> slot1 >> slot2;

    // 验证槽位索引是否有效
    if (slot1 == slot2 || slot1 >= MAX_QUEST_LOG_SIZE || slot2 >= MAX_QUEST_LOG_SIZE)
        return;

    TC_LOG_DEBUG("network", "WORLD: Received CMSG_QUESTLOG_SWAP_QUEST slot 1 = {}, slot 2 = {}", slot1, slot2);

    // 交换任务日志槽位
    GetPlayer()->SwapQuestSlot(slot1, slot2);
}

/**
 * @brief 处理放弃任务请求
 *
 * @brief 职责：
 * 处理客户端发送的CMSG_QUESTLOG_REMOVE_QUEST消息，玩家放弃任务时触发。
 * 从玩家的任务日志中移除任务，清理相关数据，并更新成就进度。
 *
 * @param recvData 接收的网络数据包，包含任务日志槽位索引
 *
 * @return 无返回值。成功时从玩家任务日志中移除任务。
 *
 * @brief 主要流程：
 * 1. 从数据包中读取任务日志槽位索引
 * 2. 验证槽位索引是否有效
 * 3. 获取该槽位的任务ID
 * 4. 尝试移除任务源物品（如果无法卸下则拒绝放弃）
 * 5. 处理特殊任务标志：
 *    - 计时任务：移除计时器
 *    - PVP任务：更新PVP状态
 * 6. 移除任务源物品和任务物品
 * 7. 放弃任务（清理任务相关数据）
 * 8. 从活跃任务列表中移除
 * 9. 移除相关的计时成就
 * 10. 记录日志和任务追踪器数据
 * 11. 调用脚本事件
 * 12. 清空任务日志槽位
 * 13. 更新放弃任务成就进度
 */
void WorldSession::HandleQuestLogRemoveQuest(WorldPacket& recvData)
{
    uint8 slot;
    recvData >> slot;

    TC_LOG_DEBUG("network", "WORLD: Received CMSG_QUESTLOG_REMOVE_QUEST slot = {}", slot);

    // 验证槽位索引是否有效
    if (slot < MAX_QUEST_LOG_SIZE)
    {
        // 获取该槽位的任务ID
        if (uint32 questId = _player->GetQuestSlotQuestId(slot))
        {
            // 尝试移除任务源物品，如果无法卸下则拒绝放弃任务
            if (!_player->TakeQuestSourceItem(questId, true))
                return;                                     // 无法卸下某些装备物品，拒绝放弃任务

            // 获取任务模板并处理特殊标志
            if (Quest const* quest = sObjectMgr->GetQuestTemplate(questId))
            {
                // 如果是计时任务，移除计时器
                if (quest->HasSpecialFlag(QUEST_SPECIAL_FLAGS_TIMED))
                    _player->RemoveTimedQuest(questId);

                // 如果是PVP任务，更新PVP状态
                if (quest->HasFlag(QUEST_FLAGS_FLAGS_PVP))
                {
                    _player->pvpInfo.IsHostile = _player->pvpInfo.IsInHostileArea || _player->HasPvPForcingQuest();
                    _player->UpdatePvPState();
                }
            }

            // 移除任务源物品
            _player->TakeQuestSourceItem(questId, true);
            // 放弃任务（移除任务相关物品，但不移除普通掉落物品）
            _player->AbandonQuest(questId);
            // 从活跃任务列表中移除
            _player->RemoveActiveQuest(questId);
            // 移除相关的计时成就
            _player->RemoveTimedAchievement(ACHIEVEMENT_TIMED_TYPE_QUEST, questId);

            TC_LOG_INFO("network", "Player {} abandoned quest {}", _player->GetGUID().ToString(), questId);

            // 如果启用了任务追踪器，更新数据库
            if (sWorld->getBoolConfig(CONFIG_QUEST_ENABLE_QUEST_TRACKER))
            {
                // 准备任务追踪器数据
                CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_QUEST_TRACK_ABANDON_TIME);
                stmt->setUInt32(0, questId);
                stmt->setUInt32(1, _player->GetGUID().GetCounter());

                // 添加到任务追踪器
                CharacterDatabase.Execute(stmt);
            }

            // 调用脚本事件
            sScriptMgr->OnQuestStatusChange(_player, questId);
        }

        // 清空任务日志槽位
        _player->SetQuestSlot(slot, 0);

        // 更新放弃任务成就进度
        _player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_QUEST_ABANDONED, 1);
    }
}

/**
 * @brief 处理确认接受组队任务请求
 *
 * @brief 职责：
 * 处理客户端发送的CMSG_QUEST_CONFIRM_ACCEPT消息，玩家确认接受组队分享的任务时触发。
 * 验证任务分享的有效性，并将任务添加到玩家的任务日志。
 *
 * @param recvData 接收的网络数据包，包含任务ID
 *
 * @return 无返回值。成功时将任务添加到玩家的任务日志。
 *
 * @brief 主要流程：
 * 1. 从数据包中读取任务ID
 * 2. 获取任务模板
 * 3. 验证任务是否具有组队接受标志
 * 4. 获取分享任务的原始玩家
 * 5. 验证是否在同一团队
 * 6. 验证原始玩家是否仍有该任务
 * 7. 验证玩家是否可以接受该任务
 * 8. 将任务添加到玩家任务日志
 * 9. 如果任务有关联源技能，施放该技能
 * 10. 清除任务分享信息
 */
void WorldSession::HandleQuestConfirmAccept(WorldPacket& recvData)
{
    uint32 questId;
    recvData >> questId;

    TC_LOG_DEBUG("network", "WORLD: Received CMSG_QUEST_CONFIRM_ACCEPT quest = {}", questId);

    // 获取任务模板并处理
    if (Quest const* quest = sObjectMgr->GetQuestTemplate(questId))
    {
        // 验证任务是否具有组队接受标志
        if (!quest->HasFlag(QUEST_FLAGS_PARTY_ACCEPT))
            return;

        // 获取分享任务的原始玩家
        Player* originalPlayer = ObjectAccessor::FindPlayer(_player->GetPlayerSharingQuest());
        if (!originalPlayer)
            return;

        // 验证是否在同一团队
        if (!_player->IsInSameRaidWith(originalPlayer))
            return;

        // 验证原始玩家是否仍有该任务
        if (!originalPlayer->IsActiveQuest(questId))
            return;

        // 验证玩家是否可以接受该任务
        if (!_player->CanTakeQuest(quest, true))
            return;

        // 验证玩家是否可以添加任务到任务日志
        if (_player->CanAddQuest(quest, true))
        {
            // 将任务添加到玩家任务日志（传入nullptr防止DB脚本重复运行）
            _player->AddQuestAndCheckCompletion(quest, nullptr);

            // 如果任务有关联的源技能，施放该技能
            if (quest->GetSrcSpell() > 0)
                _player->CastSpell(_player, quest->GetSrcSpell(), true);
        }
    }

    // 清除任务分享信息
    _player->ClearQuestSharingInfo();
}

/**
 * @brief 处理完成任务请求
 *
 * @brief 职责：
 * 处理客户端发送的CMSG_QUESTGIVER_COMPLETE_QUEST消息，玩家尝试完成任务时触发。
 * 验证任务状态，处理战场任务完成事件，并根据任务要求发送相应界面。
 *
 * @param recvData 接收的网络数据包，包含任务给予者GUID和任务ID
 *
 * @return 无返回值。发送任务物品请求界面或奖励选择界面。
 *
 * @brief 主要流程：
 * 1. 从数据包中读取GUID和任务ID
 * 2. 获取任务模板
 * 3. 获取任务给予者对象并验证其参与该任务
 * 4. 验证玩家是否可以与任务给予者交互
 * 5. 验证玩家是否拥有该任务
 * 6. 如果玩家在战场中，处理战场任务完成事件
 * 7. 根据任务状态和类型发送相应界面：
 *    - 未完成且可重复任务：发送物品请求界面
 *    - 未完成且非重复任务：发送物品请求界面
 *    - 已完成且需要物品：发送物品请求界面
 *    - 已完成且无需物品：发送奖励选择界面
 */
void WorldSession::HandleQuestgiverCompleteQuest(WorldPacket& recvData)
{
    uint32 questId;
    ObjectGuid guid;

    recvData >> guid >> questId;

    TC_LOG_DEBUG("network", "WORLD: Received CMSG_QUESTGIVER_COMPLETE_QUEST npc = {}, quest = {}", guid.ToString(), questId);

    // 获取任务模板
    Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
    if (!quest)
        return;

    // 获取任务给予者对象并验证其参与该任务
    Object* object = ObjectAccessor::GetObjectByTypeMask(*_player, guid, TYPEMASK_UNIT | TYPEMASK_GAMEOBJECT);
    if (!object || !object->hasInvolvedQuest(questId))
        return;

    // WPE保护：验证玩家是否可以与任务给予者交互
    if (!_player->CanInteractWithQuestGiver(object))
        return;

    // 验证玩家是否拥有该任务
    if (!_player->CanSeeStartQuest(quest) && _player->GetQuestStatus(questId) == QUEST_STATUS_NONE)
    {
        TC_LOG_ERROR("entities.player.cheat", "Possible hacking attempt: Player {} {} tried to complete quest [entry: {}] without being in possession of the quest!",
                      _player->GetName(), _player->GetGUID().ToString(), questId);
        return;
    }

    // 如果玩家在战场中，处理战场任务完成事件
    if (Battleground* bg = _player->GetBattleground())
        bg->HandleQuestComplete(questId, _player);

    // 根据任务状态发送相应界面
    if (_player->GetQuestStatus(questId) != QUEST_STATUS_COMPLETE)
    {
        // 任务未完成
        if (quest->IsRepeatable())
            // 可重复任务：发送物品请求界面
            _player->PlayerTalkClass->SendQuestGiverRequestItems(quest, guid, _player->CanCompleteRepeatableQuest(quest), false);
        else
            // 非重复任务：发送物品请求界面
            _player->PlayerTalkClass->SendQuestGiverRequestItems(quest, guid, _player->CanRewardQuest(quest, false), false);
    }
    else
    {
        // 任务已完成
        if (quest->GetReqItemsCount())
            // 需要物品：发送物品请求界面
            _player->PlayerTalkClass->SendQuestGiverRequestItems(quest, guid, _player->CanRewardQuest(quest, false), false);
        else
            // 无需物品：发送奖励选择界面
            _player->PlayerTalkClass->SendQuestGiverOfferReward(quest, guid, true);
    }
}

/**
 * @brief 处理任务自动启动请求
 *
 * @brief 职责：
 * 处理客户端发送的CMSG_QUESTGIVER_QUEST_AUTOLAUNCH消息。
 * 当前仅记录日志，未实现具体功能。
 *
 * @param recvPacket 接收的网络数据包（未使用）
 *
 * @return 无返回值。
 *
 * @brief 主要流程：
 * 1. 记录调试日志
 */
void WorldSession::HandleQuestgiverQuestAutoLaunch(WorldPacket& /*recvPacket*/)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_QUESTGIVER_QUEST_AUTOLAUNCH");
}

/**
 * @brief 处理向队伍推送任务请求
 *
 * @brief 职责：
 * 处理客户端发送的CMSG_PUSHQUESTTOPARTY消息，玩家向队伍成员分享任务时触发。
 * 验证玩家是否可以分享任务，并向符合条件的队伍成员发送任务详情。
 *
 * @param recvPacket 接收的网络数据包，包含任务ID
 *
 * @return 无返回值。向队伍成员发送任务分享请求。
 *
 * @brief 主要流程：
 * 1. 从数据包中读取任务ID
 * 2. 验证玩家是否可以分享该任务
 * 3. 获取任务模板
 * 4. 验证玩家是否在队伍中
 * 5. 遍历队伍成员，对每个成员进行以下检查：
 *    - 是否正在分享其他任务
 *    - 是否已完成该任务
 *    - 是否已拥有该任务
 *    - 任务日志是否已满
 *    - 是否满足每日任务限制
 *    - 是否可以接受该任务
 * 6. 向符合条件的成员发送任务详情或任务物品请求界面
 * 7. 如果任务可自动接受，自动添加任务到成员的任务日志
 */
void WorldSession::HandlePushQuestToParty(WorldPacket& recvPacket)
{
    uint32 questId;
    recvPacket >> questId;

    // 验证玩家是否可以分享该任务
    if (!_player->CanShareQuest(questId))
        return;

    TC_LOG_DEBUG("network", "WORLD: Received CMSG_PUSHQUESTTOPARTY questId = {}", questId);

    // 获取任务模板
    Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
    if (!quest)
        return;

    Player * const sender = GetPlayer();

    // 验证玩家是否在队伍中
    Group* group = sender->GetGroup();
    if (!group)
    {
        sender->SendPushToPartyResponse(sender, QUEST_PARTY_MSG_NOT_IN_PARTY);
        return;
    }

    // 遍历队伍成员
    for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
    {
        Player* receiver = itr->GetSource();

        // 跳过自己和无效玩家
        if (!receiver || receiver == sender)
            continue;

        // 检查接收者是否正在分享其他任务
        if (!receiver->GetPlayerSharingQuest().IsEmpty())
        {
            sender->SendPushToPartyResponse(receiver, QUEST_PARTY_MSG_BUSY);
            continue;
        }

        // 检查接收者的任务状态
        switch (receiver->GetQuestStatus(questId))
        {
            case QUEST_STATUS_REWARDED:
            {
                // 已完成任务
                sender->SendPushToPartyResponse(receiver, QUEST_PARTY_MSG_FINISH_QUEST);
                continue;
            }
            case QUEST_STATUS_INCOMPLETE:
            case QUEST_STATUS_COMPLETE:
            {
                // 已拥有任务
                sender->SendPushToPartyResponse(receiver, QUEST_PARTY_MSG_HAVE_QUEST);
                continue;
            }
            default:
                break;
        }

        // 检查任务日志是否已满
        if (!receiver->SatisfyQuestLog(false))
        {
            sender->SendPushToPartyResponse(receiver, QUEST_PARTY_MSG_LOG_FULL);
            continue;
        }

        // 检查是否满足每日任务限制
        if (!receiver->SatisfyQuestDay(quest, false))
        {
            sender->SendPushToPartyResponse(receiver, QUEST_PARTY_MSG_NOT_ELIGIBLE_TODAY);
            continue;
        }

        // 检查是否可以接受该任务
        if (!receiver->CanTakeQuest(quest, false))
        {
            sender->SendPushToPartyResponse(receiver, QUEST_PARTY_MSG_CANT_TAKE_QUEST);
            continue;
        }

        // 发送正在分享任务的响应
        sender->SendPushToPartyResponse(receiver, QUEST_PARTY_MSG_SHARING_QUEST);

        // 根据任务类型发送相应界面
        if ((quest->IsAutoComplete() && quest->IsRepeatable() && !quest->IsDailyOrWeekly()) || quest->HasFlag(QUEST_FLAGS_AUTOCOMPLETE))
        {
            // 自动完成任务：发送任务物品请求界面
            receiver->PlayerTalkClass->SendQuestGiverRequestItems(quest, sender->GetGUID(), receiver->CanCompleteRepeatableQuest(quest), true);
        }
        else
        {
            // 普通任务：设置任务分享信息并发送任务详情
            receiver->SetQuestSharingInfo(sender->GetGUID(), questId);
            receiver->PlayerTalkClass->SendQuestGiverQuestDetails(quest, receiver->GetGUID(), true);

            // 如果任务可自动接受，自动添加任务
            if (quest->IsAutoAccept() && receiver->CanAddQuest(quest, true) && receiver->CanTakeQuest(quest, true))
            {
                receiver->AddQuestAndCheckCompletion(quest, sender);
                sender->SendPushToPartyResponse(receiver, QUEST_PARTY_MSG_ACCEPT_QUEST);
                receiver->ClearQuestSharingInfo();
            }
        }
    }
}

/**
 * @brief 处理任务推送结果响应
 *
 * @brief 职责：
 * 处理客户端发送的MSG_QUEST_PUSH_RESULT消息，接收者对任务分享请求的响应时触发。
 * 将响应结果转发给任务分享发起者。
 *
 * @param recvPacket 接收的网络数据包，包含分享者GUID、任务ID和响应消息
 *
 * @return 无返回值。向任务分享发起者发送响应消息。
 *
 * @brief 主要流程：
 * 1. 从数据包中读取分享者GUID、任务ID和响应消息
 * 2. 验证玩家是否正在分享任务
 * 3. 验证分享者GUID是否匹配
 * 4. 找到分享者并向其发送响应消息
 * 5. 清除任务分享信息
 */
void WorldSession::HandleQuestPushResult(WorldPacket& recvPacket)
{
    ObjectGuid guid;
    uint32 questId;
    uint8 msg;
    recvPacket >> guid >> questId >> msg;

    TC_LOG_DEBUG("network", "WORLD: Received MSG_QUEST_PUSH_RESULT");

    // 验证玩家是否正在分享任务
    if (!_player->GetPlayerSharingQuest())
        return;

    // 验证分享者GUID是否匹配
    if (_player->GetPlayerSharingQuest() == guid)
    {
        // 找到分享者并向其发送响应消息
        Player* player = ObjectAccessor::FindPlayer(guid);
        if (player)
            player->SendPushToPartyResponse(_player, static_cast<QuestShareMessages>(msg));
    }

    // 清除任务分享信息
    _player->ClearQuestSharingInfo();
}

/**
 * @brief 处理批量查询任务给予者状态请求
 *
 * @brief 职责：
 * 处理客户端发送的CMSG_QUESTGIVER_STATUS_MULTIPLE_QUERY消息，批量查询周围任务给予者的状态。
 * 向客户端发送所有附近任务给予者的状态信息。
 *
 * @param recvPacket 接收的网络数据包（未使用）
 *
 * @return 无返回值。向客户端发送批量任务给予者状态响应。
 *
 * @brief 主要流程：
 * 1. 记录调试日志
 * 2. 发送周围所有任务给予者的状态信息
 */
void WorldSession::HandleQuestgiverStatusMultipleQuery(WorldPacket& /*recvPacket*/)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_QUESTGIVER_STATUS_MULTIPLE_QUERY");

    // 发送周围所有任务给予者的状态信息
    _player->SendQuestGiverStatusMultiple();
}

/**
 * @brief 处理查询已完成任务列表请求
 *
 * @brief 职责：
 * 处理客户端发送的SMSG_QUERY_QUESTS_COMPLETED_RESPONSE消息，查询玩家已完成的所有任务列表。
 * 返回玩家已完成的任务ID列表。
 *
 * @param recvData 接收的网络数据包（未使用）
 *
 * @return 无返回值。向客户端发送已完成任务列表响应。
 *
 * @brief 主要流程：
 * 1. 获取玩家已完成的任务数量
 * 2. 创建响应数据包
 * 3. 写入已完成任务数量
 * 4. 遍历所有已完成任务并写入任务ID
 * 5. 发送响应数据包
 */
void WorldSession::HandleQueryQuestsCompleted(WorldPacket & /*recvData*/)
{
    // 获取已完成任务数量
    size_t rew_count = _player->GetRewardedQuestCount();

    // 创建响应数据包
    WorldPacket data(SMSG_QUERY_QUESTS_COMPLETED_RESPONSE, 4 + 4 * rew_count);
    data << uint32(rew_count);

    // 遍历所有已完成任务并写入任务ID
    RewardedQuestSet const& rewQuests = _player->getRewardedQuests();
    for (RewardedQuestSet::const_iterator itr = rewQuests.begin(); itr != rewQuests.end(); ++itr)
        data << uint32(*itr);

    // 发送响应数据包
    SendPacket(&data);
}
