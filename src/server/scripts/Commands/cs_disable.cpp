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
 * @file cs_disable.cpp
 * @brief 游戏元素禁用命令模块
 *
 * 本模块实现了GM命令系统中用于禁用和启用各种游戏元素的命令。
 * 管理员可以通过这些命令临时或永久性地禁用特定的游戏功能，
 * 如法术、任务、地图、战场、成就条件、户外PvP、虚拟地图(VMap)和移动地图(MMap)等。
 *
 * 主要功能:
 * - 添加禁用项：将指定的游戏元素添加到禁用列表
 * - 移除禁用项：从禁用列表中移除指定的游戏元素
 * - 支持多种类型的游戏元素禁用
 *
 * 使用场景:
 * - 禁用存在Bug的法术或任务
 * - 临时关闭有问题的地图或副本
 * - 禁用特定的游戏机制用于测试
 * - 维护期间关闭特定功能
 */

/* ScriptData
Name: disable_commandscript
%Complete: 100
Comment: All disable related commands
Category: commandscripts
EndScriptData */

#include "ScriptMgr.h"
#include "AchievementMgr.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "DisableMgr.h"
#include "Language.h"
#include "ObjectMgr.h"
#include "OutdoorPvP.h"
#include "Player.h"
#include "RBAC.h"
#include "SpellMgr.h"

using namespace Trinity::ChatCommands;

/**
 * @class disable_commandscript
 * @brief 禁用命令脚本类
 *
 * 继承自CommandScript基类，提供游戏元素禁用功能的GM命令实现。
 * 该类负责注册和处理所有与禁用相关的命令，包括添加和移除禁用项。
 * 支持的禁用类型包括：法术、任务、地图、战场、成就条件、户外PvP、VMap和MMap。
 */
class disable_commandscript : public CommandScript
{
public:
    /**
     * @brief 构造函数
     *
     * 初始化禁用命令脚本，设置脚本名称为"disable_commandscript"
     */
    disable_commandscript() : CommandScript("disable_commandscript") { }

    /**
     * @brief 获取命令表
     * @return 返回命令表的常量引用
     *
     * 注册所有禁用相关的命令，包括：
     * - disable add：添加禁用项的命令组
     * - disable remove：移除禁用项的命令组
     *
     * 每个命令组支持多种类型的游戏元素
     */
    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable removeDisableCommandTable =
        {
            { "spell",                HandleRemoveDisableSpellCommand,               rbac::RBAC_PERM_COMMAND_DISABLE_REMOVE_SPELL,                Console::Yes },
            { "quest",                HandleRemoveDisableQuestCommand,               rbac::RBAC_PERM_COMMAND_DISABLE_REMOVE_QUEST,                Console::Yes },
            { "map",                  HandleRemoveDisableMapCommand,                 rbac::RBAC_PERM_COMMAND_DISABLE_REMOVE_MAP,                  Console::Yes },
            { "battleground",         HandleRemoveDisableBattlegroundCommand,        rbac::RBAC_PERM_COMMAND_DISABLE_REMOVE_BATTLEGROUND,         Console::Yes },
            { "achievement_criteria", HandleRemoveDisableAchievementCriteriaCommand, rbac::RBAC_PERM_COMMAND_DISABLE_REMOVE_ACHIEVEMENT_CRITERIA, Console::Yes },
            { "outdoorpvp",           HandleRemoveDisableOutdoorPvPCommand,          rbac::RBAC_PERM_COMMAND_DISABLE_REMOVE_OUTDOORPVP,           Console::Yes },
            { "vmap",                 HandleRemoveDisableVmapCommand,                rbac::RBAC_PERM_COMMAND_DISABLE_REMOVE_VMAP,                 Console::Yes },
            { "mmap",                 HandleRemoveDisableMMapCommand,                rbac::RBAC_PERM_COMMAND_DISABLE_REMOVE_MMAP,                 Console::Yes },
        };
        static ChatCommandTable addDisableCommandTable =
        {
            { "spell",                HandleAddDisableSpellCommand,                  rbac::RBAC_PERM_COMMAND_DISABLE_ADD_SPELL,                Console::Yes },
            { "quest",                HandleAddDisableQuestCommand,                  rbac::RBAC_PERM_COMMAND_DISABLE_ADD_QUEST,                Console::Yes },
            { "map",                  HandleAddDisableMapCommand,                    rbac::RBAC_PERM_COMMAND_DISABLE_ADD_MAP,                  Console::Yes },
            { "battleground",         HandleAddDisableBattlegroundCommand,           rbac::RBAC_PERM_COMMAND_DISABLE_ADD_BATTLEGROUND,         Console::Yes },
            { "achievement_criteria", HandleAddDisableAchievementCriteriaCommand,    rbac::RBAC_PERM_COMMAND_DISABLE_ADD_ACHIEVEMENT_CRITERIA, Console::Yes },
            { "outdoorpvp",           HandleAddDisableOutdoorPvPCommand,             rbac::RBAC_PERM_COMMAND_DISABLE_ADD_OUTDOORPVP,           Console::Yes },
            { "vmap",                 HandleAddDisableVmapCommand,                   rbac::RBAC_PERM_COMMAND_DISABLE_ADD_VMAP,                 Console::Yes },
            { "mmap",                 HandleAddDisableMMapCommand,                   rbac::RBAC_PERM_COMMAND_DISABLE_ADD_MMAP,                 Console::Yes },
        };
        static ChatCommandTable disableCommandTable =
        {
            { "add",    addDisableCommandTable },
            { "remove", removeDisableCommandTable },
        };
        static ChatCommandTable commandTable =
        {
            { "disable", disableCommandTable },
        };
        return commandTable;
    }

    /**
     * @brief 添加禁用项的通用处理函数
     * @param handler 聊天处理器，用于发送消息和获取会话信息
     * @param disableType 禁用类型（法术、任务、地图等）
     * @param entry 要禁用的游戏元素ID
     * @param flags 可选的禁用标志位，用于指定特定的禁用行为
     * @param disableComment 禁用原因的注释说明
     * @return 成功返回true，失败返回false
     *
     * 此函数是所有添加禁用项命令的核心实现，负责：
     * 1. 验证指定ID的游戏元素是否存在
     * 2. 检查该元素是否已经被禁用
     * 3. 将禁用信息写入数据库
     * 4. 向用户反馈操作结果
     *
     * 调用时机：当GM执行 .disable add 系列命令时
     * 性能注意：会执行数据库查询和插入操作
     */
    static bool HandleAddDisables(ChatHandler* handler, DisableType disableType, uint32 entry, Optional<uint16> flags, Tail disableComment)
    {
        char const* disableTypeStr = "";

        // 根据禁用类型验证对应的游戏元素是否存在
        switch (disableType)
        {
            case DISABLE_TYPE_SPELL:
            {
                // 验证法术ID是否有效
                if (!sSpellMgr->GetSpellInfo(entry))
                {
                    handler->PSendSysMessage(LANG_COMMAND_NOSPELLFOUND);
                    handler->SetSentErrorMessage(true);
                    return false;
                }
                disableTypeStr = "spell";
                break;
            }
            case DISABLE_TYPE_QUEST:
            {
                // 验证任务模板是否存在
                if (!sObjectMgr->GetQuestTemplate(entry))
                {
                    handler->PSendSysMessage(LANG_COMMAND_QUEST_NOTFOUND, entry);
                    handler->SetSentErrorMessage(true);
                    return false;
                }
                disableTypeStr = "quest";
                break;
            }
            case DISABLE_TYPE_MAP:
            {
                // 验证地图ID是否有效
                if (!sMapStore.LookupEntry(entry))
                {
                    handler->PSendSysMessage(LANG_COMMAND_NOMAPFOUND);
                    handler->SetSentErrorMessage(true);
                    return false;
                }
                disableTypeStr = "map";
                break;
            }
            case DISABLE_TYPE_BATTLEGROUND:
            {
                // 验证战场ID是否有效
                if (!sBattlemasterListStore.LookupEntry(entry))
                {
                    handler->PSendSysMessage(LANG_COMMAND_NO_BATTLEGROUND_FOUND);
                    handler->SetSentErrorMessage(true);
                    return false;
                }
                disableTypeStr = "battleground";
                break;
            }
            case DISABLE_TYPE_ACHIEVEMENT_CRITERIA:
            {
                // 验证成就条件是否存在
                if (!sAchievementMgr->GetAchievementCriteria(entry))
                {
                    handler->PSendSysMessage(LANG_COMMAND_NO_ACHIEVEMENT_CRITERIA_FOUND);
                    handler->SetSentErrorMessage(true);
                    return false;
                }
                disableTypeStr = "achievement criteria";
                break;
            }
            case DISABLE_TYPE_OUTDOORPVP:
            {
                // 验证户外PvP类型是否有效
                if (entry > MAX_OUTDOORPVP_TYPES)
                {
                    handler->PSendSysMessage(LANG_COMMAND_NO_OUTDOOR_PVP_FORUND);
                    handler->SetSentErrorMessage(true);
                    return false;
                }
                disableTypeStr = "outdoorpvp";
                break;
            }
            case DISABLE_TYPE_VMAP:
            {
                // 验证虚拟地图ID是否有效
                if (!sMapStore.LookupEntry(entry))
                {
                    handler->PSendSysMessage(LANG_COMMAND_NOMAPFOUND);
                    handler->SetSentErrorMessage(true);
                    return false;
                }
                disableTypeStr = "vmap";
                break;
            }
            case DISABLE_TYPE_MMAP:
            {
                // 验证移动地图ID是否有效
                if (!sMapStore.LookupEntry(entry))
                {
                    handler->PSendSysMessage(LANG_COMMAND_NOMAPFOUND);
                    handler->SetSentErrorMessage(true);
                    return false;
                }
                disableTypeStr = "mmap";
                break;
            }
            case DISABLE_TYPE_LFG_MAP:
            {
                // 验证随机副本地图ID是否有效
                if (!sMapStore.LookupEntry(entry))
                {
                    handler->PSendSysMessage(LANG_COMMAND_NOMAPFOUND);
                    handler->SetSentErrorMessage(true);
                    return false;
                }
                disableTypeStr = "lfg map";
                break;
            }
            default:
                break;
        }

        // 检查该元素是否已经被禁用，避免重复添加
        WorldDatabasePreparedStatement* stmt = WorldDatabase.GetPreparedStatement(WORLD_SEL_DISABLES);
        stmt->setUInt32(0, entry);
        stmt->setUInt8(1, disableType);
        PreparedQueryResult result = WorldDatabase.Query(stmt);
        if (result)
        {
            // 该元素已经被禁用，提示用户并返回失败
            handler->PSendSysMessage("This %s (Id: %u) is already disabled.", disableTypeStr, entry);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 将禁用信息插入到数据库
        stmt = WorldDatabase.GetPreparedStatement(WORLD_INS_DISABLES);
        stmt->setUInt32(0, entry);
        stmt->setUInt8(1, disableType);
        stmt->setUInt16(2, flags.value_or<uint16>(0));  // 如果未提供标志，默认为0
        stmt->setStringView(3, disableComment);          // 记录禁用原因
        WorldDatabase.Execute(stmt);

        // 向用户反馈禁用成功的信息
        handler->PSendSysMessage("Add Disabled %s (Id: %u) for reason " STRING_VIEW_FMT, disableTypeStr, entry, STRING_VIEW_FMT_ARG(disableComment));
        return true;
    }

    /**
     * @brief 添加法术禁用命令处理
     * @param handler 聊天处理器
     * @param entry 法术ID
     * @param flags 可选的禁用标志
     * @param disableComment 禁用原因注释
     * @return 成功返回true，失败返回false
     *
     * 处理 .disable add spell 命令
     */
    static bool HandleAddDisableSpellCommand(ChatHandler* handler, uint32 entry, Optional<uint16> flags, Tail disableComment)
    {
        return HandleAddDisables(handler, DISABLE_TYPE_SPELL, entry, flags, disableComment);
    }

    /**
     * @brief 添加任务禁用命令处理
     * @param handler 聊天处理器
     * @param entry 任务ID
     * @param flags 可选的禁用标志
     * @param disableComment 禁用原因注释
     * @return 成功返回true，失败返回false
     *
     * 处理 .disable add quest 命令
     */
    static bool HandleAddDisableQuestCommand(ChatHandler* handler, uint32 entry, Optional<uint16> flags, Tail disableComment)
    {
        return HandleAddDisables(handler, DISABLE_TYPE_QUEST, entry, flags, disableComment);
    }

    /**
     * @brief 添加地图禁用命令处理
     * @param handler 聊天处理器
     * @param entry 地图ID
     * @param flags 可选的禁用标志
     * @param disableComment 禁用原因注释
     * @return 成功返回true，失败返回false
     *
     * 处理 .disable add map 命令
     */
    static bool HandleAddDisableMapCommand(ChatHandler* handler, uint32 entry, Optional<uint16> flags, Tail disableComment)
    {
        return HandleAddDisables(handler, DISABLE_TYPE_MAP, entry, flags, disableComment);
    }

    /**
     * @brief 添加战场禁用命令处理
     * @param handler 聊天处理器
     * @param entry 战场ID
     * @param flags 可选的禁用标志
     * @param disableComment 禁用原因注释
     * @return 成功返回true，失败返回false
     *
     * 处理 .disable add battleground 命令
     */
    static bool HandleAddDisableBattlegroundCommand(ChatHandler* handler, uint32 entry, Optional<uint16> flags, Tail disableComment)
    {
        return HandleAddDisables(handler, DISABLE_TYPE_BATTLEGROUND, entry, flags, disableComment);
    }

    /**
     * @brief 添加成就条件禁用命令处理
     * @param handler 聊天处理器
     * @param entry 成就条件ID
     * @param flags 可选的禁用标志
     * @param disableComment 禁用原因注释
     * @return 成功返回true，失败返回false
     *
     * 处理 .disable add achievement_criteria 命令
     */
    static bool HandleAddDisableAchievementCriteriaCommand(ChatHandler* handler, uint32 entry, Optional<uint16> flags, Tail disableComment)
    {
        return HandleAddDisables(handler, DISABLE_TYPE_ACHIEVEMENT_CRITERIA, entry, flags, disableComment);
    }

    /**
     * @brief 添加户外PvP禁用命令处理
     * @param handler 聊天处理器
     * @param entry 户外PvP类型ID
     * @param flags 可选的禁用标志
     * @param disableComment 禁用原因注释
     * @return 成功返回true，失败返回false
     *
     * 处理 .disable add outdoorpvp 命令
     */
    static bool HandleAddDisableOutdoorPvPCommand(ChatHandler* handler, uint32 entry, Optional<uint16> flags, Tail disableComment)
    {
        return HandleAddDisables(handler, DISABLE_TYPE_OUTDOORPVP, entry, flags, disableComment);
    }

    /**
     * @brief 添加虚拟地图(VMAP)禁用命令处理
     * @param handler 聊天处理器
     * @param entry 地图ID
     * @param flags 可选的禁用标志
     * @param disableComment 禁用原因注释
     * @return 成功返回true，失败返回false
     *
     * 处理 .disable add vmap 命令
     */
    static bool HandleAddDisableVmapCommand(ChatHandler* handler, uint32 entry, Optional<uint16> flags, Tail disableComment)
    {
        return HandleAddDisables(handler, DISABLE_TYPE_VMAP, entry, flags, disableComment);
    }

    /**
     * @brief 添加移动地图(MMAP)禁用命令处理
     * @param handler 聊天处理器
     * @param entry 地图ID
     * @param flags 可选的禁用标志
     * @param disableComment 禁用原因注释
     * @return 成功返回true，失败返回false
     *
     * 处理 .disable add mmap 命令
     */
    static bool HandleAddDisableMMapCommand(ChatHandler* handler, uint32 entry, Optional<uint16> flags, Tail disableComment)
    {
        return HandleAddDisables(handler, DISABLE_TYPE_MMAP, entry, flags, disableComment);
    }

    /**
     * @brief 移除禁用项的通用处理函数
     * @param handler 聊天处理器，用于发送消息和获取会话信息
     * @param disableType 禁用类型（法术、任务、地图等）
     * @param entry 要移除禁用的游戏元素ID
     * @return 成功返回true，失败返回false
     *
     * 此函数是所有移除禁用项命令的核心实现，负责：
     * 1. 检查该元素是否存在于禁用列表中
     * 2. 从数据库中删除对应的禁用记录
     * 3. 向用户反馈操作结果
     *
     * 调用时机：当GM执行 .disable remove 系列命令时
     * 性能注意：会执行数据库查询和删除操作
     */
    static bool HandleRemoveDisables(ChatHandler* handler, DisableType disableType, uint32 entry)
    {
        std::string disableTypeStr = "";

        // 根据禁用类型设置对应的类型字符串
        switch (disableType)
        {
            case DISABLE_TYPE_SPELL:
                disableTypeStr = "spell";
                break;
            case DISABLE_TYPE_QUEST:
                disableTypeStr = "quest";
                break;
            case DISABLE_TYPE_MAP:
                disableTypeStr = "map";
                break;
            case DISABLE_TYPE_BATTLEGROUND:
                disableTypeStr = "battleground";
                break;
            case DISABLE_TYPE_ACHIEVEMENT_CRITERIA:
                disableTypeStr = "achievement criteria";
                break;
            case DISABLE_TYPE_OUTDOORPVP:
                disableTypeStr = "outdoorpvp";
                break;
            case DISABLE_TYPE_VMAP:
                disableTypeStr = "vmap";
                break;
            case DISABLE_TYPE_MMAP:
                disableTypeStr = "mmap";
                break;
            case DISABLE_TYPE_LFG_MAP:
                disableTypeStr = "lfg map";
                break;
            default:
                break;
        }

        // 查询该元素是否在禁用列表中
        WorldDatabasePreparedStatement* stmt = WorldDatabase.GetPreparedStatement(WORLD_SEL_DISABLES);
        stmt->setUInt32(0, entry);
        stmt->setUInt8(1, disableType);
        PreparedQueryResult result = WorldDatabase.Query(stmt);
        if (!result)
        {
            // 该元素未被禁用，提示用户并返回失败
            handler->PSendSysMessage("This %s (Id: %u) is not disabled.", disableTypeStr.c_str(), entry);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 从数据库中删除禁用记录
        stmt = WorldDatabase.GetPreparedStatement(WORLD_DEL_DISABLES);
        stmt->setUInt32(0, entry);
        stmt->setUInt8(1, disableType);
        WorldDatabase.Execute(stmt);

        // 向用户反馈移除成功的信息
        handler->PSendSysMessage("Remove Disabled %s (Id: %u)", disableTypeStr.c_str(), entry);
        return true;
    }

    /**
     * @brief 移除法术禁用命令处理
     * @param handler 聊天处理器
     * @param entry 法术ID
     * @return 成功返回true，失败返回false
     *
     * 处理 .disable remove spell 命令
     */
    static bool HandleRemoveDisableSpellCommand(ChatHandler* handler, uint32 entry)
    {
        return HandleRemoveDisables(handler, DISABLE_TYPE_SPELL, entry);
    }

    /**
     * @brief 移除任务禁用命令处理
     * @param handler 聊天处理器
     * @param entry 任务ID
     * @return 成功返回true，失败返回false
     *
     * 处理 .disable remove quest 命令
     */
    static bool HandleRemoveDisableQuestCommand(ChatHandler* handler, uint32 entry)
    {
        return HandleRemoveDisables(handler, DISABLE_TYPE_QUEST, entry);
    }

    /**
     * @brief 移除地图禁用命令处理
     * @param handler 聊天处理器
     * @param entry 地图ID
     * @return 成功返回true，失败返回false
     *
     * 处理 .disable remove map 命令
     */
    static bool HandleRemoveDisableMapCommand(ChatHandler* handler, uint32 entry)
    {
        return HandleRemoveDisables(handler, DISABLE_TYPE_MAP, entry);
    }

    /**
     * @brief 移除战场禁用命令处理
     * @param handler 聊天处理器
     * @param entry 战场ID
     * @return 成功返回true，失败返回false
     *
     * 处理 .disable remove battleground 命令
     */
    static bool HandleRemoveDisableBattlegroundCommand(ChatHandler* handler, uint32 entry)
    {
        return HandleRemoveDisables(handler, DISABLE_TYPE_BATTLEGROUND, entry);
    }

    /**
     * @brief 移除成就条件禁用命令处理
     * @param handler 聊天处理器
     * @param entry 成就条件ID
     * @return 成功返回true，失败返回false
     *
     * 处理 .disable remove achievement_criteria 命令
     */
    static bool HandleRemoveDisableAchievementCriteriaCommand(ChatHandler* handler, uint32 entry)
    {
        return HandleRemoveDisables(handler, DISABLE_TYPE_ACHIEVEMENT_CRITERIA, entry);
    }

    /**
     * @brief 移除户外PvP禁用命令处理
     * @param handler 聊天处理器
     * @param entry 户外PvP类型ID
     * @return 成功返回true，失败返回false
     *
     * 处理 .disable remove outdoorpvp 命令
     */
    static bool HandleRemoveDisableOutdoorPvPCommand(ChatHandler* handler, uint32 entry)
    {
        return HandleRemoveDisables(handler, DISABLE_TYPE_OUTDOORPVP, entry);
    }

    /**
     * @brief 移除虚拟地图(VMAP)禁用命令处理
     * @param handler 聊天处理器
     * @param entry 地图ID
     * @return 成功返回true，失败返回false
     *
     * 处理 .disable remove vmap 命令
     */
    static bool HandleRemoveDisableVmapCommand(ChatHandler* handler, uint32 entry)
    {
        return HandleRemoveDisables(handler, DISABLE_TYPE_VMAP, entry);
    }

    /**
     * @brief 移除移动地图(MMAP)禁用命令处理
     * @param handler 聊天处理器
     * @param entry 地图ID
     * @return 成功返回true，失败返回false
     *
     * 处理 .disable remove mmap 命令
     */
    static bool HandleRemoveDisableMMapCommand(ChatHandler* handler, uint32 entry)
    {
        return HandleRemoveDisables(handler, DISABLE_TYPE_MMAP, entry);
    }
};

/**
 * @brief 注册禁用命令脚本
 *
 * 此函数用于将禁用命令脚本注册到服务器中，
 * 在服务器启动时被调用以初始化命令系统
 */
void AddSC_disable_commandscript()
{
    new disable_commandscript();
}
