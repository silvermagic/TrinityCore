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
 * @file cs_arena.cpp
 * @brief 竞技场战队管理命令模块
 *
 * 本模块实现了竞技场战队管理相关的所有命令，提供GM管理竞技场战队的功能。
 * 主要功能包括：
 * - 创建竞技场战队
 * - 解散竞技场战队
 * - 重命名竞技场战队
 * - 设置竞技场战队队长
 * - 查询竞技场战队信息
 * - 搜索竞技场战队
 *
 * 竞技场系统说明：
 * - 竞技场是PvP系统的重要组成部分
 * - 战队类型包括：2v2、3v3、5v5
 * - 战队有等级制度和评分系统
 * - 战队队长可以管理成员和战队设置
 *
 * 命令层次结构：
 * - .arena create: 创建竞技场战队
 * - .arena disband: 解散竞技场战队
 * - .arena rename: 重命名竞技场战队
 * - .arena captain: 设置战队队长
 * - .arena info: 查询战队信息
 * - .arena lookup: 搜索战队
 */

/* ScriptData
Name: arena_commandscript
%Complete: 100
Comment: All arena team related commands
Category: commandscripts
EndScriptData */

#include "ScriptMgr.h"
#include "ArenaTeamMgr.h"
#include "CharacterCache.h"
#include "Chat.h"
#include "Language.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "RBAC.h"
#include "WorldSession.h"

using namespace Trinity::ChatCommands;

/**
 * @class arena_commandscript
 * @brief 竞技场战队命令脚本类
 *
 * 继承自 CommandScript 基类，实现竞技场战队管理相关的所有命令。
 * 该类提供了创建、解散、管理和查询竞技场战队的完整接口。
 */
class arena_commandscript : public CommandScript
{
public:
    /**
     * @brief 构造函数
     *
     * 初始化竞技场战队命令脚本，设置脚本名称为 "arena_commandscript"
     */
    arena_commandscript() : CommandScript("arena_commandscript") { }

    /**
     * @brief 获取命令表
     *
     * 注册所有竞技场战队相关的命令及其权限要求。
     *
     * @return ChatCommandTable 返回命令表，包含所有注册的竞技场命令
     *
     * 命令结构：
     * - arena create: 创建竞技场战队（控制台可用）
     * - arena disband: 解散竞技场战队（控制台可用）
     * - arena rename: 重命名竞技场战队（控制台可用）
     * - arena captain: 设置战队队长（仅游戏内）
     * - arena info: 查询战队信息（控制台可用）
     * - arena lookup: 搜索战队（仅游戏内）
     */
    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable arenaCommandTable =
        {
            { "create",         HandleArenaCreateCommand,   rbac::RBAC_PERM_COMMAND_ARENA_CREATE,  Console::Yes },
            { "disband",        HandleArenaDisbandCommand,  rbac::RBAC_PERM_COMMAND_ARENA_DISBAND, Console::Yes },
            { "rename",         HandleArenaRenameCommand,   rbac::RBAC_PERM_COMMAND_ARENA_RENAME,  Console::Yes },
            { "captain",        HandleArenaCaptainCommand,  rbac::RBAC_PERM_COMMAND_ARENA_CAPTAIN, Console::No },
            { "info",           HandleArenaInfoCommand,     rbac::RBAC_PERM_COMMAND_ARENA_INFO,    Console::Yes },
            { "lookup",         HandleArenaLookupCommand,   rbac::RBAC_PERM_COMMAND_ARENA_LOOKUP,  Console::No },
        };
        static ChatCommandTable commandTable =
        {
            { "arena", arenaCommandTable },
        };
        return commandTable;
    }

    /**
     * @brief 创建竞技场战队
     *
     * 创建一个新的竞技场战队，指定战队名称、类型和队长。
     *
     * @param handler 聊天命令处理器
     * @param captain 可选参数，战队队长标识符（如未指定则使用选中玩家或自己）
     * @param name 战队名称（带引号的字符串）
     * @param type 竞技场类型（2v2、3v3、5v5）
     *
     * @return bool 创建成功返回 true，失败返回 false
     *
     * @details 执行流程：
     * 1. 检查战队名称是否已存在
     * 2. 解析队长信息（如果未指定则使用目标或自己）
     * 3. 检查该玩家是否已拥有同类型的竞技场战队
     * 4. 创建新的竞技场战队对象
     * 5. 调用 Create 方法初始化战队数据
     * 6. 将战队添加到管理器中
     *
     * 创建参数说明：
     * - 战队背景图标、边框、颜色等使用默认值
     * - 战队初始评分为0
     *
     * 错误情况：
     * - 战队名称已存在
     * - 玩家已拥有同类型战队
     * - 创建失败（无效参数）
     *
     * 调用时机：管理员执行 .arena create 命令时
     *
     * @example
     * .arena create playername "Team Name" 2  // 创建2v2战队
     * .arena create "Team Name" 3  // 为选中玩家创建3v3战队
     */
    static bool HandleArenaCreateCommand(ChatHandler* handler, Optional<PlayerIdentifier> captain, QuotedString name, ArenaTeamTypes type)
    {
        // 检查战队名称是否已存在
        if (sArenaTeamMgr->GetArenaTeamByName(name))
        {
            handler->PSendSysMessage(LANG_ARENA_ERROR_NAME_EXISTS, name);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 如果没有指定队长，使用选中的目标或自己
        if (!captain)
            captain = PlayerIdentifier::FromTargetOrSelf(handler);
        if (!captain)
            return false;

        // 检查玩家是否已拥有同类型的竞技场战队
        if (sCharacterCache->GetCharacterArenaTeamIdByGuid(captain->GetGUID(), type) != 0)
        {
            handler->PSendSysMessage(LANG_ARENA_ERROR_SIZE, captain->GetName().c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 创建新的竞技场战队对象
        ArenaTeam* arena = new ArenaTeam();

        // 初始化战队数据（使用默认的背景、图标、边框等）
        if (!arena->Create(captain->GetGUID(), type, name, 4293102085, 101, 4293253939, 4, 4284049911))
        {
            // 创建失败，删除对象并返回错误
            delete arena;
            handler->SendSysMessage(LANG_BAD_VALUE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 将战队添加到管理器
        sArenaTeamMgr->AddArenaTeam(arena);
        handler->PSendSysMessage(LANG_ARENA_CREATE, arena->GetName().c_str(), arena->GetId(), arena->GetType(), arena->GetCaptain().GetCounter());

        return true;
    }

    /**
     * @brief 解散竞技场战队
     *
     * 解散指定的竞技场战队，删除所有战队数据。
     *
     * @param handler 聊天命令处理器
     * @param teamId 竞技场战队ID
     *
     * @return bool 解散成功返回 true，失败返回 false
     *
     * @details 执行流程：
     * 1. 根据ID查找竞技场战队
     * 2. 检查战队是否存在
     * 3. 检查战队是否正在比赛中
     * 4. 调用 Disband 方法解散战队
     * 5. 删除战队对象
     *
     * 安全机制：
     * - 不能解散正在比赛中的战队
     * - 解散操作不可逆，会删除所有战队数据
     *
     * 调用时机：管理员执行 .arena disband 命令时
     */
    static bool HandleArenaDisbandCommand(ChatHandler* handler, uint32 teamId)
    {
        // 根据ID查找竞技场战队
        ArenaTeam* arena = sArenaTeamMgr->GetArenaTeamById(teamId);

        if (!arena)
        {
            handler->PSendSysMessage(LANG_ARENA_ERROR_NOT_FOUND, teamId);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 检查战队是否正在比赛中
        if (arena->IsFighting())
        {
            handler->SendSysMessage(LANG_ARENA_ERROR_COMBAT);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 保存战队名称用于显示
        std::string name = arena->GetName();
        // 解散战队并删除对象
        arena->Disband();
        delete arena;

        handler->PSendSysMessage(LANG_ARENA_DISBAND, name.c_str(), teamId);
        return true;
    }

    /**
     * @brief 重命名竞技场战队
     *
     * 修改竞技场战队的名称。
     *
     * @param handler 聊天命令处理器
     * @param oldName 旧战队名称（带引号的字符串）
     * @param newName 新战队名称（带引号的字符串）
     *
     * @return bool 重命名成功返回 true，失败返回 false
     *
     * @details 执行流程：
     * 1. 根据旧名称查找竞技场战队
     * 2. 检查战队是否存在
     * 3. 检查新名称是否已被使用
     * 4. 检查战队是否正在比赛中
     * 5. 调用 SetName 方法修改战队名称
     *
     * 错误情况：
     * - 旧名称不存在
     * - 新名称已存在
     * - 战队正在比赛中
     * - 新名称无效
     *
     * 调用时机：管理员执行 .arena rename 命令时
     */
    static bool HandleArenaRenameCommand(ChatHandler* handler, QuotedString oldName, QuotedString newName)
    {
        // 根据旧名称查找竞技场战队
        ArenaTeam* arena = sArenaTeamMgr->GetArenaTeamByName(oldName);
        if (!arena)
        {
            handler->PSendSysMessage(LANG_ARENA_ERROR_NAME_NOT_FOUND, oldName.c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 检查新名称是否已存在
        if (sArenaTeamMgr->GetArenaTeamByName(newName))
        {
            handler->PSendSysMessage(LANG_ARENA_ERROR_NAME_EXISTS, newName.c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 检查战队是否正在比赛中
        if (arena->IsFighting())
        {
            handler->SendSysMessage(LANG_ARENA_ERROR_COMBAT);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 设置新名称
        if (arena->SetName(newName))
        {
            handler->PSendSysMessage(LANG_ARENA_RENAME, arena->GetId(), oldName.c_str(), newName.c_str());
            return true;
        }
        else
        {
            handler->SendSysMessage(LANG_BAD_VALUE);
            handler->SetSentErrorMessage(true);
            return false;
        }
    }

    /**
     * @brief 设置竞技场战队队长
     *
     * 将竞技场战队的队长权限转让给指定成员。仅游戏内可用。
     *
     * @param handler 聊天命令处理器
     * @param teamId 竞技场战队ID
     * @param target 可选参数，新队长标识符（如未指定则使用选中玩家或自己）
     *
     * @return bool 设置成功返回 true，失败返回 false
     *
     * @details 执行流程：
     * 1. 根据ID查找竞技场战队
     * 2. 检查战队是否存在
     * 3. 检查战队是否正在比赛中
     * 4. 解析新队长信息（如果未指定则使用目标或自己）
     * 5. 检查目标是否为战队成员
     * 6. 检查目标是否已经是队长
     * 7. 获取旧队长的名称用于显示
     * 8. 调用 SetCaptain 方法设置新队长
     *
     * 错误情况：
     * - 战队不存在
     * - 战队正在比赛中
     * - 目标不是战队成员
     * - 目标已经是队长
     *
     * 调用时机：管理员执行 .arena captain 命令时
     */
    static bool HandleArenaCaptainCommand(ChatHandler* handler, uint32 teamId, Optional<PlayerIdentifier> target)
    {
        // 根据ID查找竞技场战队
        ArenaTeam* arena = sArenaTeamMgr->GetArenaTeamById(teamId);
        if (!arena)
        {
            handler->PSendSysMessage(LANG_ARENA_ERROR_NOT_FOUND, teamId);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 检查战队是否正在比赛中
        if (arena->IsFighting())
        {
            handler->SendSysMessage(LANG_ARENA_ERROR_COMBAT);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 如果没有指定目标，使用选中的目标或自己
        if (!target)
            target = PlayerIdentifier::FromTargetOrSelf(handler);
        if (!target)
            return false;

        // 检查目标是否为战队成员
        if (!arena->IsMember(target->GetGUID()))
        {
            handler->PSendSysMessage(LANG_ARENA_ERROR_NOT_MEMBER, target->GetName().c_str(), arena->GetName().c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 检查目标是否已经是队长
        if (arena->GetCaptain() == target->GetGUID())
        {
            handler->PSendSysMessage(LANG_ARENA_ERROR_CAPTAIN, target->GetName().c_str(), arena->GetName().c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 获取旧队长的名称用于显示
        CharacterCacheEntry const* oldCaptainNameData = sCharacterCache->GetCharacterCacheByGuid(arena->GetCaptain());
        char const* oldCaptainName = oldCaptainNameData ? oldCaptainNameData->Name.c_str() : "<unknown>";

        // 设置新队长
        arena->SetCaptain(target->GetGUID());
        handler->PSendSysMessage(LANG_ARENA_CAPTAIN, arena->GetName().c_str(), arena->GetId(), oldCaptainName, target->GetName().c_str());

        return true;
    }

    /**
     * @brief 查询竞技场战队信息
     *
     * 显示指定竞技场战队的详细信息，包括战队属性和成员列表。
     *
     * @param handler 聊天命令处理器
     * @param teamId 竞技场战队ID
     *
     * @return bool 查询成功返回 true，失败返回 false
     *
     * @details 执行流程：
     * 1. 根据ID查找竞技场战队
     * 2. 检查战队是否存在
     * 3. 显示战队基本信息：名称、ID、评分、类型
     * 4. 遍历并显示所有成员：名称、GUID、个人评分、是否为队长
     *
     * 显示信息包括：
     * - 战队名称和ID
     * - 战队评分
     * - 战队类型（2v2、3v3、5v5）
     * - 所有成员的详细信息
     *
     * 调用时机：管理员执行 .arena info 命令时
     */
    static bool HandleArenaInfoCommand(ChatHandler* handler, uint32 teamId)
    {
        ArenaTeam* arena = sArenaTeamMgr->GetArenaTeamById(teamId);

        if (!arena)
        {
            handler->PSendSysMessage(LANG_ARENA_ERROR_NOT_FOUND, teamId);
            handler->SetSentErrorMessage(true);
            return false;
        }

        handler->PSendSysMessage(LANG_ARENA_INFO_HEADER, arena->GetName().c_str(), arena->GetId(), arena->GetRating(), arena->GetType(), arena->GetType());
        for (ArenaTeam::MemberList::iterator itr = arena->m_membersBegin(); itr != arena->m_membersEnd(); ++itr)
            handler->PSendSysMessage(LANG_ARENA_INFO_MEMBERS, itr->Name.c_str(), itr->Guid.GetCounter(), itr->PersonalRating, (arena->GetCaptain() == itr->Guid ? "- Captain" : ""));

        return true;
    }

    /**
     * @brief 搜索竞技场战队
     *
     * 根据名称关键词搜索竞技场战队，显示匹配的战队列表。仅游戏内可用。
     *
     * @param handler 聊天命令处理器
     * @param needle 搜索关键词（战队名称的部分匹配）
     *
     * @return bool 搜索成功返回 true，失败返回 false
     *
     * @details 执行流程：
     * 1. 检查搜索关键词是否为空
     * 2. 遍历所有竞技场战队
     * 3. 对每个战队的名称进行大小写不敏感的子串匹配
     * 4. 如果匹配成功，显示战队基本信息
     * 5. 如果没有找到匹配的战队，显示错误消息
     *
     * 显示信息包括：
     * - 战队名称
     * - 战队ID
     * - 战队类型（2v2、3v3、5v5）
     *
     * 搜索特点：
     * - 大小写不敏感
     * - 支持部分匹配
     * - 仅在游戏内可用
     *
     * 调用时机：玩家执行 .arena lookup 命令时
     *
     * @example
     * .arena lookup gladiator  // 搜索名称包含 "gladiator" 的战队
     */
    static bool HandleArenaLookupCommand(ChatHandler* handler, Tail needle)
    {
        // 检查搜索关键词是否为空
        if (needle.empty())
            return false;

        bool found = false;
        // 遍历所有竞技场战队
        for (auto [teamId, team] : sArenaTeamMgr->GetArenaTeams())
        {
            // 大小写不敏感的子串匹配
            if (StringContainsStringI(team->GetName(), needle))
            {
                // 仅在游戏内显示（需要会话）
                if (handler->GetSession())
                {
                    handler->PSendSysMessage(LANG_ARENA_LOOKUP, team->GetName().c_str(), team->GetId(), team->GetType(), team->GetType());
                    found = true;
                    continue;
                }
             }
        }

        // 如果没有找到匹配的战队，显示错误消息
        if (!found)
            handler->PSendSysMessage(LANG_ARENA_ERROR_NAME_NOT_FOUND, std::string(needle).c_str());

        return true;
    }
};

/**
 * @brief 注册竞技场战队命令脚本
 *
 * 此函数在服务器启动时被调用，用于创建并注册竞技场战队命令脚本实例。
 * 该函数由脚本管理系统自动调用。
 *
 * 调用时机：服务器初始化时，由脚本加载系统调用
 */
void AddSC_arena_commandscript()
{
    new arena_commandscript();
}
