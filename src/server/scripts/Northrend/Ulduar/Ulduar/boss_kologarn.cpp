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
 * @file boss_kologarn.cpp
 * @brief 奥杜尔副本 - 科隆加恩首领战脚本
 *
 * 本模块实现了奥杜尔副本中的科隆加恩首领战。
 * 科隆加恩是一个巨大的石头巨人，拥有左右手臂作为独立的可破坏部位。
 *
 * 战斗机制：
 * - 科隆加恩本体固定位置，不移动
 * - 左右手臂作为独立部位，可以被摧毁
 * - 手臂被摧毁后会在40秒后重生
 * - 摧毁手臂会召唤碎石小怪
 * - 双手都摧毁时，科隆加恩使用石吼技能
 * - 使用聚焦眼棱追踪随机玩家
 *
 * 成就：
 * - "卸膀"：在12秒内摧毁双手
 * - "碎石者"：快速清除碎石小怪
 */

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellAuraEffects.h"
#include "SpellScript.h"
#include "ulduar.h"
#include "Vehicle.h"

/* ScriptData
SDName: boss_kologarn
SD%Complete: 90
SDComment: @todo Achievements
SDCategory: Ulduar
EndScriptData */

/**
 * @brief 科隆加恩战斗使用的法术ID枚举
 *
 * 包含科隆加恩本体、手臂和碎石相关的所有法术
 */
enum Spells
{
    // 手臂相关
    SPELL_ARM_DEAD_DAMAGE               = 63629,  ///< 手臂死亡伤害：手臂被摧毁时对本体造成的伤害
    SPELL_TWO_ARM_SMASH                 = 63356,  ///< 双手猛击：双手都在时的猛击技能
    SPELL_ONE_ARM_SMASH                 = 63573,  ///< 单手猛击：只有一只手时的猛击技能
    SPELL_ARM_SWEEP                     = 63766,  ///< 手臂横扫：左手的横扫技能
    SPELL_STONE_SHOUT                   = 63716,  ///< 石吼：双手都摧毁时的全团伤害技能
    SPELL_PETRIFY_BREATH                = 62030,  ///< 石化吐息：近战范围外时的吐息技能
    SPELL_STONE_GRIP                    = 62166,  ///< 石握：右手抓住玩家
    SPELL_STONE_GRIP_CANCEL             = 65594,  ///< 取消石握：释放被抓住的玩家
    SPELL_SUMMON_RUBBLE                 = 63633,  ///< 召唤碎石：手臂摧毁时召唤碎石小怪
    SPELL_FALLING_RUBBLE                = 63821,  ///< 坠落碎石：碎石落下的视觉效果
    SPELL_ARM_ENTER_VEHICLE             = 65343,  ///< 手臂进入载具：手臂附着的载具机制
    SPELL_ARM_ENTER_VISUAL              = 64753,  ///< 手臂进入视觉效果：手臂重生的视觉效果

    // 聚焦眼棱
    SPELL_SUMMON_FOCUSED_EYEBEAM        = 63342,  ///< 召唤聚焦眼棱：召唤眼棱追踪玩家
    SPELL_FOCUSED_EYEBEAM_PERIODIC      = 63347,  ///< 聚焦眼棱周期伤害：眼棱的周期性伤害
    SPELL_FOCUSED_EYEBEAM_VISUAL        = 63369,  ///< 聚焦眼棱视觉效果：眼棱的视觉光束
    SPELL_FOCUSED_EYEBEAM_VISUAL_LEFT   = 63676,  ///< 聚焦眼棱左眼视觉效果：左眼的光束
    SPELL_FOCUSED_EYEBEAM_VISUAL_RIGHT  = 63702,  ///< 聚焦眼棱右眼视觉效果：右眼的光束

    // 被动技能
    SPELL_KOLOGARN_REDUCE_PARRY         = 64651,  ///< 科隆加恩减少招架：减少招架几率的被动光环
    SPELL_KOLOGARN_PACIFY               = 63726,  ///< 科隆加恩安抚：死亡时的安抚效果
    SPELL_KOLOGARN_UNK_0                = 65219,  ///< 科隆加恩未知效果：未在DBC中找到的法术

    SPELL_BERSERK                       = 47008   ///< 狂暴：硬暴怒机制（ID待确认）
};

/**
 * @brief NPC ID枚举
 *
 * 定义战斗中使用的特殊NPC
 */
enum NPCs
{
    NPC_RUBBLE_STALKER                  = 33809,  ///< 碎石潜伏者：用于召唤碎石的触发NPC
    NPC_ARM_SWEEP_STALKER               = 33661   ///< 手臂横扫潜伏者：用于手臂横扫技能的触发NPC
};

/**
 * @brief 科隆加恩战斗的事件ID枚举
 *
 * 用于AI事件调度系统，管理技能冷却和特殊行为
 */
enum Events
{
    EVENT_NONE = 0,                    ///< 无事件
    EVENT_INSTALL_ACCESSORIES,         ///< 安装附件：安装手臂的载具机制
    EVENT_MELEE_CHECK,                 ///< 近战检查：检查目标是否在近战范围
    EVENT_SMASH,                       ///< 猛击：双手或单手猛击技能
    EVENT_SWEEP,                       ///< 横扫：左手横扫技能
    EVENT_STONE_SHOUT,                 ///< 石吼：双手都摧毁时的全团伤害
    EVENT_STONE_GRIP,                  ///< 石握：右手抓住玩家
    EVENT_FOCUSED_EYEBEAM,             ///< 聚焦眼棱：眼棱追踪技能
    EVENT_RESPAWN_LEFT_ARM,            ///< 重生左手：左手摧毁后40秒重生
    EVENT_RESPAWN_RIGHT_ARM,           ///< 重生右手：右手摧毁后40秒重生
    EVENT_ENRAGE,                      ///< 狂暴：10分钟超时暴怒
};

/**
 * @brief 科隆加恩战斗的台词ID枚举
 *
 * 定义科隆加恩的各种战斗台词索引
 */
enum Yells
{
    SAY_AGGRO                               = 0,  ///< 开战台词
    SAY_SLAY                                = 1,  ///< 击杀玩家台词
    SAY_LEFT_ARM_GONE                       = 2,  ///< 左手被摧毁台词
    SAY_RIGHT_ARM_GONE                      = 3,  ///< 右手被摧毁台词
    SAY_SHOCKWAVE                           = 4,  ///< 震荡波台词（未使用）
    SAY_GRAB_PLAYER                         = 5,  ///< 抓住玩家台词
    SAY_DEATH                               = 6,  ///< 死亡台词
    SAY_BERSERK                             = 7,  ///< 狂暴台词
    EMOTE_STONE_GRIP                        = 8   ///< 石握表情（提示玩家自救）
};

/**
 * @class boss_kologarn
 * @brief 科隆加恩首领脚本类
 *
 * 科隆加恩是奥杜尔副本中的巨人首领，拥有左右手臂作为可破坏部位。
 * 主要特点：
 * - 本体固定位置，不移动
 * - 左右手臂独立存在，可以被摧毁并重生
 * - 使用聚焦眼棱追踪随机玩家
 * - 手臂被摧毁时召唤碎石小怪
 * - 双手都摧毁时使用石吼全团伤害
 */
class boss_kologarn : public CreatureScript
{
    public:
        boss_kologarn() : CreatureScript("boss_kologarn") { }

        /**
         * @struct boss_kologarnAI
         * @brief 科隆加恩的AI实现
         *
         * 实现科隆加恩的战斗逻辑，包括手臂管理、碎石召唤和眼棱追踪
         */
        struct boss_kologarnAI : public BossAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物对象指针
             *
             * 初始化科隆加恩：
             * - 移除不可交互标志
             * - 设置为固定位置（根除状态）
             * - 施加减招架光环
             * - 禁用移动
             */
            boss_kologarnAI(Creature* creature) : BossAI(creature, DATA_KOLOGARN),
                left(false), right(false)
            {
                me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);  // 允许玩家交互
                me->SetControlled(true, UNIT_STATE_ROOT);  // 固定位置，不移动

                DoCast(SPELL_KOLOGARN_REDUCE_PARRY);  // 施加减招架光环
                SetCombatMovement(false);  // 禁用战斗移动
            }

            bool left;   ///< 左手是否存在
            bool right;  ///< 右手是否存在
            ObjectGuid eyebeamTarget;  ///< 聚焦眼棱目标的GUID

            /**
             * @brief 进入战斗
             * @param who 进入战斗的目标
             *
             * 首领进入战斗时的初始化：
             * - 播放开战台词
             * - 启动所有技能的计时器
             * - 让双手也进入战斗
             */
            void JustEngagedWith(Unit* who) override
            {
                Talk(SAY_AGGRO);

                // 启动技能循环
                events.ScheduleEvent(EVENT_MELEE_CHECK, 6s);  // 近战检查
                events.ScheduleEvent(EVENT_SMASH, 5s);  // 猛击
                events.ScheduleEvent(EVENT_SWEEP, 19s);  // 横扫
                events.ScheduleEvent(EVENT_STONE_GRIP, 25s);  // 石握
                events.ScheduleEvent(EVENT_FOCUSED_EYEBEAM, 21s);  // 聚焦眼棱
                events.ScheduleEvent(EVENT_ENRAGE, 10min);  // 10分钟狂暴

                // 让双手也进入战斗
                if (Vehicle* vehicle = me->GetVehicleKit())
                    for (uint8 i = 0; i < 2; ++i)
                        if (Unit* arm = vehicle->GetPassenger(i))
                            DoZoneInCombat(arm->ToCreature());

                BossAI::JustEngagedWith(who);
            }

            /**
             * @brief 重置首领状态
             *
             * 在战斗结束或重置时调用：
             * - 调用基类重置
             * - 移除不可交互标志
             * - 清空眼棱目标
             */
            void Reset() override
            {
                _Reset();
                me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                eyebeamTarget.Clear();
            }

            /**
             * @brief 首领死亡
             * @param killer 击杀者
             *
             * 处理首领死亡逻辑：
             * - 播放死亡台词
             * - 施加安抚效果
             * - 移动到原位
             * - 设置尸体长期存在（防止消失）
             */
            void JustDied(Unit* /*killer*/) override
            {
                Talk(SAY_DEATH);
                DoCast(SPELL_KOLOGARN_PACIFY);  // 施加安抚效果
                me->GetMotionMaster()->MoveTargetedHome();  // 返回原位
                me->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);  // 设置不可交互
                me->SetCorpseDelay(604800);  // 尸体存在7天（604800秒）
                _JustDied();
            }

            /**
             * @brief 击杀单位
             * @param who 被击杀的单位
             *
             * 玩家被击杀时播放台词
             */
            void KilledUnit(Unit* who) override
            {
                if (who->GetTypeId() == TYPEID_PLAYER)
                    Talk(SAY_SLAY);
            }

            /**
             * @brief 乘客登载事件
             * @param who 登载的单位（手臂）
             * @param seatId 座位ID
             * @param apply true=登载，false=下船
             *
             * 处理手臂的附着和脱离：
             * - 手臂被摧毁时：召唤碎石、造成伤害、启动重生计时器
             * - 手臂重生时：取消石吼、让手臂进入战斗
             */
            void PassengerBoarded(Unit* who, int8 /*seatId*/, bool apply) override
            {
                bool isEncounterInProgress = instance->GetBossState(DATA_KOLOGARN) == IN_PROGRESS;

                // 处理左手
                if (who->GetEntry() == NPC_LEFT_ARM)
                {
                    left = apply;  // 更新左手状态
                    if (!apply && isEncounterInProgress)
                    {
                        Talk(SAY_LEFT_ARM_GONE);  // 左手被摧毁台词
                        events.ScheduleEvent(EVENT_RESPAWN_LEFT_ARM, 40s);  // 40秒后重生
                    }
                }
                // 处理右手
                else if (who->GetEntry() == NPC_RIGHT_ARM)
                {
                    right = apply;  // 更新右手状态
                    if (!apply && isEncounterInProgress)
                    {
                        Talk(SAY_RIGHT_ARM_GONE);  // 右手被摧毁台词
                        events.ScheduleEvent(EVENT_RESPAWN_RIGHT_ARM, 40s);  // 40秒后重生
                    }
                }

                // 战斗未进行时不处理后续逻辑
                if (!isEncounterInProgress)
                    return;

                // 手臂被摧毁
                if (!apply)
                {
                    // 对本体造成伤害
                    who->CastSpell(me, SPELL_ARM_DEAD_DAMAGE, true);

                    // 召唤碎石小怪
                    if (Creature* rubbleStalker = who->FindNearestCreature(NPC_RUBBLE_STALKER, 70.0f))
                    {
                        rubbleStalker->CastSpell(rubbleStalker, SPELL_FALLING_RUBBLE, true);  // 碎石坠落效果
                        rubbleStalker->CastSpell(rubbleStalker, SPELL_SUMMON_RUBBLE, true);  // 召唤碎石
                        who->ToCreature()->DespawnOrUnsummon();  // 消散手臂
                    }

                    // 双手都被摧毁：启动石吼
                    if (!right && !left)
                        events.ScheduleEvent(EVENT_STONE_SHOUT, 5s);

                    // 启动"卸膀"成就计时器
                    instance->DoStartTimedAchievement(ACHIEVEMENT_TIMED_TYPE_EVENT, CRITERIA_DISARMED);
                }
                // 手臂重生
                else
                {
                    events.CancelEvent(EVENT_STONE_SHOUT);  // 取消石吼
                    DoZoneInCombat(who->ToCreature());  // 让手臂进入战斗
                }
            }

            /**
             * @brief 召唤生物
             * @param summon 召唤的生物
             *
             * 处理召唤物的初始化：
             * - 聚焦眼棱：设置视觉效果和追踪目标
             * - 碎石：添加到召唤物列表
             */
            void JustSummoned(Creature* summon) override
            {
                BossAI::JustSummoned(summon);

                switch (summon->GetEntry())
                {
                    case NPC_FOCUSED_EYEBEAM:
                        // 左眼：设置左眼视觉效果
                        summon->CastSpell(me, SPELL_FOCUSED_EYEBEAM_VISUAL_LEFT, true);
                        break;
                    case NPC_FOCUSED_EYEBEAM_RIGHT:
                        // 右眼：设置右眼视觉效果
                        summon->CastSpell(me, SPELL_FOCUSED_EYEBEAM_VISUAL_RIGHT, true);
                        break;
                    case NPC_RUBBLE:
                        // 碎石：添加到召唤物列表
                        summons.Summon(summon);
                        [[fallthrough]];
                    default:
                        return;
                }

                // 眼棱初始化
                summon->CastSpell(summon, SPELL_FOCUSED_EYEBEAM_PERIODIC, true);  // 周期伤害
                summon->CastSpell(summon, SPELL_FOCUSED_EYEBEAM_VISUAL, true);  // 视觉效果
                summon->SetReactState(REACT_PASSIVE);  // 被动状态

                // 设置眼棱追踪目标
                if (eyebeamTarget)
                {
                    if (Unit* target = ObjectAccessor::GetUnit(*summon, eyebeamTarget))
                    {
                        summon->Attack(target, false);  // 攻击目标
                        summon->GetMotionMaster()->MoveChase(target);  // 追踪目标
                    }
                }
            }

            /**
             * @brief 更新AI
             * @param diff 时间差（毫秒）
             *
             * 主循环，处理技能施放和战斗逻辑
             * 性能注意事项：每次更新都检查施法状态，避免重复施法
             */
            void UpdateAI(uint32 diff) override
            {
                // 确保有有效目标
                if (!UpdateVictim())
                    return;

                events.Update(diff);

                // 如果正在施法，暂停技能循环
                if (me->HasUnitState(UNIT_STATE_CASTING))
                    return;

                // 处理事件队列
                while (uint32 eventId = events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_MELEE_CHECK:
                            // 近战检查：如果目标不在近战范围，使用石化吐息
                            if (!me->IsWithinMeleeRange(me->GetVictim()))
                                DoCast(SPELL_PETRIFY_BREATH);
                            events.ScheduleEvent(EVENT_MELEE_CHECK, 1s);  // 每秒检查一次
                            break;

                        case EVENT_SWEEP:
                            // 横扫：左手存在时使用
                            if (left)
                                DoCast(me->FindNearestCreature(NPC_ARM_SWEEP_STALKER, 500.0f, true), SPELL_ARM_SWEEP, true);
                            events.ScheduleEvent(EVENT_SWEEP, 25s);  // 25秒CD
                            break;

                        case EVENT_SMASH:
                            // 猛击：根据手臂数量选择技能
                            if (left && right)
                                DoCastVictim(SPELL_TWO_ARM_SMASH);  // 双手猛击
                            else if (left || right)
                                DoCastVictim(SPELL_ONE_ARM_SMASH);  // 单手猛击
                            events.ScheduleEvent(EVENT_SMASH, 15s);  // 15秒CD
                            break;

                        case EVENT_STONE_SHOUT:
                            // 石吼：双手都被摧毁时使用，全团自然伤害
                            DoCast(SPELL_STONE_SHOUT);
                            events.ScheduleEvent(EVENT_STONE_SHOUT, 2s);  // 2秒CD
                            break;

                        case EVENT_ENRAGE:
                            // 狂暴：10分钟超时
                            DoCast(SPELL_BERSERK);
                            Talk(SAY_BERSERK);
                            break;

                        case EVENT_RESPAWN_LEFT_ARM:
                        case EVENT_RESPAWN_RIGHT_ARM:
                        {
                            // 重生手臂：重新安装手臂到载具
                            if (Vehicle* vehicle = me->GetVehicleKit())
                            {
                                int8 seat = eventId == EVENT_RESPAWN_LEFT_ARM ? 0 : 1;  // 左手座位0，右手座位1
                                uint32 entry = eventId == EVENT_RESPAWN_LEFT_ARM ? NPC_LEFT_ARM : NPC_RIGHT_ARM;
                                vehicle->InstallAccessory(entry, seat, true, TEMPSUMMON_MANUAL_DESPAWN, 0);
                            }
                            break;
                        }

                        case EVENT_STONE_GRIP:
                        {
                            // 石握：右手存在时使用，抓住随机玩家
                            if (right)
                            {
                                DoCast(SPELL_STONE_GRIP);
                                Talk(SAY_GRAB_PLAYER);
                                Talk(EMOTE_STONE_GRIP);  // 表情提示玩家
                            }
                            events.ScheduleEvent(EVENT_STONE_GRIP, 25s);  // 25秒CD
                            break;
                        }

                        case EVENT_FOCUSED_EYEBEAM:
                            // 聚焦眼棱：召唤眼棱追踪随机玩家
                            if (Unit* eyebeamTargetUnit = SelectTarget(SelectTargetMethod::MaxDistance, 0, 0, true))
                            {
                                eyebeamTarget = eyebeamTargetUnit->GetGUID();  // 记录目标
                                DoCast(me, SPELL_SUMMON_FOCUSED_EYEBEAM, true);  // 召唤眼棱
                            }
                            events.ScheduleEvent(EVENT_FOCUSED_EYEBEAM, 15s, 35s);  // 15-35秒CD
                            break;
                    }

                    // 施法后暂停事件处理
                    if (me->HasUnitState(UNIT_STATE_CASTING))
                        return;
                }

                // 执行近战攻击
                DoMeleeAttackIfReady();
            }
        };

        /**
         * @brief 获取AI实例
         * @param creature 生物对象
         * @return AI实例指针
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetUlduarAI<boss_kologarnAI>(creature);
        }
};

/**
 * @class spell_ulduar_rubble_summon
 * @brief 召唤碎石法术脚本 (63633)
 *
 * 处理碎石召唤机制：
 * - 召唤5个碎石小怪
 * - 设置科隆加恩为原始施放者（用于战利品和经验）
 */
// 63633 - Summon Rubble
class spell_ulduar_rubble_summon : public SpellScriptLoader
{
    public:
        spell_ulduar_rubble_summon() : SpellScriptLoader("spell_ulduar_rubble_summon") { }

        /**
         * @class spell_ulduar_rubble_summonSpellScript
         * @brief 召唤碎石法术脚本实现
         */
        class spell_ulduar_rubble_summonSpellScript : public SpellScript
        {
            PrepareSpellScript(spell_ulduar_rubble_summonSpellScript);

            /**
             * @brief 处理脚本效果
             * @param effIndex 效果索引
             *
             * 召唤5个碎石小怪，设置科隆加恩为原始施放者
             */
            void HandleScript(SpellEffIndex /*effIndex*/)
            {
                Unit* caster = GetCaster();
                if (!caster)
                    return;

                // 获取科隆加恩的GUID作为原始施放者
                ObjectGuid originalCaster = caster->GetInstanceScript() ? caster->GetInstanceScript()->GetGuidData(DATA_KOLOGARN) : ObjectGuid::Empty;
                uint32 spellId = GetEffectValue();

                // 召唤5个碎石
                for (uint8 i = 0; i < 5; ++i)
                    caster->CastSpell(caster, spellId, originalCaster);
            }

            void Register() override
            {
                OnEffectHitTarget += SpellEffectFn(spell_ulduar_rubble_summonSpellScript::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
            }
        };

        SpellScript* GetSpellScript() const override
        {
            return new spell_ulduar_rubble_summonSpellScript();
        }
};

/**
 * @class StoneGripTargetSelector
 * @brief 石握目标选择器
 *
 * 谓词函数，用于选择石握技能的目标：
 * - 排除主坦克（如果有多个敌人）
 * - 排除非玩家目标
 *
 * 这确保石握抓住非坦克玩家，增加战术复杂性
 */
// predicate function to select non main tank target
class StoneGripTargetSelector
{
    public:
        /**
         * @brief 构造函数
         * @param me 首领生物
         * @param victim 当前目标（主坦克）
         */
        StoneGripTargetSelector(Creature* me, Unit const* victim) : _me(me), _victim(victim) { }

        /**
         * @brief 选择操作符
         * @param target 目标对象
         * @return true表示排除该目标，false表示保留
         */
        bool operator()(WorldObject* target)
        {
            // 排除主坦克（如果有多个敌人）
            if (target == _victim && _me->GetThreatManager().GetThreatListSize() > 1)
                return true;

            // 排除非玩家目标
            if (target->GetTypeId() != TYPEID_PLAYER)
                return true;

            return false;
        }

        Creature* _me;           ///< 首领生物
        Unit const* _victim;     ///< 当前目标（主坦克）
};

/**
 * @class spell_ulduar_stone_grip_cast_target
 * @brief 石握施放目标法术脚本 (62166, 63981)
 *
 * 处理石握技能的目标选择：
 * - 10人模式：抓住1个玩家
 * - 25人模式：抓住3个玩家
 * - 排除主坦克和非玩家目标
 * - 确保多个效果作用于相同目标
 */
// 62166, 63981 - Stone Grip
class spell_ulduar_stone_grip_cast_target : public SpellScriptLoader
{
    public:
        spell_ulduar_stone_grip_cast_target() : SpellScriptLoader("spell_ulduar_stone_grip_cast_target") { }

        /**
         * @class spell_ulduar_stone_grip_cast_target_SpellScript
         * @brief 石握施放目标法术脚本实现
         */
        class spell_ulduar_stone_grip_cast_target_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_ulduar_stone_grip_cast_target_SpellScript);

            /**
             * @brief 加载验证
             * @return 是否成功加载
             *
             * 确保施放者是生物类型
             */
            bool Load() override
            {
                if (GetCaster()->GetTypeId() != TYPEID_UNIT)
                    return false;
                return true;
            }

            /**
             * @brief 初始过滤目标
             * @param unitList 目标列表
             *
             * 选择石握的目标：
             * 1. 排除主坦克和非玩家
             * 2. 根据难度确定最大目标数
             * 3. 随机选择目标
             * 4. 保存目标列表供后续效果使用
             */
            void FilterTargetsInitial(std::list<WorldObject*>& unitList)
            {
                // 移除主坦克和非玩家目标
                unitList.remove_if(StoneGripTargetSelector(GetCaster()->ToCreature(), GetCaster()->GetThreatManager().GetCurrentVictim()));

                // 根据难度模式确定最大目标数
                uint32 maxTargets = 1;  // 10人模式
                if (GetSpellInfo()->Id == 63981)
                    maxTargets = 3;  // 25人模式

                // 随机移除目标直到达到最大数量
                while (maxTargets < unitList.size())
                {
                    std::list<WorldObject*>::iterator itr = unitList.begin();
                    advance(itr, urand(0, unitList.size()-1));
                    unitList.erase(itr);
                }

                // 保存目标列表供后续效果使用
                _unitList = unitList;
            }

            /**
             * @brief 填充后续目标
             * @param unitList 目标列表
             *
             * 确保所有效果作用于相同的目标集合
             */
            void FillTargetsSubsequential(std::list<WorldObject*>& unitList)
            {
                unitList = _unitList;
            }

            void Register() override
            {
                // 注册所有效果的目标选择
                OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_ulduar_stone_grip_cast_target_SpellScript::FilterTargetsInitial, EFFECT_0, TARGET_UNIT_SRC_AREA_ENEMY);
                OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_ulduar_stone_grip_cast_target_SpellScript::FillTargetsSubsequential, EFFECT_1, TARGET_UNIT_SRC_AREA_ENEMY);
                OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_ulduar_stone_grip_cast_target_SpellScript::FillTargetsSubsequential, EFFECT_2, TARGET_UNIT_SRC_AREA_ENEMY);
            }

        private:
            std::list<WorldObject*> _unitList;  ///< 共享的目标列表，用于多个效果
        };

        SpellScript* GetSpellScript() const override
        {
            return new spell_ulduar_stone_grip_cast_target_SpellScript();
        }
};

/**
 * @class spell_ulduar_cancel_stone_grip
 * @brief 取消石握法术脚本 (65594)
 *
 * 当右手受到足够伤害时释放被抓住的玩家
 */
// 65594 - Cancel Stone Grip
class spell_ulduar_cancel_stone_grip : public SpellScriptLoader
{
    public:
        spell_ulduar_cancel_stone_grip() : SpellScriptLoader("spell_ulduar_cancel_stone_grip") { }

        class spell_ulduar_cancel_stone_gripSpellScript : public SpellScript
        {
            PrepareSpellScript(spell_ulduar_cancel_stone_gripSpellScript);

            /**
             * @brief 处理脚本效果：移除石握光环
             * @param effIndex 效果索引
             *
             * 根据难度模式选择正确的效果索引来移除石握光环
             */
            void HandleScript(SpellEffIndex /*effIndex*/)
            {
                Unit* target = GetHitUnit();
                if (!target || !target->GetVehicle())
                    return;

                // 根据难度选择效果索引
                SpellEffIndex effectIndexToCancel = EFFECT_0;  // 10人模式
                if (target->GetMap()->Is25ManRaid())
                    effectIndexToCancel = EFFECT_1;  // 25人模式

                // 移除石握光环
                target->RemoveAura(GetEffectInfo(effectIndexToCancel).CalcValue());
            }

            void Register() override
            {
                OnEffectHitTarget += SpellEffectFn(spell_ulduar_cancel_stone_gripSpellScript::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
            }
        };

        SpellScript* GetSpellScript() const override
        {
            return new spell_ulduar_cancel_stone_gripSpellScript();
        }
};

/**
 * @class spell_ulduar_squeezed_lifeless
 * @brief 被挤压致死法术脚本 (64702)
 *
 * 当玩家在石握中死亡时，将玩家从载具中移出并放置在安全位置
 */
// 64702 - Squeezed Lifeless
class spell_ulduar_squeezed_lifeless : public SpellScriptLoader
{
    public:
        spell_ulduar_squeezed_lifeless() : SpellScriptLoader("spell_ulduar_squeezed_lifeless") { }

        class spell_ulduar_squeezed_lifeless_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_ulduar_squeezed_lifeless_SpellScript);

            /**
             * @brief 处理即死效果：移出载具并传送到安全位置
             * @param effIndex 效果索引
             *
             * 将死亡的玩家从右手载具中移出，放置在房间内安全位置
             * 注意：正确的退出位置目前无法正常工作，参见Unit::ExitVehicle文档
             */
            void HandleInstaKill(SpellEffIndex /*effIndex*/)
            {
                if (!GetHitPlayer() || !GetHitPlayer()->GetVehicle())
                    return;

                // 设置安全退出位置（随机偏移避免重叠）
                Position pos;
                pos.m_positionX = 1756.25f + irand(-3, 3);
                pos.m_positionY = -8.3f + irand(-3, 3);
                pos.m_positionZ = 448.8f;
                pos.SetOrientation(float(M_PI));

                // 玩家离开载具
                GetHitPlayer()->DestroyForNearbyPlayers();
                GetHitPlayer()->ExitVehicle(&pos);
                GetHitPlayer()->UpdateObjectVisibility(false);
            }

            void Register() override
            {
                OnEffectHitTarget += SpellEffectFn(spell_ulduar_squeezed_lifeless_SpellScript::HandleInstaKill, EFFECT_1, SPELL_EFFECT_INSTAKILL);
            }
        };

        SpellScript* GetSpellScript() const override
        {
            return new spell_ulduar_squeezed_lifeless_SpellScript();
        }
};

/**
 * @class spell_ulduar_stone_grip_absorb
 * @brief 石握吸收护盾法术脚本 (64224, 64225)
 *
 * 当右手受到足够伤害（护盾被打破）时，释放所有被抓住的玩家
 */
// 64224, 64225 - Stone Grip Absorb
class spell_ulduar_stone_grip_absorb : public SpellScriptLoader
{
    public:
        spell_ulduar_stone_grip_absorb() : SpellScriptLoader("spell_ulduar_stone_grip_absorb") { }

        class spell_ulduar_stone_grip_absorb_AuraScript : public AuraScript
        {
            PrepareAuraScript(spell_ulduar_stone_grip_absorb_AuraScript);

            /**
             * @brief 光环移除处理：当右手受到足够伤害时释放玩家
             * @param aurEff 光环效果
             * @param mode 处理模式
             *
             * 当右手（载具）受到特定伤害时触发，释放所有被抓住的玩家
             */
            void OnRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
            {
                // 只在受到伤害移除时触发，不是自然过期
                if (GetTargetApplication()->GetRemoveMode() != AURA_REMOVE_BY_ENEMY_SPELL)
                    return;

                if (!GetOwner()->ToCreature())
                    return;

                // 查找碎石潜伏者并施放取消石握法术
                uint32 rubbleStalkerEntry = (GetOwner()->GetMap()->GetDifficulty() == DUNGEON_DIFFICULTY_NORMAL ? 33809 : 33942);
                Creature* rubbleStalker = GetOwner()->FindNearestCreature(rubbleStalkerEntry, 200.0f, true);
                if (rubbleStalker)
                    rubbleStalker->CastSpell(rubbleStalker, SPELL_STONE_GRIP_CANCEL, true);
            }

            void Register() override
            {
                AfterEffectRemove += AuraEffectRemoveFn(spell_ulduar_stone_grip_absorb_AuraScript::OnRemove, EFFECT_0, SPELL_AURA_SCHOOL_ABSORB, AURA_EFFECT_HANDLE_REAL);
            }
        };

        AuraScript* GetAuraScript() const override
        {
            return new spell_ulduar_stone_grip_absorb_AuraScript();
        }
};

/**
 * @class spell_ulduar_stone_grip
 * @brief 石握光环法术脚本 (62056, 63985)
 *
 * 管理石握的载具控制和击晕效果，包括玩家释放时的位置处理
 */
// 62056, 63985 - Stone Grip
class spell_ulduar_stone_grip : public SpellScriptLoader
{
    public:
        spell_ulduar_stone_grip() : SpellScriptLoader("spell_ulduar_stone_grip") { }

        class spell_ulduar_stone_grip_AuraScript : public AuraScript
        {
            PrepareAuraScript(spell_ulduar_stone_grip_AuraScript);

            /**
             * @brief 移除击晕效果
             * @param aurEff 光环效果
             * @param mode 处理模式
             */
            void OnRemoveStun(AuraEffect const* aurEff, AuraEffectHandleModes /*mode*/)
            {
                if (Player* owner = GetOwner()->ToPlayer())
                    owner->RemoveAurasDueToSpell(aurEff->GetAmount());
            }

            /**
             * @brief 移除载具控制：将玩家从载具中释放
             * @param aurEff 光环效果
             * @param mode 处理模式
             *
             * 处理玩家被释放时的载具退出和位置设置
             */
            void OnRemoveVehicle(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
            {
                PreventDefaultAction();
                Unit* caster = GetCaster();
                if (!caster)
                    return;

                // 设置退出位置
                Position exitPosition;
                exitPosition.m_positionX = 1750.0f;
                exitPosition.m_positionY = -7.5f + frand(-3.0f, 3.0f);
                exitPosition.m_positionZ = 457.9322f;

                // 移除载具的待处理事件
                GetTarget()->GetVehicleKit()->RemovePendingEventsForPassenger(caster);
                caster->_ExitVehicle(&exitPosition);
                caster->RemoveAurasDueToSpell(GetId());

                // 临时重定位玩家以发送正确的下落移动包
                Position oldPos = caster->GetPosition();
                caster->Relocate(exitPosition);
                caster->GetMotionMaster()->MoveFall();
                caster->Relocate(oldPos);
            }

            void Register() override
            {
                OnEffectRemove += AuraEffectRemoveFn(spell_ulduar_stone_grip_AuraScript::OnRemoveVehicle, EFFECT_0, SPELL_AURA_CONTROL_VEHICLE, AURA_EFFECT_HANDLE_REAL);
                AfterEffectRemove += AuraEffectRemoveFn(spell_ulduar_stone_grip_AuraScript::OnRemoveStun, EFFECT_2, SPELL_AURA_MOD_STUN, AURA_EFFECT_HANDLE_REAL);
            }
        };

        AuraScript* GetAuraScript() const override
        {
            return new spell_ulduar_stone_grip_AuraScript();
        }
};

/**
 * @class spell_kologarn_stone_shout
 * @brief 石吼法术脚本 (63720, 64004)
 *
 * 当双手都被摧毁时，科隆加恩使用石吼攻击所有玩家和宠物
 */
// 63720, 64004 - Stone Shout
class spell_kologarn_stone_shout : public SpellScriptLoader
{
    public:
        spell_kologarn_stone_shout() : SpellScriptLoader("spell_kologarn_stone_shout") { }

        class spell_kologarn_stone_shout_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_kologarn_stone_shout_SpellScript);

            /**
             * @brief 过滤目标：只影响玩家和宠物
             * @param targets 目标列表
             */
            void FilterTargets(std::list<WorldObject*>& targets)
            {
                targets.remove_if([](WorldObject* object) -> bool
                {
                    // 保留玩家
                    if (object->GetTypeId() == TYPEID_PLAYER)
                        return false;

                    // 保留宠物
                    if (Creature* creature = object->ToCreature())
                        return !creature->IsPet();

                    return true;
                });
            }

            void Register() override
            {
                OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_kologarn_stone_shout_SpellScript::FilterTargets, EFFECT_0, TARGET_UNIT_SRC_AREA_ENEMY);
            }
        };

        SpellScript* GetSpellScript() const override
        {
            return new spell_kologarn_stone_shout_SpellScript();
        }
};

/**
 * @class spell_kologarn_summon_focused_eyebeam
 * @brief 召唤聚焦眼棱触发法术脚本 (63342)
 *
 * 处理聚焦眼棱的召唤，强制施放者施放触发法术
 */
// 63342 - Focused Eyebeam Summon Trigger
class spell_kologarn_summon_focused_eyebeam : public SpellScriptLoader
{
    public:
        spell_kologarn_summon_focused_eyebeam() : SpellScriptLoader("spell_kologarn_summon_focused_eyebeam") { }

        class spell_kologarn_summon_focused_eyebeam_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_kologarn_summon_focused_eyebeam_SpellScript);

            /**
             * @brief 处理强制施法：强制施放者施放触发的眼棱法术
             * @param effIndex 效果索引
             */
            void HandleForceCast(SpellEffIndex effIndex)
            {
                PreventHitDefaultEffect(effIndex);
                GetCaster()->CastSpell(GetCaster(), GetEffectInfo().TriggerSpell, true);
            }

            void Register() override
            {
                OnEffectHitTarget += SpellEffectFn(spell_kologarn_summon_focused_eyebeam_SpellScript::HandleForceCast, EFFECT_0, SPELL_EFFECT_FORCE_CAST);
                OnEffectHitTarget += SpellEffectFn(spell_kologarn_summon_focused_eyebeam_SpellScript::HandleForceCast, EFFECT_1, SPELL_EFFECT_FORCE_CAST);
            }
        };

        SpellScript* GetSpellScript() const override
        {
            return new spell_kologarn_summon_focused_eyebeam_SpellScript();
        }
};

/**
 * @brief 注册科隆加恩战斗脚本
 *
 * 注册所有科隆加恩相关的脚本：
 * - 首领AI脚本
 * - 法术脚本（碎石、石握、石吼、眼棱等）
 */
void AddSC_boss_kologarn()
{
    new boss_kologarn();  // 科隆加恩首领
    new spell_ulduar_rubble_summon();  // 召唤碎石
    new spell_ulduar_squeezed_lifeless();  // 被挤压致死
    new spell_ulduar_cancel_stone_grip();  // 取消石握
    new spell_ulduar_stone_grip_cast_target();  // 石握目标选择
    new spell_ulduar_stone_grip_absorb();  // 石握吸收护盾
    new spell_ulduar_stone_grip();  // 石握光环
    new spell_kologarn_stone_shout();  // 石吼
    new spell_kologarn_summon_focused_eyebeam();  // 聚焦眼棱
}
