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
 * @file cs_pet.cpp
 * @brief 宠物管理命令模块
 *
 * 本模块提供了一系列GM命令，用于管理玩家的宠物。
 * 主要功能包括：
 * - 创建宠物：将选中的生物驯服为玩家的宠物
 * - 学习技能：让宠物学习指定的法术
 * - 遗忘技能：让宠物遗忘指定的法术
 * - 设置等级：修改宠物的等级
 *
 * 这些命令主要用于游戏测试、宠物调试和玩家支持。
 * 所有命令都需要相应的RBAC权限才能执行，且仅限游戏内使用。
 */

#include "ScriptMgr.h"
#include "Chat.h"
#include "Language.h"
#include "Log.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Pet.h"
#include "Player.h"
#include "RBAC.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "WorldSession.h"

using namespace Trinity::ChatCommands;

/**
 * @brief 获取选中目标的宠物或自己的宠物
 * @param handler 聊天处理器指针
 * @return 返回宠物指针，如果没有找到返回nullptr
 *
 * 该辅助函数用于确定要操作的宠物对象：
 * 1. 如果选中的目标是玩家，返回该玩家的宠物
 * 2. 如果选中的目标是宠物，直接返回该宠物
 * 3. 否则，返回命令执行者自己的宠物
 *
 * @note 该函数为多个宠物命令提供统一的目标选择逻辑
 */
inline Pet* GetSelectedPlayerPetOrOwn(ChatHandler* handler)
{
    if (Unit* target = handler->getSelectedUnit())
    {
        if (target->GetTypeId() == TYPEID_PLAYER)
            return target->ToPlayer()->GetPet();
        if (target->IsPet())
            return target->ToPet();
        return nullptr;
    }
    Player* player = handler->GetSession()->GetPlayer();
    return player ? player->GetPet() : nullptr;
}

/**
 * @class pet_commandscript
 * @brief 宠物命令脚本类
 *
 * 该类继承自CommandScript，提供所有与宠物管理相关的GM命令。
 * 命令包括：创建宠物、学习技能、遗忘技能、设置等级等。
 * 所有命令都需要相应的RBAC权限，且仅限游戏内使用（Console::No）。
 */
class pet_commandscript : public CommandScript
{
public:
    /**
     * @brief 构造函数
     *
     * 初始化宠物命令脚本，注册命令名称为"pet_commandscript"
     */
    pet_commandscript() : CommandScript("pet_commandscript") { }

    /**
     * @brief 获取所有宠物命令的命令表
     * @return 返回命令表，包含所有宠物相关命令的定义
     *
     * 该函数注册了以下命令：
     * - pet create: 创建宠物（将选中生物驯服为宠物）
     * - pet learn: 让宠物学习法术
     * - pet unlearn: 让宠物遗忘法术
     * - pet level: 设置宠物等级
     *
     * 所有命令都设置为Console::No，即仅限游戏内使用
     */
    ChatCommandTable GetCommands() const override
    {
        // 宠物命令子表
        static ChatCommandTable petCommandTable =
        {
            { "create",  HandlePetCreateCommand,  rbac::RBAC_PERM_COMMAND_PET_CREATE,  Console::No },
            { "learn",   HandlePetLearnCommand,   rbac::RBAC_PERM_COMMAND_PET_LEARN,   Console::No },
            { "unlearn", HandlePetUnlearnCommand, rbac::RBAC_PERM_COMMAND_PET_UNLEARN, Console::No },
            { "level",   HandlePetLevelCommand,   rbac::RBAC_PERM_COMMAND_PET_LEVEL,   Console::No },
        };

        static ChatCommandTable commandTable =
        {
            { "pet", petCommandTable },
        };
        return commandTable;
    }

    /**
     * @brief 创建宠物命令处理函数
     * @param handler 聊天处理器指针
     * @return 命令执行成功返回true，失败返回false
     *
     * .pet create - 将选中的生物驯服为自己的宠物
     *
     * 执行流程：
     * 1. 获取执行者玩家和选中的生物目标
     * 2. 验证目标是否可以驯服（不是宠物、不是玩家）
     * 3. 检查生物的家族类型（CREATURE_FAMILY_NONE会导致崩溃）
     * 4. 检查玩家是否已有宠物
     * 5. 创建驯服的宠物
     * 6. 消除原始生物
     * 7. 计算宠物等级（至少比玩家低5级）
     * 8. 添加宠物到世界并显示升级效果
     * 9. 设置玩家拥有的宠物
     * 10. 初始化宠物天赋并保存到数据库
     *
     * @note 只能驯服具有有效家族类型的生物
     *       猎人、术士、死亡骑士等职业使用此功能
     */
    static bool HandlePetCreateCommand(ChatHandler* handler)
    {
        Player* player = handler->GetSession()->GetPlayer();
        Creature* creatureTarget = handler->getSelectedCreature();

        if (!creatureTarget || creatureTarget->IsPet() || creatureTarget->GetTypeId() == TYPEID_PLAYER)
        {
            handler->PSendSysMessage(LANG_SELECT_CREATURE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        CreatureTemplate const* creatureTemplate = creatureTarget->GetCreatureTemplate();
        // Creatures with family CREATURE_FAMILY_NONE crashes the server
        if (creatureTemplate->family == CREATURE_FAMILY_NONE)
        {
            handler->PSendSysMessage("This creature cannot be tamed. Family id: 0 (CREATURE_FAMILY_NONE).");
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (player->GetPetGUID())
        {
            handler->PSendSysMessage("You already have a pet");
            handler->SetSentErrorMessage(true);
            return false;
        }

        // Everything looks OK, create new pet
        Pet* pet = player->CreateTamedPetFrom(creatureTarget);

        // "kill" original creature
        creatureTarget->DespawnOrUnsummon();

        uint8 level = (creatureTarget->GetLevel() < (player->GetLevel() - 5)) ? (player->GetLevel() - 5) : player->GetLevel();

        // prepare visual effect for levelup
        pet->SetUInt32Value(UNIT_FIELD_LEVEL, level - 1);

        // add to world
        pet->GetMap()->AddToMap(pet->ToCreature());

        // visual effect for levelup
        pet->SetUInt32Value(UNIT_FIELD_LEVEL, level);

        // caster have pet now
        player->SetMinion(pet, true);

        pet->InitTalentForLevel();

        pet->SavePetToDB(PET_SAVE_AS_CURRENT);
        player->PetSpellInitialize();

        return true;
    }

    /**
     * @brief 让宠物学习法术命令处理函数
     * @param handler 聊天处理器指针
     * @param spellInfo 要学习的法术信息指针
     * @return 命令执行成功返回true，失败返回false
     *
     * .pet learn <法术ID> - 让选中的宠物学习指定法术
     *
     * 执行流程：
     * 1. 获取选中的宠物（来自选中玩家、选中宠物或自己的宠物）
     * 2. 检查宠物是否已经学会该法术
     * 3. 验证法术是否有效
     * 4. 让宠物学习法术
     * 5. 发送学习成功消息
     *
     * @note 可以用于给宠物添加非默认技能，用于测试或特殊用途
     */
    static bool HandlePetLearnCommand(ChatHandler* handler, SpellInfo const* spellInfo)
    {
        Pet* pet = GetSelectedPlayerPetOrOwn(handler);

        if (!pet)
        {
            handler->SendSysMessage(LANG_SELECT_PLAYER_OR_PET);
            handler->SetSentErrorMessage(true);
            return false;
        }

        uint32 spellId = spellInfo->Id;

        // Check if pet already has it
        if (pet->HasSpell(spellId))
        {
            handler->PSendSysMessage("Pet already has spell: %u", spellId);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // Check if spell is valid
        if (!SpellMgr::IsSpellValid(spellInfo))
        {
            handler->PSendSysMessage(LANG_COMMAND_SPELL_BROKEN, spellId);
            handler->SetSentErrorMessage(true);
            return false;
        }

        pet->learnSpell(spellId);

        handler->PSendSysMessage("Pet has learned spell %u", spellId);
        return true;
    }

    /**
     * @brief 让宠物遗忘法术命令处理函数
     * @param handler 聊天处理器指针
     * @param spellInfo 要遗忘的法术信息指针
     * @return 命令执行成功返回true，失败返回false
     *
     * .pet unlearn <法术ID> - 让选中的宠物遗忘指定法术
     *
     * 执行流程：
     * 1. 获取选中的宠物
     * 2. 检查宠物是否学会该法术
     * 3. 如果已学会，让宠物遗忘法术
     * 4. 如果未学会，发送提示消息
     *
     * @note 可以用于移除宠物不需要的技能
     */
    static bool HandlePetUnlearnCommand(ChatHandler* handler, SpellInfo const* spellInfo)
    {
        Pet* pet = GetSelectedPlayerPetOrOwn(handler);
        if (!pet)
        {
            handler->SendSysMessage(LANG_SELECT_PLAYER_OR_PET);
            handler->SetSentErrorMessage(true);
            return false;
        }

        uint32 spellId = spellInfo->Id;

        if (pet->HasSpell(spellId))
            pet->removeSpell(spellId, false);
        else
            handler->PSendSysMessage("Pet doesn't have that spell");

        return true;
    }

    /**
     * @brief 设置宠物等级命令处理函数
     * @param handler 聊天处理器指针
     * @param level 等级值（可选，可以是绝对等级或相对变化）
     * @return 命令执行成功返回true，失败返回false
     *
     * .pet level [等级] - 设置选中宠物的等级
     *
     * 参数说明：
     * - 如果不提供等级，则将宠物等级设为主人等级
     * - 正数：增加指定等级
     * - 负数：减少指定等级
     * - 最终等级限制在1到主人等级之间
     *
     * 执行流程：
     * 1. 获取选中的宠物和其主人
     * 2. 计算新等级（限制有效范围）
     * 3. 设置宠物等级
     *
     * @note 宠物等级不能超过主人等级
     */
    static bool HandlePetLevelCommand(ChatHandler* handler, Optional<int32> level)
    {
        Pet* pet = GetSelectedPlayerPetOrOwn(handler);
        Player* owner = pet ? pet->GetOwner() : nullptr;
        if (!pet || !owner)
        {
            handler->SendSysMessage(LANG_SELECT_PLAYER_OR_PET);
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (!level)
            level = owner->GetLevel() - pet->GetLevel();

        if (level == 0 || level < -STRONG_MAX_LEVEL || level > STRONG_MAX_LEVEL)
        {
            handler->SendSysMessage(LANG_BAD_VALUE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        int32 newLevel = pet->GetLevel() + *level;
        if (newLevel < 1)
            newLevel = 1;
        else if (newLevel > owner->GetLevel())
            newLevel = owner->GetLevel();

        pet->GivePetLevel(newLevel);
        return true;
    }
};

void AddSC_pet_commandscript()
{
    new pet_commandscript();
}
