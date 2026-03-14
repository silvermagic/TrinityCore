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
 * @file cs_cast.cpp
 * @brief 施法命令模块
 *
 * 本模块实现了与施法相关的GM命令，提供了灵活的法术施放控制。
 * 提供的功能包括：
 * - 对目标施放法术
 * - 让选中单位对自己施放法术
 * - 在指定距离施放法术
 * - 让目标对自己施放法术
 * - 让选中单位对其目标施放法术
 * - 在指定坐标施放法术
 *
 * 所有施法命令都支持"triggered"参数，用于跳过施法检查和消耗。
 */

/* ScriptData
Name: cast_commandscript
%Complete: 100
Comment: All cast related commands
Category: commandscripts
EndScriptData */

#include "ScriptMgr.h"
#include "Chat.h"
#include "Creature.h"
#include "Language.h"
#include "Player.h"
#include "RBAC.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "WorldSession.h"

using namespace Trinity::ChatCommands;

/**
 * @class cast_commandscript
 * @brief 施法命令脚本类
 *
 * 提供法术施放相关的GM命令处理功能，支持多种施法模式。
 * 所有命令仅限游戏内使用，不支持控制台执行。
 *
 * 施法模式包括：
 * - 标准施法：玩家对目标施法
 * - 反向施法：目标对玩家施法
 * - 距离施法：在指定距离施法
 * - 自身施法：目标对自己施法
 * - 目标施法：选中单位对其目标施法
 * - 坐标施法：在指定坐标施法
 */
class cast_commandscript : public CommandScript
{
public:
    /**
     * @brief 构造函数
     *
     * 初始化施法命令脚本，注册脚本名称为"cast_commandscript"
     */
    cast_commandscript() : CommandScript("cast_commandscript") { }

    /**
     * @brief 获取命令表
     * @return 返回施法相关命令的命令表
     *
     * 注册以下施法命令：
     * - cast: 对目标施放法术
     * - cast back: 让选中单位对自己施放法术
     * - cast dist: 在指定距离施放法术
     * - cast self: 让目标对自己施放法术
     * - cast target: 让选中单位对其目标施放法术
     * - cast dest: 在指定坐标施放法术
     *
     * 所有命令仅限游戏内使用（Console::No）
     */
    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable castCommandTable =
        {
            { "back",   HandleCastBackCommand,  rbac::RBAC_PERM_COMMAND_CAST_BACK,   Console::No },
            { "dist",   HandleCastDistCommand,  rbac::RBAC_PERM_COMMAND_CAST_DIST,   Console::No },
            { "self",   HandleCastSelfCommand,  rbac::RBAC_PERM_COMMAND_CAST_SELF,   Console::No },
            { "target", HandleCastTargetCommad, rbac::RBAC_PERM_COMMAND_CAST_TARGET, Console::No },
            { "dest",   HandleCastDestCommand,  rbac::RBAC_PERM_COMMAND_CAST_DEST,   Console::No },
            { "",       HandleCastCommand,      rbac::RBAC_PERM_COMMAND_CAST,        Console::No },
        };
        static ChatCommandTable commandTable =
        {
            { "cast", castCommandTable },
        };
        return commandTable;
    }

    /**
     * @brief 检查法术是否存在且有效
     * @param handler 聊天命令处理器
     * @param spell 法术信息指针
     * @return 法术有效返回true，否则返回false
     *
     * 验证法术的两个方面：
     * 1. 法术是否存在（指针非空）
     * 2. 法术是否可用（未被禁用或损坏）
     *
     * 如果法术无效，会向GM发送错误消息。
     */
    static bool CheckSpellExistsAndIsValid(ChatHandler* handler, SpellInfo const* spell)
    {
        // 检查法术是否存在
        if (!spell)
        {
            handler->PSendSysMessage(LANG_COMMAND_NOSPELLFOUND);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 检查法术是否有效（未被禁用或损坏）
        if (!SpellMgr::IsSpellValid(spell, handler->GetSession()->GetPlayer()))
        {
            handler->PSendSysMessage(LANG_COMMAND_SPELL_BROKEN, spell->Id);
            handler->SetSentErrorMessage(true);
            return false;
        }
        return true;
    }

    /**
     * @brief 获取触发标志
     * @param triggeredStr 可选的触发字符串参数
     * @return 返回触发标志，如果字符串无效返回std::nullopt
     *
     * 解析"triggered"参数，支持缩写形式（如"trig"、"trigger"等）。
     *
     * 触发模式特性：
     * - 跳过法术消耗（法力、材料等）
     * - 跳过施法时间
     * - 跳过冷却时间
     * - 忽略大多数施法限制
     * - 用于测试和调试
     *
     * @example
     * - "trig" -> TRIGGERED_FULL_DEBUG_MASK
     * - "triggered" -> TRIGGERED_FULL_DEBUG_MASK
     * - 无参数 -> TRIGGERED_NONE
     * - "invalid" -> std::nullopt (错误)
     */
    static Optional<TriggerCastFlags> GetTriggerFlags(Optional<std::string> triggeredStr)
    {
        if (triggeredStr)
        {
            // 检查是否以"triggered"开头（支持缩写，如"trig"、"trigger"等）
            if (StringStartsWith("triggered", *triggeredStr))
                return TRIGGERED_FULL_DEBUG_MASK;
            else
                return std::nullopt;
        }
        return TRIGGERED_NONE;
    }

    /**
     * @brief 对目标施放法术
     * @param handler 聊天命令处理器
     * @param spell 法术信息
     * @param triggeredStr 可选的触发模式字符串
     * @return 施法成功返回true，否则返回false
     *
     * 命令格式: .cast <法术ID> [triggered]
     * 调用时机: GM需要对选中目标施放法术时
     *
     * 玩家对当前选中的目标施放指定法术。
     * 如果未选中目标，会提示选择目标。
     *
     * @example
     * - .cast 12345         (对目标施放法术12345)
     * - .cast 12345 trig    (以触发模式施放，跳过消耗和限制)
     */
    static bool HandleCastCommand(ChatHandler* handler, SpellInfo const* spell, Optional<std::string> triggeredStr)
    {
        // 获取选中的目标
        Unit* target = handler->getSelectedUnit();
        if (!target)
        {
            handler->SendSysMessage(LANG_SELECT_CHAR_OR_CREATURE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 验证法术有效性
        if (!CheckSpellExistsAndIsValid(handler, spell))
            return false;

        // 获取触发标志
        Optional<TriggerCastFlags> triggerFlags = GetTriggerFlags(triggeredStr);
        if (!triggerFlags)
            return false;

        // 玩家对目标施放法术
        handler->GetSession()->GetPlayer()->CastSpell(target, spell->Id, *triggerFlags);

        return true;
    }

    /**
     * @brief 让选中单位对玩家施放法术
     * @param handler 聊天命令处理器
     * @param spell 法术信息
     * @param triggeredStr 可选的触发模式字符串
     * @return 施法成功返回true，否则返回false
     *
     * 命令格式: .cast back <法术ID> [triggered]
     * 调用时机: GM需要测试NPC对玩家的法术效果时
     *
     * 让选中的生物对玩家自己施放指定法术。
     * 只能对生物使用，不能对玩家使用。
     *
     * 用途:
     * - 测试NPC法术对玩家的效果
     * - 模拟怪物攻击
     *
     * @example
     * - .cast back 12345    (选中生物对玩家施放法术12345)
     */
    static bool HandleCastBackCommand(ChatHandler* handler, SpellInfo const* spell, Optional<std::string> triggeredStr)
    {
        // 获取选中的生物
        Creature* caster = handler->getSelectedCreature();
        if (!caster)
        {
            handler->SendSysMessage(LANG_SELECT_CHAR_OR_CREATURE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 验证法术有效性
        if (!CheckSpellExistsAndIsValid(handler, spell))
            return false;

        // 获取触发标志
        Optional<TriggerCastFlags> triggerFlags = GetTriggerFlags(triggeredStr);
        if (!triggerFlags)
            return false;

        // 生物对玩家施放法术
        caster->CastSpell(handler->GetSession()->GetPlayer(), spell->Id, *triggerFlags);

        return true;
    }

    /**
     * @brief 在指定距离施放法术
     * @param handler 聊天命令处理器
     * @param spell 法术信息
     * @param dist 距离（码）
     * @param triggeredStr 可选的触发模式字符串
     * @return 施法成功返回true，否则返回false
     *
     * 命令格式: .cast dist <法术ID> <距离> [triggered]
     * 调用时机: GM需要在指定距离施放法术时
     *
     * 在玩家前方指定距离的位置施放法术。
     * 距离单位为码（yards）。
     *
     * 用途:
     * - 测试AOE法术在特定距离的效果
     * - 测试法术射程限制
     *
     * @example
     * - .cast dist 12345 20    (在玩家前方20码处施放法术12345)
     * - .cast dist 12345 10 trig (以触发模式在10码处施放)
     */
    static bool HandleCastDistCommand(ChatHandler* handler, SpellInfo const* spell, float dist, Optional<std::string> triggeredStr)
    {
        // 验证法术有效性
        if (!CheckSpellExistsAndIsValid(handler, spell))
            return false;

        // 获取触发标志
        Optional<TriggerCastFlags> triggerFlags = GetTriggerFlags(triggeredStr);
        if (!triggerFlags)
            return false;

        // 计算玩家前方指定距离的坐标点
        float x, y, z;
        handler->GetSession()->GetPlayer()->GetClosePoint(x, y, z, dist);
        // 玩家向该坐标施放法术
        handler->GetSession()->GetPlayer()->CastSpell(Position{ x, y, z }, spell->Id, *triggerFlags);

        return true;
    }

    /**
     * @brief 让目标对自己施放法术
     * @param handler 聊天命令处理器
     * @param spell 法术信息
     * @param triggeredStr 可选的触发模式字符串
     * @return 施法成功返回true，否则返回false
     *
     * 命令格式: .cast self <法术ID> [triggered]
     * 调用时机: GM需要让目标对自己施放法术时
     *
     * 让选中的目标对自己施放指定法术。
     * 可以作用于玩家或生物。
     *
     * 用途:
     * - 测试增益/减益法术的自我效果
     * - 测试自伤法术
     *
     * @example
     * - .cast self 12345    (选中目标对自己施放法术12345)
     */
    static bool HandleCastSelfCommand(ChatHandler* handler, SpellInfo const* spell, Optional<std::string> triggeredStr)
    {
        // 获取选中的目标
        Unit* target = handler->getSelectedUnit();
        if (!target)
        {
            handler->SendSysMessage(LANG_SELECT_CHAR_OR_CREATURE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 验证法术有效性
        if (!CheckSpellExistsAndIsValid(handler, spell))
            return false;

        // 获取触发标志
        Optional<TriggerCastFlags> triggerFlags = GetTriggerFlags(triggeredStr);
        if (!triggerFlags)
            return false;

        // 目标对自己施放法术
        target->CastSpell(target, spell->Id, *triggerFlags);

        return true;
    }

    /**
     * @brief 让选中单位对其目标施放法术
     * @param handler 聊天命令处理器
     * @param spell 法术信息
     * @param triggeredStr 可选的触发模式字符串
     * @return 施法成功返回true，否则返回false
     *
     * 命令格式: .cast target <法术ID> [triggered]
     * 调用时机: GM需要让NPC对其当前目标施放法术时
     *
     * 让选中的生物对其当前的仇恨目标施放法术。
     * 只能对生物使用，且生物必须有攻击目标。
     *
     * 用途:
     * - 测试NPC战斗中的法术施放
     * - 调试NPC AI的法术选择逻辑
     *
     * @example
     * - .cast target 12345    (选中生物对其目标施放法术12345)
     */
    static bool HandleCastTargetCommad(ChatHandler* handler, SpellInfo const* spell, Optional<std::string> triggeredStr)
    {
        // 获取选中的生物
        Creature* caster = handler->getSelectedCreature();
        if (!caster)
        {
            handler->SendSysMessage(LANG_SELECT_CHAR_OR_CREATURE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 检查生物是否有攻击目标
        if (!caster->GetVictim())
        {
            handler->SendSysMessage(LANG_SELECTED_TARGET_NOT_HAVE_VICTIM);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 验证法术有效性
        if (!CheckSpellExistsAndIsValid(handler, spell))
            return false;

        // 获取触发标志
        Optional<TriggerCastFlags> triggerFlags = GetTriggerFlags(triggeredStr);
        if (!triggerFlags)
            return false;

        // 生物对其当前目标施放法术
        caster->CastSpell(caster->GetVictim(), spell->Id, *triggerFlags);

        return true;
    }

    /**
     * @brief 在指定坐标施放法术
     * @param handler 聊天命令处理器
     * @param spell 法术信息
     * @param x X坐标
     * @param y Y坐标
     * @param z Z坐标
     * @param triggeredStr 可选的触发模式字符串
     * @return 施法成功返回true，否则返回false
     *
     * 命令格式: .cast dest <法术ID> <X> <Y> <Z> [triggered]
     * 调用时机: GM需要在精确坐标施放法术时
     *
     * 让选中的单位向指定的世界坐标施放法术。
     * 可用于玩家或生物。
     *
     * 用途:
     * - 测试定点AOE法术
     * - 在特定位置召唤物体
     * - 测试地形相关的法术效果
     *
     * @example
     * - .cast dest 12345 1234.5 5678.9 100.0  (在指定坐标施放法术12345)
     * - .cast dest 12345 0 0 0 trig            (以触发模式在(0,0,0)施放)
     */
    static bool HandleCastDestCommand(ChatHandler* handler, SpellInfo const* spell, float x, float y, float z, Optional<std::string> triggeredStr)
    {
        // 获取施法者（选中的单位）
        Unit* caster = handler->getSelectedUnit();
        if (!caster)
        {
            handler->SendSysMessage(LANG_SELECT_CHAR_OR_CREATURE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 验证法术有效性
        if (!CheckSpellExistsAndIsValid(handler, spell))
            return false;

        // 获取触发标志
        Optional<TriggerCastFlags> triggerFlags = GetTriggerFlags(triggeredStr);
        if (!triggerFlags)
            return false;

        // 施法者向指定坐标施放法术
        caster->CastSpell(Position{ x, y, z }, spell->Id, *triggerFlags);

        return true;
    }
};

/**
 * @brief 注册施法命令脚本
 *
 * 此函数在服务器启动时被调用，用于创建并注册施法命令脚本实例。
 * 这是脚本系统的标准入口点，将脚本添加到命令处理系统中。
 */
void AddSC_cast_commandscript()
{
    new cast_commandscript();
}
