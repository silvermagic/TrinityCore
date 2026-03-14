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
 * @file PassiveAI.cpp
 * @brief 被动AI模块实现文件
 *
 * 本文件实现了多种被动型AI类，用于控制不需要主动行为的生物单位。
 * 这些AI实现提供了基础的被动行为框架，供特定场景使用。
 */

#include "PassiveAI.h"
#include "Creature.h"
#include "MovementDefines.h"

/**
 * @brief PassiveAI构造函数
 *
 * 初始化被动AI实例，将生物的反应状态设置为被动模式
 *
 * @param creature 拥有此AI的生物对象
 *
 * 设置说明：
 * - REACT_PASSIVE: 被动反应状态，生物不会主动攻击视线范围内的敌人
 * - 这是被动AI的核心特征，确保生物保持被动行为
 */
PassiveAI::PassiveAI(Creature* creature) : CreatureAI(creature)
{
    creature->SetReactState(REACT_PASSIVE);
}

/**
 * @brief PossessedAI构造函数
 *
 * 初始化附身AI实例，将生物的反应状态设置为被动模式
 *
 * @param creature 拥有此AI的生物对象
 *
 * 设置说明：
 * - REACT_PASSIVE: 虽然设置为被动，但AI会响应控制者的攻击命令
 * - 这种组合允许玩家控制生物的攻击行为
 */
PossessedAI::PossessedAI(Creature* creature) : CreatureAI(creature)
{
    creature->SetReactState(REACT_PASSIVE);
}

/**
 * @brief NullCreatureAI构造函数
 *
 * 初始化空AI实例，将生物的反应状态设置为被动模式
 *
 * @param creature 拥有此AI的生物对象
 *
 * 设置说明：
 * - REACT_PASSIVE: 确保生物不产生任何主动行为
 * - 适用于触发器、效果产生器等不需要AI逻辑的单位
 */
NullCreatureAI::NullCreatureAI(Creature* creature) : CreatureAI(creature)
{
    creature->SetReactState(REACT_PASSIVE);
}

/**
 * @brief 检查空AI是否适用于指定生物
 *
 * 静态函数，判断是否应该为此生物使用空AI。
 * 根据生物的标志和类型返回不同的优先级。
 *
 * @param creature 要检查的生物指针
 * @return int32 返回权限值：
 *         - PERMIT_BASE_PROACTIVE + 50: 如果有法术点击标志（优先级最高）
 *         - PERMIT_BASE_PROACTIVE: 如果是触发器
 *         - PERMIT_BASE_IDLE: 默认情况（低优先级）
 *
 * 判断逻辑：
 * 1. 法术点击标志：UNIT_NPC_FLAG_SPELLCLICK 表示生物可以通过点击触发法术
 *    这类生物通常不需要AI行为（如任务物品、传送门等）
 * 2. 触发器类型：IsTrigger() 判断生物是否为触发器类型
 *    触发器通常是不可见的辅助单位，用于法术效果等
 * 3. 其他情况：返回基础空闲优先级，作为后备选项
 *
 * 性能注意事项：
 * - 此函数在AI选择过程中会被频繁调用
 * - 应保持简单的判断逻辑，避免复杂计算
 */
int32 NullCreatureAI::Permissible(Creature const* creature)
{
    // 法术点击生物（如任务物品、交互对象）优先使用空AI
    if (creature->HasNpcFlag(UNIT_NPC_FLAG_SPELLCLICK))
        return PERMIT_BASE_PROACTIVE + 50;

    // 触发器生物使用空AI
    if (creature->IsTrigger())
        return PERMIT_BASE_PROACTIVE;

    // 默认返回空闲优先级，作为后备选项
    return PERMIT_BASE_IDLE;
}

/**
 * @brief PassiveAI更新函数
 *
 * 每个游戏周期调用的主更新函数。
 * 检查并修复异常的战斗状态。
 *
 * @param diff 自上次更新以来经过的时间（毫秒），当前未使用
 *
 * 主要逻辑：
 * - 检查生物是否处于"已参与战斗但实际没有敌人"的异常状态
 * - IsEngaged(): 生物已进入战斗管理系统
 * - !IsInCombat(): 生物当前没有战斗目标
 * - 如果存在这种异常，强制触发脱离战斗模式
 *
 * 使用场景：
 * - 某些特殊情况下生物可能被错误地标记为战斗状态
 * - 这会导致生物无法正常重置或响应其他事件
 * - 此检查确保被动AI的生物不会陷入异常状态
 *
 * @note 这种状态检查对被动AI特别重要，
 *       因为被动AI不会主动攻击，可能更容易出现状态不一致
 */
void PassiveAI::UpdateAI(uint32)
{
    // 检查异常战斗状态：已参与战斗但没有实际敌人
    if (me->IsEngaged() && !me->IsInCombat())
        EnterEvadeMode(EVADE_REASON_NO_HOSTILES);
}

/**
 * @brief PossessedAI开始攻击函数
 *
 * 当控制者命令生物攻击目标时调用。
 * 立即开始对目标进行近战攻击。
 *
 * @param target 要攻击的目标单位
 *
 * 主要逻辑：
 * - 调用生物的 Attack 方法
 * - 参数 true 表示使用近战攻击模式
 * - 不设置移动追逐，由 UpdateAI 处理
 *
 * @note 与普通AI不同，附身AI不会自动管理移动和威胁，
 *       控制者需要手动调整位置
 */
void PossessedAI::AttackStart(Unit* target)
{
    me->Attack(target, true);
}

/**
 * @brief PossessedAI更新函数
 *
 * 每个游戏周期调用的主更新函数。
 * 处理附身生物的攻击逻辑。
 *
 * @param diff 自上次更新以来经过的时间（毫秒），当前未使用
 *
 * 主要逻辑：
 * 1. 检查是否有当前攻击目标
 * 2. 验证目标是否仍然有效：
 *    - IsValidAttackTarget 检查目标是否可以被攻击
 *    - 包括检查：死亡状态、敌对关系、可见性等
 * 3. 如果目标无效，停止攻击
 * 4. 如果目标有效，执行近战攻击
 *
 * 近战攻击说明：
 * - DoMeleeAttackIfReady 会检查攻击冷却时间
 * - 自动处理主手和副手攻击
 * - 只有在近战范围内才会实际造成伤害
 *
 * @note 附身AI不会自动切换目标或选择新目标，
 *       这由控制者决定
 */
void PossessedAI::UpdateAI(uint32 /*diff*/)
{
    // 检查是否有当前攻击目标
    if (me->GetVictim())
    {
        // 验证目标是否仍然有效
        if (!me->IsValidAttackTarget(me->GetVictim()))
            me->AttackStop();  // 目标无效，停止攻击
        else
            DoMeleeAttackIfReady();  // 目标有效，执行近战攻击
    }
}

/**
 * @brief PossessedAI死亡通知函数
 *
 * 当附身生物死亡时调用。
 * 移除可掠夺标志，防止玩家从尸体上获得战利品。
 *
 * @param u 击杀者（未使用）
 *
 * 主要逻辑：
 * - 移除 UNIT_DYNFLAG_LOOTABLE 动态标志
 * - 这会导致尸体不可被掠夺
 *
 * 设计原因：
 * - 附身生物通常是玩家临时控制的NPC
 * - 如果允许掠夺，玩家可能滥用精神控制获取额外物品
 * - 例如：控制稀有怪物然后击杀获取战利品
 *
 * @note 这是重要的游戏平衡机制，
 *       确保精神控制类法术不会提供额外收益
 */
void PossessedAI::JustDied(Unit* /*u*/)
{
    // 附身生物死亡时禁用战利品，防止滥用
    me->RemoveDynamicFlag(UNIT_DYNFLAG_LOOTABLE);
}

/**
 * @brief CritterAI进入战斗通知函数
 *
 * 当小动物被攻击进入战斗时调用。
 * 立即触发逃跑行为，小动物不会反击。
 *
 * @param who 攻击者（未使用）
 *
 * 主要逻辑：
 * - 检查是否已经在逃跑状态（UNIT_STATE_FLEEING）
 * - 如果不在逃跑状态，设置控制状态为逃跑
 * - SetControlled(true, UNIT_STATE_FLEEING) 会触发逃跑AI
 *
 * 逃跑机制说明：
 * - 逃跑状态会让生物随机方向逃离攻击者
 * - 逃跑持续一段时间后自动停止
 * - 小动物逃跑距离有限，通常很容易被追上
 *
 * @note 小动物的设计是让玩家能够轻松击杀获取物品或成就，
 *       而不是进行有意义的战斗
 */
void CritterAI::JustEngagedWith(Unit* /*who*/)
{
    // 如果不在逃跑状态，立即开始逃跑
    if (!me->HasUnitState(UNIT_STATE_FLEEING))
        me->SetControlled(true, UNIT_STATE_FLEEING);
}

/**
 * @brief CritterAI移动完成通知函数
 *
 * 当小动物完成一个移动动作时调用。
 * 用于处理逃跑移动完成后的脱战。
 *
 * @param type 移动类型
 * @param id 移动标识（未使用）
 *
 * 主要逻辑：
 * - 检查移动类型是否为 TIMED_FLEEING_MOTION_TYPE（定时逃跑类型）
 * - 这是逃跑AI使用的特殊移动类型
 * - 如果是逃跑完成，触发脱离战斗模式
 *
 * 移动类型说明：
 * - TIMED_FLEEING_MOTION_TYPE: 定时逃跑，持续指定时间后停止
 * - 小动物逃跑使用此类型，确保逃跑一段时间后自动停止
 *
 * @note 逃跑完成后小动物会脱战重置，
 *       回到原来的位置或附近继续游荡
 */
void CritterAI::MovementInform(uint32 type, uint32 /*id*/)
{
    // 检查是否为逃跑移动完成
    if (type == TIMED_FLEEING_MOTION_TYPE)
        EnterEvadeMode(EVADE_REASON_OTHER);
}

/**
 * @brief CritterAI脱离战斗模式函数
 *
 * 当小动物需要脱离战斗时调用。
 * 清除逃跑状态并执行正常的脱战流程。
 *
 * @param why 脱战原因
 *
 * 主要逻辑：
 * 1. 检查是否在逃跑状态
 * 2. 如果在逃跑状态，解除逃跑控制
 * 3. 调用父类的脱战函数完成重置
 *
 * 脱战流程：
 * - 清除逃跑状态：SetControlled(false, UNIT_STATE_FLEEING)
 * - 调用父类脱战：CreatureAI::EnterEvadeMode(why)
 * - 父类会处理：清除仇恨列表、重置生命值、返回初始位置等
 *
 * @note 必须先清除逃跑状态，否则脱战逻辑可能不正常工作
 */
void CritterAI::EnterEvadeMode(EvadeReason why)
{
    // 清除逃跑状态
    if (me->HasUnitState(UNIT_STATE_FLEEING))
        me->SetControlled(false, UNIT_STATE_FLEEING);

    // 调用父类脱战逻辑
    CreatureAI::EnterEvadeMode(why);
}

/**
 * @brief 检查小动物AI是否适用于指定生物
 *
 * 静态函数，判断是否应该为此生物使用小动物AI。
 *
 * @param creature 要检查的生物指针
 * @return int32 返回权限值：
 *         - PERMIT_BASE_PROACTIVE: 如果是小动物且不是守护者
 *         - PERMIT_BASE_NO: 其他情况
 *
 * 判断逻辑：
 * 1. IsCritter(): 检查生物是否为小动物类型
 *    - 小动物通常是中立的小型生物（如兔子、老鼠）
 * 2. UNIT_MASK_GUARDIAN: 检查是否不是守护者类型
 *    - 守护者是某种特殊单位类型，可能有战斗能力
 * 3. 只有小动物类型且不是守护者才适用此AI
 *
 * @note 守护者类型的小动物可能由特定法术或效果召唤，
 *       需要不同的AI行为
 */
int32 CritterAI::Permissible(Creature const* creature)
{
    // 检查是否为小动物类型，且不是守护者（守护者可能有战斗能力）
    if (creature->IsCritter() && !creature->HasUnitTypeMask(UNIT_MASK_GUARDIAN))
        return PERMIT_BASE_PROACTIVE;

    return PERMIT_BASE_NO;
}

/**
 * @brief TriggerAI被召唤通知函数
 *
 * 当触发器被召唤到世界时调用。
 * 自动施放预设的第一个法术。
 *
 * @param summoner 召唤者对象
 *
 * 主要逻辑：
 * 1. 检查是否设置了第一个法术槽（m_spells[0]）
 *    - m_spells 是 creature_template 中的预设法术数组
 *    - 触发器通常只使用第一个法术槽
 * 2. 创建额外的施法参数
 *    - OriginalCaster: 设置原始施法者为召唤者
 *    - 这确保法术效果正确归属到召唤者
 * 3. 对自身施放法术
 *
 * 法术归属说明：
 * - 原始施法者（OriginalCaster）决定了：
 *   - 法术伤害的归属
 *   - 光环效果的来源
 *   - 成就和统计的计数
 * - 设置为召唤者确保正确的游戏逻辑
 *
 * 使用场景示例：
 * - 术士的献祭陷阱：触发器施放伤害法术
 * - 法师的暴风雪：触发器施放区域伤害
 * - 任务触发器：施放视觉效果或任务法术
 *
 * @note 触发器通常是不可见的，只用于施放法术，
 *       施法后可能立即消失或持续一段时间
 */
void TriggerAI::IsSummonedBy(WorldObject* summoner)
{
    // 检查是否设置了第一个法术槽
    if (me->m_spells[0])
    {
        // 创建施法参数
        CastSpellExtraArgs extra;
        // 设置原始施法者为召唤者，确保正确的归属
        extra.OriginalCaster = summoner->GetGUID();
        // 对自身施放法术
        me->CastSpell(me, me->m_spells[0], extra);
    }
}

/**
 * @brief 检查触发器AI是否适用于指定生物
 *
 * 静态函数，判断是否应该为此生物使用触发器AI。
 *
 * @param creature 要检查的生物指针
 * @return int32 返回权限值：
 *         - PERMIT_BASE_SPECIAL: 如果是触发器且有预设法术
 *         - PERMIT_BASE_NO: 其他情况
 *
 * 判断逻辑：
 * 1. IsTrigger(): 检查生物是否为触发器类型
 *    - 触发器通常是不可见的辅助单位
 * 2. m_spells[0]: 检查是否设置了第一个法术
 *    - 触发器AI主要用于自动施放预设的法术
 *    - 没有预设法术的触发器使用空AI即可
 *
 * 优先级说明：
 * - PERMIT_BASE_SPECIAL 是较高的优先级
 * - 确保有法术的触发器正确使用此AI
 * - 此AI会在召唤时自动施放法术
 *
 * @note 没有预设法术的触发器会使用 NullCreatureAI，
 *       因为它们不需要任何行为
 */
int32 TriggerAI::Permissible(Creature const* creature)
{
    // 检查是否为触发器类型，且设置了第一个法术
    if (creature->IsTrigger() && creature->m_spells[0])
        return PERMIT_BASE_SPECIAL;

    return PERMIT_BASE_NO;
}
