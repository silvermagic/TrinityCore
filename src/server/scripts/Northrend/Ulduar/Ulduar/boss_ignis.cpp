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
 * @file boss_ignis.cpp
 * @brief 伊格尼斯（Ignis the Furnace Master）Boss 战斗脚本模块
 *
 * 模块职责：
 * 实现奥杜尔副本中伊格尼斯 Boss 的完整战斗逻辑，包括：
 * - 伊格尼斯的主要战斗 AI 和技能系统
 * - 火焰喷射、灼烧、炉渣罐等主要技能
 * - 铁构造体的激活和加热机制
 * - 构造体熔化和碎裂机制（困难模式成就）
 * - Shattered 成就系统（5 秒内击杀 2 个构造体）
 *
 * 战斗机制：
 * - 伊格尼斯定期激活铁构造体
 * - 构造体需要在灼烧区域中加热到熔化状态
 * - 熔化的构造体被水冷却后变为易碎状态
 * - 易碎状态的构造体可以被玩家击碎
 */

#include "ScriptMgr.h"
#include "GameTime.h"
#include "InstanceScript.h"
#include "ObjectAccessor.h"
#include "ScriptedCreature.h"
#include "SpellAuras.h"
#include "SpellScript.h"
#include "ulduar.h"
#include "Vehicle.h"

/**
 * @brief 对话和喊叫枚举定义
 *
 * 定义伊格尼斯在战斗中的各种对话和表情文本 ID
 */
enum Yells
{
    SAY_AGGRO       = 0,  ///< 开怪对话
    SAY_SUMMON      = 1,  ///< 召唤构造体对话
    SAY_SLAG_POT    = 2,  ///< 炉渣罐对话
    SAY_SCORCH      = 3,  ///< 灼烧对话
    SAY_SLAY        = 4,  ///< 击杀玩家对话
    SAY_BERSERK     = 5,  ///< 狂暴对话
    SAY_DEATH       = 6,  ///< 死亡对话
    EMOTE_JETS      = 7   ///< 火焰喷射表情
};

/**
 * @brief 法术 ID 枚举定义
 *
 * 定义伊格尼斯战斗中使用的所有法术 ID
 */
enum Spells
{
    // Boss 技能
    SPELL_FLAME_JETS            = 62680,  ///< 火焰喷射 - 对所有玩家造成火焰伤害
    SPELL_SCORCH                = 62546,  ///< 灼烧 - 对前方锥形区域造成火焰伤害并留下灼烧区域
    SPELL_SLAG_POT              = 62717,  ///< 炉渣罐 - 抓住玩家并投入熔炉
    SPELL_SLAG_POT_DAMAGE       = 65722,  ///< 炉渣罐伤害 - 对被抓玩家造成持续伤害
    SPELL_SLAG_IMBUED           = 62836,  ///< 炉渣灌注 - 增加玩家伤害的增益
    SPELL_ACTIVATE_CONSTRUCT    = 62488,  ///< 激活构造体 - 激活铁构造体
    SPELL_STRENGHT              = 64473,  ///< 力量 - 每激活一个构造体增加 Boss 伤害
    SPELL_GRAB                  = 62707,  ///< 抓取 - 抓住玩家的法术
    SPELL_BERSERK               = 47008,  ///< 狂暴 - 战斗超时后的狂暴效果

    // 铁构造体技能
    SPELL_HEAT                  = 65667,  ///< 加热 - 构造体在灼烧区域中的加热效果
    SPELL_MOLTEN                = 62373,  ///< 熔化 - 构造体完全加热后的熔化状态
    SPELL_BRITTLE               = 62382,  ///< 易碎 10 人 - 熔化构造体被水冷却后的易碎状态
    SPELL_BRITTLE_25            = 67114,  ///< 易碎 25 人 - 25 人模式的易碎状态
    SPELL_SHATTER               = 62383,  ///< 碎裂 - 易碎构造体被攻击后的碎裂效果
    SPELL_GROUND                = 62548,  ///< 地面 - 地面灼烧效果
};

/**
 * @brief 事件 ID 枚举定义
 *
 * 定义战斗中使用的所有事件 ID，用于事件调度系统
 */
enum Events
{
    EVENT_JET           = 1,  ///< 火焰喷射事件
    EVENT_SCORCH        = 2,  ///< 灼烧事件
    EVENT_SLAG_POT      = 3,  ///< 炉渣罐事件
    EVENT_GRAB_POT      = 4,  ///< 抓取玩家事件
    EVENT_CHANGE_POT    = 5,  ///< 更换炉渣罐座位事件
    EVENT_END_POT       = 6,  ///< 结束炉渣罐事件
    EVENT_CONSTRUCT     = 7,  ///< 召唤构造体事件
    EVENT_BERSERK       = 8,  ///< 狂暴事件
};

/**
 * @brief 动作枚举定义
 *
 * 定义 Boss 和构造体之间的通信动作
 */
enum Actions
{
    ACTION_REMOVE_BUFF = 20,  ///< 移除增益动作 - 构造体被击碎时通知 Boss 移除力量增益
};

/**
 * @brief 生物 NPC ID 枚举定义
 *
 * 定义伊格尼斯战斗中涉及的所有 NPC ID
 */
enum Creatures
{
    NPC_IRON_CONSTRUCT  = 33121,  ///< 铁构造体 - 可被激活和加热的小怪
    NPC_GROUND_SCORCH   = 33221,  ///< 地面灼烧 - 灼烧区域触发器
};

/**
 * @brief 成就数据枚举定义
 *
 * 定义成就相关的数据 ID
 */
enum AchievementData
{
    DATA_SHATTERED                  = 29252926,  ///< Shattered 成就数据 (2925, 2926 是成就 ID)
    ACHIEVEMENT_IGNIS_START_EVENT   = 20951,     ///< 伊格尼斯成就事件 ID
};

/// 构造体生成点数量
#define CONSTRUCT_SPAWN_POINTS 20

/**
 * @brief 铁构造体生成位置数组
 *
 * 定义 20 个铁构造体的生成位置和朝向
 * 这些位置分布在房间的两侧，靠近灼烧区域
 */
Position const ConstructSpawnPosition[CONSTRUCT_SPAWN_POINTS] =
{
    {630.366f, 216.772f, 360.891f, 3.001970f},
    {630.594f, 231.846f, 360.891f, 3.124140f},
    {630.435f, 337.246f, 360.886f, 3.211410f},
    {630.493f, 313.349f, 360.886f, 3.054330f},
    {630.444f, 321.406f, 360.886f, 3.124140f},
    {630.366f, 247.307f, 360.888f, 3.211410f},
    {630.698f, 305.311f, 360.886f, 3.001970f},
    {630.500f, 224.559f, 360.891f, 3.054330f},
    {630.668f, 239.840f, 360.890f, 3.159050f},
    {630.384f, 329.585f, 360.886f, 3.159050f},
    {543.220f, 313.451f, 360.886f, 0.104720f},
    {543.356f, 329.408f, 360.886f, 6.248280f},
    {543.076f, 247.458f, 360.888f, 6.213370f},
    {543.117f, 232.082f, 360.891f, 0.069813f},
    {543.161f, 305.956f, 360.886f, 0.157080f},
    {543.277f, 321.482f, 360.886f, 0.052360f},
    {543.316f, 337.468f, 360.886f, 6.195920f},
    {543.280f, 239.674f, 360.890f, 6.265730f},
    {543.265f, 217.147f, 360.891f, 0.174533f},
    {543.256f, 224.831f, 360.891f, 0.122173f},
};

/**
 * @class boss_ignis
 * @brief 伊格尼斯生物脚本类
 *
 * 继承自 CreatureScript，负责注册伊格尼斯 Boss 的 AI 脚本
 */
class boss_ignis : public CreatureScript
{
    public:
        /**
         * @brief 构造函数
         *
         * 注册 "boss_ignis" 脚本名称
         */
        boss_ignis() : CreatureScript("boss_ignis") { }

        /**
         * @struct boss_ignis_AI
         * @brief 伊格尼斯 AI 结构
         *
         * 继承自 BossAI，实现伊格尼斯的核心战斗逻辑
         */
        struct boss_ignis_AI : public BossAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物对象指针
             */
            boss_ignis_AI(Creature* creature) : BossAI(creature, DATA_IGNIS)
            {
                Initialize();
            }

            /**
             * @brief 初始化成员变量
             *
             * 重置所有状态变量为初始值
             */
            void Initialize()
            {
                _slagPotGUID.Clear();     // 炉渣罐目标 GUID
                _shattered = false;       // Shattered 成就标志
                _firstConstructKill = 0;  // 第一个构造体被击杀的时间
            }

            /**
             * @brief 重置战斗状态
             *
             * 在战斗结束或重置时调用，清理战斗数据
             */
            void Reset() override
            {
                _Reset();
                // 移除所有载具乘客
                if (Vehicle* _vehicle = me->GetVehicleKit())
                    _vehicle->RemoveAllPassengers();

                // 停止成就计时
                instance->DoStopTimedAchievement(ACHIEVEMENT_TIMED_TYPE_EVENT, ACHIEVEMENT_IGNIS_START_EVENT);
            }

            /**
             * @brief 进入战斗
             * @param who 仇恨目标
             *
             * 开始战斗时调用，调度所有战斗事件
             */
            void JustEngagedWith(Unit* who) override
            {
                BossAI::JustEngagedWith(who);
                Talk(SAY_AGGRO);

                // 调度战斗事件
                events.ScheduleEvent(EVENT_JET, 30s);           // 火焰喷射
                events.ScheduleEvent(EVENT_SCORCH, 25s);        // 灼烧
                events.ScheduleEvent(EVENT_SLAG_POT, 35s);      // 炉渣罐
                events.ScheduleEvent(EVENT_CONSTRUCT, 15s);     // 召唤构造体
                events.ScheduleEvent(EVENT_END_POT, 40s);       // 结束炉渣罐
                events.ScheduleEvent(EVENT_BERSERK, 480s);      // 狂暴（8分钟）

                Initialize();
                // 开始成就计时
                instance->DoStartTimedAchievement(ACHIEVEMENT_TIMED_TYPE_EVENT, ACHIEVEMENT_IGNIS_START_EVENT);
            }

            /**
             * @brief 死亡处理
             * @param killer 击杀者（未使用）
             *
             * Boss 死亡时调用，播放死亡对话并处理战利品
             */
            void JustDied(Unit* /*killer*/) override
            {
                _JustDied();
                Talk(SAY_DEATH);
            }

            /**
             * @brief 获取数据
             * @param type 数据类型
             * @return 数据值
             *
             * 用于获取成就相关的数据
             */
            uint32 GetData(uint32 type) const override
            {
                if (type == DATA_SHATTERED)
                    return _shattered ? 1 : 0;  // 返回 Shattered 成就状态

                return 0;
            }

            /**
             * @brief 击杀单位
             * @param who 被击杀的单位
             *
             * 当击杀玩家时播放对话
             */
            void KilledUnit(Unit* who) override
            {
                if (who->GetTypeId() == TYPEID_PLAYER)
                    Talk(SAY_SLAY);
            }

            /**
             * @brief 召唤生物处理
             * @param summon 召唤的生物
             *
             * 处理召唤的铁构造体和地面灼烧
             */
            void JustSummoned(Creature* summon) override
            {
                // 处理铁构造体
                if (summon->GetEntry() == NPC_IRON_CONSTRUCT)
                {
                    summon->SetFaction(FACTION_MONSTER_2);
                    summon->SetReactState(REACT_AGGRESSIVE);
                    // 移除所有不可攻击标志
                    summon->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_UNINTERACTIBLE | UNIT_FLAG_PACIFIED | UNIT_FLAG_STUNNED);
                    summon->SetImmuneToPC(false);
                    summon->SetControlled(false, UNIT_STATE_ROOT);
                }

                // 让召唤物攻击当前目标
                summon->AI()->AttackStart(me->GetVictim());
                summon->AI()->DoZoneInCombat();
                summons.Summon(summon);
            }

            /**
             * @brief 执行动作
             * @param action 动作 ID
             *
             * 处理构造体被击碎时移除力量增益
             */
            void DoAction(int32 action) override
            {
                if (action != ACTION_REMOVE_BUFF)
                    return;

                // 移除一层力量增益
                me->RemoveAuraFromStack(SPELL_STRENGHT);

                // Shattered 成就：检查是否在 5 秒内击杀了 2 个构造体
                time_t secondKill = GameTime::GetGameTime();
                if ((secondKill - _firstConstructKill) < 5)
                    _shattered = true;
                _firstConstructKill = secondKill;
            }

            /**
             * @brief 更新 AI
             * @param diff 时间差（毫秒）
             *
             * 每帧调用，处理战斗逻辑和事件调度
             * 性能注意事项：此函数每帧调用，需保持高效
             */
            void UpdateAI(uint32 diff) override
            {
                if (!UpdateVictim())
                    return;

                events.Update(diff);

                // 如果正在施法，暂停其他动作
                if (me->HasUnitState(UNIT_STATE_CASTING))
                    return;

                // 处理事件队列
                while (uint32 eventId = events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_JET:  // 火焰喷射
                            Talk(EMOTE_JETS);
                            DoCast(me, SPELL_FLAME_JETS);
                            events.ScheduleEvent(EVENT_JET, 35s, 40s);
                            break;
                        case EVENT_SLAG_POT:  // 炉渣罐
                            // 随机选择一个非坦克玩家
                            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 1, 100, true))
                            {
                                Talk(SAY_SLAG_POT);
                                _slagPotGUID = target->GetGUID();
                                DoCast(target, SPELL_GRAB);  // 抓取玩家
                                events.DelayEvents(3s);
                                events.ScheduleEvent(EVENT_GRAB_POT, 500ms);
                            }
                            events.ScheduleEvent(EVENT_SLAG_POT, RAID_MODE(30s, 15s));
                            break;
                        case EVENT_GRAB_POT:  // 抓取玩家到载具
                            if (Unit* slagPotTarget = ObjectAccessor::GetUnit(*me, _slagPotGUID))
                            {
                                slagPotTarget->EnterVehicle(me, 0);  // 玩家进入座位 0
                                events.CancelEvent(EVENT_GRAB_POT);
                                events.ScheduleEvent(EVENT_CHANGE_POT, 1s);
                            }
                            break;
                        case EVENT_CHANGE_POT:  // 更换座位并施放炉渣罐
                            if (Unit* slagPotTarget = ObjectAccessor::GetUnit(*me, _slagPotGUID))
                            {
                                DoCast(slagPotTarget, SPELL_SLAG_POT, true);
                                slagPotTarget->EnterVehicle(me, 1);  // 移动到座位 1
                                events.CancelEvent(EVENT_CHANGE_POT);
                                events.ScheduleEvent(EVENT_END_POT, 10s);
                            }
                            break;
                        case EVENT_END_POT:  // 结束炉渣罐，释放玩家
                            if (Unit* slagPotTarget = ObjectAccessor::GetUnit(*me, _slagPotGUID))
                            {
                                slagPotTarget->ExitVehicle();  // 玩家退出载具
                                slagPotTarget = nullptr;
                                _slagPotGUID.Clear();
                                events.CancelEvent(EVENT_END_POT);
                            }
                            break;
                        case EVENT_SCORCH:  // 灼烧
                            Talk(SAY_SCORCH);
                            // 在当前目标位置生成灼烧区域
                            if (Unit* target = me->GetVictim())
                                me->SummonCreature(NPC_GROUND_SCORCH, target->GetPositionX(), target->GetPositionY(), target->GetPositionZ(), 0, TEMPSUMMON_TIMED_DESPAWN, 45s);
                            DoCast(SPELL_SCORCH);
                            events.ScheduleEvent(EVENT_SCORCH, 25s);
                            break;
                        case EVENT_CONSTRUCT:  // 召唤铁构造体
                            Talk(SAY_SUMMON);
                            // 从 20 个生成点中随机选择一个
                            DoSummon(NPC_IRON_CONSTRUCT, ConstructSpawnPosition[urand(0, CONSTRUCT_SPAWN_POINTS - 1)], 30s, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT);
                            DoCast(SPELL_STRENGHT);  // 增加力量增益
                            DoCast(me, SPELL_ACTIVATE_CONSTRUCT);  // 激活构造体
                            events.ScheduleEvent(EVENT_CONSTRUCT, RAID_MODE(40s, 30s));
                            break;
                        case EVENT_BERSERK:  // 狂暴
                            DoCast(me, SPELL_BERSERK, true);
                            Talk(SAY_BERSERK);
                            break;
                    }

                    // 如果正在施法，暂停后续事件处理
                    if (me->HasUnitState(UNIT_STATE_CASTING))
                        return;
                }

                DoMeleeAttackIfReady();
            }

        private:
            ObjectGuid _slagPotGUID;       ///< 炉渣罐目标的 GUID
            time_t _firstConstructKill;    ///< 第一个构造体被击杀的时间
            bool _shattered;               ///< Shattered 成就标志（5 秒内击杀 2 个构造体）

        };

        /**
         * @brief 获取 AI 实例
         * @param creature 生物对象
         * @return AI 实例指针
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetUlduarAI<boss_ignis_AI>(creature);
        }
};

/**
 * @class npc_iron_construct
 * @brief 铁构造体脚本类
 *
 * 实现铁构造体的 AI 逻辑，包括加热、熔化和碎裂机制
 */
class npc_iron_construct : public CreatureScript
{
    public:
        /**
         * @brief 构造函数
         */
        npc_iron_construct() : CreatureScript("npc_iron_construct") { }

        /**
         * @struct npc_iron_constructAI
         * @brief 铁构造体 AI 结构
         *
         * 实现构造体的状态转换：加热 -> 熔化 -> 易碎 -> 碎裂
         */
        struct npc_iron_constructAI : public ScriptedAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物对象指针
             */
            npc_iron_constructAI(Creature* creature) : ScriptedAI(creature), _instance(creature->GetInstanceScript())
            {
                Initialize();
                creature->SetReactState(REACT_PASSIVE);  // 初始为被动状态
            }

            /**
             * @brief 初始化成员变量
             */
            void Initialize()
            {
                _brittled = false;  // 是否处于易碎状态
            }

            /**
             * @brief 重置
             */
            void Reset() override
            {
                Initialize();
            }

            /**
             * @brief 受到伤害处理
             * @param attacker 攻击者（未使用）
             * @param damage 伤害值（引用）
             * @param damageType 伤害类型（未使用）
             * @param spellInfo 法术信息（未使用）
             *
             * 当构造体处于易碎状态且受到足够伤害时，触发碎裂
             */
            void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
            {
                // 检查是否处于易碎状态且受到至少 5000 点伤害
                if (me->HasAura(RAID_MODE(SPELL_BRITTLE, SPELL_BRITTLE_25)) && damage >= 5000)
                {
                    DoCast(SPELL_SHATTER);  // 施放碎裂效果

                    // 通知伊格尼斯移除力量增益
                    if (Creature* ignis = _instance->GetCreature(DATA_IGNIS))
                        if (ignis->AI())
                            ignis->AI()->DoAction(ACTION_REMOVE_BUFF);

                    me->DespawnOrUnsummon(1s);  // 1 秒后消失
                }
            }

            /**
             * @brief 更新 AI
             * @param uiDiff 时间差（毫秒）
             *
             * 处理构造体的状态转换逻辑
             */
            void UpdateAI(uint32 /*uiDiff*/) override
            {
                if (!UpdateVictim())
                    return;

                // 检查加热层数，达到 10 层时变为熔化状态
                if (Aura* aur = me->GetAura(SPELL_HEAT))
                {
                    if (aur->GetStackAmount() >= 10)
                    {
                        me->RemoveAura(SPELL_HEAT);
                        DoCast(SPELL_MOLTEN);  // 变为熔化状态
                        _brittled = false;
                    }
                }

                // 水池区域：熔化状态的构造体进入水中变为易碎状态
                if (me->IsInWater() && !_brittled && me->HasAura(SPELL_MOLTEN))
                {
                    DoCast(SPELL_BRITTLE);  // 变为易碎状态
                    me->RemoveAura(SPELL_MOLTEN);
                    _brittled = true;
                }

                DoMeleeAttackIfReady();
            }

        private:
            InstanceScript* _instance;
            bool _brittled;
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetUlduarAI<npc_iron_constructAI>(creature);
        }
};

/**
 * @class npc_scorch_ground
 * @brief 地面灼烧脚本类
 *
 * 实现地面灼烧区域的 AI，对构造体进行加热
 */
class npc_scorch_ground : public CreatureScript
{
    public:
        npc_scorch_ground() : CreatureScript("npc_scorch_ground") { }

        struct npc_scorch_groundAI : public ScriptedAI
        {
            npc_scorch_groundAI(Creature* creature) : ScriptedAI(creature)
            {
                Initialize();
                me->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE | UNIT_FLAG_PACIFIED);
                me->SetControlled(true, UNIT_STATE_ROOT);
                creature->SetDisplayId(16925); //model 2 in db cannot overwrite wdb fields
            }

            void Initialize()
            {
                _heat = false;
                _constructGUID.Clear();
                _heatTimer = 0;
            }

            void MoveInLineOfSight(Unit* who) override
            {
                if (!_heat)
                {
                    if (who->GetEntry() == NPC_IRON_CONSTRUCT)
                    {
                        if (!who->HasAura(SPELL_HEAT) || !who->HasAura(SPELL_MOLTEN))
                        {
                            _constructGUID = who->GetGUID();
                            _heat = true;
                        }
                    }
                }
            }

            void Reset() override
            {
                Initialize();
                DoCast(me, SPELL_GROUND);
            }

            /**
             * @brief 更新 AI
             * @param uiDiff 时间差（毫秒）
             *
             * 每秒为构造体添加一层加热效果
             */
            void UpdateAI(uint32 uiDiff) override
            {
                if (_heat)
                {
                    if (_heatTimer <= uiDiff)
                    {
                        Creature* construct = ObjectAccessor::GetCreature(*me, _constructGUID);
                        // 只为未熔化的构造体添加加热效果
                        if (construct && !construct->HasAura(SPELL_MOLTEN))
                        {
                            me->AddAura(SPELL_HEAT, construct);
                            _heatTimer = 1000;  // 每秒添加一层
                        }
                    }
                    else
                        _heatTimer -= uiDiff;
                }
            }

        private:
            ObjectGuid _constructGUID;  ///< 构造体 GUID
            uint32 _heatTimer;          ///< 加热计时器
            bool _heat;                 ///< 是否正在加热
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetUlduarAI<npc_scorch_groundAI>(creature);
        }
};

/**
 * @class spell_ignis_slag_pot
 * @brief 炉渣罐法术脚本类
 *
 * 实现炉渣罐的周期性伤害和移除后的增益效果
 */
class spell_ignis_slag_pot : public SpellScriptLoader
{
    public:
        spell_ignis_slag_pot() : SpellScriptLoader("spell_ignis_slag_pot") { }

        class spell_ignis_slag_pot_AuraScript : public AuraScript
        {
            PrepareAuraScript(spell_ignis_slag_pot_AuraScript);

            bool Validate(SpellInfo const* /*spellInfo*/) override
            {
                return ValidateSpellInfo({ SPELL_SLAG_POT_DAMAGE, SPELL_SLAG_IMBUED });
            }

            /**
             * @brief 周期性效果处理
             * @param aurEff 光环效果
             *
             * 每秒对目标造成炉渣罐伤害
             */
            void HandleEffectPeriodic(AuraEffect const* /*aurEff*/)
            {
                if (Unit* caster = GetCaster())
                {
                    Unit* target = GetTarget();
                    caster->CastSpell(target, SPELL_SLAG_POT_DAMAGE, true);
                }
            }

            /**
             * @brief 移除效果处理
             * @param aurEff 光环效果
             * @param mode 处理模式
             *
             * 如果目标存活，给予炉渣灌注增益
             */
            void OnRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
            {
                if (GetTarget()->IsAlive())
                    GetTarget()->CastSpell(GetTarget(), SPELL_SLAG_IMBUED, true);
            }

            void Register() override
            {
                OnEffectPeriodic += AuraEffectPeriodicFn(spell_ignis_slag_pot_AuraScript::HandleEffectPeriodic, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY);
                AfterEffectRemove += AuraEffectRemoveFn(spell_ignis_slag_pot_AuraScript::OnRemove, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY, AURA_EFFECT_HANDLE_REAL);
            }
        };

        AuraScript* GetAuraScript() const override
        {
            return new spell_ignis_slag_pot_AuraScript();
        }
};

/**
 * @class achievement_ignis_shattered
 * @brief Shattered 成就脚本类
 *
 * 检查是否完成 Shattered 成就（5 秒内击杀 2 个构造体）
 */
class achievement_ignis_shattered : public AchievementCriteriaScript
{
    public:
        achievement_ignis_shattered() : AchievementCriteriaScript("achievement_ignis_shattered") { }

        /**
         * @brief 检查成就条件
         * @param source 玩家（未使用）
         * @param target 目标单位（Boss）
         * @return 是否满足成就条件
         */
        bool OnCheck(Player* /*source*/, Unit* target) override
        {
            if (UnitAI* ai = target ? target->GetAI() : nullptr)
                return ai->GetData(DATA_SHATTERED) != 0;

            return false;
        }
};

/**
 * @brief 注册脚本
 *
 * 注册所有伊格尼斯相关的脚本
 */
void AddSC_boss_ignis()
{
    new boss_ignis();
    new npc_iron_construct();
    new npc_scorch_ground();
    new spell_ignis_slag_pot();
    new achievement_ignis_shattered();
}
