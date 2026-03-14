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
Name: modify_commandscript
%Complete: 100
Comment: All modify related commands
Category: commandscripts
EndScriptData */

/**
 * @file cs_modify.cpp
 * @brief 游戏修改命令模块
 *
 * 本模块提供了一系列GM命令，用于修改玩家和单位的各种属性。
 * 主要功能包括：
 * - 修改玩家属性：生命值、法力值、能量、怒气、符文能量等
 * - 修改玩家资源：金币、荣誉点、竞技场点、经验值等
 * - 修改玩家状态：速度、缩放、阵营、性别、醉酒状态等
 * - 修改单位外观：变形（morph）、模型缩放
 * - 修改玩家声望和天赋点
 * - 设置玩家阶段掩码和姿态状态
 *
 * 这些命令主要用于游戏测试、事件管理和玩家支持。
 * 所有命令都需要相应的RBAC权限才能执行。
 */

#include "ScriptMgr.h"
#include "Chat.h"
#include "DBCStores.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Pet.h"
#include "Player.h"
#include "RBAC.h"
#include "ReputationMgr.h"
#include "Util.h"
#include "WorldSession.h"

#if TRINITY_COMPILER == TRINITY_COMPILER_GNU
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

/**
 * @class modify_commandscript
 * @brief 修改命令脚本类
 *
 * 该类继承自CommandScript，提供所有与修改玩家/单位属性相关的GM命令。
 * 命令包括：修改生命值、法力值、速度、金币、声望、荣誉点、竞技场点、
 * 变形、缩放、阵营、性别等。所有命令都需要相应的RBAC权限。
 */
class modify_commandscript : public CommandScript
{
public:
    /**
     * @brief 构造函数
     *
     * 初始化修改命令脚本，注册命令名称为"modify_commandscript"
     */
    modify_commandscript() : CommandScript("modify_commandscript") { }

    /**
     * @brief 获取所有修改命令的命令表
     * @return 返回命令表向量，包含所有修改相关命令的定义
     *
     * 该函数注册了以下命令类别：
     * - modifyspeedCommandTable: 速度修改命令（包括所有速度、后退速度、飞行速度、行走速度、游泳速度）
     * - modifyCommandTable: 主要修改命令（竞技场点、位、醉酒、能量、阵营、性别、荣誉、HP、法力等）
     * - commandTable: 顶层命令（morph变形、demorph取消变形、modify修改）
     *
     * @note 每个命令都关联了相应的RBAC权限控制
     */
    std::vector<ChatCommand> GetCommands() const override
    {
        // 速度修改命令子表
        static std::vector<ChatCommand> modifyspeedCommandTable =
        {
            { "all",      rbac::RBAC_PERM_COMMAND_MODIFY_SPEED_ALL,      false, &HandleModifyASpeedCommand, "" },
            { "backwalk", rbac::RBAC_PERM_COMMAND_MODIFY_SPEED_BACKWALK, false, &HandleModifyBWalkCommand,  "" },
            { "fly",      rbac::RBAC_PERM_COMMAND_MODIFY_SPEED_FLY,      false, &HandleModifyFlyCommand,    "" },
            { "walk",     rbac::RBAC_PERM_COMMAND_MODIFY_SPEED_WALK,     false, &HandleModifySpeedCommand,  "" },
            { "swim",     rbac::RBAC_PERM_COMMAND_MODIFY_SPEED_SWIM,     false, &HandleModifySwimCommand,   "" },
            { "",         rbac::RBAC_PERM_COMMAND_MODIFY_SPEED,          false, &HandleModifyASpeedCommand, "" },
        };
        // 主要修改命令表
        static std::vector<ChatCommand> modifyCommandTable =
        {
            { "arenapoints",  rbac::RBAC_PERM_COMMAND_MODIFY_ARENAPOINTS,  false, &HandleModifyArenaCommand,         "" },
            { "bit",          rbac::RBAC_PERM_COMMAND_MODIFY_BIT,          false, &HandleModifyBitCommand,           "" },
            { "drunk",        rbac::RBAC_PERM_COMMAND_MODIFY_DRUNK,        false, &HandleModifyDrunkCommand,         "" },
            { "energy",       rbac::RBAC_PERM_COMMAND_MODIFY_ENERGY,       false, &HandleModifyEnergyCommand,        "" },
            { "faction",      rbac::RBAC_PERM_COMMAND_MODIFY_FACTION,      false, &HandleModifyFactionCommand,       "" },
            { "gender",       rbac::RBAC_PERM_COMMAND_MODIFY_GENDER,       false, &HandleModifyGenderCommand,        "" },
            { "honor",        rbac::RBAC_PERM_COMMAND_MODIFY_HONOR,        false, &HandleModifyHonorCommand,         "" },
            { "hp",           rbac::RBAC_PERM_COMMAND_MODIFY_HP,           false, &HandleModifyHPCommand,            "" },
            { "mana",         rbac::RBAC_PERM_COMMAND_MODIFY_MANA,         false, &HandleModifyManaCommand,          "" },
            { "money",        rbac::RBAC_PERM_COMMAND_MODIFY_MONEY,        false, &HandleModifyMoneyCommand,         "" },
            { "mount",        rbac::RBAC_PERM_COMMAND_MODIFY_MOUNT,        false, &HandleModifyMountCommand,         "" },
            { "phase",        rbac::RBAC_PERM_COMMAND_MODIFY_PHASE,        false, &HandleModifyPhaseCommand,         "" },
            { "rage",         rbac::RBAC_PERM_COMMAND_MODIFY_RAGE,         false, &HandleModifyRageCommand,          "" },
            { "reputation",   rbac::RBAC_PERM_COMMAND_MODIFY_REPUTATION,   false, &HandleModifyRepCommand,           "" },
            { "runicpower",   rbac::RBAC_PERM_COMMAND_MODIFY_RUNICPOWER,   false, &HandleModifyRunicPowerCommand,    "" },
            { "scale",        rbac::RBAC_PERM_COMMAND_MODIFY_SCALE,        false, &HandleModifyScaleCommand,         "" },
            { "speed",        rbac::RBAC_PERM_COMMAND_MODIFY_SPEED,        false, nullptr,           "", modifyspeedCommandTable },
            { "spell",        rbac::RBAC_PERM_COMMAND_MODIFY_SPELL,        false, &HandleModifySpellCommand,         "" },
            { "standstate",   rbac::RBAC_PERM_COMMAND_MODIFY_STANDSTATE,   false, &HandleModifyStandStateCommand,    "" },
            { "talentpoints", rbac::RBAC_PERM_COMMAND_MODIFY_TALENTPOINTS, false, &HandleModifyTalentCommand,        "" },
            { "xp",           rbac::RBAC_PERM_COMMAND_MODIFY_XP,           false, &HandleModifyXPCommand,            "" },
        };
        // 顶层命令表
        static std::vector<ChatCommand> commandTable =
        {
            { "morph",   rbac::RBAC_PERM_COMMAND_MORPH,   false, &HandleModifyMorphCommand,          "" },
            { "demorph", rbac::RBAC_PERM_COMMAND_DEMORPH, false, &HandleDeMorphCommand,              "" },
            { "modify",  rbac::RBAC_PERM_COMMAND_MODIFY,  false, nullptr,                 "", modifyCommandTable },
        };
        return commandTable;
    }

    /**
     * @brief 发送修改通知给目标玩家
     * @tparam Args 可变参数模板
     * @param handler 聊天处理器指针，用于发送消息
     * @param target 目标单位（必须是玩家）
     * @param resourceMessage 发送给执行者的消息ID
     * @param resourceReportMessage 发送给目标的消息ID
     * @param args 可变参数列表，用于格式化消息
     *
     * 该函数用于在修改操作完成后，向执行者和目标玩家发送通知消息。
     * 如果目标玩家需要接收报告，则会同时向其发送格式化后的消息。
     *
     * @note 仅当目标是玩家时才发送消息
     */
    template<typename... Args>
    static void NotifyModification(ChatHandler* handler, Unit* target, TrinityStrings resourceMessage, TrinityStrings resourceReportMessage, Args&&... args)
    {
        if (Player* player = target->ToPlayer())
        {
            handler->PSendSysMessage(resourceMessage, handler->GetNameLink(player).c_str(), args...);
            if (handler->needReportToTarget(player))
                ChatHandler(player->GetSession()).PSendSysMessage(resourceReportMessage, handler->GetNameLink().c_str(), std::forward<Args>(args)...);
        }
    }

    /**
     * @brief 检查修改资源参数的有效性
     * @param handler 聊天处理器指针
     * @param args 命令参数字符串
     * @param target 目标玩家
     * @param res [out] 输出的资源值（已乘以倍数）
     * @param resmax [out] 输出的资源最大值（已乘以倍数）
     * @param multiplier 倍数因子，默认为1（用于能量、怒气等转换）
     * @return 如果参数有效且通过权限检查返回true，否则返回false
     *
     * 该函数用于验证修改资源命令的参数：
     * 1. 检查参数是否为空
     * 2. 解析参数并应用倍数
     * 3. 验证值的范围（必须大于0且res <= resmax）
     * 4. 检查目标是否存在
     * 5. 检查执行者权限是否高于目标
     *
     * @note 能量和怒气在游戏中显示值与实际存储值有10倍差异
     */
    static bool CheckModifyResources(ChatHandler* handler, char const* args, Player* target, int32& res, int32& resmax, int8 const multiplier = 1)
    {
        if (!*args)
            return false;

        res = atoi((char*)args) * multiplier;
        resmax = atoi((char*)args) * multiplier;

        if (res < 1 || resmax < 1 || resmax < res)
        {
            handler->SendSysMessage(LANG_BAD_VALUE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (!target)
        {
            handler->SendSysMessage(LANG_NO_CHAR_SELECTED);
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (handler->HasLowerSecurity(target, ObjectGuid::Empty))
            return false;

        return true;
    }

    /**
     * @brief 修改玩家生命值命令处理函数
     * @param handler 聊天处理器指针
     * @param args 命令参数（生命值）
     * @return 命令执行成功返回true，失败返回false
     *
     * .modify hp <值> - 设置选中玩家或自己的生命值
     *
     * 执行流程：
     * 1. 获取目标玩家（选中的玩家或自己）
     * 2. 检查参数有效性
     * 3. 设置最大生命值和当前生命值
     * 4. 发送修改通知
     *
     * @see CheckModifyResources() 参数验证
     */
    //Edit Player HP
    static bool HandleModifyHPCommand(ChatHandler* handler, char const* args)
    {
        int32 hp, hpmax;
        Player* target = handler->getSelectedPlayerOrSelf();
        if (CheckModifyResources(handler, args, target, hp, hpmax))
        {
            NotifyModification(handler, target, LANG_YOU_CHANGE_HP, LANG_YOURS_HP_CHANGED, hp, hpmax);
            target->SetMaxHealth(hpmax);
            target->SetHealth(hp);
            return true;
        }
        return false;
    }

    /**
     * @brief 修改玩家法力值命令处理函数
     * @param handler 聊天处理器指针
     * @param args 命令参数（法力值）
     * @return 命令执行成功返回true，失败返回false
     *
     * .modify mana <值> - 设置选中玩家或自己的法力值
     *
     * 执行流程：
     * 1. 获取目标玩家
     * 2. 检查参数有效性
     * 3. 设置最大法力值和当前法力值
     * 4. 发送修改通知
     */
    //Edit Player Mana
    static bool HandleModifyManaCommand(ChatHandler* handler, char const* args)
    {
        int32 mana, manamax;
        Player* target = handler->getSelectedPlayerOrSelf();

        if (CheckModifyResources(handler, args, target, mana, manamax))
        {
            NotifyModification(handler, target, LANG_YOU_CHANGE_MANA, LANG_YOURS_MANA_CHANGED, mana, manamax);
            target->SetMaxPower(POWER_MANA, manamax);
            target->SetPower(POWER_MANA, mana);
            return true;
        }
        return false;
    }

    /**
     * @brief 修改玩家能量命令处理函数
     * @param handler 聊天处理器指针
     * @param args 命令参数（能量值）
     * @return 命令执行成功返回true，失败返回false
     *
     * .modify energy <值> - 设置选中玩家或自己的能量值
     *
     * 执行流程：
     * 1. 获取目标玩家
     * 2. 检查参数有效性（能量值乘以10作为实际存储值）
     * 3. 设置最大能量和当前能量
     * 4. 发送修改通知（显示值除以10）
     *
     * @note 能量在游戏中显示值与内部存储值有10倍差异
     *       显示值乘以10 = 存储值
     */
    //Edit Player Energy
    static bool HandleModifyEnergyCommand(ChatHandler* handler, char const* args)
    {
        int32 energy, energymax;
        Player* target = handler->getSelectedPlayerOrSelf();
        int8 const energyMultiplier = 10;
        if (CheckModifyResources(handler, args, target, energy, energymax, energyMultiplier))
        {
            NotifyModification(handler, target, LANG_YOU_CHANGE_ENERGY, LANG_YOURS_ENERGY_CHANGED, energy / energyMultiplier, energymax / energyMultiplier);
            target->SetMaxPower(POWER_ENERGY, energymax);
            target->SetPower(POWER_ENERGY, energy);
            return true;
        }
        return false;
    }

    /**
     * @brief 修改玩家怒气命令处理函数
     * @param handler 聊天处理器指针
     * @param args 命令参数（怒气值）
     * @return 命令执行成功返回true，失败返回false
     *
     * .modify rage <值> - 设置选中玩家或自己的怒气值
     *
     * 执行流程：
     * 1. 获取目标玩家
     * 2. 检查参数有效性（怒气值乘以10作为实际存储值）
     * 3. 设置最大怒气和当前怒气
     * 4. 发送修改通知（显示值除以10）
     *
     * @note 怒气在游戏中显示值与内部存储值有10倍差异
     *       显示值乘以10 = 存储值
     */
    //Edit Player Rage
    static bool HandleModifyRageCommand(ChatHandler* handler, char const* args)
    {
        int32 rage, ragemax;
        Player* target = handler->getSelectedPlayerOrSelf();
        int8 const rageMultiplier = 10;
        if (CheckModifyResources(handler, args, target, rage, ragemax, rageMultiplier))
        {
            NotifyModification(handler, target, LANG_YOU_CHANGE_RAGE, LANG_YOURS_RAGE_CHANGED, rage / rageMultiplier, ragemax / rageMultiplier);
            target->SetMaxPower(POWER_RAGE, ragemax);
            target->SetPower(POWER_RAGE, rage);
            return true;
        }
        return false;
    }

    /**
     * @brief 修改玩家符文能量命令处理函数
     * @param handler 聊天处理器指针
     * @param args 命令参数（符文能量值）
     * @return 命令执行成功返回true，失败返回false
     *
     * .modify runicpower <值> - 设置选中玩家或自己的符文能量值
     *
     * 执行流程：
     * 1. 获取目标玩家
     * 2. 检查参数有效性（符文能量值乘以10作为实际存储值）
     * 3. 设置最大符文能量和当前符文能量
     * 4. 发送修改通知（显示值除以10）
     *
     * @note 符文能量在游戏中显示值与内部存储值有10倍差异
     *       这是死亡骑士职业使用的资源类型
     */
    // Edit Player Runic Power
    static bool HandleModifyRunicPowerCommand(ChatHandler* handler, char const* args)
    {
        int32 rune, runemax;
        Player* target = handler->getSelectedPlayerOrSelf();
        int8 const runeMultiplier = 10;
        if (CheckModifyResources(handler, args, target, rune, runemax, runeMultiplier))
        {
            NotifyModification(handler, target, LANG_YOU_CHANGE_RUNIC_POWER, LANG_YOURS_RUNIC_POWER_CHANGED, rune / runeMultiplier, runemax / runeMultiplier);
            target->SetMaxPower(POWER_RUNIC_POWER, runemax);
            target->SetPower(POWER_RUNIC_POWER, rune);
            return true;
        }
        return false;
    }

    /**
     * @brief 修改单位阵营命令处理函数
     * @param handler 聊天处理器指针
     * @param factionid 阵营ID（可选）
     * @param flag 单位标志（可选）
     * @param npcflag NPC标志（可选）
     * @param dyflag 动态标志（可选）
     * @return 命令执行成功返回true，失败返回false
     *
     * .modify faction [阵营ID] [标志] [NPC标志] [动态标志]
     *
     * 执行流程：
     * 1. 获取选中的生物目标
     * 2. 如果未提供参数，显示当前阵营信息
     * 3. 如果提供阵营ID，验证其有效性
     * 4. 设置新的阵营、单位标志、NPC标志和动态标志
     *
     * @note 此命令主要用于NPC/生物，不是玩家阵营修改
     *       阵营模板ID定义在 FactionTemplate.dbc 中
     */
    //Edit Player Faction
    static bool HandleModifyFactionCommand(ChatHandler* handler, Optional<uint32> factionid, Optional<uint32> flag, Optional<uint32> npcflag, Optional<uint32> dyflag)
    {
        Creature* target = handler->getSelectedCreature();
        if (!target)
        {
            handler->SendSysMessage(LANG_SELECT_CREATURE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (!flag)
            flag = target->GetUnitFlags();

        if (!npcflag)
            npcflag = target->GetNpcFlags();

        if (!dyflag)
            dyflag = target->GetDynamicFlags();

        if (!factionid)
        {
            handler->PSendSysMessage(LANG_CURRENT_FACTION, target->GetGUID().GetCounter(), *factionid, *flag, *npcflag, *dyflag);
            return true;
        }

        if (!sFactionTemplateStore.LookupEntry(*factionid))
        {
            handler->PSendSysMessage(LANG_WRONG_FACTION, *factionid);
            handler->SetSentErrorMessage(true);
            return false;
        }

        handler->PSendSysMessage(LANG_YOU_CHANGE_FACTION, target->GetGUID().GetCounter(), *factionid, *flag, *npcflag, *dyflag);

        target->SetFaction(*factionid);
        target->ReplaceAllUnitFlags(UnitFlags(*flag));
        target->ReplaceAllNpcFlags(NPCFlags(*npcflag));
        target->ReplaceAllDynamicFlags(*dyflag);

        return true;
    }

    /**
     * @brief 修改玩家法术修正值命令处理函数
     * @param handler 聊天处理器指针
     * @param args 命令参数（法术平坦修正ID 操作符 值 标记）
     * @return 命令执行成功返回true，失败返回false
     *
     * .modify spell <法术平坦修正ID> <操作符> <值> [标记]
     *
     * 该命令直接修改玩家的法术修正值，用于调试法术效果。
     * 参数格式：<flatid> <op> <val> [mark]
     *
     * 执行流程：
     * 1. 解析参数：法术平坦修正ID、操作符、值、标记
     * 2. 获取目标玩家
     * 3. 检查权限
     * 4. 发送修改包（SMSG_SET_FLAT_SPELL_MODIFIER）给客户端
     *
     * @note 这是一个底层调试命令，需要深入了解法术系统
     */
    //Edit Player Spell
    static bool HandleModifySpellCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        char* pspellflatid = strtok((char*)args, " ");
        if (!pspellflatid)
            return false;

        char* pop = strtok(nullptr, " ");
        if (!pop)
            return false;

        char* pval = strtok(nullptr, " ");
        if (!pval)
            return false;

        uint16 mark;

        char* pmark = strtok(nullptr, " ");

        uint8 spellflatid = atoi(pspellflatid);
        uint8 op   = atoi(pop);
        uint16 val = atoi(pval);
        if (!pmark)
            mark = 65535;
        else
            mark = atoi(pmark);

        Player* target = handler->getSelectedPlayerOrSelf();
        if (target == nullptr)
        {
            handler->SendSysMessage(LANG_NO_CHAR_SELECTED);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // check online security
        if (handler->HasLowerSecurity(target, ObjectGuid::Empty))
            return false;

        handler->PSendSysMessage(LANG_YOU_CHANGE_SPELLFLATID, spellflatid, val, mark, handler->GetNameLink(target).c_str());
        if (handler->needReportToTarget(target))
            ChatHandler(target->GetSession()).PSendSysMessage(LANG_YOURS_SPELLFLATID_CHANGED, handler->GetNameLink().c_str(), spellflatid, val, mark);

        WorldPacket data(SMSG_SET_FLAT_SPELL_MODIFIER, (1+1+2+2));
        data << uint8(spellflatid);
        data << uint8(op);
        data << uint16(val);
        data << uint16(mark);
        target->SendDirectMessage(&data);

        return true;
    }

    /**
     * @brief 修改玩家或宠物天赋点数命令处理函数
     * @param handler 聊天处理器指针
     * @param args 命令参数（天赋点数）
     * @return 命令执行成功返回true，失败返回false
     *
     * .modify talentpoints <值> - 设置选中玩家或宠物的天赋点数
     *
     * 执行流程：
     * 1. 解析天赋点数参数
     * 2. 获取目标单位（玩家或宠物）
     * 3. 如果是玩家，设置自由天赋点数并更新客户端
     * 4. 如果是宠物（永久的），设置宠物天赋点数并通知主人
     *
     * @note 宠物天赋点数修改需要主人在线
     */
    //Edit Player TP
    static bool HandleModifyTalentCommand (ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        int tp = atoi((char*)args);
        if (tp < 0)
            return false;

        Unit* target = handler->getSelectedUnit();
        if (!target)
        {
            handler->SendSysMessage(LANG_NO_CHAR_SELECTED);
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (target->GetTypeId() == TYPEID_PLAYER)
        {
            // check online security
            if (handler->HasLowerSecurity(target->ToPlayer(), ObjectGuid::Empty))
                return false;
            target->ToPlayer()->SetFreeTalentPoints(tp);
            target->ToPlayer()->SendTalentsInfoData(false);
            return true;
        }
        else if (target->IsPet())
        {
            Unit* owner = target->GetOwner();
            if (owner && owner->GetTypeId() == TYPEID_PLAYER && ((Pet*)target)->IsPermanentPetFor(owner->ToPlayer()))
            {
                // check online security
                if (handler->HasLowerSecurity(owner->ToPlayer(), ObjectGuid::Empty))
                    return false;
                ((Pet*)target)->SetFreeTalentPoints(tp);
                owner->ToPlayer()->SendTalentsInfoData(true);
                return true;
            }
        }

        handler->SendSysMessage(LANG_NO_CHAR_SELECTED);
        handler->SetSentErrorMessage(true);
        return false;
    }

    /**
     * @brief 检查修改速度参数的有效性（已解析速度值版本）
     * @param handler 聊天处理器指针
     * @param target 目标单位
     * @param speed 速度值（已解析）
     * @param minimumBound 最小速度边界
     * @param maximumBound 最大速度边界
     * @param checkInFlight 是否检查玩家是否在飞行中，默认为true
     * @return 如果速度有效且通过权限检查返回true，否则返回false
     *
     * 该函数用于验证速度值是否在有效范围内，并检查：
     * 1. 速度是否在[min, max]范围内
     * 2. 目标是否存在
     * 3. 执行者权限是否高于目标（对于玩家）
     * 4. 玩家是否在飞行中（如果需要检查）
     *
     * @note 飞行中的玩家不能修改移动速度
     */
    static bool CheckModifySpeed(ChatHandler* handler, Unit* target, float speed, float minimumBound, float maximumBound, bool checkInFlight = true)
    {
        if (speed > maximumBound || speed < minimumBound)
        {
            handler->SendSysMessage(LANG_BAD_VALUE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (!target)
        {
            handler->SendSysMessage(LANG_NO_CHAR_SELECTED);
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (Player* player = target->ToPlayer())
        {
            // check online security
            if (handler->HasLowerSecurity(player, ObjectGuid::Empty))
                return false;

            if (player->IsInFlight() && checkInFlight)
            {
                handler->PSendSysMessage(LANG_CHAR_IN_FLIGHT, handler->GetNameLink(player).c_str());
                handler->SetSentErrorMessage(true);
                return false;
            }
        }
        return true;
    }

    /**
     * @brief 检查修改速度参数的有效性（从字符串解析版本）
     * @param handler 聊天处理器指针
     * @param args 命令参数字符串
     * @param target 目标单位
     * @param speed [out] 输出的速度值
     * @param minimumBound 最小速度边界
     * @param maximumBound 最大速度边界
     * @param checkInFlight 是否检查玩家是否在飞行中，默认为true
     * @return 如果参数有效且通过权限检查返回true，否则返回false
     *
     * 该函数从字符串参数解析速度值，并调用另一个重载版本进行验证。
     * 执行流程：
     * 1. 检查参数是否为空
     * 2. 解析速度浮点值
     * 3. 调用重载版本进行完整验证
     */
    static bool CheckModifySpeed(ChatHandler* handler, char const* args, Unit* target, float& speed, float minimumBound, float maximumBound, bool checkInFlight = true)
    {
        if (!*args)
            return false;

        speed = (float)atof((char*)args);
        return CheckModifySpeed(handler, target, speed, minimumBound, maximumBound, checkInFlight);
    }

    /**
     * @brief 修改玩家所有速度命令处理函数
     * @param handler 聊天处理器指针
     * @param args 命令参数（速度倍率）
     * @return 命令执行成功返回true，失败返回false
     *
     * .modify speed all <速度> 或 .modify speed <速度>
     *
     * 该命令同时设置所有类型的速度：
     * - 行走速度（MOVE_WALK）
     * - 奔跑速度（MOVE_RUN）
     * - 游泳速度（MOVE_SWIM）
     * - 飞行速度（MOVE_FLIGHT）
     *
     * 速度有效范围：0.1 到 50.0
     *
     * @note 速度倍率是相对于基础速度的倍数，1.0为正常速度
     */
    //Edit Player Aspeed
    static bool HandleModifyASpeedCommand(ChatHandler* handler, char const* args)
    {
        float allSpeed;
        Player* target = handler->getSelectedPlayerOrSelf();
        if (CheckModifySpeed(handler, args, target, allSpeed, 0.1f, 50.0f))
        {
            NotifyModification(handler, target, LANG_YOU_CHANGE_ASPEED, LANG_YOURS_ASPEED_CHANGED, allSpeed);
            target->SetSpeedRate(MOVE_WALK, allSpeed);
            target->SetSpeedRate(MOVE_RUN, allSpeed);
            target->SetSpeedRate(MOVE_SWIM, allSpeed);
            target->SetSpeedRate(MOVE_FLIGHT, allSpeed);
            return true;
        }
        return false;
    }

    /**
     * @brief 修改玩家奔跑速度命令处理函数
     * @param handler 聊天处理器指针
     * @param args 命令参数（速度倍率）
     * @return 命令执行成功返回true，失败返回false
     *
     * .modify speed walk <速度> - 设置奔跑速度
     *
     * 只修改奔跑速度（MOVE_RUN），速度有效范围：0.1 到 50.0
     */
    //Edit Player Speed
    static bool HandleModifySpeedCommand(ChatHandler* handler, char const* args)
    {
        float Speed;
        Player* target = handler->getSelectedPlayerOrSelf();
        if (CheckModifySpeed(handler, args, target, Speed, 0.1f, 50.0f))
        {
            NotifyModification(handler, target, LANG_YOU_CHANGE_SPEED, LANG_YOURS_SPEED_CHANGED, Speed);
            target->SetSpeedRate(MOVE_RUN, Speed);
            return true;
        }
        return false;
    }

    /**
     * @brief 修改玩家游泳速度命令处理函数
     * @param handler 聊天处理器指针
     * @param args 命令参数（速度倍率）
     * @return 命令执行成功返回true，失败返回false
     *
     * .modify speed swim <速度> - 设置游泳速度
     *
     * 只修改游泳速度（MOVE_SWIM），速度有效范围：0.1 到 50.0
     */
    //Edit Player Swim Speed
    static bool HandleModifySwimCommand(ChatHandler* handler, char const* args)
    {
        float swimSpeed;
        Player* target = handler->getSelectedPlayerOrSelf();
        if (CheckModifySpeed(handler, args, target, swimSpeed, 0.1f, 50.0f))
        {
            NotifyModification(handler, target, LANG_YOU_CHANGE_SWIM_SPEED, LANG_YOURS_SWIM_SPEED_CHANGED, swimSpeed);
            target->SetSpeedRate(MOVE_SWIM, swimSpeed);
            return true;
        }
        return false;
    }

    /**
     * @brief 修改玩家后退速度命令处理函数
     * @param handler 聊天处理器指针
     * @param args 命令参数（速度倍率）
     * @return 命令执行成功返回true，失败返回false
     *
     * .modify speed backwalk <速度> - 设置后退速度
     *
     * 只修改后退速度（MOVE_RUN_BACK），速度有效范围：0.1 到 50.0
     */
    //Edit Player Backwards Walk Speed
    static bool HandleModifyBWalkCommand(ChatHandler* handler, char const* args)
    {
        float backSpeed;
        Player* target = handler->getSelectedPlayerOrSelf();
        if (CheckModifySpeed(handler, args, target, backSpeed, 0.1f, 50.0f))
        {
            NotifyModification(handler, target, LANG_YOU_CHANGE_BACK_SPEED, LANG_YOURS_BACK_SPEED_CHANGED, backSpeed);
            target->SetSpeedRate(MOVE_RUN_BACK, backSpeed);
            return true;
        }
        return false;
    }

    /**
     * @brief 修改玩家飞行速度命令处理函数
     * @param handler 聊天处理器指针
     * @param args 命令参数（速度倍率）
     * @return 命令执行成功返回true，失败返回false
     *
     * .modify speed fly <速度> - 设置飞行速度
     *
     * 只修改飞行速度（MOVE_FLIGHT），速度有效范围：0.1 到 50.0
     * 该命令不检查玩家是否在飞行状态（checkInFlight = false）
     *
     * @note 允许在飞行状态下修改飞行速度
     */
    //Edit Player Fly
    static bool HandleModifyFlyCommand(ChatHandler* handler, char const* args)
    {
        float flySpeed;
        Player* target = handler->getSelectedPlayerOrSelf();
        if (CheckModifySpeed(handler, args, target, flySpeed, 0.1f, 50.0f, false))
        {
            NotifyModification(handler, target, LANG_YOU_CHANGE_FLY_SPEED, LANG_YOURS_FLY_SPEED_CHANGED, flySpeed);
            target->SetSpeedRate(MOVE_FLIGHT, flySpeed);
            return true;
        }
        return false;
    }

    /**
     * @brief 修改玩家或生物缩放比例命令处理函数
     * @param handler 聊天处理器指针
     * @param args 命令参数（缩放比例）
     * @return 命令执行成功返回true，失败返回false
     *
     * .modify scale <比例> - 设置选中单位或自己的模型缩放比例
     *
     * 缩放比例有效范围：0.1 到 10.0
     * 该命令适用于玩家和NPC/生物
     *
     * @note 过大或过小的缩放可能导致视觉效果异常
     */
    //Edit Player or Creature Scale
    static bool HandleModifyScaleCommand(ChatHandler* handler, char const* args)
    {
        float Scale;
        Unit* target = handler->getSelectedUnit();
        if (CheckModifySpeed(handler, args, target, Scale, 0.1f, 10.0f, false))
        {
            NotifyModification(handler, target, LANG_YOU_CHANGE_SIZE, LANG_YOURS_SIZE_CHANGED, Scale);
            target->SetObjectScale(Scale);
            return true;
        }
        return false;
    }

    /**
     * @brief 使玩家骑乘坐骑命令处理函数
     * @param handler 聊天处理器指针
     * @param mount 坐骑显示ID
     * @param speed 移动速度
     * @return 命令执行成功返回true，失败返回false
     *
     * .modify mount <坐骑ID> <速度> - 使选中玩家或自己骑乘指定坐骑
     *
     * 执行流程：
     * 1. 验证坐骑显示ID是否有效（从CreatureDisplayInfoStore查找）
     * 2. 获取目标玩家
     * 3. 检查权限
     * 4. 验证速度范围（0.1 到 50.0）
     * 5. 让玩家骑乘坐骑并设置奔跑和飞行速度
     *
     * @note 坐骑ID是生物显示信息ID，定义在 CreatureDisplayInfo.dbc 中
     */
    //Enable Player mount
    static bool HandleModifyMountCommand(ChatHandler* handler, uint32 mount, float speed)
    {
        if (!sCreatureDisplayInfoStore.LookupEntry(mount))
        {
            handler->SendSysMessage(LANG_NO_MOUNT);
            handler->SetSentErrorMessage(true);
            return false;
        }

        Player* target = handler->getSelectedPlayerOrSelf();
        if (!target)
        {
            handler->SendSysMessage(LANG_NO_CHAR_SELECTED);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // check online security
        if (handler->HasLowerSecurity(target, ObjectGuid::Empty))
            return false;

        if (!CheckModifySpeed(handler, target, speed, 0.1f, 50.0f))
            return false;

        NotifyModification(handler, target, LANG_YOU_GIVE_MOUNT, LANG_MOUNT_GIVED);
        target->Mount(mount);
        target->SetSpeedRate(MOVE_RUN, speed);
        target->SetSpeedRate(MOVE_FLIGHT, speed);
        return true;
    }

    /**
     * @brief 修改玩家金币命令处理函数
     * @param handler 聊天处理器指针
     * @param args 命令参数（金币数量或金币字符串）
     * @return 命令执行成功返回true，失败返回false
     *
     * .modify money <数量> - 增加或减少选中玩家或自己的金币
     *
     * 参数格式支持两种方式：
     * 1. 数值方式：直接输入数值（可正可负）
     * 2. 字符串方式：使用"g"（金）、"s"（银）、"c"（铜）格式
     *    例如：10g20s30c 表示10金20银30铜
     *
     * 执行流程：
     * 1. 获取目标玩家
     * 2. 解析金币参数
     * 3. 如果金额为负，扣除金币（最多扣到0）
     * 4. 如果金额为正，增加金币（不超过MAX_MONEY_AMOUNT）
     * 5. 发送修改通知
     *
     * @note 金币上限为 MAX_MONEY_AMOUNT (214748g 36s 47c)
     */
    //Edit Player money
    static bool HandleModifyMoneyCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        Player* target = handler->getSelectedPlayerOrSelf();
        if (!target)
        {
            handler->SendSysMessage(LANG_NO_CHAR_SELECTED);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // check online security
        if (handler->HasLowerSecurity(target, ObjectGuid::Empty))
            return false;

        Optional<int32> moneyToAddO = 0;
        if (strchr(args, 'g') || strchr(args, 's') || strchr(args, 'c'))
            moneyToAddO = MoneyStringToMoney(std::string(args));
        else
            moneyToAddO = Trinity::StringTo<int32>(args);

        if (!moneyToAddO)
            return false;

        int32 moneyToAdd = *moneyToAddO;

        uint32 targetMoney = target->GetMoney();

        if (moneyToAdd < 0)
        {
            int32 newmoney = int32(targetMoney) + moneyToAdd;

            TC_LOG_DEBUG("misc", "{}", handler->PGetParseString(LANG_CURRENT_MONEY, targetMoney, moneyToAdd, newmoney));
            if (newmoney <= 0)
            {
                NotifyModification(handler, target, LANG_YOU_TAKE_ALL_MONEY, LANG_YOURS_ALL_MONEY_GONE);
                target->SetMoney(0);
            }
            else
            {
                if (newmoney > static_cast<int32>(MAX_MONEY_AMOUNT))
                    newmoney = MAX_MONEY_AMOUNT;

                handler->PSendSysMessage(LANG_YOU_TAKE_MONEY, abs(moneyToAdd), handler->GetNameLink(target).c_str());
                if (handler->needReportToTarget(target))
                    ChatHandler(target->GetSession()).PSendSysMessage(LANG_YOURS_MONEY_TAKEN, handler->GetNameLink().c_str(), abs(moneyToAdd));
                target->SetMoney(newmoney);
            }
        }
        else
        {
            handler->PSendSysMessage(LANG_YOU_GIVE_MONEY, moneyToAdd, handler->GetNameLink(target).c_str());
            if (handler->needReportToTarget(target))
                ChatHandler(target->GetSession()).PSendSysMessage(LANG_YOURS_MONEY_GIVEN, handler->GetNameLink().c_str(), moneyToAdd);

            if (targetMoney >= MAX_MONEY_AMOUNT - moneyToAdd)
                moneyToAdd -= targetMoney;

            target->ModifyMoney(moneyToAdd);
        }

        TC_LOG_DEBUG("misc", "{}", handler->PGetParseString(LANG_NEW_MONEY, targetMoney, moneyToAdd, target->GetMoney()));

        return true;
    }

    /**
     * @brief 修改单位字段位标志命令处理函数
     * @param handler 聊天处理器指针
     * @param args 命令参数（字段索引 位位置）
     * @return 命令执行成功返回true，失败返回false
     *
     * .modify bit <字段> <位> - 切换选中单位的指定位标志
     *
     * 该命令用于直接操作单位字段的位标志，是一个底层调试工具。
     * 如果该位已设置，则清除；如果未设置，则设置。
     *
     * 参数：
     * - 字段：从OBJECT_END开始的有效字段索引
     * - 位：1到32之间的位位置
     *
     * @note 这是对单位字段值的直接位操作，需要谨慎使用
     */
    //Edit Unit field
    static bool HandleModifyBitCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        Unit* target = handler->getSelectedUnit();
        if (!target)
        {
            handler->SendSysMessage(LANG_NO_CHAR_SELECTED);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // check online security
        if (target->GetTypeId() == TYPEID_PLAYER && handler->HasLowerSecurity(target->ToPlayer(), ObjectGuid::Empty))
            return false;

        char* pField = strtok((char*)args, " ");
        if (!pField)
            return false;

        char* pBit = strtok(nullptr, " ");
        if (!pBit)
            return false;

        uint16 field = atoi(pField);
        uint32 bit   = atoi(pBit);

        if (field < OBJECT_END || field >= target->GetValuesCount())
        {
            handler->SendSysMessage(LANG_BAD_VALUE);
            handler->SetSentErrorMessage(true);
            return false;
        }
        if (bit < 1 || bit > 32)
        {
            handler->SendSysMessage(LANG_BAD_VALUE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (target->HasFlag(field, (1<<(bit-1))))
        {
            target->RemoveFlag(field, (1<<(bit-1)));
            handler->PSendSysMessage(LANG_REMOVE_BIT, bit, field);
        }
        else
        {
            target->SetFlag(field, (1<<(bit-1)));
            handler->PSendSysMessage(LANG_SET_BIT, bit, field);
        }
        return true;
    }

    /**
     * @brief 修改玩家荣誉点数命令处理函数
     * @param handler 聊天处理器指针
     * @param args 命令参数（荣誉点数）
     * @return 命令执行成功返回true，失败返回false
     *
     * .modify honor <数量> - 增加选中玩家的荣誉点数（可正可负）
     *
     * 执行流程：
     * 1. 获取目标玩家
     * 2. 检查权限
     * 3. 修改荣誉点数
     * 4. 发送当前荣誉点数信息
     *
     * @note 荣誉点数用于PvP奖励系统
     */
    static bool HandleModifyHonorCommand (ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        Player* target = handler->getSelectedPlayerOrSelf();
        if (!target)
        {
            handler->SendSysMessage(LANG_PLAYER_NOT_FOUND);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // check online security
        if (handler->HasLowerSecurity(target, ObjectGuid::Empty))
            return false;

        int32 amount = (uint32)atoi(args);

        target->ModifyHonorPoints(amount);

        handler->PSendSysMessage(LANG_COMMAND_MODIFY_HONOR, handler->GetNameLink(target).c_str(), target->GetHonorPoints());

        return true;
    }

    /**
     * @brief 修改玩家醉酒状态命令处理函数
     * @param handler 聊天处理器指针
     * @param args 命令参数（醉酒等级，0-100）
     * @return 命令执行成功返回true，失败返回false
     *
     * .modify drunk <等级> - 设置选中玩家或自己的醉酒程度
     *
     * 醉酒等级范围：0（清醒）到 100（完全醉酒）
     * 超过100的值会被限制为100
     *
     * @note 醉酒状态会影响画面模糊效果和某些NPC的互动
     */
    static bool HandleModifyDrunkCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        uint8 drunklevel = (uint8)atoi(args);
        if (drunklevel > 100)
            drunklevel = 100;

        if (Player* target = handler->getSelectedPlayerOrSelf())
            target->SetDrunkValue(drunklevel);

        return true;
    }

    /**
     * @brief 修改玩家声望命令处理函数
     * @param handler 聊天处理器指针
     * @param args 命令参数（阵营ID 数值 或 阵营ID 声望等级名称 [偏移量]）
     * @return 命令执行成功返回true，失败返回false
     *
     * .modify reputation <阵营ID> <数值或等级名称> [偏移量]
     *
     * 该命令支持两种设置方式：
     * 1. 直接数值方式：.modify reputation <阵营ID> <数值>
     *    例如：.modify reputation 72 42000
     *
     * 2. 声望等级名称方式：.modify reputation <阵营ID> <等级名> [偏移量]
     *    等级名可以是：hated（仇恨）、hostile（敌对）、unfriendly（冷淡）、
     *                 neutral（中立）、friendly（友好）、honored（尊敬）、
     *                 revered（崇敬）、exalted（崇拜）
     *    例如：.modify reputation 72 exalted
     *         .modify reputation 72 revered 5000
     *
     * 执行流程：
     * 1. 获取目标玩家并检查权限
     * 2. 从链接提取阵营ID
     * 3. 解析声望数值或等级名称
     * 4. 验证阵营是否存在且有声望索引
     * 5. 设置声望值并发送状态更新
     *
     * @note 声望值范围：-42000（仇恨）到 42999（崇拜）
     */
    static bool HandleModifyRepCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        Player* target = handler->getSelectedPlayerOrSelf();
        if (!target)
        {
            handler->SendSysMessage(LANG_PLAYER_NOT_FOUND);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // check online security
        if (handler->HasLowerSecurity(target, ObjectGuid::Empty))
            return false;

        char* factionTxt = handler->extractKeyFromLink((char*)args, "Hfaction");
        if (!factionTxt)
            return false;

        uint32 factionId = atoi(factionTxt);

        int32 amount = 0;
        char *rankTxt = strtok(nullptr, " ");
        if (!factionId || !rankTxt)
            return false;

        amount = atoi(rankTxt);
        if ((amount == 0) && (rankTxt[0] != '-') && !isdigit((unsigned char)rankTxt[0]))
        {
            std::string rankStr = rankTxt;
            std::wstring wrankStr;
            if (!Utf8toWStr(rankStr, wrankStr))
                return false;
            wstrToLower(wrankStr);

            int r = 0;
            amount = -42000;
            for (; r < MAX_REPUTATION_RANK; ++r)
            {
                std::string rank = handler->GetTrinityString(ReputationRankStrIndex[r]);
                if (rank.empty())
                    continue;

                std::wstring wrank;
                if (!Utf8toWStr(rank, wrank))
                    continue;

                wstrToLower(wrank);

                if (wrank.substr(0, wrankStr.size()) == wrankStr)
                {
                    char *deltaTxt = strtok(nullptr, " ");
                    if (deltaTxt)
                    {
                        int32 delta = atoi(deltaTxt);
                        if ((delta < 0) || (delta > ReputationMgr::PointsInRank[r] -1))
                        {
                            handler->PSendSysMessage(LANG_COMMAND_FACTION_DELTA, (ReputationMgr::PointsInRank[r]-1));
                            handler->SetSentErrorMessage(true);
                            return false;
                        }
                        amount += delta;
                    }
                    break;
                }
                amount += ReputationMgr::PointsInRank[r];
            }
            if (r >= MAX_REPUTATION_RANK)
            {
                handler->PSendSysMessage(LANG_COMMAND_INVALID_PARAM, rankTxt);
                handler->SetSentErrorMessage(true);
                return false;
            }
        }

        FactionEntry const* factionEntry = sFactionStore.LookupEntry(factionId);

        if (!factionEntry)
        {
            handler->PSendSysMessage(LANG_COMMAND_FACTION_UNKNOWN, factionId);
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (factionEntry->ReputationIndex < 0)
        {
            handler->PSendSysMessage(LANG_COMMAND_FACTION_NOREP_ERROR, factionEntry->Name[handler->GetSessionDbcLocale()], factionId);
            handler->SetSentErrorMessage(true);
            return false;
        }

        target->GetReputationMgr().SetOneFactionReputation(factionEntry, amount, false);
        target->GetReputationMgr().SendState(target->GetReputationMgr().GetState(factionEntry));
        handler->PSendSysMessage(LANG_COMMAND_MODIFY_REP, factionEntry->Name[handler->GetSessionDbcLocale()], factionId,
            handler->GetNameLink(target).c_str(), target->GetReputationMgr().GetReputation(factionEntry));
        return true;
    }

    /**
     * @brief 变形玩家或生物命令处理函数
     * @param handler 聊天处理器指针
     * @param display_id 显示ID（模型ID）
     * @return 命令执行成功返回true，失败返回false
     *
     * .morph <显示ID> - 将选中单位或自己变形为指定模型
     *
     * 执行流程：
     * 1. 获取目标单位（选中单位或自己）
     * 2. 检查权限（如果目标是玩家）
     * 3. 设置单位的显示ID
     *
     * @note 显示ID定义在 CreatureDisplayInfo.dbc 中
     *       变形是临时的，登出或使用demorph命令可恢复
     */
    //morph creature or player
    static bool HandleModifyMorphCommand(ChatHandler* handler, uint32 display_id)
    {
        Unit* target = handler->getSelectedUnit();
        if (!target)
            target = handler->GetSession()->GetPlayer();

        // check online security
        else if (target->GetTypeId() == TYPEID_PLAYER && handler->HasLowerSecurity(target->ToPlayer(), ObjectGuid::Empty))
            return false;

        target->SetDisplayId(display_id);

        return true;
    }

    /**
     * @brief 设置玩家临时阶段掩码命令处理函数
     * @param handler 聊天处理器指针
     * @param phasemask 阶段掩码值
     * @return 命令执行成功返回true，失败返回false
     *
     * .modify phase <阶段掩码> - 设置选中单位或自己的阶段掩码
     *
     * 阶段掩码控制玩家/单位能看到的游戏对象和其他单位。
     * 不同的阶段掩码值让玩家看到不同阶段的游戏内容。
     *
     * 执行流程：
     * 1. 获取目标单位
     * 2. 检查权限
     * 3. 设置阶段掩码并更新客户端
     *
     * @note 这是临时修改，登出后阶段掩码会重置
     *       阶段系统用于任务链中的阶段性内容展示
     */
    //set temporary phase mask for player
    static bool HandleModifyPhaseCommand(ChatHandler* handler, uint32 phasemask)
    {
        Unit* target = handler->getSelectedUnit();
        if (!target)
            target = handler->GetSession()->GetPlayer();

        // check online security
        else if (target->GetTypeId() == TYPEID_PLAYER && handler->HasLowerSecurity(target->ToPlayer(), ObjectGuid::Empty))
            return false;

        target->SetPhaseMask(phasemask, true);
        return true;
    }

    /**
     * @brief 修改玩家姿态状态命令处理函数
     * @param handler 聊天处理器指针
     * @param args 命令参数（动画ID）
     * @return 命令执行成功返回true，失败返回false
     *
     * .modify standstate <动画ID> - 设置玩家的表情状态
     *
     * 动画ID对应不同的表情/姿态，如站立、坐下、睡觉等。
     * 该命令影响角色的持续表情动画。
     *
     * @note 表情状态定义在 Emotes.dbc 中
     */
    //change standstate
    static bool HandleModifyStandStateCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        uint32 anim_id = atoi((char*)args);
        handler->GetSession()->GetPlayer()->SetEmoteState(Emote(anim_id));

        return true;
    }

    /**
     * @brief 修改玩家竞技场点数命令处理函数
     * @param handler 聊天处理器指针
     * @param args 命令参数（竞技场点数）
     * @return 命令执行成功返回true，失败返回false
     *
     * .modify arenapoints <数量> - 增加选中玩家的竞技场点数
     *
     * 执行流程：
     * 1. 获取目标玩家
     * 2. 解析竞技场点数
     * 3. 修改竞技场点数
     * 4. 发送当前竞技场点数信息
     *
     * @note 竞技场点数用于购买高级PvP装备
     */
    static bool HandleModifyArenaCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        Player* target = handler->getSelectedPlayerOrSelf();
        if (!target)
        {
            handler->SendSysMessage(LANG_PLAYER_NOT_FOUND);
            handler->SetSentErrorMessage(true);
            return false;
        }

        int32 amount = (uint32)atoi(args);

        target->ModifyArenaPoints(amount);

        handler->PSendSysMessage(LANG_COMMAND_MODIFY_ARENA, handler->GetNameLink(target).c_str(), target->GetArenaPoints());

        return true;
    }

    /**
     * @brief 修改玩家性别命令处理函数
     * @param handler 聊天处理器指针
     * @param args 命令参数（"male" 或 "female"）
     * @return 命令执行成功返回true，失败返回false
     *
     * .modify gender <male/female> - 修改选中玩家或自己的性别
     *
     * 执行流程：
     * 1. 获取目标玩家
     * 2. 获取玩家种族信息
     * 3. 解析性别参数（"male"或"female"）
     * 4. 设置性别和原生性别
     * 5. 重新初始化显示ID以匹配新性别
     * 6. 发送修改通知
     *
     * @note 性别修改会改变角色的外观模型
     */
    static bool HandleModifyGenderCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        Player* target = handler->getSelectedPlayerOrSelf();

        if (!target)
        {
            handler->PSendSysMessage(LANG_PLAYER_NOT_FOUND);
            handler->SetSentErrorMessage(true);
            return false;
        }

        PlayerInfo const* info = sObjectMgr->GetPlayerInfo(target->GetRace(), target->GetClass());
        if (!info)
            return false;

        char const* gender_str = (char*)args;
        int gender_len = strlen(gender_str);

        Gender gender;

        if (!strncmp(gender_str, "male", gender_len))            // MALE
        {
            if (target->GetGender() == GENDER_MALE)
                return true;

            gender = GENDER_MALE;
        }
        else if (!strncmp(gender_str, "female", gender_len))    // FEMALE
        {
            if (target->GetGender() == GENDER_FEMALE)
                return true;

            gender = GENDER_FEMALE;
        }
        else
        {
            handler->SendSysMessage(LANG_MUST_MALE_OR_FEMALE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // Set gender
        target->SetGender(gender);
        target->SetNativeGender(gender);

        // Change display ID
        target->InitDisplayIds();

        char const* gender_full = gender ? "female" : "male";

        handler->PSendSysMessage(LANG_YOU_CHANGE_GENDER, handler->GetNameLink(target).c_str(), gender_full);

        if (handler->needReportToTarget(target))
            ChatHandler(target->GetSession()).PSendSysMessage(LANG_YOUR_GENDER_CHANGED, gender_full, handler->GetNameLink().c_str());

        return true;
    }

    /**
     * @brief 取消变形命令处理函数
     * @param handler 聊天处理器指针
     * @param args 未使用的参数
     * @return 命令执行成功返回true，失败返回false
     *
     * .demorph - 恢复选中单位或自己的原始模型
     *
     * 执行流程：
     * 1. 获取目标单位
     * 2. 检查权限
     * 3. 调用DeMorph恢复原始显示ID
     *
     * @note 这是.morph命令的逆操作，用于取消变形效果
     */
//demorph player or unit
    static bool HandleDeMorphCommand(ChatHandler* handler, char const* /*args*/)
    {
        Unit* target = handler->getSelectedUnit();
        if (!target)
            target = handler->GetSession()->GetPlayer();

        // check online security
        else if (target->GetTypeId() == TYPEID_PLAYER && handler->HasLowerSecurity(target->ToPlayer(), ObjectGuid::Empty))
            return false;

        target->DeMorph();

        return true;
    }

    /**
     * @brief 修改玩家经验值命令处理函数
     * @param handler 聊天处理器指针
     * @param args 命令参数（经验值数量）
     * @return 命令执行成功返回true，失败返回false
     *
     * .modify xp <数量> - 给予选中玩家或自己指定数量的经验值
     *
     * 执行流程：
     * 1. 解析经验值参数（必须大于0）
     * 2. 获取目标玩家
     * 3. 检查权限
     * 4. 给予玩家经验值（可能触发升级）
     *
     * @note 经验值给予会触发正常的升级流程和检查
     */
    // mod xp command
    static bool HandleModifyXPCommand(ChatHandler *handler, char const* args)
    {
        if (!*args)
            return false;

        int32 xp = atoi((char*)args);

        if (xp < 1)
        {
            handler->SendSysMessage(LANG_BAD_VALUE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        Player* target = handler->getSelectedPlayerOrSelf();
        if (!target)
        {
            handler->SendSysMessage(LANG_NO_CHAR_SELECTED);
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (handler->HasLowerSecurity(target, ObjectGuid::Empty))
            return false;

        // we can run the command
        target->GiveXP(xp, nullptr);
        return true;
    }
};

void AddSC_modify_commandscript()
{
    new modify_commandscript();
}
