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
 * @file    cs_group.cpp
 * @brief   组队管理命令模块
 *
 * @details 本模块实现了所有与组队相关的GM命令，包括：
 *          - 组队长、助手、主坦、主助手的设置
 *          - 组队的解散、移除成员、加入成员
 *          - 组队成员的召唤、复活、修理、等级调整
 *          - 组队信息列表显示
 *
 * @note    这些命令主要用于GM管理玩家组队
 *          所有命令都需要相应的RBAC权限才能执行
 *
 * @see     Group, Player, GroupMgr
 */

#include "ScriptMgr.h"
#include "CharacterCache.h"
#include "ChatCommandTags.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "GroupMgr.h"
#include "Language.h"
#include "LFG.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "RBAC.h"
#include "WorldSession.h"

#if TRINITY_COMPILER == TRINITY_COMPILER_GNU
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

using namespace Trinity::ChatCommands;

/**
 * @class group_commandscript
 * @brief 组队命令脚本类
 *
 * @details 继承自 CommandScript，提供所有组队相关的GM命令处理功能。
 *          该类实现了组队的管理操作，包括队长设置、成员管理、召唤等。
 *
 * @note    所有命令都需要对应的RBAC权限
 *          大部分命令只能在游戏中使用，不支持控制台执行
 */
class group_commandscript : public CommandScript
{
public:
    /**
     * @brief 构造函数
     * @param name 脚本名称，固定为 "group_commandscript"
     */
    group_commandscript() : CommandScript("group_commandscript") { }

    /**
     * @brief 获取命令表
     * @return 返回组队命令表，包含所有子命令及其处理函数
     *
     * @details 定义了以下命令：
     *          - set leader: 设置队长
     *          - set assistant: 设置助手
     *          - set maintank: 设置主坦
     *          - set mainassist: 设置主助手
     *          - leader: 设置队长（快捷命令）
     *          - disband: 解散组队
     *          - remove: 移除成员
     *          - join: 加入组队
     *          - list: 列出组队成员
     *          - summon: 召唤组队成员
     *          - revive: 复活组队成员
     *          - repair: 修理组队成员装备
     *          - level: 调整组队成员等级
     */
    std::vector<ChatCommand> GetCommands() const override
    {
        static std::vector<ChatCommand> groupSetCommandTable =
        {
            { "leader",     rbac::RBAC_PERM_COMMAND_GROUP_LEADER,     false, &HandleGroupLeaderCommand,     "" },
            { "assistant",  rbac::RBAC_PERM_COMMAND_GROUP_ASSISTANT,  false, &HandleGroupAssistantCommand,  "" },
            { "maintank",   rbac::RBAC_PERM_COMMAND_GROUP_MAINTANK,   false, &HandleGroupMainTankCommand,   "" },
            { "mainassist", rbac::RBAC_PERM_COMMAND_GROUP_MAINASSIST, false, &HandleGroupMainAssistCommand, "" }
        };

        static std::vector<ChatCommand> groupCommandTable =
        {
            { "set",     rbac::RBAC_PERM_COMMAND_GROUP_SET,       false, nullptr,                    "", groupSetCommandTable },
            { "leader",  rbac::RBAC_PERM_COMMAND_GROUP_LEADER,    false, &HandleGroupLeaderCommand,  "" },
            { "disband", rbac::RBAC_PERM_COMMAND_GROUP_DISBAND,   false, &HandleGroupDisbandCommand, "" },
            { "remove",  rbac::RBAC_PERM_COMMAND_GROUP_REMOVE,    false, &HandleGroupRemoveCommand,  "" },
            { "join",    rbac::RBAC_PERM_COMMAND_GROUP_JOIN,      false, &HandleGroupJoinCommand,    "" },
            { "list",    rbac::RBAC_PERM_COMMAND_GROUP_LIST,      false, &HandleGroupListCommand,    "" },
            { "summon",  rbac::RBAC_PERM_COMMAND_GROUP_SUMMON,    false, &HandleGroupSummonCommand,  "" },
            { "revive",  rbac::RBAC_PERM_COMMAND_REVIVE,          true,  &HandleGroupReviveCommand,  "" },
            { "repair",  rbac::RBAC_PERM_COMMAND_REPAIRITEMS,     true,  &HandleGroupRepairCommand,  "" },
            { "level",   rbac::RBAC_PERM_COMMAND_CHARACTER_LEVEL, true,  &HandleGroupLevelCommand,   "" }
        };

        static std::vector<ChatCommand> commandTable =
        {
            { "group", rbac::RBAC_PERM_COMMAND_GROUP, false, nullptr, "", groupCommandTable },
        };
        return commandTable;
    }

    /**
     * @brief 调整组队成员等级命令处理
     * @param handler 聊天命令处理器
     * @param player 可选的玩家标识，默认为目标或自己
     * @param level 目标等级
     * @return 成功返回 true，失败返回 false
     *
     * @details 将组队中所有成员的等级调整到指定值。
     *          - 等级必须大于等于1
     *          - 如果玩家未指定，默认使用目标或自己
     *          - 对组队中所有在线成员生效
     *          - 同时重置天赋和经验值
     *          - 通知玩家等级变化
     *
     * @note 调用时机：GM执行 .group level 命令时
     *       性能注意事项：遍历组队所有成员，逐个设置等级
     */
    static bool HandleGroupLevelCommand(ChatHandler* handler, Optional<PlayerIdentifier> player, int16 level)
    {
        // 验证等级有效性
        if (level < 1)
            return false;
        // 如果未指定玩家，使用目标或自己
        if (!player)
            player = PlayerIdentifier::FromTargetOrSelf(handler);
        if (!player)
            return false;

        Player* target = player->GetConnectedPlayer();
        if (!target)
            return false;

        // 获取玩家所在的组队
        Group* groupTarget = target->GetGroup();
        if (!groupTarget)
            return false;

        // 遍历组队所有成员，设置等级
        for (GroupReference* it = groupTarget->GetFirstMember(); it != nullptr; it = it->next())
        {
            target = it->GetSource();
            if (target)
            {
                uint8 oldlevel = static_cast<uint8>(target->GetLevel());

                // 仅在等级发生变化时处理
                if (level != oldlevel)
                {
                    target->SetLevel(static_cast<uint8>(level));
                    // 为新等级初始化天赋
                    target->InitTalentForLevel();
                    // 重置经验值为0
                    target->SetXP(0);
                }

                // 通知目标玩家等级变化
                if (handler->needReportToTarget(target))
                {
                    if (oldlevel < static_cast<uint8>(level))
                        ChatHandler(target->GetSession()).PSendSysMessage(LANG_YOURS_LEVEL_UP, handler->GetNameLink().c_str(), level);
                    else                                                // if (oldlevel > newlevel)
                        ChatHandler(target->GetSession()).PSendSysMessage(LANG_YOURS_LEVEL_DOWN, handler->GetNameLink().c_str(), level);
                }
            }
        }
        return true;
    }

    /**
     * @brief 复活组队成员命令处理
     * @param handler 聊天命令处理器
     * @param args 参数字符串，包含玩家名称
     * @return 成功返回 true，失败返回 false
     *
     * @details 复活组队中所有死亡的成员。
     *          - 复活生命值根据权限决定（满血或50%）
     *          - 生成骨头并移除尸体
     *          - 保存玩家状态到数据库
     *
     * @note 调用时机：GM执行 .group revive 命令时
     *       性能注意事项：遍历组队所有成员并保存到数据库
     */
    static bool HandleGroupReviveCommand(ChatHandler* handler, char const* args)
    {
        Player* playerTarget;
        // 提取目标玩家
        if (!handler->extractPlayerTarget((char*)args, &playerTarget))
            return false;

        Group* groupTarget = playerTarget->GetGroup();
        if (!groupTarget)
            return false;

        // 遍历组队所有成员，复活死亡玩家
        for (GroupReference* it = groupTarget->GetFirstMember(); it != nullptr; it = it->next())
        {
            Player* target = it->GetSource();
            if (target)
            {
                // 根据权限决定复活后的生命值百分比（满血或50%）
                target->ResurrectPlayer(target->GetSession()->HasPermission(rbac::RBAC_PERM_RESURRECT_WITH_FULL_HPS) ? 1.0f : 0.5f);
                // 生成骨头并移除尸体
                target->SpawnCorpseBones();
                // 保存到数据库
                target->SaveToDB();
            }
        }

        return true;
    }

    /**
     * @brief 修理组队成员装备命令处理
     * @param handler 聊天命令处理器
     * @param args 参数字符串，包含玩家名称
     * @return 成功返回 true，失败返回 false
     *
     * @details 修理组队中所有成员的装备耐久度。
     *          - 修理所有装备，不消耗金币
     *          - 仅对在线玩家生效
     *
     * @note 调用时机：GM执行 .group repair 命令时
     *       性能注意事项：遍历组队所有成员
     */
    // Repair group of players
    static bool HandleGroupRepairCommand(ChatHandler* handler, char const* args)
    {
        Player* playerTarget;
        // 提取目标玩家
        if (!handler->extractPlayerTarget((char*)args, &playerTarget))
            return false;

        Group* groupTarget = playerTarget->GetGroup();
        if (!groupTarget)
            return false;

        // 遍历组队所有成员，修理装备
        for (GroupReference* it = groupTarget->GetFirstMember(); it != nullptr; it = it->next())
        {
            Player* target = it->GetSource();
            if (target)
            {
                // 修理所有装备耐久度（不消耗金币）
                target->DurabilityRepairAll(false, 0, false);
            }
        }

        return true;
    }

    /**
     * @brief 召唤组队成员命令处理
     * @param handler 聊天命令处理器
     * @param args 参数字符串，包含玩家名称
     * @return 成功返回 true，失败返回 false
     *
     * @details 将组队中所有成员召唤到GM当前位置附近。
     *          - 验证权限，不能召唤权限更高的玩家
     *          - 处理副本召唤的特殊逻辑
     *          - 如果玩家在飞行中，会中断飞行
     *          - 保存玩家原始位置以便召回
     *
     * @note 调用时机：GM执行 .group summon 命令时
     *       性能注意事项：遍历组队所有成员，处理传送
     *       副本注意事项：
     *       - 副本间传送有限制
     *       - 队长不在同一副本时只能召唤本副本成员
     */
    // Summon group of player
    static bool HandleGroupSummonCommand(ChatHandler* handler, char const* args)
    {
        Player* target;
        // 提取目标玩家
        if (!handler->extractPlayerTarget((char*)args, &target))
            return false;

        // check online security
        // 检查在线安全权限，不能召唤权限更高的玩家
        if (handler->HasLowerSecurity(target, ObjectGuid::Empty))
            return false;

        Group* group = target->GetGroup();

        std::string nameLink = handler->GetNameLink(target);

        if (!group)
        {
            handler->PSendSysMessage(LANG_NOT_IN_GROUP, nameLink.c_str());
            return false;
        }

        Player* gmPlayer = handler->GetSession()->GetPlayer();
        Map* gmMap = gmPlayer->GetMap();
        bool toInstance = gmMap->Instanceable();
        bool onlyLocalSummon = false;

        // make sure people end up on our instance of the map, disallow far summon if intended destination is different from actual destination
        // note: we could probably relax this further by checking permanent saves and the like, but eh
        // :close enough:
        // 确保玩家到达我们的副本实例，如果预期目的地与实际目的地不同则禁止远距离召唤
        // 注意：可以通过检查永久存档等进一步放宽限制
        if (toInstance)
        {
            // 检查队长是否在GM所在副本
            Player* groupLeader = ObjectAccessor::GetPlayer(gmMap, group->GetLeaderGUID());
            if (!groupLeader || (groupLeader->GetMapId() != gmMap->GetId()) || (groupLeader->GetInstanceId() != gmMap->GetInstanceId()))
            {
                handler->SendSysMessage(LANG_PARTIAL_GROUP_SUMMON);
                onlyLocalSummon = true;
            }
        }

        // 遍历组队所有成员，执行召唤
        for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
        {
            Player* player = itr->GetSource();

            // 跳过无效玩家、GM自己和断线玩家
            if (!player || player == gmPlayer || !player->GetSession())
                continue;

            // check online security
            // 检查在线安全权限
            if (handler->HasLowerSecurity(player, ObjectGuid::Empty))
                continue;

            std::string plNameLink = handler->GetNameLink(player);

            // 如果玩家正在传送中，跳过
            if (player->IsBeingTeleported())
            {
                handler->PSendSysMessage(LANG_IS_TELEPORTED, plNameLink.c_str());
                continue;
            }

            // 处理副本召唤逻辑
            if (toInstance)
            {
                Map* playerMap = player->GetMap();

                if (
                    (onlyLocalSummon || (playerMap->Instanceable() && playerMap->GetId() == gmMap->GetId())) && // either no far summon allowed or we're in the same map as player (no map switch)
                    ((playerMap->GetId() != gmMap->GetId()) || (playerMap->GetInstanceId() != gmMap->GetInstanceId())) // so we need to be in the same map and instance of the map, otherwise skip
                    // 条件：要么不允许远距离召唤，要么与玩家在同一地图（无地图切换）
                    // 并且需要在相同的地图和副本实例中，否则跳过
                    )
                {
                    // cannot summon from instance to instance
                    // 不能从副本召唤到副本
                    handler->PSendSysMessage(LANG_CANNOT_SUMMON_INST_INST, plNameLink.c_str());
                    continue;
                }
            }

            // 发送召唤消息
            handler->PSendSysMessage(LANG_SUMMONING, plNameLink.c_str(), "");
            if (handler->needReportToTarget(player))
                ChatHandler(player->GetSession()).PSendSysMessage(LANG_SUMMONED_BY, handler->GetNameLink().c_str());

            // stop flight if need
            // 如果在飞行中，停止飞行；否则保存召回位置
            if (player->IsInFlight())
                player->FinishTaxiFlight();
            else
                player->SaveRecallPosition(); // save only in non-flight case

            // before GM
            // 在GM附近生成召唤位置
            float x, y, z;
            gmPlayer->GetClosePoint(x, y, z, player->GetCombatReach());
            player->TeleportTo(gmPlayer->GetMapId(), x, y, z, player->GetOrientation());
        }

        return true;
    }

    /**
     * @brief 设置组队队长命令处理
     * @param handler 聊天命令处理器
     * @param args 参数字符串，包含玩家名称
     * @return 成功返回 true，失败返回 false
     *
     * @details 将指定玩家设置为组队队长。
     *          - 玩家必须在组队中
     *          - 如果已经是队长则不做操作
     *          - 更改队长并发送组队更新
     *
     * @note 调用时机：GM执行 .group leader 命令时
     *       性能注意事项：发送组队更新数据包
     */
    static bool HandleGroupLeaderCommand(ChatHandler* handler, char const* args)
    {
        Player* player = nullptr;
        Group* group = nullptr;
        ObjectGuid guid;
        char* nameStr = strtok((char*)args, " ");

        // 获取玩家、组队和GUID信息
        if (!handler->GetPlayerGroupAndGUIDByName(nameStr, player, group, guid))
            return false;

        if (!group)
        {
            handler->PSendSysMessage(LANG_GROUP_NOT_IN_GROUP, player->GetName().c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 仅在当前队长不同时更改
        if (group->GetLeaderGUID() != guid)
        {
            group->ChangeLeader(guid);
            group->SendUpdate();
        }

        return true;
    }

    /**
     * @brief 组队标志命令通用处理函数
     * @param handler 聊天命令处理器
     * @param args 参数字符串，包含玩家名称
     * @param flag 要设置的成员标志
     * @param what 标志名称描述
     * @return 成功返回 true，失败返回 false
     *
     * @details 设置或取消组队成员的标志（助手、主坦、主助手）。
     *          - 玩家必须在团队中
     *          - 队长不能设置为助手
     *          - 切换标志状态并通知
     *
     * @note 调用时机：由 HandleGroupAssistantCommand、HandleGroupMainTankCommand、HandleGroupMainAssistCommand 调用
     *       性能注意事项：发送组队更新数据包
     */
    static bool GroupFlagCommand(ChatHandler* handler, char const* args, GroupMemberFlags flag, char const* what)
    {
        Player* player = nullptr;
        Group* group = nullptr;
        ObjectGuid guid;
        char* nameStr = strtok((char*)args, " ");

        // 获取玩家、组队和GUID信息
        if (!handler->GetPlayerGroupAndGUIDByName(nameStr, player, group, guid))
            return false;

        if (!group)
        {
            handler->PSendSysMessage(LANG_GROUP_NOT_IN_GROUP, player->GetName().c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 只有团队才能设置这些标志
        if (!group->isRaidGroup())
        {
            handler->PSendSysMessage(LANG_GROUP_NOT_IN_RAID_GROUP, player->GetName().c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 队长不能设置为助手
        if (flag == MEMBER_FLAG_ASSISTANT && group->IsLeader(guid))
        {
            handler->PSendSysMessage(LANG_LEADER_CANNOT_BE_ASSISTANT, player->GetName().c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 切换标志状态
        if (group->GetMemberFlags(guid) & flag)
        {
            // 已有标志，取消它
            group->SetGroupMemberFlag(guid, false, flag);
            handler->PSendSysMessage(LANG_GROUP_ROLE_CHANGED, player->GetName().c_str(), "no longer", what);
        }
        else
        {
            // 没有标志，设置它
            group->SetGroupMemberFlag(guid, true, flag);
            handler->PSendSysMessage(LANG_GROUP_ROLE_CHANGED, player->GetName().c_str(), "now", what);
        }
        return true;
    }

    /**
     * @brief 设置组队助手命令处理
     * @param handler 聊天命令处理器
     * @param args 参数字符串，包含玩家名称
     * @return 成功返回 true，失败返回 false
     *
     * @details 设置或取消指定玩家为组队助手。
     *          助手可以邀请成员和调整 loot 设置。
     *
     * @note 调用时机：GM执行 .group set assistant 命令时
     */
    static bool HandleGroupAssistantCommand(ChatHandler* handler, char const* args)
    {
        return GroupFlagCommand(handler, args, MEMBER_FLAG_ASSISTANT, "Assistant");
    }

    /**
     * @brief 设置组队主坦命令处理
     * @param handler 聊天命令处理器
     * @param args 参数字符串，包含玩家名称
     * @return 成功返回 true，失败返回 false
     *
     * @details 设置或取消指定玩家为组队主坦。
     *          主坦会在团队框架中有特殊标记。
     *
     * @note 调用时机：GM执行 .group set maintank 命令时
     */
    static bool HandleGroupMainTankCommand(ChatHandler* handler, char const* args)
    {
        return GroupFlagCommand(handler, args, MEMBER_FLAG_MAINTANK, "Main Tank");
    }

    /**
     * @brief 设置组队主助手命令处理
     * @param handler 聊天命令处理器
     * @param args 参数字符串，包含玩家名称
     * @return 成功返回 true，失败返回 false
     *
     * @details 设置或取消指定玩家为组队主助手。
     *          主助手会在团队框架中有特殊标记。
     *
     * @note 调用时机：GM执行 .group set mainassist 命令时
     */
    static bool HandleGroupMainAssistCommand(ChatHandler* handler, char const* args)
    {
        return GroupFlagCommand(handler, args, MEMBER_FLAG_MAINASSIST, "Main Assist");
    }

    /**
     * @brief 解散组队命令处理
     * @param handler 聊天命令处理器
     * @param args 参数字符串，包含玩家名称
     * @return 成功返回 true，失败返回 false
     *
     * @details 解散指定玩家所在的组队。
     *          - 解散后所有成员将离开组队
     *          - 组队数据将被清除
     *
     * @note 调用时机：GM执行 .group disband 命令时
     *       性能注意事项：清理组队数据，通知所有成员
     */
    static bool HandleGroupDisbandCommand(ChatHandler* handler, char const* args)
    {
        Player* player = nullptr;
        Group* group = nullptr;
        ObjectGuid guid;
        char* nameStr = strtok((char*)args, " ");

        // 获取玩家、组队和GUID信息
        if (!handler->GetPlayerGroupAndGUIDByName(nameStr, player, group, guid))
            return false;

        if (!group)
        {
            handler->PSendSysMessage(LANG_GROUP_NOT_IN_GROUP, player->GetName().c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 解散组队
        group->Disband();
        return true;
    }

    /**
     * @brief 从组队移除成员命令处理
     * @param handler 聊天命令处理器
     * @param args 参数字符串，包含玩家名称
     * @return 成功返回 true，失败返回 false
     *
     * @details 将指定玩家从组队中移除。
     *          - 玩家必须在组队中
     *          - 移除后通知其他成员
     *
     * @note 调用时机：GM执行 .group remove 命令时
     *       性能注意事项：发送组队更新数据包
     */
    static bool HandleGroupRemoveCommand(ChatHandler* handler, char const* args)
    {
        Player* player = nullptr;
        Group* group = nullptr;
        ObjectGuid guid;
        char* nameStr = strtok((char*)args, " ");

        // 获取玩家、组队和GUID信息
        if (!handler->GetPlayerGroupAndGUIDByName(nameStr, player, group, guid))
            return false;

        if (!group)
        {
            handler->PSendSysMessage(LANG_GROUP_NOT_IN_GROUP, player->GetName().c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 从组队移除指定成员
        group->RemoveMember(guid);
        return true;
    }

    /**
     * @brief 加入组队命令处理
     * @param handler 聊天命令处理器
     * @param args 参数字符串，包含组队玩家名称和要加入的玩家名称
     * @return 成功返回 true，失败返回 false
     *
     * @details 将指定玩家加入到一个已存在的组队中。
     *          - 第一个参数：组队中的任意成员名称
     *          - 第二个参数：要加入的玩家名称
     *          - 目标玩家不能已在其他组队中
     *          - 组队不能已满
     *
     * @note 调用时机：GM执行 .group join 命令时
     *       性能注意事项：发送组队更新数据包
     */
    static bool HandleGroupJoinCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        Player* playerSource = nullptr;
        Player* playerTarget = nullptr;
        Group* groupSource = nullptr;
        Group* groupTarget = nullptr;
        ObjectGuid guidSource;
        ObjectGuid guidTarget;
        char* nameplgrStr = strtok((char*)args, " ");
        char* nameplStr = strtok(nullptr, " ");

        // 获取源玩家（组队成员）信息
        if (!handler->GetPlayerGroupAndGUIDByName(nameplgrStr, playerSource, groupSource, guidSource, true))
            return false;

        if (!groupSource)
        {
            handler->PSendSysMessage(LANG_GROUP_NOT_IN_GROUP, playerSource->GetName().c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 获取目标玩家信息
        if (!handler->GetPlayerGroupAndGUIDByName(nameplStr, playerTarget, groupTarget, guidTarget, true))
            return false;

        // 检查目标玩家是否已在组队中
        if (groupTarget || playerTarget->GetGroup() == groupSource)
        {
            handler->PSendSysMessage(LANG_GROUP_ALREADY_IN_GROUP, playerTarget->GetName().c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 检查组队是否已满
        if (groupSource->IsFull())
        {
            handler->PSendSysMessage(LANG_GROUP_FULL);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 将目标玩家加入组队
        groupSource->AddMember(playerTarget);
        // 广播组队更新
        groupSource->BroadcastGroupUpdate();
        handler->PSendSysMessage(LANG_GROUP_PLAYER_JOINED, playerTarget->GetName().c_str(), playerSource->GetName().c_str());
        return true;
    }

    /**
     * @brief 列出组队成员命令处理
     * @param handler 聊天命令处理器
     * @param target 目标玩家标识
     * @return 成功返回 true，失败返回 false
     *
     * @details 显示指定玩家所在组队的所有成员信息。
     *          - 显示组队类型（小队/团队）和成员数量
     *          - 显示每个成员的名称、在线状态、区域、相位、标志和角色
     *          - 对于离线玩家，从数据库查询组队信息
     *
     * @note 调用时机：GM执行 .group list 命令时
     *       性能注意事项：如果目标离线，需要查询数据库
     */
    static bool HandleGroupListCommand(ChatHandler* handler, PlayerIdentifier const& target)
    {
        char const* zoneName = "<ERROR>";
        char const* onlineState = "Offline";

        // Next, we need a group. So we define a group variable.
        // 定义组队变量
        Group* groupTarget = nullptr;

        // We try to extract a group from an online player.
        // 尝试从在线玩家获取组队
        if (target.IsConnected())
            groupTarget = target.GetConnectedPlayer()->GetGroup();
        else
        {
            // If not, we extract it from the SQL.
            // 如果玩家离线，从数据库查询组队信息
            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_GROUP_MEMBER);
            stmt->setUInt32(0, target.GetGUID().GetCounter());
            PreparedQueryResult resultGroup = CharacterDatabase.Query(stmt);
            if (resultGroup)
                groupTarget = sGroupMgr->GetGroupByDbStoreId((*resultGroup)[0].GetUInt32());
        }

        // If both fails, players simply has no party. Return false.
        // 如果两种方式都失败，玩家没有组队
        if (!groupTarget)
        {
            handler->PSendSysMessage(LANG_GROUP_NOT_IN_GROUP, target.GetName().c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }

        // We get the group members after successfully detecting a group.
        // 获取组队成员列表
        Group::MemberSlotList const& members = groupTarget->GetMemberSlots();

        // To avoid a cluster fuck, namely trying multiple queries to simply get a group member count...
        // 显示组队类型和成员数量
        handler->PSendSysMessage(LANG_GROUP_TYPE, (groupTarget->isRaidGroup() ? "raid" : "party"), members.size());
        // ... we simply move the group type and member count print after retrieving the slots and simply output it's size.

        // While rather dirty codestyle-wise, it saves space (if only a little). For each member, we look several informations up.
        // 遍历所有成员，显示详细信息
        for (Group::MemberSlotList::const_iterator itr = members.begin(); itr != members.end(); ++itr)
        {
            // Define temporary variable slot to iterator.
            // 获取成员槽位信息
            Group::MemberSlot const& slot = *itr;

            // Check for given flag and assign it to that iterator
            // 检查并组装成员标志字符串
            std::string flags;
            if (slot.flags & MEMBER_FLAG_ASSISTANT)
                flags = "Assistant";

            if (slot.flags & MEMBER_FLAG_MAINTANK)
            {
                if (!flags.empty())
                    flags.append(", ");
                flags.append("MainTank");
            }

            if (slot.flags & MEMBER_FLAG_MAINASSIST)
            {
                if (!flags.empty())
                    flags.append(", ");
                flags.append("MainAssist");
            }

            if (flags.empty())
                flags = "None";

            // Check if iterator is online. If is...
            // 检查成员是否在线，获取详细信息
            Player* p = ObjectAccessor::FindPlayer((*itr).guid);
            uint32 phase = 0;
            if (p)
            {
                // ... than, it prints information like "is online", where he is, etc...
                // 成员在线，获取区域和相位信息
                onlineState = "online";
                phase = (!p->IsGameMaster() ? p->GetPhaseMask() : -1);
                uint32 locale = handler->GetSessionDbcLocale();

                // 获取区域名称
                AreaTableEntry const* area = sAreaTableStore.LookupEntry(p->GetAreaId());
                if (area)
                {
                    AreaTableEntry const* zone = sAreaTableStore.LookupEntry(area->ParentAreaID);
                    if (zone)
                        zoneName = zone->AreaName[locale];
                }
            }

            // Now we can print those informations for every single member of each group!
            // 输出成员信息
            handler->PSendSysMessage(LANG_GROUP_PLAYER_NAME_GUID, slot.name.c_str(), onlineState,
                zoneName, phase, slot.guid.GetCounter(), flags.c_str(),
                lfg::GetRolesString(slot.roles).c_str());
        }

        // And finish after every iterator is done.
        return true;
    }
};

/**
 * @brief 注册组队命令脚本
 *
 * @details 创建并注册组队命令脚本实例到脚本系统中。
 *          该函数在服务器启动时被调用，用于初始化所有组队相关命令。
 */
void AddSC_group_commandscript()
{
    new group_commandscript();
}
