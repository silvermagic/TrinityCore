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
 * @file boss_festergut.cpp
 * @brief 冰冠堡垒瘟疫区第二个首领 - 腐面者的战斗脚本
 *
 * 本模块实现了冰冠堡垒瘟疫区第二个首领腐面者的完整战斗逻辑。
 *
 * 战斗机制概述：
 * 1. 吸入瘟疫：BOSS周期性吸入房间内的瘴气，增加伤害并减少瘴气覆盖范围
 * 2. 刺鼻瘴气：吸入3次后施放，清空所有瘴气并造成大量伤害
 * 3. 瘴气孢子：随机玩家感染孢子，需要团队分摊感染以获得免疫增益
 * 4. 胃部膨胀：对主坦造成叠加伤害，10层后爆炸
 *
 * 英雄模式差异：
 * - 增加恶性气体技能，针对远程玩家
 * - 孢子数量更多
 */

#include "icecrown_citadel.h"
#include "Containers.h"
#include "InstanceScript.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "ScriptMgr.h"
#include "SpellAuras.h"
#include "SpellMgr.h"
#include "SpellScript.h"

/**
 * @brief 首领台词文本ID枚举
 *
 * 定义腐面者战斗中的各种台词和表情文本
 */
enum ScriptTexts
{
    SAY_STINKY_DEAD             = 0,  ///< 臭臭死亡时的台词
    SAY_AGGRO                   = 1,  ///< 开怪台词
    EMOTE_GAS_SPORE             = 2,  ///< 瘴气孢子表情
    EMOTE_WARN_GAS_SPORE        = 3,  ///< 瘴气孢子预警表情
    SAY_PUNGENT_BLIGHT          = 4,  ///< 刺鼻瘴气台词
    EMOTE_WARN_PUNGENT_BLIGHT   = 5,  ///< 刺鼻瘴气预警表情
    EMOTE_PUNGENT_BLIGHT        = 6,  ///< 刺鼻瘴气表情
    SAY_KILL                    = 7,  ///< 击杀玩家台词
    SAY_BERSERK                 = 8,  ///< 狂暴台词
    SAY_DEATH                   = 9,  ///< 死亡台词
};

/**
 * @brief 法术ID枚举
 *
 * 定义战斗中使用的所有法术ID
 */
enum Spells
{
    // Festergut - 腐面者
    SPELL_INHALE_BLIGHT         = 69165,  ///< 吸入瘴气 - 吸入房间内的瘴气
    SPELL_PUNGENT_BLIGHT        = 69195,  ///< 刺鼻瘴气 - 释放所有瘴气造成大量伤害
    SPELL_GASTRIC_BLOAT         = 72219,  ///< 胃部膨胀 - 对坦克的叠加伤害（72214是正确的带触发版本，但生物触发目前不能有冷却）
    SPELL_GASTRIC_EXPLOSION     = 72227,  ///< 胃部爆炸 - 10层胃部膨胀后爆炸
    SPELL_GAS_SPORE             = 69278,  ///< 瘴气孢子 - 感染随机玩家
    SPELL_VILE_GAS              = 69240,  ///< 恶性气体 - 英雄模式针对远程
    SPELL_INOCULATED            = 69291,  ///< 免疫 - 孢子爆炸后获得

    // Stinky - 臭臭（房间内的小怪）
    SPELL_MORTAL_WOUND          = 71127,  ///< 致命伤口 - 治疗减益
    SPELL_DECIMATE              = 71123,  ///< 毁灭 - 将所有人生命值降至相同百分比
    SPELL_PLAGUE_STENCH         = 71805,  ///< 瘟疫恶臭 - AOE伤害光环
};

// 用于HasAura检查的辅助宏
#define PUNGENT_BLIGHT_HELPER RAID_MODE<uint32>(69195, 71219, 73031, 73032)
#define INOCULATED_HELPER     RAID_MODE<uint32>(69291, 72101, 72102, 72103)

/**
 * @brief 气态瘴气法术ID数组
 *
 * 对应吸入瘴气的3个阶段
 */
uint32 const gaseousBlight[3]        = {69157, 69162, 69164};

/**
 * @brief 气态瘴气视觉效果法术ID数组
 *
 * 用于房间内的视觉效果
 */
uint32 const gaseousBlightVisual[3]  = {69126, 69152, 69154};

/**
 * @brief 事件ID枚举
 *
 * 定义战斗中的各种计时器事件
 */
enum Events
{
    EVENT_BERSERK       = 1,  ///< 狂暴事件
    EVENT_INHALE_BLIGHT = 2,  ///< 吸入瘴气事件
    EVENT_VILE_GAS      = 3,  ///< 恶性气体事件
    EVENT_GAS_SPORE     = 4,  ///< 瘴气孢子事件
    EVENT_GASTRIC_BLOAT = 5,  ///< 胃部膨胀事件

    EVENT_DECIMATE      = 6,  ///< 毁灭事件（臭臭）
    EVENT_MORTAL_WOUND  = 7,  ///< 致命伤口事件（臭臭）
};

/**
 * @brief 杂项数据枚举
 *
 * 定义用于数据传递的各种常量
 */
enum Misc
{
    DATA_INOCULATED_STACK       = 69291  ///< 免疫叠加层数数据
};

/**
 * @struct boss_festergut
 * @brief 腐面者AI
 *
 * 实现腐面者的完整战斗逻辑，包括：
 * - 瘴气管理机制（吸入和释放）
 * - 孢子感染和免疫机制
 * - 胃部膨胀叠加机制
 * - 与普崔塞德教授教授的联动
 */
struct boss_festergut : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     *
     * 初始化BOSS的基本属性
     */
    boss_festergut(Creature* creature) : BossAI(creature, DATA_FESTERGUT)
    {
        _maxInoculatedStack = 0;
        _inhaleCounter = 0;
    }

    /**
     * @brief 重置函数
     *
     * 当BOSS脱离战斗或被重置时调用，恢复所有状态并初始化事件调度
     *
     * 调用时机：
     * - BOSS脱离战斗
     * - 团队灭团
     * - 手动重置实例
     */
    void Reset() override
    {
        _Reset();
        events.ScheduleEvent(EVENT_BERSERK, 5min);
        events.ScheduleEvent(EVENT_INHALE_BLIGHT, 25s, 30s);
        events.ScheduleEvent(EVENT_GAS_SPORE, 20s, 25s);
        events.ScheduleEvent(EVENT_GASTRIC_BLOAT, 12500ms, 15s);
        _maxInoculatedStack = 0;
        _inhaleCounter = 0;
        me->RemoveAurasDueToSpell(SPELL_BERSERK2);
        // 查找并清理瘴气控制生物
        if (Creature* gasDummy = me->FindNearestCreature(NPC_GAS_DUMMY, 100.0f, true))
        {
            _gasDummyGUID = gasDummy->GetGUID();
            for (uint8 i = 0; i < 3; ++i)
            {
                me->RemoveAurasDueToSpell(gaseousBlight[i]);
                gasDummy->RemoveAurasDueToSpell(gaseousBlightVisual[i]);
            }
        }
    }

    /**
     * @brief 进入战斗函数
     * @param who 触发战斗的单位
     *
     * 当BOSS进入战斗状态时调用，检查前置条件并通知教授
     *
     * 调用时机：BOSS被玩家攻击或主动攻击玩家
     */
    void JustEngagedWith(Unit* who) override
    {
        if (!instance->CheckRequiredBosses(DATA_FESTERGUT, who->ToPlayer()))
        {
            EnterEvadeMode(EVADE_REASON_OTHER);
            instance->DoCastSpellOnPlayers(LIGHT_S_HAMMER_TELEPORT);
            return;
        }

        me->setActive(true);
        Talk(SAY_AGGRO);
        if (Creature* gasDummy = me->FindNearestCreature(NPC_GAS_DUMMY, 100.0f, true))
            _gasDummyGUID = gasDummy->GetGUID();
        // 通知教授腐面者进入战斗
        if (Creature* professor = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_PROFESSOR_PUTRICIDE)))
            professor->AI()->DoAction(ACTION_FESTERGUT_COMBAT);
        DoZoneInCombat();
    }

    /**
     * @brief 死亡函数
     * @param killer 击杀者
     *
     * 当BOSS死亡时调用，处理战利品和通知教授
     *
     * 调用时机：BOSS生命值降为0
     */
    void JustDied(Unit* /*killer*/) override
    {
        _JustDied();
        Talk(SAY_DEATH);
        // 通知教授腐面者死亡
        if (Creature* professor = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_PROFESSOR_PUTRICIDE)))
            professor->AI()->DoAction(ACTION_FESTERGUT_DEATH);

        RemoveBlight();
    }

    /**
     * @brief 返回出生点函数
     *
     * 当BOSS脱离战斗返回出生点时调用
     *
     * 调用时机：BOSS脱战后返回初始位置
     */
    void JustReachedHome() override
    {
        _JustReachedHome();
        instance->SetBossState(DATA_FESTERGUT, FAIL);
    }

    /**
     * @brief 进入躲避模式
     * @param why 躲避原因
     *
     * 同时让教授也进入躲避模式
     */
    void EnterEvadeMode(EvadeReason why) override
    {
        ScriptedAI::EnterEvadeMode(why);
        if (Creature* professor = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_PROFESSOR_PUTRICIDE)))
            professor->AI()->EnterEvadeMode();
    }

    /**
     * @brief 击杀单位函数
     * @param victim 被击杀的单位
     *
     * 当BOSS击杀玩家时调用，播放击杀台词
     *
     * 调用时机：BOSS击杀玩家
     */
    void KilledUnit(Unit* victim) override
    {
        if (victim->GetTypeId() == TYPEID_PLAYER)
            Talk(SAY_KILL);
    }

    /**
     * @brief 法术命中目标回调
     * @param target 目标
     * @param spellInfo 法术信息
     *
     * 处理刺鼻瘴气命中目标后移除免疫光环
     */
    void SpellHitTarget(WorldObject* target, SpellInfo const* spellInfo) override
    {
        Unit* unitTarget = target->ToUnit();
        if (!unitTarget)
            return;

        if (spellInfo->Id == PUNGENT_BLIGHT_HELPER)
            unitTarget->RemoveAurasDueToSpell(INOCULATED_HELPER);
    }

    /**
     * @brief 更新AI函数
     * @param diff 时间差（毫秒）
     *
     * 每个游戏循环调用，处理事件调度和战斗逻辑
     *
     * 性能注意事项：此函数每帧调用，避免复杂计算
     */
    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        events.Update(diff);

        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_INHALE_BLIGHT:
                {
                    RemoveBlight();
                    if (_inhaleCounter == 3)
                    {
                        // 吸入3次后释放刺鼻瘴气
                        Talk(EMOTE_WARN_PUNGENT_BLIGHT);
                        Talk(SAY_PUNGENT_BLIGHT);
                        DoCastSelf(SPELL_PUNGENT_BLIGHT);
                        _inhaleCounter = 0;
                        // 通知教授释放瘴气
                        if (Creature* professor = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_PROFESSOR_PUTRICIDE)))
                            professor->AI()->DoAction(ACTION_FESTERGUT_GAS);
                        events.RescheduleEvent(EVENT_GAS_SPORE, 20s, 25s);
                    }
                    else
                    {
                        // 吸入瘴气
                        DoCastSelf(SPELL_INHALE_BLIGHT);
                        // 直接施放，条件会处理目标
                        ++_inhaleCounter;
                        if (_inhaleCounter < 3)
                            me->CastSpell(me, gaseousBlight[_inhaleCounter], me->GetGUID());
                    }

                    events.ScheduleEvent(EVENT_INHALE_BLIGHT, 33500ms, 35s);
                    break;
                }
                case EVENT_VILE_GAS:
                {
                    // 英雄模式：对远程玩家施放恶性气体
                    std::list<Unit*> ranged, melee;
                    uint32 minTargets = RAID_MODE<uint32>(3, 8, 3, 8);
                    SelectTargetList(ranged, 25, SelectTargetMethod::Random, 0, -5.0f, true);
                    SelectTargetList(melee, 25, SelectTargetMethod::Random, 0, 5.0f, true);
                    while (ranged.size() < minTargets)
                    {
                        if (melee.empty())
                            break;

                        Unit* target = Trinity::Containers::SelectRandomContainerElement(melee);
                        ranged.push_back(target);
                        melee.remove(target);
                    }

                    if (!ranged.empty())
                    {
                        Trinity::Containers::RandomResize(ranged, RAID_MODE<uint32>(1, 3, 1, 3));
                        for (std::list<Unit*>::iterator itr = ranged.begin(); itr != ranged.end(); ++itr)
                            DoCast(*itr, SPELL_VILE_GAS);
                    }

                    events.ScheduleEvent(EVENT_VILE_GAS, 28s, 35s);
                    break;
                }
                case EVENT_GAS_SPORE:
                    // 施放瘴气孢子
                    Talk(EMOTE_WARN_GAS_SPORE);
                    Talk(EMOTE_GAS_SPORE);
                    me->CastSpell(me, SPELL_GAS_SPORE, CastSpellExtraArgs().AddSpellMod(SPELLVALUE_MAX_TARGETS, RAID_MODE<int32>(2, 3, 2, 3)));
                    events.ScheduleEvent(EVENT_GAS_SPORE, 40s, 45s);
                    events.RescheduleEvent(EVENT_VILE_GAS, 28s, 35s);
                    break;
                case EVENT_GASTRIC_BLOAT:
                    // 对主坦施放胃部膨胀
                    DoCastVictim(SPELL_GASTRIC_BLOAT);
                    events.ScheduleEvent(EVENT_GASTRIC_BLOAT, 15s, 17500ms);
                    break;
                case EVENT_BERSERK:
                    // 5分钟狂暴
                    DoCastSelf(SPELL_BERSERK2);
                    Talk(SAY_BERSERK);
                    break;
                default:
                    break;
            }

            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        DoMeleeAttackIfReady();
    }

    /**
     * @brief 设置数据
     * @param type 数据类型
     * @param data 数据值
     *
     * 用于接收免疫叠加层数数据
     */
    void SetData(uint32 type, uint32 data) override
    {
        if (type == DATA_INOCULATED_STACK && data > _maxInoculatedStack)
            _maxInoculatedStack = data;
    }

    /**
     * @brief 获取数据
     * @param type 数据类型
     * @return 数据值
     *
     * 用于成就检查
     */
    uint32 GetData(uint32 type) const override
    {
        if (type == DATA_INOCULATED_STACK)
            return uint32(_maxInoculatedStack);

        return 0;
    }

    /**
     * @brief 移除瘴气
     *
     * 清除房间内所有瘴气效果
     */
    void RemoveBlight()
    {
        if (Creature* gasDummy = ObjectAccessor::GetCreature(*me, _gasDummyGUID))
            for (uint8 i = 0; i < 3; ++i)
            {
                me->RemoveAurasDueToSpell(gaseousBlight[i]);
                gasDummy->RemoveAurasDueToSpell(gaseousBlightVisual[i]);
            }
    }

private:
    ObjectGuid _gasDummyGUID;      ///< 瘴气控制生物GUID
    uint32 _maxInoculatedStack;    ///< 最大免疫叠加层数
    uint32 _inhaleCounter;         ///< 吸入瘴气计数器
};

/**
 * @struct npc_stinky_icc
 * @brief 臭臭AI
 *
 * 实现腐面者房间内小怪臭臭的战斗逻辑
 */
struct npc_stinky_icc : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_stinky_icc(Creature* creature) : ScriptedAI(creature), _instance(creature->GetInstanceScript()) { }

    /**
     * @brief 重置函数
     *
     * 初始化事件调度
     */
    void Reset() override
    {
        _events.Reset();
        _events.ScheduleEvent(EVENT_DECIMATE, 20s, 25s);
        _events.ScheduleEvent(EVENT_MORTAL_WOUND, 3s, 7s);
    }

    /**
     * @brief 进入战斗函数
     * @param target 触发战斗的单位
     *
     * 施放瘟疫恶臭光环
     */
    void JustEngagedWith(Unit* /*target*/) override
    {
        DoCastSelf(SPELL_PLAGUE_STENCH);
    }

    /**
     * @brief 更新AI函数
     * @param diff 时间差（毫秒）
     *
     * 处理战斗逻辑
     */
    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        _events.Update(diff);

        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_DECIMATE:
                    DoCastVictim(SPELL_DECIMATE);
                    _events.ScheduleEvent(EVENT_DECIMATE, 20s, 25s);
                    break;
                case EVENT_MORTAL_WOUND:
                    DoCastVictim(SPELL_MORTAL_WOUND);
                    _events.ScheduleEvent(EVENT_MORTAL_WOUND, 10s, 12500ms);
                    break;
                default:
                    break;
            }
        }

        DoMeleeAttackIfReady();
    }

    /**
     * @brief 死亡函数
     * @param killer 击杀者
     *
     * 通知腐面者臭臭死亡
     */
    void JustDied(Unit* /*killer*/) override
    {
        if (Creature* festergut = ObjectAccessor::GetCreature(*me, _instance->GetGuidData(DATA_FESTERGUT)))
            if (festergut->IsAlive())
                festergut->AI()->Talk(SAY_STINKY_DEAD);
    }

private:
    EventMap _events;          ///< 事件映射表
    InstanceScript* _instance; ///< 实例脚本指针
};

// 69195, 71219, 73031, 73032 - Pungent Blight
/**
 * @class spell_festergut_pungent_blight
 * @brief 刺鼻瘴气法术脚本
 *
 * 处理刺鼻瘴气施放后移除吸入的瘴气光环
 */
class spell_festergut_pungent_blight : public SpellScript
{
    PrepareSpellScript(spell_festergut_pungent_blight);

    bool Load() override
    {
        return GetCaster()->GetTypeId() == TYPEID_UNIT;
    }

    /**
     * @brief 处理脚本效果
     * @param effIndex 效果索引
     *
     * 移除吸入的瘴气光环
     */
    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        // 获取当前难度的吸入瘴气ID
        uint32 blightId = sSpellMgr->GetSpellIdForDifficulty(uint32(GetEffectValue()), GetCaster());

        // 移除光环
        GetCaster()->RemoveAurasDueToSpell(blightId);
        GetCaster()->ToCreature()->AI()->Talk(EMOTE_PUNGENT_BLIGHT);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_festergut_pungent_blight::HandleScript, EFFECT_1, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

// 72219, 72551, 72552, 72553 - Gastric Bloat
/**
 * @class spell_festergut_gastric_bloat
 * @brief 胃部膨胀法术脚本
 *
 * 处理胃部膨胀叠加到10层后的爆炸
 */
class spell_festergut_gastric_bloat : public SpellScript
{
    PrepareSpellScript(spell_festergut_gastric_bloat);

    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo({ SPELL_GASTRIC_EXPLOSION });
    }

    /**
     * @brief 处理脚本效果
     * @param effIndex 效果索引
     *
     * 检查叠加层数，10层时触发爆炸
     */
    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        Aura const* aura = GetHitUnit()->GetAura(GetSpellInfo()->Id);
        if (!(aura && aura->GetStackAmount() == 10))
            return;

        // 10层时移除光环并爆炸
        GetHitUnit()->RemoveAurasDueToSpell(GetSpellInfo()->Id);
        GetHitUnit()->CastSpell(GetHitUnit(), SPELL_GASTRIC_EXPLOSION, true);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_festergut_gastric_bloat::HandleScript, EFFECT_2, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

// 69290, 71222, 73033, 73034 - Blighted Spores
/**
 * @class spell_festergut_blighted_spores
 * @brief 瘴气孢子光环脚本
 *
 * 处理孢子结束后的免疫效果和任务物品
 */
class spell_festergut_blighted_spores : public AuraScript
{
    PrepareAuraScript(spell_festergut_blighted_spores);

    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo({ SPELL_INOCULATED, SPELL_ORANGE_BLIGHT_RESIDUE });
    }

    /**
     * @brief 光环移除时的额外效果
     * @param aurEff 光环效果
     * @param mode 处理模式
     *
     * 施放免疫光环并处理任务物品
     */
    void ExtraEffect(AuraEffect const* aurEff, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->CastSpell(GetTarget(), SPELL_INOCULATED, true);
        if (InstanceScript* instance = GetTarget()->GetInstanceScript())
            if (Creature* festergut = ObjectAccessor::GetCreature(*GetTarget(), instance->GetGuidData(DATA_FESTERGUT)))
                festergut->AI()->SetData(DATA_INOCULATED_STACK, GetStackAmount());

        HandleResidue();
    }

    /**
     * @brief 处理残渣任务物品
     *
     * 为正在进行任务的玩家提供任务物品
     */
    void HandleResidue()
    {
        Player* target = GetUnitOwner()->ToPlayer();
        if (!target)
            return;

        if (target->HasAura(SPELL_ORANGE_BLIGHT_RESIDUE))
            return;

        uint32 questId = target->GetMap()->Is25ManRaid() ? QUEST_RESIDUE_RENDEZVOUS_25 : QUEST_RESIDUE_RENDEZVOUS_10;
        if (target->GetQuestStatus(questId) != QUEST_STATUS_INCOMPLETE)
            return;

        target->CastSpell(target, SPELL_ORANGE_BLIGHT_RESIDUE, TRIGGERED_FULL_MASK);
    }

    void Register() override
    {
        OnEffectRemove += AuraEffectRemoveFn(spell_festergut_blighted_spores::ExtraEffect, EFFECT_0, SPELL_AURA_PERIODIC_DAMAGE, AURA_EFFECT_HANDLE_REAL);
    }
};

/**
 * @class achievement_flu_shot_shortage
 * @brief 流感疫苗短缺成就脚本
 *
 * 检查团队是否在所有人获得3层免疫前击败BOSS
 */
class achievement_flu_shot_shortage : public AchievementCriteriaScript
{
    public:
        achievement_flu_shot_shortage() : AchievementCriteriaScript("achievement_flu_shot_shortage") { }

        /**
         * @brief 成就检查
         * @param source 触发玩家
         * @param target 目标单位（BOSS）
         * @return true 如果成就条件满足
         */
        bool OnCheck(Player* /*source*/, Unit* target) override
        {
            if (target && target->GetTypeId() == TYPEID_UNIT)
                return target->ToCreature()->AI()->GetData(DATA_INOCULATED_STACK) < 3;

            return false;
        }
};

/**
 * @brief 注册腐面者脚本
 *
 * 注册所有生物AI、法术脚本和成就脚本
 */
void AddSC_boss_festergut()
{
    // 注册生物AI
    RegisterIcecrownCitadelCreatureAI(boss_festergut);
    RegisterIcecrownCitadelCreatureAI(npc_stinky_icc);

    // 注册法术脚本
    RegisterSpellScript(spell_festergut_pungent_blight);
    RegisterSpellScript(spell_festergut_gastric_bloat);
    RegisterSpellScript(spell_festergut_blighted_spores);

    // 注册成就脚本
    new achievement_flu_shot_shortage();
}
