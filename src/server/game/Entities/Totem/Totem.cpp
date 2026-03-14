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
 * @file Totem.cpp
 * @brief 图腾实体实现模块
 *
 * 本文件实现了图腾(Totem)类的核心功能,包括:
 *   - 图腾的生命周期管理(创建、更新、销毁)
 *   - 图腾属性初始化和统计系统
 *   - 图腾法术施放和免疫机制
 *   - 图腾与所有者及队伍成员的交互
 *
 * 图腾是萨满职业特有的召唤单位,分为火、土、水、风四种元素类型,
 * 每种类型在同一时间只能存在一个图腾。图腾提供各种增益效果、
 * 治疗能力或攻击能力。
 *
 * 继承关系:
 *   Object -> WorldObject -> Unit -> Creature -> TemporarySummon -> Minion -> Totem
 *
 * 关键特性:
 *   - 图腾具有固定的持续时间,到期自动消失
 *   - 图腾对所有者死亡敏感,所有者死亡时图腾消失
 *   - 图腾对多种负面效果免疫(恐惧、变形、周期性伤害等)
 *   - 被动图腾自动施放法术,主动图腾需要手动激活
 */

#include "Totem.h"
#include "Group.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SpellHistory.h"
#include "SpellMgr.h"
#include "SpellInfo.h"
#include "TotemPackets.h"

/**
 * @brief 图腾构造函数
 *
 * @brief 职责
 * 初始化图腾对象的基本属性，设置单位类型掩码和默认值
 *
 * @param properties 召唤属性配置，包含图腾的基本属性设置
 * @param owner 图腾的所有者（施法者），通常是萨满玩家
 *
 * @brief 初始化流程
 *   1. 调用父类 Minion 构造函数，不进行面向初始化（图腾不需要面向）
 *   2. 设置单位类型掩码为图腾类型，用于快速类型判断
 *   3. 初始化持续时间为0，将在 InitStats 中设置实际值
 *   4. 设置默认图腾类型为被动图腾，将在 InitStats 中根据法术判断实际类型
 *
 * @brief 性能说明
 *   - 构造函数执行效率高，仅进行基本的成员初始化
 *   - 避免在构造函数中进行复杂的属性计算，延迟到 InitStats 处理
 *
 * @see Minion::Minion
 * @see UNIT_MASK_TOTEM
 */
Totem::Totem(SummonPropertiesEntry const* properties, Unit* owner) : Minion(properties, owner, false)
{
    // 标记为图腾类型单位，用于类型检查和过滤
    m_unitTypeMask |= UNIT_MASK_TOTEM;

    // 初始化持续时间，实际值将在 InitStats 中通过法术数据设置
    m_duration = 0;

    // 默认设置为被动图腾，将在 InitStats 中根据法术施法时间判断实际类型
    m_type = TOTEM_PASSIVE;
}

/**
 * @brief 图腾更新函数
 *
 * @brief 职责
 * 每帧更新图腾状态，包括生命周期检测和持续时间倒计时
 *
 * @param time 距离上次更新的时间差（毫秒）
 *
 * @brief 主要流程
 *   1. 检查所有者是否存活，若死亡则取消召唤图腾
 *   2. 检查图腾自身是否存活，若死亡则取消召唤
 *   3. 检查图腾持续时间是否到期，到期则取消召唤
 *   4. 更新剩余持续时间
 *   5. 调用父类 Creature::Update 进行常规更新
 *
 * @brief 时序说明
 *   - 该函数由游戏主循环每帧调用（通常约 50ms 一次）
 *   - 生命周期检查优先于持续时间检查，确保图腾不会孤立存在
 *   - 持续时间到期时自动触发 UnSummon，无需外部干预
 *
 * @brief 性能考虑
 *   - 每帧只执行简单的状态检查和数值减法，开销极低
 *   - UnSummon 调用后立即返回，避免不必要的后续处理
 *
 * @see Creature::Update
 * @see UnSummon
 */
void Totem::Update(uint32 time)
{
    // 生命周期检查：所有者或图腾死亡时立即移除
    // 这是图腾的固有特性，所有者死亡时图腾无法继续存在
    if (!GetOwner()->IsAlive() || !IsAlive())
    {
        UnSummon(); // 移除自身并清理相关状态
        return;
    }

    // 持续时间检查：时间到期时自动消失
    // 图腾通常是临时的，有固定的持续时间限制
    if (m_duration <= time)
    {
        UnSummon(); // 时间到期，移除图腾
        return;
    }
    else
    {
        // 扣除已过去的时间，更新剩余持续时间
        m_duration -= time;
    }

    // 调用父类更新函数进行常规更新（如 AI 处理、法术施放等）
    Creature::Update(time);
}

/**
 * @brief 初始化图腾属性
 *
 * @brief 职责
 * 设置图腾的初始属性，包括发送创建包、设置外观模型、判断图腾类型等
 *
 * @param duration 图腾的持续时间（毫秒）
 *
 * @brief 主要流程
 *   1. 向客户端发送图腾创建消息（SMSG_TOTEM_CREATED）
 *   2. 根据施法者种族设置图腾的显示模型ID
 *   3. 调用父类 Minion::InitStats 初始化基础属性
 *   4. 根据图腾法术是否有施法时间判断图腾类型（主动/被动）
 *   5. 特殊处理哨兵图腾的反应状态
 *   6. 设置图腾持续时间和等级
 *
 * @brief 时序说明
 *   - 该函数在图腾被召唤时调用，位于构造函数之后、InitSummon 之前
 *   - 客户端要求在图腾添加到世界之前发送 SMSG_TOTEM_CREATED
 *   - 必须在发送创建包之后才能设置图腾属性，否则客户端可能显示异常
 *
 * @brief 图腾槽位说明
 *   - 火图腾槽位：SUMMON_SLOT_TOTEM_FIRE (73)
 *   - 土图腾槽位：SUMMON_SLOT_TOTEM_EARTH (74)
 *   - 水图腾槽位：SUMMON_SLOT_TOTEM_WATER (75)
 *   - 风图腾槽位：SUMMON_SLOT_TOTEM_AIR (76)
 *   同类型图腾在同一时间只能存在一个，新图腾会替换旧图腾
 *
 * @brief 种族模型说明
 *   - 不同种族的图腾有不同的外观模型（如兽人、牛头人、巨魔等）
 *   - 模型ID 从 ObjectMgr 的图腾模型表中查询
 *   - 如果找不到对应模型，使用默认模型并记录调试日志
 *
 * @see Minion::InitStats
 * @see ObjectMgr::GetModelForTotem
 * @see SUMMON_SLOT_TOTEM_FIRE
 */
void Totem::InitStats(uint32 duration)
{
    // 客户端要求在添加到世界之前发送 SMSG_TOTEM_CREATED，并在移除旧图腾之前发送
    // 这确保客户端能够正确显示图腾的创建动画和UI提示
    if (Player* owner = GetOwner()->ToPlayer())
    {
        uint32 slot = m_Properties->Slot;

        // 检查是否为有效的图腾槽位（火、土、水、风四种图腾）
        if (slot >= SUMMON_SLOT_TOTEM_FIRE && slot < MAX_TOTEM_SLOT)
        {
            // 构建并发送图腾创建数据包
            WorldPackets::Totem::TotemCreated data;
            data.Totem = GetGUID();                                    // 图腾唯一标识符
            data.Slot = slot - SUMMON_SLOT_TOTEM_FIRE;                 // 图腾槽位索引（0-3），客户端UI显示用
            data.Duration = duration;                                  // 持续时间，用于客户端倒计时显示
            data.SpellID = GetUInt32Value(UNIT_CREATED_BY_SPELL);     // 创建法术ID，用于客户端法术提示
            owner->SendDirectMessage(data.Write());
        }

        // 根据施法者种族设置图腾的显示模型ID
        // 不同种族的图腾有不同的外观，例如兽人的火图腾与牛头人的火图腾外观不同
        if (uint32 totemDisplayId = sObjectMgr->GetModelForTotem(SummonSlot(slot), Races(owner->GetRace())))
        {
            SetDisplayId(totemDisplayId);
        }
        else
        {
            // 如果没有找到对应的模型，记录调试日志并使用默认模型
            TC_LOG_DEBUG("misc", "Totem with entry {}, owned by player {} ({} {} {}) in slot {}, created by spell {}, does not have a specialized model. Set to default.",
                         GetEntry(), owner->GetGUID().ToString(), owner->GetLevel(), EnumUtils::ToTitle(Races(owner->GetRace())), EnumUtils::ToTitle(Classes(owner->GetClass())), slot, GetUInt32Value(UNIT_CREATED_BY_SPELL));
        }
    }

    // 调用父类初始化属性，设置基础召唤物属性
    Minion::InitStats(duration);

    // 获取图腾施放的法术信息，判断图腾类型
    // 主动图腾：法术有施法时间，需要玩家手动激活（如火焰新星图腾）
    // 被动图腾：法术无施法时间，自动施放效果（如治疗之泉图腾）
    if (SpellInfo const* totemSpell = sSpellMgr->GetSpellInfo(GetSpell()))
    {
        if (totemSpell->CalcCastTime())   // 如果法术有施法时间，则为主动图腾
        {
            m_type = TOTEM_ACTIVE;
        }
    }

    // 哨兵图腾特殊处理：设置为攻击性反应状态
    // 哨兵图腾具有特殊的警戒功能，需要主动攻击进入范围的敌对目标
    if (GetEntry() == SENTRY_TOTEM_ENTRY)
    {
        SetReactState(REACT_AGGRESSIVE);
    }

    // 设置图腾持续时间
    m_duration = duration;

    // 设置图腾等级等于所有者等级
    // 图腾的属性（如生命值、法术强度）通常与所有者等级相关
    SetLevel(GetOwner()->GetLevel());
}

/**
 * @brief 初始化图腾召唤
 *
 * @brief 职责
 * 图腾召唤后的初始化处理，主要为被动图腾施放法术
 *
 * @brief 主要流程
 *   1. 如果是被动图腾且有法术，立即对自己施放法术
 *   2. 某些图腾可能同时拥有即时效果和被动法术，处理第二个法术
 *
 * @brief 图腾类型行为说明
 *   - 被动图腾（TOTEM_PASSIVE）：召唤时自动施放法术，产生持续效果
 *     例如：治疗之泉图腾会施放周期性治疗光环
 *   - 主动图腾（TOTEM_ACTIVE）：需要手动激活才能施放法术
 *     例如：火焰新星图腾需要玩家点击才能造成范围伤害
 *
 * @brief 双法术图腾说明
 *   某些特殊图腾拥有两个法术槽位：
 *   - 第一个法术（slot 0）：通常是图腾的主要效果
 *   - 第二个法术（slot 1）：辅助效果或特殊机制
 *   例如：某些图腾可能同时拥有视觉效果法术和实际功能法术
 *
 * @brief 时序说明
 *   - 该函数在图腾被添加到世界后调用
 *   - 位于 InitStats 之后，此时图腾已完全初始化
 *   - 施放的法术会立即生效，无需等待下一帧更新
 *
 * @see CastSpell
 * @see GetSpell
 * @see TOTEM_PASSIVE
 */
void Totem::InitSummon()
{
    // 被动图腾在召唤时自动施放其法术
    // 这确保图腾一旦出现就立即产生效果（如治疗光环、增益效果等）
    if (m_type == TOTEM_PASSIVE && GetSpell())
    {
        CastSpell(this, GetSpell(), true); // triggered = true，无视施法条件立即施放
    }

    // 某些图腾可以同时拥有即时效果和被动法术（第二个法术）
    // 第二个法术通常用于特殊的视觉效果或辅助功能
    if (GetSpell(1))
    {
        CastSpell(this, GetSpell(1), true);
    }
}

/**
 * @brief 取消召唤图腾
 *
 * @brief 职责
 * 移除图腾并清理相关状态，包括停止战斗、移除光环、清理槽位等
 *
 * @param msTime 延迟取消召唤的时间（毫秒），0表示立即取消
 *
 * @brief 主要流程
 *   1. 如果指定延迟时间，添加延迟取消召唤事件
 *   2. 停止战斗状态
 *   3. 移除图腾自身的法术光环
 *   4. 清除所有者的图腾槽位
 *   5. 移除所有者身上的图腾光环
 *   6. 特殊处理哨兵图腾的光环移除
 *   7. 如果所有者是玩家，处理冷却事件和队伍成员光环移除
 *   8. 设置死亡状态以触发正确的动画
 *   9. 将图腾添加到移除列表
 *
 * @brief 延迟取消说明
 *   - 延迟取消用于实现图腾消失的过渡效果
 *   - 例如某些图腾在消失前可能有特殊动画或音效
 *   - 延迟事件通过 ForcedUnsummonDelayEvent 实现，到期后再次调用 UnSummon(0)
 *
 * @brief 光环清理说明
 *   图腾移除时需要清理多个位置的光环：
 *   - 图腾自身的光环（由图腾施放在自己身上的效果）
 *   - 所有者身上的光环（由图腾施放在所有者身上的增益）
 *   - 队伍成员身上的光环（由图腾施放在队友身上的群体增益）
 *   这样确保图腾消失后，所有相关效果都被正确移除
 *
 * @brief 槽位清理说明
 *   清除所有者的图腾槽位后，才能召唤同类型的新图腾
 *   这是图腾系统的核心限制：同类型图腾在同一时间只能存在一个
 *
 * @brief 死亡状态说明
 *   设置图腾为死亡状态是为了触发正确的客户端动画
 *   客户端需要看到图腾"死亡"的视觉效果，而不是直接消失
 *
 * @see ForcedUnsummonDelayEvent
 * @see CombatStop
 * @see RemoveAurasDueToSpell
 */
void Totem::UnSummon(uint32 msTime)
{
    // 如果指定了延迟时间，添加延迟取消召唤事件
    // 这允许图腾在消失前播放过渡动画或音效
    if (msTime)
    {
        m_Events.AddEvent(new ForcedUnsummonDelayEvent(*this), m_Events.CalculateTime(Milliseconds(msTime)));
        return;
    }

    // 停止战斗状态，确保图腾不会在消失时继续攻击
    CombatStop();

    // 移除图腾自身的法术光环
    // 例如：治疗之泉图腾施放在自己身上的周期性治疗光环
    RemoveAurasDueToSpell(GetSpell(), GetGUID());

    // 清除所有者的图腾槽位
    // 这是必要的步骤，否则无法召唤同类型的新图腾
    for (uint8 i = SUMMON_SLOT_TOTEM_FIRE; i < MAX_TOTEM_SLOT; ++i)
    {
        if (GetOwner()->m_SummonSlot[i] == GetGUID())
        {
            GetOwner()->m_SummonSlot[i].Clear();
            break;
        }
    }

    // 移除所有者身上的图腾法术光环
    // 例如：某些图腾会给所有者施加增益效果
    GetOwner()->RemoveAurasDueToSpell(GetSpell(), GetGUID());

    // 移除哨兵图腾的特殊光环
    // 哨兵图腾会施加特殊的视野效果，需要单独处理
    if (GetEntry() == SENTRY_TOTEM_ENTRY)
    {
        GetOwner()->RemoveAurasDueToSpell(SENTRY_TOTEM_SPELLID);
    }

    // 如果所有者是玩家，处理玩家特定的清理逻辑
    if (Player* owner = GetOwner()->ToPlayer())
    {
        // 发送自动重复取消消息，停止任何自动攻击动作
        owner->SendAutoRepeatCancel(this);

        // 发送冷却事件，重置法术冷却
        // 这允许玩家在图腾消失后立即重新施放该图腾
        if (SpellInfo const* spell = sSpellMgr->GetSpellInfo(GetUInt32Value(UNIT_CREATED_BY_SPELL)))
        {
            GetSpellHistory()->SendCooldownEvent(spell, 0, nullptr, false);
        }

        // 移除队伍成员身上的图腾光环
        // 某些图腾（如风怒图腾）会给整个小队施加增益效果
        // 图腾消失时需要清理所有队友身上的这些效果
        if (Group* group = owner->GetGroup())
        {
            for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
            {
                Player* target = itr->GetSource();
                // 只处理在同一地图且同一小队的成员
                // 跨地图或跨小队的队友不受此图腾影响
                if (target && target->IsInMap(owner) && group->SameSubGroup(owner, target))
                {
                    target->RemoveAurasDueToSpell(GetSpell(), GetGUID());
                }
            }
        }
    }

    // 任何图腾的取消召唤都表现为图腾死亡，这是正确播放动画所需的
    // 客户端需要看到图腾"死亡"的视觉效果，而不是直接消失
    if (IsAlive())
    {
        setDeathState(DEAD);
    }

    // 将图腾添加到移除列表，等待系统在下次更新时清理
    // 使用移除列表而不是立即删除，确保对象生命周期管理正确
    AddObjectToRemoveList();
}

/**
 * @brief 判断图腾是否对法术效果免疫
 *
 * @brief 职责
 * 检查图腾是否对特定法术效果免疫，实现图腾的特殊免疫规则
 *
 * @param spellInfo 法术信息，包含法术的基本属性和效果列表
 * @param spellEffectInfo 法术效果信息，描述具体的效果类型和参数
 * @param caster 施法者，可能是敌对单位、友方单位或图腾自身
 * @param requireImmunityPurgesEffectAttribute 是否需要免疫清除效果属性（用于某些特殊免疫机制）
 *
 * @return true 图腾对该法术效果免疫，效果不会生效
 * @return false 图腾不免疫该法术效果，效果正常生效
 *
 * @brief 主要流程
 *   1. 图腾对所有增益法术免疫（除了石爪图腾吸收和哨兵图腾绑定视野）
 *   2. 图腾免疫特定类型的光环效果（周期性伤害、吸血、恐惧、变形）
 *   3. 调用父类 Creature::IsImmunedToSpellEffect 进行其他免疫检查
 *
 * @brief 免疫规则详解
 *
 *   增益法术免疫规则：
 *   - 图腾对所有正面效果免疫，除非是自己施放给自己的
 *   - 这防止了图腾被敌人利用作为增益目标（防止滥用机制）
 *   - 特例：
 *     * 石爪图腾吸收效果（SENTRY_STONECLAW_SPELLID）：允许对自己施加吸收护盾
 *     * 哨兵图腾绑定视野（SENTRY_BIND_SIGHT_SPELLID）：允许绑定视野效果
 *     * 目标为 TARGET_UNIT_CASTER 的效果：图腾施放给自己的效果
 *     * 目标检查类型为 TARGET_CHECK_ENTRY 的效果：特定目标检查的效果
 *
 *   负面效果免疫规则：
 *   - SPELL_AURA_PERIODIC_DAMAGE：免疫周期性伤害（如持续燃烧、中毒等）
 *   - SPELL_AURA_PERIODIC_LEECH：免疫周期性吸取（如生命偷取）
 *   - SPELL_AURA_MOD_FEAR：免疫恐惧效果（防止图腾被恐惧跑开）
 *   - SPELL_AURA_TRANSFORM：免疫变形效果（防止图腾被变成小动物）
 *   这些免疫确保图腾不会被控制或轻易摧毁，保持其战术价值
 *
 * @brief 设计考虑
 *   图腾的免疫机制设计遵循以下原则：
 *   - 图腾应该是稳定可靠的，不应轻易被敌人干扰
 *   - 图腾不应成为敌人增益的目标（防止滥用）
 *   - 图腾不应被控制效果影响（保持战术价值）
 *   - 图腾仍可被直接攻击摧毁（保持游戏平衡）
 *
 * @see Creature::IsImmunedToSpellEffect
 * @see SpellInfo::IsPositive
 * @see SPELL_AURA_PERIODIC_DAMAGE
 */
bool Totem::IsImmunedToSpellEffect(SpellInfo const* spellInfo, SpellEffectInfo const& spellEffectInfo, WorldObject const* caster,
    bool requireImmunityPurgesEffectAttribute /*= false*/) const
{
    // 图腾对所有增益法术免疫，除了石爪图腾的吸收效果和哨兵图腾的绑定视野
    // 这是图腾的核心免疫机制，防止图腾被敌人滥用为增益目标
    //
    // 增益法术判断逻辑：
    // 1. 法术是正面的（IsPositive()）
    // 2. 效果目标不是施法者自己（不是 TARGET_UNIT_CASTER）
    // 3. 目标检查类型不是 TARGET_CHECK_ENTRY（允许某些特定目标检查）
    // 4. 不是石爪图腾和哨兵图腾的特例法术
    if (spellEffectInfo.Effect != SPELL_EFFECT_DUMMY &&
        spellEffectInfo.Effect != SPELL_EFFECT_SCRIPT_EFFECT &&
        spellInfo->IsPositive() && spellEffectInfo.TargetA.GetTarget() != TARGET_UNIT_CASTER &&
        spellEffectInfo.TargetA.GetCheckType() != TARGET_CHECK_ENTRY && spellInfo->Id != SENTRY_STONECLAW_SPELLID && spellInfo->Id != SENTRY_BIND_SIGHT_SPELLID)
    {
        return true;
    }

    // 图腾免疫特定类型的光环效果
    // 这些免疫确保图腾不会被控制或受到持续性伤害
    switch (spellEffectInfo.ApplyAuraName)
    {
        case SPELL_AURA_PERIODIC_DAMAGE:    // 周期性伤害（如持续伤害效果、DOT）
            // 图腾免疫所有周期性伤害，防止被轻易摧毁
            return true;
        case SPELL_AURA_PERIODIC_LEECH:     // 周期性吸取（如生命偷取）
            // 图腾免疫生命偷取，防止被敌人利用恢复生命
            return true;
        case SPELL_AURA_MOD_FEAR:           // 恐惧效果
            // 图腾免疫恐惧，确保图腾不会因为恐惧而移动位置
            return true;
        case SPELL_AURA_TRANSFORM:          // 变形效果
            // 图腾免疫变形，防止图腾被变成小动物或其他形态
            return true;
        default:
            break;
    }

    // 调用父类方法检查其他免疫规则
    // 这处理了通用的免疫机制，如免疫特定学校的魔法等
    return Creature::IsImmunedToSpellEffect(spellInfo, spellEffectInfo, caster, requireImmunityPurgesEffectAttribute);
}
