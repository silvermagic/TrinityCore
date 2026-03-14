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
Name: instance_commandscript
%Complete: 100
Comment: All instance related commands
Category: commandscripts
EndScriptData */

/**
 * @file cs_instance.cpp
 * @brief 副本管理命令模块
 *
 * 本模块提供了与副本（Instance）相关的GM命令，用于管理和查询玩家的副本绑定信息。
 * 主要功能包括：
 * - 列出玩家和团队的副本绑定信息
 * - 解除玩家的副本绑定
 * - 查询副本统计信息
 * - 手动保存副本数据
 * - 设置和查询Boss战斗状态
 *
 * 这些命令主要用于GM调试和管理服务器副本状态。
 */

#include "ScriptMgr.h"
#include "Chat.h"
#include "GameTime.h"
#include "Group.h"
#include "InstanceSaveMgr.h"
#include "InstanceScript.h"
#include "Language.h"
#include "MapManager.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "RBAC.h"
#include "WorldSession.h"

using namespace Trinity::ChatCommands;

/**
 * @class instance_commandscript
 * @brief 副本命令脚本类
 *
 * 继承自CommandScript，提供所有与副本管理相关的GM命令。
 * 该类注册并实现了副本绑定、统计、Boss状态管理等命令的处理函数。
 */
class instance_commandscript : public CommandScript
{
public:
    /**
     * @brief 构造函数
     *
     * 初始化命令脚本，设置脚本名称为"instance_commandscript"
     */
    instance_commandscript() : CommandScript("instance_commandscript") { }

    /**
     * @brief 获取命令表
     * @return 返回命令表的映射关系
     *
     * 注册所有副本相关命令及其对应的处理函数和权限要求。
     * 包括：
     * - listbinds: 列出副本绑定
     * - unbind: 解除副本绑定
     * - stats: 副本统计信息
     * - savedata: 保存副本数据
     * - setbossstate: 设置Boss状态
     * - getbossstate: 获取Boss状态
     */
    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable instanceCommandTable =
        {
            { "listbinds",    HandleInstanceListBindsCommand,    rbac::RBAC_PERM_COMMAND_INSTANCE_LISTBINDS,      Console::No },
            { "unbind",       HandleInstanceUnbindCommand,       rbac::RBAC_PERM_COMMAND_INSTANCE_UNBIND,         Console::No },
            { "stats",        HandleInstanceStatsCommand,        rbac::RBAC_PERM_COMMAND_INSTANCE_STATS,          Console::Yes },
            { "savedata",     HandleInstanceSaveDataCommand,     rbac::RBAC_PERM_COMMAND_INSTANCE_SAVEDATA,       Console::No },
            { "setbossstate", HandleInstanceSetBossStateCommand, rbac::RBAC_PERM_COMMAND_INSTANCE_SET_BOSS_STATE, Console::Yes },
            { "getbossstate", HandleInstanceGetBossStateCommand, rbac::RBAC_PERM_COMMAND_INSTANCE_GET_BOSS_STATE, Console::Yes },
        };

        static ChatCommandTable commandTable =
        {
            { "instance", instanceCommandTable },
        };

        return commandTable;
    }

    /**
     * @brief 处理列出副本绑定命令
     * @param handler 聊天命令处理器
     * @return 命令执行成功返回true，否则返回false
     *
     * 显示目标玩家及其所在团队的所有副本绑定信息。
     * 包括：
     * - 地图ID、副本实例ID
     * - 是否永久绑定、是否已过期、是否延长
     * - 难度等级、是否可重置
     * - 重置剩余时间
     *
     * 调用时机：GM执行 .instance listbinds 命令时
     * 性能注意事项：遍历所有难度的副本绑定，大团本可能有较多绑定数据
     */
    static bool HandleInstanceListBindsCommand(ChatHandler* handler)
    {
        // 获取目标玩家，如果未选中则使用自己
        Player* player = handler->getSelectedPlayer();
        if (!player)
            player = handler->GetSession()->GetPlayer();

        // 统计并显示玩家个人的副本绑定
        uint32 counter = 0;
        for (uint8 i = 0; i < MAX_DIFFICULTY; ++i)
        {
            // 遍历该难度下的所有副本绑定
            for (auto const& [mapId, bind] : player->GetBoundInstances(Difficulty(i)))
            {
                InstanceSave const* save = bind.save;
                // 计算剩余时间
                std::string timeleft = secsToTimeString(save->GetResetTime() - GameTime::GetGameTime(), TimeFormat::ShortText);
                // 显示绑定详情：地图ID、实例ID、永久性、过期状态、难度、可重置性、剩余时间
                handler->PSendSysMessage(LANG_COMMAND_LIST_BIND_INFO, mapId, save->GetInstanceId(), bind.perm ? "yes" : "no", bind.extendState == EXTEND_STATE_EXPIRED ? "expired" : bind.extendState == EXTEND_STATE_EXTENDED ? "yes" : "no", save->GetDifficulty(), save->CanReset() ? "yes" : "no", timeleft.c_str());
                counter++;
            }
        }
        handler->PSendSysMessage(LANG_COMMAND_LIST_BIND_PLAYER_BINDS, counter);

        // 统计并显示团队的副本绑定
        counter = 0;
        if (Group* group = player->GetGroup())
        {
            for (uint8 i = 0; i < MAX_DIFFICULTY; ++i)
            {
                // 遍历团队在该难度下的所有副本绑定
                for (auto const& [mapId, bind] : group->GetBoundInstances(Difficulty(i)))
                {
                    InstanceSave* save = bind.save;
                    std::string timeleft = secsToTimeString(save->GetResetTime() - GameTime::GetGameTime(), TimeFormat::ShortText);
                    // 团队绑定不显示延长状态（显示为"-"）
                    handler->PSendSysMessage(LANG_COMMAND_LIST_BIND_INFO, mapId, save->GetInstanceId(), bind.perm ? "yes" : "no", "-", save->GetDifficulty(), save->CanReset() ? "yes" : "no", timeleft.c_str());
                    counter++;
                }
            }
        }
        handler->PSendSysMessage(LANG_COMMAND_LIST_BIND_GROUP_BINDS, counter);

        return true;
    }

    /**
     * @brief 处理解除副本绑定命令
     * @param handler 聊天命令处理器
     * @param mapArg 地图ID或"all"（解除所有绑定）
     * @param difficultyArg 可选的难度参数，用于筛选特定难度的绑定
     * @return 命令执行成功返回true，否则返回false
     *
     * 解除目标玩家的指定副本绑定。
     * 注意：不会解除玩家当前所在地图的绑定。
     *
     * 调用时机：GM执行 .instance unbind <mapId|all> [difficulty] 命令时
     * 性能注意事项：会修改玩家绑定数据，需要遍历所有难度
     */
    static bool HandleInstanceUnbindCommand(ChatHandler* handler, Variant<uint16, EXACT_SEQUENCE("all")> mapArg, Optional<uint8> difficultyArg)
    {
        // 获取目标玩家
        Player* player = handler->getSelectedPlayer();
        if (!player)
            player = handler->GetSession()->GetPlayer();

        uint16 counter = 0;
        uint16 mapId = 0;

        // 解析地图参数
        if (mapArg.holds_alternative<uint16>())
        {
            mapId = mapArg.get<uint16>();
            if (!mapId)
                return false;
        }

        // 遍历所有难度，解除符合条件的绑定
        for (uint8 i = 0; i < MAX_DIFFICULTY; ++i)
        {
            Player::BoundInstancesMap& binds = player->GetBoundInstances(Difficulty(i));
            for (Player::BoundInstancesMap::iterator itr = binds.begin(); itr != binds.end();)
            {
                InstanceSave const* save = itr->second.save;
                // 检查是否符合解除条件：
                // 1. 不是当前所在地图
                // 2. 地图ID匹配（如果指定了）或解除所有（mapId=0表示all）
                // 3. 难度匹配（如果指定了）
                if (itr->first != player->GetMapId() && (!mapId || mapId == itr->first) && (!difficultyArg || difficultyArg == save->GetDifficulty()))
                {
                    std::string timeleft = secsToTimeString(save->GetResetTime() - GameTime::GetGameTime(), TimeFormat::ShortText);
                    handler->PSendSysMessage(LANG_COMMAND_INST_UNBIND_UNBINDING, itr->first, save->GetInstanceId(), itr->second.perm ? "yes" : "no", save->GetDifficulty(), save->CanReset() ? "yes" : "no", timeleft.c_str());
                    // 解除绑定并删除迭代器（UnbindInstance会处理迭代器）
                    player->UnbindInstance(itr, Difficulty(i));
                    counter++;
                }
                else
                    ++itr;
            }
        }
        handler->PSendSysMessage(LANG_COMMAND_INST_UNBIND_UNBOUND, counter);

        return true;
    }

    /**
     * @brief 处理副本统计命令
     * @param handler 聊天命令处理器
     * @return 始终返回true
     *
     * 显示服务器当前的副本统计信息，包括：
     * - 已加载的副本实例数量
     * - 在副本中的玩家数量
     * - 副本存档数量
     * - 绑定副本的玩家总数
     * - 绑定副本的团队总数
     *
     * 调用时机：GM执行 .instance stats 命令时
     * 性能注意事项：控制台命令，仅查询全局管理器数据，性能开销小
     */
    static bool HandleInstanceStatsCommand(ChatHandler* handler)
    {
        handler->PSendSysMessage(LANG_COMMAND_INST_STAT_LOADED_INST, sMapMgr->GetNumInstances());
        handler->PSendSysMessage(LANG_COMMAND_INST_STAT_PLAYERS_IN, sMapMgr->GetNumPlayersInInstances());
        handler->PSendSysMessage(LANG_COMMAND_INST_STAT_SAVES, sInstanceSaveMgr->GetNumInstanceSaves());
        handler->PSendSysMessage(LANG_COMMAND_INST_STAT_PLAYERSBOUND, sInstanceSaveMgr->GetNumBoundPlayersTotal());
        handler->PSendSysMessage(LANG_COMMAND_INST_STAT_GROUPSBOUND, sInstanceSaveMgr->GetNumBoundGroupsTotal());

        return true;
    }

    /**
     * @brief 处理保存副本数据命令
     * @param handler 聊天命令处理器
     * @return 成功返回true，失败返回false
     *
     * 强制保存当前副本的数据到数据库。
     * 仅在副本内有效，且副本必须有InstanceScript。
     *
     * 调用时机：GM执行 .instance savedata 命令时
     * 性能注意事项：会触发数据库写操作
     */
    static bool HandleInstanceSaveDataCommand(ChatHandler* handler)
    {
        Player* player = handler->GetSession()->GetPlayer();
        // 获取当前所在的副本地图
        InstanceMap* map = player->GetMap()->ToInstanceMap();
        if (!map)
        {
            handler->PSendSysMessage(LANG_NOT_DUNGEON);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 检查是否有副本脚本
        if (!map->GetInstanceScript())
        {
            handler->PSendSysMessage(LANG_NO_INSTANCE_DATA);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 保存副本数据到数据库
        map->GetInstanceScript()->SaveToDB();

        return true;
    }

    /**
     * @brief 处理设置Boss状态命令
     * @param handler 聊天命令处理器
     * @param encounterId Boss遭遇战ID
     * @param state 要设置的状态（如InProgress, Done, Fail等）
     * @param player 可选的目标玩家标识
     * @return 成功返回true，失败返回false
     *
     * 设置副本中指定Boss的战斗状态。主要用于调试和测试。
     * 控制台执行时必须指定玩家参数。
     *
     * 调用时机：GM执行 .instance setbossstate <encounterId> <state> [playerName] 命令时
     * 性能注意事项：轻量级操作，仅修改内存状态
     */
    static bool HandleInstanceSetBossStateCommand(ChatHandler* handler, uint32 encounterId, EncounterState state, Optional<PlayerIdentifier> player)
    {
        // 控制台执行时必须提供角色名称
        if (!player && !handler->GetSession())
        {
            handler->PSendSysMessage(LANG_CMD_SYNTAX);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 如果未指定玩家，使用自己或目标
        if (!player)
            player = PlayerIdentifier::FromSelf(handler);

        if (!player->IsConnected())
        {
            handler->PSendSysMessage(LANG_PLAYER_NOT_FOUND);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 获取玩家所在的副本地图
        InstanceMap* map = player->GetConnectedPlayer()->GetMap()->ToInstanceMap();
        if (!map)
        {
            handler->PSendSysMessage(LANG_NOT_DUNGEON);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 检查是否有副本脚本
        if (!map->GetInstanceScript())
        {
            handler->PSendSysMessage(LANG_NO_INSTANCE_DATA);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 验证遭遇战ID是否有效
        if (encounterId > map->GetInstanceScript()->GetEncounterCount())
        {
            handler->PSendSysMessage(LANG_BAD_VALUE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 设置Boss状态
        map->GetInstanceScript()->SetBossState(encounterId, state);
        handler->PSendSysMessage(LANG_COMMAND_INST_SET_BOSS_STATE, encounterId, state, EnumUtils::ToConstant(state));
        return true;
    }

    /**
     * @brief 处理获取Boss状态命令
     * @param handler 聊天命令处理器
     * @param encounterId Boss遭遇战ID
     * @param player 可选的目标玩家标识
     * @return 成功返回true，失败返回false
     *
     * 查询副本中指定Boss的当前战斗状态。
     * 控制台执行时必须指定玩家参数。
     *
     * 调用时机：GM执行 .instance getbossstate <encounterId> [playerName] 命令时
     * 性能注意事项：轻量级只读操作
     */
    static bool HandleInstanceGetBossStateCommand(ChatHandler* handler, uint32 encounterId, Optional<PlayerIdentifier> player)
    {
        // 控制台执行时必须提供角色名称
        if (!player && !handler->GetSession())
        {
            handler->PSendSysMessage(LANG_CMD_SYNTAX);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 如果未指定玩家，使用自己或目标
        if (!player)
            player = PlayerIdentifier::FromSelf(handler);

        if (!player->IsConnected())
        {
            handler->PSendSysMessage(LANG_PLAYER_NOT_FOUND);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 获取玩家所在的副本地图
        InstanceMap* map = player->GetConnectedPlayer()->GetMap()->ToInstanceMap();
        if (!map)
        {
            handler->PSendSysMessage(LANG_NOT_DUNGEON);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 检查是否有副本脚本
        if (!map->GetInstanceScript())
        {
            handler->PSendSysMessage(LANG_NO_INSTANCE_DATA);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 验证遭遇战ID是否有效
        if (encounterId > map->GetInstanceScript()->GetEncounterCount())
        {
            handler->PSendSysMessage(LANG_BAD_VALUE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 获取并显示Boss状态
        EncounterState state = map->GetInstanceScript()->GetBossState(encounterId);
        handler->PSendSysMessage(LANG_COMMAND_INST_GET_BOSS_STATE, encounterId, state, EnumUtils::ToConstant(state));
        return true;
    }
};

/**
 * @brief 注册副本命令脚本
 *
 * 创建并注册instance_commandscript实例到脚本系统。
 * 该函数在服务器启动时被脚本系统调用。
 */
void AddSC_instance_commandscript()
{
    new instance_commandscript();
}
