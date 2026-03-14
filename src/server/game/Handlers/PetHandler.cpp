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
 * @file PetHandler.cpp
 * @brief 宠物系统处理模块实现文件
 *
 * @职责 处理玩家与宠物的各种交互操作,包括:
 *       - 宠物召唤/解散
 *       - 宠物技能施放和自动施法设置
 *       - 宠物命令处理(攻击、跟随、停留、放弃)
 *       - 宠物改名和天赋学习
 *       - 宠物栏管理
 *       - 魅惑生物控制
 *       - 临时宠物(小动物)管理
 *
 * @调用时机
 *       - 玩家点击宠物动作栏按钮时
 *       - 玩家右键点击宠物时
 *       - 玩家使用宠物技能时
 *       - 宠物栏操作时
 *       - 宠物天赋学习时
 *
 * @性能注意事项
 *       - 宠物技能施放涉及法术系统,需要完整的法术验证
 *       - 多宠物情况需要遍历所有受控单位
 *       - 宠物改名需要数据库事务处理
 *       - 宠物栏操作涉及数据库更新
 */

#include "WorldSession.h"
#include "Common.h"
#include "CreatureAI.h"
#include "DatabaseEnv.h"
#include "Group.h"
#include "Log.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Pet.h"
#include "PetAI.h"
#include "PetPackets.h"
#include "Player.h"
#include "Spell.h"
#include "SpellHistory.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Util.h"
#include "World.h"
#include "WorldPacket.h"

/**
 * @brief 处理解散小动物操作码
 *
 * @职责 处理玩家解散临时宠物(非战斗宠物/小动物)的请求
 *       这些小动物通常是装饰性的,没有战斗能力
 *
 * @param packet 接收到的网络包数据
 *        - CritterGUID: 小动物的GUID
 *
 * @返回值 无
 *
 * @主要流程
 *   1. 查找小动物单位
 *   2. 验证小动物是否属于玩家
 *   3. 如果是召唤生物,执行解散操作
 */
void WorldSession::HandleDismissCritter(WorldPackets::Pet::DismissCritter& packet)
{
    // 查找小动物(可能是生物、宠物或载具)
    Unit* pet = ObjectAccessor::GetCreatureOrPetOrVehicle(*_player, packet.CritterGUID);

    if (!pet)
    {
        TC_LOG_DEBUG("entities.pet", "Vanitypet ({}) does not exist - player '{}' ({} / account: {}) attempted to dismiss it (possibly lagged out)",
            packet.CritterGUID.ToString(), GetPlayer()->GetName(), GetPlayer()->GetGUID().ToString(), GetAccountId());
        return;
    }

    // 验证小动物是否属于玩家
    if (_player->GetCritterGUID() == pet->GetGUID())
    {
         // 如果是召唤生物,执行解散
         if (pet->GetTypeId() == TYPEID_UNIT && pet->IsSummon())
             pet->ToTempSummon()->UnSummon();
    }
}

/**
 * @brief 处理宠物动作操作码
 *
 * @职责 处理玩家对宠物发出的各种命令和动作请求
 *       包括命令(攻击/跟随/停留/放弃)、反应模式(主动/被动/防御)、技能施放等
 *
 * @param recvData 接收到的网络包数据
 *        - guid1: 宠物的GUID
 *        - data: 动作数据(包含技能ID和动作类型标志)
 *        - guid2: 目标单位的GUID(攻击命令时使用)
 *
 * @返回值 无
 *
 * @主要流程
 *   1. 解析宠物GUID、动作数据和目标GUID
 *   2. 从data中提取技能ID和动作类型标志
 *   3. 验证宠物是否存在且属于玩家
 *   4. 验证宠物存活状态(某些技能可以在死亡时施放)
 *   5. 特殊处理:魅惑玩家只能攻击
 *   6. 处理单个或多个同名宠物的情况
 *   7. 调用HandlePetActionHelper执行具体动作
 *
 * @动作类型标志说明
 *       - ACT_COMMAND (0x07): 命令类型(停留/跟随/攻击/放弃)
 *       - ACT_REACTION (0x06): 反应模式(被动/防御/主动)
 *       - ACT_DISABLED (0x81): 禁用自动施法
 *       - ACT_PASSIVE (0x01): 被动技能
 *       - ACT_ENABLED (0xC1): 启用自动施法
 */
void WorldSession::HandlePetAction(WorldPacket& recvData)
{
    ObjectGuid guid1;    // 宠物GUID
    uint32 data;         // 动作数据(高16位=动作类型,低16位=技能ID)
    ObjectGuid guid2;    // 目标GUID
    recvData >> guid1;
    recvData >> data;
    recvData >> guid2;

    // 从data中提取技能ID和动作类型标志
    uint32 spellid = UNIT_ACTION_BUTTON_ACTION(data);     // 低16位:技能ID或命令ID
    uint8 flag = UNIT_ACTION_BUTTON_TYPE(data);           // 高16位:动作类型标志

    // 获取宠物单位(也用于被魅惑的生物)
    Unit* pet = ObjectAccessor::GetUnit(*_player, guid1);
    TC_LOG_DEBUG("entities.pet", "HandlePetAction: {} - flag: {}, spellid: {}, target: {}.", guid1.ToString(), uint32(flag), spellid, guid2.ToString());

    // 验证宠物是否存在
    if (!pet)
    {
        TC_LOG_DEBUG("entities.pet", "HandlePetAction: {} doesn't exist for {} {}", guid1.ToString(), GetPlayer()->GetGUID().ToString(), GetPlayer()->GetName());
        return;
    }

    // 验证宠物是否属于玩家
    if (pet != GetPlayer()->GetFirstControlled())
    {
        TC_LOG_DEBUG("entities.pet", "HandlePetAction: {} does not belong to {} {}", guid1.ToString(), GetPlayer()->GetGUID().ToString(), GetPlayer()->GetName());
        return;
    }

    // 宠物死亡状态检查
    if (!pet->IsAlive())
    {
        // 某些技能可以在死亡时施放(如被动技能)
        SpellInfo const* spell = (flag == ACT_ENABLED || flag == ACT_PASSIVE) ? sSpellMgr->GetSpellInfo(spellid) : nullptr;
        if (!spell)
            return;
        if (!spell->HasAttribute(SPELL_ATTR0_CASTABLE_WHILE_DEAD))
            return;
    }

    /// @todo 允许控制被魅惑的玩家?
    // 被魅惑的玩家只能执行攻击命令
    if (pet->GetTypeId() == TYPEID_PLAYER && !(flag == ACT_COMMAND && spellid == COMMAND_ATTACK))
        return;

    // 处理单个宠物或多个同名宠物的情况
    if (GetPlayer()->m_Controlled.size() == 1)
    {
        // 单个宠物,直接处理
        HandlePetActionHelper(pet, guid1, spellid, flag, guid2);
    }
    else
    {
        // 多个宠物,需要处理所有同名的宠物(例如图腾召唤的多个相同宠物)
        // 注意:解散宠物会改变m_Controlled,所以需要先收集所有目标宠物
        std::vector<Unit*> controlled;
        for (Unit::ControlList::iterator itr = GetPlayer()->m_Controlled.begin(); itr != GetPlayer()->m_Controlled.end(); ++itr)
            if ((*itr)->GetEntry() == pet->GetEntry() && (*itr)->IsAlive())
                controlled.push_back(*itr);

        // 对所有同名宠物执行相同的动作
        for (std::vector<Unit*>::iterator itr = controlled.begin(); itr != controlled.end(); ++itr)
            HandlePetActionHelper(*itr, guid1, spellid, flag, guid2);
    }
}

/**
 * @brief 处理宠物停止攻击操作码
 *
 * @职责 处理玩家命令宠物停止攻击的请求
 *       宠物会立即停止当前的攻击行为
 *
 * @param packet 接收到的网络包数据
 *        - PetGUID: 宠物的GUID
 *
 * @返回值 无
 *
 * @主要流程
 *   1. 查找宠物单位
 *   2. 验证宠物是否属于玩家(宠物或被魅惑单位)
 *   3. 验证宠物是否存活
 *   4. 停止宠物的攻击行为
 */
void WorldSession::HandlePetStopAttack(WorldPackets::Pet::PetStopAttack& packet)
{
    // 查找宠物(可能是生物、宠物或载具)
    Unit* pet = ObjectAccessor::GetCreatureOrPetOrVehicle(*_player, packet.PetGUID);

    if (!pet)
    {
        TC_LOG_ERROR("entities.pet", "HandlePetStopAttack: {} does not exist", packet.PetGUID.ToString());
        return;
    }

    // 验证宠物是否属于玩家
    if (pet != GetPlayer()->GetPet() && pet != GetPlayer()->GetCharmed())
    {
        TC_LOG_ERROR("entities.pet", "HandlePetStopAttack: {} isn't a pet or charmed creature of player {}",
            packet.PetGUID.ToString(), GetPlayer()->GetName());
        return;
    }

    // 宠物必须存活才能停止攻击
    if (!pet->IsAlive())
        return;

    // 停止攻击
    pet->AttackStop();
}

/**
 * @brief 宠物动作辅助处理函数
 *
 * @职责 实际执行宠物动作的内部处理函数
 *       根据动作类型执行相应的命令、反应模式或技能施放
 *
 * @param pet 宠物单位指针
 * @param guid1 宠物的GUID
 * @param spellid 技能ID或命令ID
 * @param flag 动作类型标志
 * @param guid2 目标单位的GUID
 *
 * @返回值 无
 *
 * @主要流程 根据flag类型分发到不同的处理分支:
 *   1. ACT_COMMAND (0x07): 命令类型
 *      - COMMAND_STAY: 停留命令
 *      - COMMAND_FOLLOW: 跟随命令
 *      - COMMAND_ATTACK: 攻击命令
 *      - COMMAND_ABANDON: 放弃/解散命令
 *   2. ACT_REACTION (0x06): 反应模式
 *      - REACT_PASSIVE: 被动模式
 *      - REACT_DEFENSIVE: 防御模式
 *      - REACT_AGGRESSIVE: 主动模式
 *   3. ACT_DISABLED/ACT_PASSIVE/ACT_ENABLED: 技能施放
 *      - 验证技能有效性
 *      - 检查施法条件
 *      - 执行施法或自动转向
 *      - 处理施法失败情况
 *
 * @性能注意事项
 *       - 技能施放涉及完整的法术验证流程
 *       - 攻击命令会触发AI的AttackStart,可能涉及复杂的寻路和目标切换
 */
void WorldSession::HandlePetActionHelper(Unit* pet, ObjectGuid guid1, uint32 spellid, uint16 flag, ObjectGuid guid2)
{
    // 获取宠物的魅惑信息
    CharmInfo* charmInfo = pet->GetCharmInfo();
    if (!charmInfo)
    {
        TC_LOG_DEBUG("entities.pet", "WorldSession::HandlePetAction(petGuid: {}, tagGuid: {}, spellId: {}, flag: {}): object {} is considered pet-like but doesn't have a charminfo!",
            guid1.ToString(), guid2.ToString(), spellid, flag, pet->GetGUID().ToString());
        return;
    }

    switch (flag)
    {
        case ACT_COMMAND: // 0x07 - 命令类型
            switch (spellid)
            {
                case COMMAND_STAY: // 停留命令
                    // 清除当前移动并设置为空闲状态
                    pet->GetMotionMaster()->Clear(MOTION_PRIORITY_NORMAL);
                    pet->GetMotionMaster()->MoveIdle();

                    // 更新宠物的命令状态
                    charmInfo->SetCommandState(COMMAND_STAY);
                    charmInfo->SetIsCommandAttack(false);
                    charmInfo->SetIsAtStay(true);
                    charmInfo->SetIsCommandFollow(false);
                    charmInfo->SetIsFollowing(false);
                    charmInfo->SetIsReturning(false);
                    // 保存停留位置
                    charmInfo->SaveStayPosition();
                    break;

                case COMMAND_FOLLOW: // 跟随命令
                    // 停止攻击和施法
                    pet->AttackStop();
                    pet->InterruptNonMeleeSpells(false);
                    // 设置跟随主人
                    pet->GetMotionMaster()->MoveFollow(_player, PET_FOLLOW_DIST, pet->GetFollowAngle());

                    // 更新宠物的命令状态
                    charmInfo->SetCommandState(COMMAND_FOLLOW);
                    charmInfo->SetIsCommandAttack(false);
                    charmInfo->SetIsAtStay(false);
                    charmInfo->SetIsReturning(true);
                    charmInfo->SetIsCommandFollow(true);
                    charmInfo->SetIsFollowing(false);
                    break;

                case COMMAND_ATTACK: // 攻击命令
                {
                    // 如果主人被缴械,宠物不能攻击
                    if (_player->HasAuraType(SPELL_AURA_MOD_PACIFY))
                    {
                        // pet->SendPetCastFail(spellid, SPELL_FAILED_PACIFIED);
                        /// @todo 发送正确的错误消息给客户端
                        return;
                    }

                    // 获取攻击目标
                    Unit* TargetUnit = ObjectAccessor::GetUnit(*_player, guid2);
                    if (!TargetUnit)
                        return;

                    // 验证目标是否为有效攻击目标
                    if (Unit* owner = pet->GetOwner())
                        if (!owner->IsValidAttackTarget(TargetUnit))
                            return;

                    // 如果宠物没有目标或目标不同,则开始攻击
                    if (pet->GetVictim() != TargetUnit || !pet->GetCharmInfo()->IsCommandAttack())
                    {
                        // 停止当前攻击
                        if (pet->GetVictim())
                            pet->AttackStop();

                        // 根据宠物类型处理攻击
                        if (pet->GetTypeId() != TYPEID_PLAYER && pet->ToCreature()->IsAIEnabled())
                        {
                            // 更新宠物状态为攻击模式
                            charmInfo->SetIsCommandAttack(true);
                            charmInfo->SetIsAtStay(false);
                            charmInfo->SetIsFollowing(false);
                            charmInfo->SetIsCommandFollow(false);
                            charmInfo->SetIsReturning(false);

                            // 启动攻击(使用PetAI的特殊方法强制切换目标)
                            CreatureAI* AI = pet->ToCreature()->AI();
                            if (PetAI* petAI = dynamic_cast<PetAI*>(AI))
                                petAI->_AttackStart(TargetUnit); // 强制目标切换
                            else
                                AI->AttackStart(TargetUnit);

                            // 10%几率播放特殊宠物攻击语音,否则播放低吼声
                            if (pet->IsPet() && ((Pet*)pet)->getPetType() == SUMMON_PET && pet != TargetUnit && urand(0, 100) < 10)
                                pet->SendPetTalk((uint32)PET_TALK_ATTACK);
                            else
                            {
                                // 90%几率播放宠物语音,100%几率播放被魅惑生物语音
                                pet->SendPetAIReaction(guid1);
                            }
                        }
                        else // 被魅惑的玩家
                        {
                            // 更新被魅惑玩家的状态
                            charmInfo->SetIsCommandAttack(true);
                            charmInfo->SetIsAtStay(false);
                            charmInfo->SetIsFollowing(false);
                            charmInfo->SetIsCommandFollow(false);
                            charmInfo->SetIsReturning(false);

                            // 直接攻击目标
                            pet->Attack(TargetUnit, true);
                            pet->SendPetAIReaction(guid1);
                        }
                    }
                    break;
                }

                case COMMAND_ABANDON: // 放弃/解散命令
                    // 如果是被魅惑的单位,停止魅惑
                    if (pet->GetCharmerGUID() == GetPlayer()->GetGUID())
                        _player->StopCastingCharm();
                    // 如果是玩家拥有的宠物
                    else if (pet->GetOwnerGUID() == GetPlayer()->GetGUID())
                    {
                        ASSERT(pet->GetTypeId() == TYPEID_UNIT);
                        if (pet->IsPet())
                        {
                            // 猎人宠物:永久删除
                            if (((Pet*)pet)->getPetType() == HUNTER_PET)
                                GetPlayer()->RemovePet((Pet*)pet, PET_SAVE_AS_DELETED);
                            // 召唤宠物:解散但不删除
                            else
                                GetPlayer()->RemovePet((Pet*)pet, PET_SAVE_NOT_IN_SLOT);
                        }
                        else if (pet->HasUnitTypeMask(UNIT_MASK_MINION))
                        {
                            // 仆从(如萨满图腾、法师水元素等):直接解散
                            ((Minion*)pet)->UnSummon();
                        }
                    }
                    break;

                default:
                    TC_LOG_ERROR("entities.pet", "WORLD: unknown PET flag Action {} and spellid {}.", uint32(flag), spellid);
            }
            break;

        case ACT_REACTION: // 0x6 - 反应模式
            switch (spellid)
            {
                case REACT_PASSIVE: // 被动模式
                    pet->AttackStop();
                    [[fallthrough]];
                case REACT_DEFENSIVE: // 防御模式
                case REACT_AGGRESSIVE: // 主动模式
                    if (pet->GetTypeId() == TYPEID_UNIT)
                        pet->ToCreature()->SetReactState(ReactStates(spellid));
                    break;
            }
            break;

        case ACT_DISABLED: // 0x81 - 禁用自动施法
        case ACT_PASSIVE:  // 0x01 - 被动技能
        case ACT_ENABLED:  // 0xC1 - 启用自动施法
        {
            Unit* unit_target = nullptr;

            // 获取技能目标
            if (guid2)
                unit_target = ObjectAccessor::GetUnit(*_player, guid2);

            // 验证技能是否存在
            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellid);
            if (!spellInfo)
            {
                TC_LOG_ERROR("spells.pet", "WORLD: unknown PET spell id {}", spellid);
                return;
            }

            // 检查技能是否为AOE伤害技能,如果是则不允许施放(防止误伤)
            for (SpellEffectInfo const& spellEffectInfo : spellInfo->GetEffects())
            {
                if (spellEffectInfo.TargetA.GetTarget() == TARGET_UNIT_SRC_AREA_ENEMY ||
                    spellEffectInfo.TargetA.GetTarget() == TARGET_UNIT_DEST_AREA_ENEMY ||
                    spellEffectInfo.TargetA.GetTarget() == TARGET_DEST_DYNOBJ_ENEMY)
                    return;
            }

            // 宠物必须学会该技能且技能不能是被动技能
            if (!pet->HasSpell(spellid) || spellInfo->IsPassive())
                return;

            // 清除状态标志,就像主人点击了攻击一样
            // AI会在AttackStart后重置这些标志,即使施法失败
            if (pet->GetCharmInfo())
            {
                pet->GetCharmInfo()->SetIsAtStay(false);
                pet->GetCharmInfo()->SetIsCommandAttack(true);
                pet->GetCharmInfo()->SetIsReturning(false);
                pet->GetCharmInfo()->SetIsFollowing(false);
            }

            // 创建法术对象
            Spell* spell = new Spell(pet, spellInfo, TRIGGERED_NONE);

            // 检查宠物施法条件
            SpellCastResult result = spell->CheckPetCast(unit_target);

            // 自动转向目标(除非被附身)
            if (result == SPELL_FAILED_UNIT_NOT_INFRONT && !pet->isPossessed() && !pet->IsVehicle())
            {
                if (unit_target)
                {
                    // 转向目标
                    if (!pet->HasSpellFocus())
                        pet->SetInFront(unit_target);
                    // 发送更新给目标玩家
                    if (Player* player = unit_target->ToPlayer())
                        pet->SendUpdateToPlayer(player);
                }
                else if (Unit* unit_target2 = spell->m_targets.GetUnitTarget())
                {
                    if (!pet->HasSpellFocus())
                        pet->SetInFront(unit_target2);
                    if (Player* player = unit_target2->ToPlayer())
                        pet->SendUpdateToPlayer(player);
                }

                // 发送更新给宠物主人
                if (Unit* powner = pet->GetCharmerOrOwner())
                    if (Player* player = powner->ToPlayer())
                        pet->SendUpdateToPlayer(player);

                result = SPELL_CAST_OK;
            }

            // 施法成功
            if (result == SPELL_CAST_OK)
            {
                unit_target = spell->m_targets.GetUnitTarget();

                // 10%几率播放特殊宠物攻击语音,否则播放低吼声
                // 实际上这只发生在特殊技能上,如小鬼的火焰护盾、虚空行者的折磨
                // 但检查每个技能太愚蠢了
                if (pet->IsPet() && (((Pet*)pet)->getPetType() == SUMMON_PET) && (pet != unit_target) && (urand(0, 100) < 10))
                    pet->SendPetTalk((uint32)PET_TALK_SPECIAL_SPELL);
                else
                {
                    pet->SendPetAIReaction(guid1);
                }

                // 如果目标是敌对单位且宠物没有被附身,则开始攻击
                if (unit_target && !GetPlayer()->IsFriendlyTo(unit_target) && !pet->isPossessed() && !pet->IsVehicle())
                {
                    // 如果宠物没有目标或目标不同,则切换目标
                    if (pet->GetVictim() != unit_target)
                    {
                        if (CreatureAI* AI = pet->ToCreature()->AI())
                        {
                            if (PetAI* petAI = dynamic_cast<PetAI*>(AI))
                                petAI->_AttackStart(unit_target); // 强制切换受害者
                            else
                                AI->AttackStart(unit_target);
                        }
                    }
                }

                // 准备施法
                spell->prepare(spell->m_targets);
            }
            else // 施法失败
            {
                // 发送施法失败消息
                if (pet->isPossessed() || pet->IsVehicle()) /// @todo: 确认此检查
                    Spell::SendCastResult(GetPlayer(), spellInfo, 0, result);
                else
                    spell->SendPetCastResult(result);

                // 重置冷却时间
                if (!pet->GetSpellHistory()->HasCooldown(spellid))
                    pet->GetSpellHistory()->ResetCooldown(spellid, true);

                // 清理法术对象
                spell->finish(false);
                delete spell;

                // 重置特定标志(施法失败时)。AI会重置其他标志
                if (pet->GetCharmInfo())
                    pet->GetCharmInfo()->SetIsCommandAttack(false);
            }
            break;
        }

        default:
            TC_LOG_ERROR("entities.pet", "WORLD: unknown PET flag Action {} and spellid {}.", uint32(flag), spellid);
    }
}

/**
 * @brief 处理查询宠物名称操作码
 *
 * @职责 处理客户端查询宠物名称的请求
 *       用于显示其他玩家的宠物信息
 *
 * @param recvData 接收到的网络包数据
 *        - petnumber: 宠物编号
 *        - petguid: 宠物的GUID
 *
 * @返回值 无
 *
 * @主要流程
 *   1. 读取宠物编号和GUID
 *   2. 发送宠物名称响应
 */
void WorldSession::HandleQueryPetName(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network.opcode", "WORLD: Received CMSG_PET_NAME_QUERY");

    uint32 petnumber;
    ObjectGuid petguid;

    recvData >> petnumber;
    recvData >> petguid;

    SendQueryPetNameResponse(petguid, petnumber);
}

/**
 * @brief 发送宠物名称查询响应
 *
 * @职责 向客户端发送宠物的名称信息
 *       包括宠物名称、时间戳和变格名称(如果有)
 *
 * @param petguid 宠物的GUID
 * @param petnumber 宠物编号
 *
 * @返回值 无
 *
 * @主要流程
 *   1. 查找宠物单位
 *   2. 如果宠物不存在,发送空响应
 *   3. 如果宠物存在,发送名称、时间戳和变格名称
 */
void WorldSession::SendQueryPetNameResponse(ObjectGuid petguid, uint32 petnumber)
{
    // 查找宠物
    Creature* pet = ObjectAccessor::GetCreatureOrPetOrVehicle(*_player, petguid);
    if (!pet)
    {
        // 宠物不存在,发送空响应
        WorldPacket data(SMSG_PET_NAME_QUERY_RESPONSE, (4+1+4+1));
        data << uint32(petnumber);
        data << uint8(0);   // 名称长度为0
        data << uint32(0);  // 时间戳为0
        data << uint8(0);   // 没有变格名称
        _player->SendDirectMessage(&data);
        return;
    }

    // 发送宠物名称响应
    WorldPacket data(SMSG_PET_NAME_QUERY_RESPONSE, (4+4+pet->GetName().size()+1));
    data << uint32(petnumber);
    data << pet->GetName();
    data << uint32(pet->GetUInt32Value(UNIT_FIELD_PET_NAME_TIMESTAMP));

    // 如果是宠物且有变格名称,发送变格名称
    if (pet->IsPet() && ((Pet*)pet)->GetDeclinedNames())
    {
        data << uint8(1);  // 有变格名称
        for (uint8 i = 0; i < MAX_DECLINED_NAME_CASES; ++i)
            data << ((Pet*)pet)->GetDeclinedNames()->name[i];
    }
    else
        data << uint8(0);  // 没有变格名称

    _player->SendDirectMessage(&data);
}

/**
 * @brief 检查宠物管理员NPC有效性
 *
 * @职责 验证玩家是否可以与宠物管理员NPC交互
 *       支持通过法术或GM权限打开宠物栏
 *
 * @param guid NPC的GUID(或者是玩家自己的GUID用于法术/GM情况)
 *
 * @返回值 true=验证通过,false=验证失败
 *
 * @主要流程
 *   1. 如果GUID是玩家自己,检查GM权限或打开宠物栏光环
 *   2. 如果GUID是NPC,验证NPC是否可以交互且是宠物管理员
 */
bool WorldSession::CheckStableMaster(ObjectGuid guid)
{
    // 法术情况或GM
    if (guid == GetPlayer()->GetGUID())
    {
        // 玩家必须有GM权限或打开宠物栏光环
        if (!GetPlayer()->IsGameMaster() && !GetPlayer()->HasAuraType(SPELL_AURA_OPEN_STABLE))
        {
            TC_LOG_DEBUG("entities.player.cheat", "{} attempt open stable in cheating way.", guid.ToString());
            return false;
        }
    }
    // 宠物管理员情况
    else
    {
        // 验证NPC是否存在且玩家可以与之交互(NPC必须有UNIT_NPC_FLAG_STABLEMASTER标志)
        if (!GetPlayer()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_STABLEMASTER))
        {
            TC_LOG_DEBUG("entities.player", "Stablemaster {} not found or you can't interact with him.", guid.ToString());
            return false;
        }
    }
    return true;
}

/**
 * @brief 处理宠物动作栏设置操作码
 *
 * @职责 处理玩家修改宠物动作栏的请求
 *       包括添加、移除、交换、启用/禁用技能等操作
 *
 * @param recvData 接收到的网络包数据
 *        - petguid: 宠物的GUID
 *        - count: 操作数量(1=移除/添加,2=交换)
 *        - position[]: 动作栏位置索引
 *        - data[]: 动作数据(包含技能ID和动作类型)
 *
 * @返回值 无
 *
 * @主要流程
 *   1. 验证宠物有效性
 *   2. 解析动作栏操作数据
 *   3. 验证操作合法性(命令和反应按钮不能移除,只能移动)
 *   4. 处理多个同名宠物的情况
 *   5. 更新动作栏并设置自动施法状态
 *
 * @注意事项
 *       - 命令和反应按钮只能移动,不能移除
 *       - 宠物必须学会技能才能添加到动作栏
 *       - ACT_ENABLED启用自动施法,ACT_DISABLED禁用自动施法
 */
void WorldSession::HandlePetSetAction(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network.opcode", "WORLD: Received CMSG_PET_SET_ACTION");

    ObjectGuid petguid;
    uint8  count;

    recvData >> petguid;

    // 获取宠物单位
    Unit* pet = ObjectAccessor::GetUnit(*_player, petguid);

    // 验证宠物有效性
    if (!pet || pet != _player->GetFirstControlled())
    {
        TC_LOG_ERROR("entities.pet", "HandlePetSetAction: Unknown {} or owner ({})", petguid.ToString(), _player->GetGUID().ToString());
        return;
    }

    // 获取宠物的魅惑信息
    CharmInfo* charmInfo = pet->GetCharmInfo();
    if (!charmInfo)
    {
        TC_LOG_ERROR("entities.pet", "WorldSession::HandlePetSetAction: object {} is considered pet-like but doesn't have a charminfo!", pet->GetGUID().ToString());
        return;
    }

    // 根据数据包大小确定操作数量
    // count=1: 移除或添加操作
    // count=2: 交换操作
    count = (recvData.size() == 24) ? 2 : 1;

    uint32 position[2];     // 动作栏位置索引
    uint32 data[2];         // 动作数据(技能ID+动作类型)
    bool move_command = false;  // 是否为命令移动操作

    // 读取操作数据
    for (uint8 i = 0; i < count; ++i)
    {
        recvData >> position[i];
        recvData >> data[i];

        uint8 act_state = UNIT_ACTION_BUTTON_TYPE(data[i]);

        // 忽略无效位置
        if (position[i] >= MAX_UNIT_ACTION_BAR_INDEX)
            return;

        // 在正常情况下,命令和反应按钮只能移动,不能移除
        // 移动时count==2,移除时count==1
        // 忽略移除命令|反应按钮的尝试(正常情况下不可能)
        if (act_state == ACT_COMMAND || act_state == ACT_REACTION)
        {
            if (count == 1)
                return;

            move_command = true;
        }
    }

    // 收集所有同名宠物(处理多个相同宠物的情况)
    std::vector<Unit*> pets;
    for (Unit* controlled : _player->m_Controlled)
        if (controlled->GetEntry() == pet->GetEntry() && controlled->IsAlive())
            pets.push_back(controlled);

    // 对所有同名宠物执行操作
    for (Unit* petControlled : pets)
    {
        // 检查交换操作的正确性
        // 在命令->技能交换时,客户端会先在另一个数据包中移除技能,所以只检查命令移动的正确性
        if (move_command)
        {
            uint8 act_state_0 = UNIT_ACTION_BUTTON_TYPE(data[0]);
            if (act_state_0 == ACT_COMMAND || act_state_0 == ACT_REACTION)
            {
                uint32 spell_id_0 = UNIT_ACTION_BUTTON_ACTION(data[0]);
                UnitActionBarEntry const* actionEntry_1 = charmInfo->GetActionBarEntry(position[1]);
                // 验证目标位置是否包含相同的动作
                if (!actionEntry_1 || spell_id_0 != actionEntry_1->GetAction() ||
                    act_state_0 != actionEntry_1->GetType())
                    return;
            }

            uint8 act_state_1 = UNIT_ACTION_BUTTON_TYPE(data[1]);
            if (act_state_1 == ACT_COMMAND || act_state_1 == ACT_REACTION)
            {
                uint32 spell_id_1 = UNIT_ACTION_BUTTON_ACTION(data[1]);
                UnitActionBarEntry const* actionEntry_0 = charmInfo->GetActionBarEntry(position[0]);
                // 验证目标位置是否包含相同的动作
                if (!actionEntry_0 || spell_id_1 != actionEntry_0->GetAction() ||
                    act_state_1 != actionEntry_0->GetType())
                    return;
            }
        }

        // 执行动作栏更新
        for (uint8 i = 0; i < count; ++i)
        {
            uint32 spell_id = UNIT_ACTION_BUTTON_ACTION(data[i]);
            uint8 act_state = UNIT_ACTION_BUTTON_TYPE(data[i]);

            TC_LOG_DEBUG("entities.pet", "Player {} has changed pet spell action. Position: {}, Spell: {}, State: 0x{:X}",
                _player->GetName(), position[i], spell_id, uint32(act_state));

            // 如果是技能操作(启用/禁用/施放)且指定了技能(0=移除技能)但宠物没学会,则不添加
            if (!((act_state == ACT_ENABLED || act_state == ACT_DISABLED || act_state == ACT_PASSIVE) && spell_id && !petControlled->HasSpell(spell_id)))
            {
                if (SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spell_id))
                {
                    // 启用自动施法
                    if (act_state == ACT_ENABLED)
                    {
                        if (petControlled->GetTypeId() == TYPEID_UNIT && petControlled->IsPet())
                            ((Pet*)petControlled)->ToggleAutocast(spellInfo, true);
                        else
                            // 对所有同名被魅惑单位启用自动施法
                            for (Unit::ControlList::iterator itr = GetPlayer()->m_Controlled.begin(); itr != GetPlayer()->m_Controlled.end(); ++itr)
                                if ((*itr)->GetEntry() == petControlled->GetEntry())
                                    (*itr)->GetCharmInfo()->ToggleCreatureAutocast(spellInfo, true);
                    }
                    // 禁用自动施法
                    else if (act_state == ACT_DISABLED)
                    {
                        if (petControlled->GetTypeId() == TYPEID_UNIT && petControlled->IsPet())
                            ((Pet*)petControlled)->ToggleAutocast(spellInfo, false);
                        else
                            // 对所有同名被魅惑单位禁用自动施法
                            for (Unit::ControlList::iterator itr = GetPlayer()->m_Controlled.begin(); itr != GetPlayer()->m_Controlled.end(); ++itr)
                                if ((*itr)->GetEntry() == petControlled->GetEntry())
                                    (*itr)->GetCharmInfo()->ToggleCreatureAutocast(spellInfo, false);
                    }
                }

                // 更新动作栏
                charmInfo->SetActionBar(position[i], spell_id, ActiveStates(act_state));
            }
        }
    }
}

void WorldSession::HandlePetRename(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network.opcode", "WORLD: Received CMSG_PET_RENAME");

    ObjectGuid petguid;
    uint8 isdeclined;

    std::string name;
    DeclinedName declinedname;

    recvData >> petguid;
    recvData >> name;
    recvData >> isdeclined;

    PetStable* petStable = _player->GetPetStable();
    Pet* pet = ObjectAccessor::GetPet(*_player, petguid);
    if (!pet || !pet->IsPet() || ((Pet*)pet)->getPetType() != HUNTER_PET || !pet->HasPetFlag(UNIT_PET_FLAG_CAN_BE_RENAMED) ||
        pet->GetOwnerGUID() != _player->GetGUID() || !pet->GetCharmInfo() ||
        !petStable || !petStable->CurrentPet || petStable->CurrentPet->PetNumber != pet->GetCharmInfo()->GetPetNumber())
        return;

    PetNameInvalidReason res = ObjectMgr::CheckPetName(name, GetSessionDbcLocale());
    if (res != PET_NAME_SUCCESS)
    {
        SendPetNameInvalid(res, name, nullptr);
        return;
    }

    if (sObjectMgr->IsReservedName(name))
    {
        SendPetNameInvalid(PET_NAME_RESERVED, name, nullptr);
        return;
    }

    pet->SetName(name);

    if (pet->GetOwner()->GetGroup())
        pet->GetOwner()->SetGroupUpdateFlag(GROUP_UPDATE_FLAG_PET_NAME);

    pet->RemovePetFlag(UNIT_PET_FLAG_CAN_BE_RENAMED);

    petStable->CurrentPet->Name = name;
    petStable->CurrentPet->WasRenamed = true;

    if (isdeclined)
    {
        for (uint8 i = 0; i < MAX_DECLINED_NAME_CASES; ++i)
        {
            recvData >> declinedname.name[i];
        }

        std::wstring wname;
        if (!Utf8toWStr(name, wname))
            return;

        if (!ObjectMgr::CheckDeclinedNames(wname, declinedname))
        {
            SendPetNameInvalid(PET_NAME_DECLENSION_DOESNT_MATCH_BASE_NAME, name, &declinedname);
            return;
        }
    }

    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    if (isdeclined)
    {
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_CHAR_PET_DECLINEDNAME);
        stmt->setUInt32(0, pet->GetCharmInfo()->GetPetNumber());
        trans->Append(stmt);

        stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_CHAR_PET_DECLINEDNAME);
        stmt->setUInt32(0, pet->GetCharmInfo()->GetPetNumber());
        stmt->setUInt32(1, _player->GetGUID().GetCounter());

        for (uint8 i = 0; i < 5; i++)
            stmt->setString(i + 2, declinedname.name[i]);

        trans->Append(stmt);
    }

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHAR_PET_NAME);
    stmt->setString(0, name);
    stmt->setUInt32(1, _player->GetGUID().GetCounter());
    stmt->setUInt32(2, pet->GetCharmInfo()->GetPetNumber());
    trans->Append(stmt);

    CharacterDatabase.CommitTransaction(trans);

    pet->SetPetNameTimestamp(uint32(GameTime::GetGameTime())); // cast can't be helped
}

void WorldSession::HandlePetAbandon(WorldPackets::Pet::PetAbandon& packet)
{
    if (!_player->IsInWorld())
        return;

    // pet/charmed
    Creature* pet = ObjectAccessor::GetCreatureOrPetOrVehicle(*_player, packet.PetGUID);
    if (pet && pet->ToPet() && pet->ToPet()->getPetType() == HUNTER_PET)
    {
        if (pet->GetGUID() == _player->GetPetGUID())
        {
            uint32 feelty = pet->GetPower(POWER_HAPPINESS);
            pet->SetPower(POWER_HAPPINESS, feelty > 50000 ? (feelty-50000) : 0);
        }

        _player->RemovePet(pet->ToPet(), PET_SAVE_AS_DELETED);
    }
}

void WorldSession::HandlePetSpellAutocastOpcode(WorldPackets::Pet::PetSpellAutocast& packet)
{
    Creature* pet = ObjectAccessor::GetCreatureOrPetOrVehicle(*_player, packet.PetGUID);
    if (!pet)
    {
        TC_LOG_ERROR("entities.pet", "WorldSession::HandlePetSpellAutocastOpcode: Pet {} not found.", packet.PetGUID.ToString());
        return;
    }

    if (pet != _player->GetGuardianPet() && pet != _player->GetCharmed())
    {
        TC_LOG_ERROR("entities.pet", "WorldSession::HandlePetSpellAutocastOpcode: {} isn't pet of player {} ({}).",
            packet.PetGUID.ToString(), GetPlayer()->GetName(), GetPlayer()->GetGUID().ToString());
        return;
    }

    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(packet.SpellID);
    if (!spellInfo)
    {
        TC_LOG_ERROR("spells.pet", "WorldSession::HandlePetSpellAutocastOpcode: Unknown spell id {} used by {}.", packet.SpellID, packet.PetGUID.ToString());
        return;
    }

    std::vector<Unit*> pets;
    for (Unit* controlled : _player->m_Controlled)
        if (controlled->GetEntry() == pet->GetEntry() && controlled->IsAlive())
            pets.push_back(controlled);

    for (Unit* petControlled : pets)
    {
        // do not add not learned spells/ passive spells
        if (!petControlled->HasSpell(packet.SpellID) || !spellInfo->IsAutocastable())
            return;

        CharmInfo* charmInfo = petControlled->GetCharmInfo();
        if (!charmInfo)
        {
            TC_LOG_ERROR("entities.pet", "WorldSession::HandlePetSpellAutocastOpcode: object {} is considered pet-like but doesn't have a charminfo!", petControlled->GetGUID().ToString());
            return;
        }

        if (Pet* summon = petControlled->ToPet())
            summon->ToggleAutocast(spellInfo, packet.AutocastEnabled);
        else
            charmInfo->ToggleCreatureAutocast(spellInfo, packet.AutocastEnabled);

        charmInfo->SetSpellAutocast(spellInfo, packet.AutocastEnabled);
    }
}

void WorldSession::HandlePetCastSpellOpcode(WorldPacket& recvPacket)
{
    TC_LOG_DEBUG("network.opcode", "WORLD: Received CMSG_PET_CAST_SPELL");

    ObjectGuid guid;
    uint8 castCount;
    uint32 spellId;
    uint8 castFlags;

    recvPacket >> guid >> castCount >> spellId >> castFlags;

    TC_LOG_DEBUG("entities.pet", "WORLD: CMSG_PET_CAST_SPELL, {}, castCount: {}, spellId {}, castFlags {}", guid.ToString(), castCount, spellId, castFlags);

    // This opcode is also sent from charmed and possessed units (players and creatures)
    if (!_player->GetGuardianPet() && !_player->GetCharmed())
        return;

    Unit* caster = ObjectAccessor::GetUnit(*_player, guid);

    if (!caster || (caster != _player->GetGuardianPet() && caster != _player->GetCharmed()))
    {
        TC_LOG_ERROR("entities.pet", "HandlePetCastSpellOpcode: {} isn't pet of player {} ({}).", guid.ToString(), GetPlayer()->GetName(), GetPlayer()->GetGUID().ToString());
        return;
    }

    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    if (!spellInfo)
    {
        TC_LOG_ERROR("spells.pet", "WORLD: unknown PET spell id {}", spellId);
        return;
    }

    SpellCastTargets targets;
    targets.Read(recvPacket, caster);
    HandleClientCastFlags(recvPacket, castFlags, targets);

    TriggerCastFlags triggerCastFlags = TRIGGERED_NONE;

    if (spellInfo->IsPassive())
        return;

    // cast only learned spells
    if (!caster->HasSpell(spellId))
    {
        bool allow = false;

        // allow casting of spells triggered by clientside periodic trigger auras
        if (caster->HasAuraTypeWithTriggerSpell(SPELL_AURA_PERIODIC_TRIGGER_SPELL_FROM_CLIENT, spellId))
        {
            allow = true;
            triggerCastFlags = TRIGGERED_FULL_MASK;
        }

        if (!allow)
            return;
    }

    Spell* spell = new Spell(caster, spellInfo, triggerCastFlags);
    spell->m_fromClient = true;
    spell->m_cast_count = castCount; // probably pending spell cast
    spell->InitExplicitTargets(targets);

    SpellCastResult result = spell->CheckPetCast(nullptr);

    if (result == SPELL_CAST_OK)
    {
        if (Creature* creature = caster->ToCreature())
        {
            if (Pet* pet = creature->ToPet())
            {
                // 10% chance to play special pet attack talk, else growl
                // actually this only seems to happen on special spells, fire shield for imp, torment for voidwalker, but it's stupid to check every spell
                if (pet->getPetType() == SUMMON_PET && (urand(0, 100) < 10))
                    pet->SendPetTalk(PET_TALK_SPECIAL_SPELL);
                else
                    pet->SendPetAIReaction(guid);
            }
        }

        spell->prepare(spell->m_targets);
    }
    else
    {
        spell->SendPetCastResult(result);

        if (!caster->GetSpellHistory()->HasCooldown(spellId))
            caster->GetSpellHistory()->ResetCooldown(spellId, true);

        spell->finish(false);
        delete spell;
    }
}

void WorldSession::SendPetNameInvalid(uint32 error, const std::string& name, DeclinedName *declinedName)
{
    WorldPacket data(SMSG_PET_NAME_INVALID, 4 + name.size() + 1 + 1);
    data << uint32(error);
    data << name;
    if (declinedName)
    {
        data << uint8(1);
        for (uint32 i = 0; i < MAX_DECLINED_NAME_CASES; ++i)
            data << declinedName->name[i];
    }
    else
        data << uint8(0);
    SendPacket(&data);
}

void WorldSession::HandlePetLearnTalent(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network.opcode", "WORLD: Received CMSG_PET_LEARN_TALENT");

    ObjectGuid guid;
    uint32 talentId, requestedRank;
    recvData >> guid >> talentId >> requestedRank;

    _player->LearnPetTalent(guid, talentId, requestedRank);
    _player->SendTalentsInfoData(true);
}

void WorldSession::HandleLearnPreviewTalentsPet(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network.opcode", "WORLD: Received CMSG_LEARN_PREVIEW_TALENTS_PET");

    ObjectGuid guid;
    recvData >> guid;

    uint32 talentsCount;
    recvData >> talentsCount;

    uint32 talentId, talentRank;

    // Client has max 24 talents, rounded up : 30
    uint32 const MaxTalentsCount = 30;

    for (uint32 i = 0; i < talentsCount && i < MaxTalentsCount; ++i)
    {
        recvData >> talentId >> talentRank;

        _player->LearnPetTalent(guid, talentId, talentRank);
    }

    _player->SendTalentsInfoData(true);

    recvData.rfinish();
}

void WorldSession::HandleRequestPetInfo(WorldPackets::Pet::RequestPetInfo& /*packet*/)
{
    // Handle the packet CMSG_REQUEST_PET_INFO - sent when player does ingame /reload command

    // Packet sent when player has a pet
    if (_player->GetPet())
        _player->PetSpellInitialize();
    else if (Unit* charm = _player->GetCharmed())
    {
        // Packet sent when player has a possessed unit
        if (charm->HasUnitState(UNIT_STATE_POSSESSED))
            _player->PossessSpellInitialize();
        // Packet sent when player controlling a vehicle
        else if (charm->HasUnitFlag(UNIT_FLAG_PLAYER_CONTROLLED) && charm->HasUnitFlag(UNIT_FLAG_POSSESSED))
            _player->VehicleSpellInitialize();
        // Packet sent when player has a charmed unit
        else
            _player->CharmSpellInitialize();
    }
}
