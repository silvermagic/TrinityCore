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
 * @file boss_anubarak_trial.cpp
 * @brief 十字军试炼副本 - 阿努巴拉克（Anub'arak）BOSS 战斗脚本
 *
 * 本模块实现了阿努巴拉克 BOSS 战斗的核心逻辑，包括：
 * - 阿努巴拉克 BOSS AI 及其技能系统
 * - 地下阶段和地面阶段的切换机制
 * - 追击尖刺（Pursuing Spike）机制
 * - 霜冻之球（Frost Sphere）和永冻土（Permafrost）系统
 * - 虫群甲虫（Scarab）和尼鲁布潜地者（Nerubian Burrower）小怪
 * - 吸血虫群（Leeching Swarm）P3 阶段机制
 *
 * 已知问题：
 * - 阿努巴拉克的地下阶段部分功能未正常工作
 * - 穿刺命中永冻土后的传送功能未正常工作（整个传送法术需要改进）
 * - 甲虫击杀积分未正确计入
 *
 * @author TrinityCore Team
 */

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ScriptedCreature.h"
#include "SpellAuraEffects.h"
#include "SpellMgr.h"
#include "SpellScript.h"
#include "TemporarySummon.h"
#include "trial_of_the_crusader.h"

/**
 * @enum Yells
 * @brief 阿努巴拉克的台词和表情枚举
 */
enum Yells
{
    SAY_INTRO               = 0,    ///< 开场白
    SAY_AGGRO               = 1,    ///< 开怪台词
    EMOTE_SUBMERGE          = 2,    ///< 下潜表情
    EMOTE_BURROWER          = 3,    ///< 潜地者表情
    EMOTE_EMERGE            = 4,    ///< 出现表情
    SAY_LEECHING_SWARM      = 5,    ///< 吸血虫群台词
    EMOTE_LEECHING_SWARM    = 6,    ///< 吸血虫群表情
    SAY_KILL_PLAYER         = 7,    ///< 击杀玩家台词
    SAY_DEATH               = 8,    ///< 死亡台词

    EMOTE_SPIKE             = 0     ///< 尖刺追击表情
};

/**
 * @enum Summons
 * @brief 召唤生物 NPC ID 枚举
 */
enum Summons
{
    NPC_FROST_SPHERE     = 34606,   ///< 霜冻之球
    NPC_BURROW           = 34862,   ///< 地洞（用于召唤甲虫）
    NPC_BURROWER         = 34607,   ///< 尼鲁布潜地者
    NPC_SCARAB           = 34605,   ///< 虫群甲虫
    NPC_SPIKE            = 34660    ///< 追击尖刺
};

/**
 * @enum BossSpells
 * @brief 阿努巴拉克战斗中的法术 ID 枚举
 *
 * 包含阿努巴拉克本身、召唤物和环境机制所使用的所有法术
 */
enum BossSpells
{
    // 阿努巴拉克主要技能
    SPELL_FREEZE_SLASH          = 66012,   ///< 冻结斩：对主坦造成伤害并降低移动速度
    SPELL_PENETRATING_COLD      = 66013,   ///< 刺骨之寒：对多个目标造成冰霜伤害和 DOT
    SPELL_LEECHING_SWARM        = 66118,   ///< 吸血虫群：P3 阶段全团吸血技能
    SPELL_LEECHING_SWARM_HEAL   = 66125,   ///< 吸血虫群治疗效果：为 BOSS 恢复生命值
    SPELL_LEECHING_SWARM_DMG    = 66240,   ///< 吸血虫群伤害效果：对玩家造成伤害
    SPELL_MARK                  = 67574,   ///< 标记：标记被尖刺追击的玩家
    SPELL_SPIKE_CALL            = 66169,   ///< 召唤尖刺：召唤追击尖刺
    SPELL_SUBMERGE_ANUBARAK     = 65981,   ///< 下潜：阿努巴拉克进入地下
    SPELL_CLEAR_ALL_DEBUFFS     = 34098,   ///< 清除所有减益效果：下潜时清除 BOSS 身上的 debuff
    SPELL_EMERGE_ANUBARAK       = 65982,   ///< 出现：阿努巴拉克从地下出现
    SPELL_SUMMON_BEATLES        = 66339,   ///< 召唤甲虫：召唤虫群甲虫
    SPELL_SUMMON_BURROWER       = 66332,   ///< 召唤潜地者：召唤尼鲁布潜地者

    // 地洞（Burrow）相关
    SPELL_CHURNING_GROUND       = 66969,   ///< 翻腾之地：地洞周围的视觉效果

    // 甲虫（Scarab）相关
    SPELL_DETERMINATION         = 66092,   ///< 决心：甲虫的增益 buff，提升伤害
    SPELL_ACID_MANDIBLE         = 65774,   ///< 酸性大颚：被动触发，造成自然伤害

    // 潜地者（Burrower）相关
    SPELL_SPIDER_FRENZY         = 66128,   ///< 蛛形狂乱：提升攻击速度
    SPELL_EXPOSE_WEAKNESS       = 67720,   ///< 暴露弱点：被动触发，增加受到的伤害
    SPELL_SHADOW_STRIKE         = 66134,   ///< 暗影打击：瞬移到目标身后造成高额伤害（英雄模式）
    SPELL_SUBMERGE_EFFECT       = 68394,   ///< 下潜效果：潜地者进入地下的视觉效果
    SPELL_AWAKENED              = 66311,   ///< 唤醒：潜地者出现的增益效果
    SPELL_EMERGE_EFFECT         = 65982,   ///< 出现效果：潜地者从地下出现的视觉效果

    SPELL_PERSISTENT_DIRT       = 68048,   ///< 持久尘土：潜地者下潜时的地面效果

    SUMMON_SCARAB               = NPC_SCARAB,           ///< 召唤甲虫
    SUMMON_FROSTSPHERE          = NPC_FROST_SPHERE,     ///< 召唤霜冻之球
    SPELL_BERSERK               = 26662,   ///< 狂暴：10 分钟狂暴计时器

    // 霜冻之球（Frost Sphere）相关
    SPELL_FROST_SPHERE          = 67539,   ///< 霜冻之球：球体的浮空效果
    SPELL_PERMAFROST            = 66193,   ///< 永冻土：球体落地后形成的冰面
    SPELL_PERMAFROST_VISUAL     = 65882,   ///< 永冻土视觉：冰面的视觉效果
    SPELL_PERMAFROST_MODEL      = 66185,   ///< 永冻土模型：改变球体外观为冰面

    // 尖刺（Spike）相关
    SPELL_SUMMON_SPIKE          = 66169,   ///< 召唤尖刺
    SPELL_SPIKE_SPEED1          = 65920,   ///< 尖刺速度 1：初始速度
    SPELL_SPIKE_TRAIL           = 65921,   ///< 尖刺轨迹：移动轨迹视觉效果
    SPELL_SPIKE_SPEED2          = 65922,   ///< 尖刺速度 2：中等速度
    SPELL_SPIKE_SPEED3          = 65923,   ///< 尖刺速度 3：最高速度
    SPELL_SPIKE_FAIL            = 66181,   ///< 尖刺失败：尖刺撞击永冻土后的眩晕效果
    SPELL_SPIKE_TELE            = 66170    ///< 尖刺传送：尖刺撞击后的传送效果
};

/**
 * @brief 永冻土法术 ID 辅助宏
 *
 * 根据团队规模返回对应的永冻土法术 ID
 * 参数分别为：10人普通、25人普通、10人英雄、25人英雄
 */
#define SPELL_PERMAFROST_HELPER RAID_MODE<uint32>(66193, 67855, 67856, 67857)

/**
 * @enum SummonActions
 * @brief 召唤物动作枚举
 *
 * 用于与召唤物 AI 通信的动作类型
 */
enum SummonActions
{
    ACTION_SHADOW_STRIKE    = 0,    ///< 执行暗影打击（潜地者使用）
    ACTION_SCARAB_SUBMERGE  = 1     ///< 甲虫下潜消失
};

/**
 * @brief 霜冻之球刷新位置数组
 *
 * 定义了 6 个霜冻之球的初始刷新位置
 * 这些位置均匀分布在战斗区域周围
 */
const Position SphereSpawn[6] =
{
    {779.8038f, 150.6580f, 158.1426f, 0},   ///< 位置 1：东北方向
    {736.0243f, 113.4201f, 158.0226f, 0},   ///< 位置 2：西南方向
    {712.5712f, 160.9948f, 158.4368f, 0},   ///< 位置 3：西北方向
    {701.4271f, 126.4740f, 158.0205f, 0},   ///< 位置 4：西侧
    {747.9202f, 155.0920f, 158.0613f, 0},   ///< 位置 5：中央偏北
    {769.6285f, 121.1024f, 158.0504f, 0},   ///< 位置 6：东南方向
};

/**
 * @enum MovementPoints
 * @brief 移动路径点枚举
 *
 * 用于标识生物的特定移动目标点
 */
enum MovementPoints
{
    POINT_FALL_GROUND           = 1    ///< 落地路径点：霜冻之球下落到地面
};

/**
 * @enum PursuingSpikesPhases
 * @brief 追击尖刺的阶段枚举
 *
 * 定义追击尖刺的不同速度阶段
 * 尖刺会逐渐加速，直到追上目标或撞击永冻土
 */
enum PursuingSpikesPhases
{
    PHASE_NO_MOVEMENT       = 0,    ///< 无移动阶段：尖刺被永冻土阻挡后的短暂停顿
    PHASE_IMPALE_NORMAL     = 1,    ///< 普通穿刺阶段：初始追击速度
    PHASE_IMPALE_MIDDLE     = 2,    ///< 中速穿刺阶段：速度提升一档
    PHASE_IMPALE_FAST       = 3     ///< 高速穿刺阶段：最高追击速度
};

/**
 * @enum Events
 * @brief 事件枚举
 *
 * 用于管理阿努巴拉克的技能冷却和时间调度
 */
enum Events
{
    // Anub'arak 事件
    EVENT_FREEZE_SLASH              = 1,    ///< 冻结斩事件：每 15 秒触发
    EVENT_PENETRATING_COLD          = 2,    ///< 刺骨之寒事件：每 20 秒触发
    EVENT_SUMMON_NERUBIAN           = 3,    ///< 召唤尼鲁布潜地者事件：每 45 秒触发
    EVENT_NERUBIAN_SHADOW_STRIKE    = 4,    ///< 潜地者暗影打击事件：每 30 秒触发（英雄模式）
    EVENT_SUBMERGE                  = 5,    ///< 下潜事件：每 80 秒触发
    EVENT_EMERGE                    = 6,    ///< 出现事件：下潜 60 秒后触发
    EVENT_PURSUING_SPIKE            = 7,    ///< 召唤追击尖刺事件：下潜后 2 秒触发
    EVENT_SUMMON_SCARAB             = 8,    ///< 召唤甲虫事件：下潜期间每 4 秒触发
    EVENT_SUMMON_FROST_SPHERE       = 9,    ///< 召唤霜冻之球事件：每 20-30 秒触发（非英雄模式）
    EVENT_BERSERK                   = 10    ///< 狂暴事件：10 分钟后触发
};

/**
 * @enum Phases
 * @brief 阿努巴拉克的战斗阶段枚举
 *
 * 定义 BOSS 的主要战斗阶段，用于事件调度系统
 */
enum Phases
{
    // Anub'arak 阶段
    PHASE_MELEE                 = 1,    ///< 近战阶段：BOSS 在地面战斗
    PHASE_SUBMERGED             = 2     ///< 下潜阶段：BOSS 在地下，召唤小怪和尖刺
};

/**
 * @struct boss_anubarak_trial
 * @brief 阿努巴拉克 BOSS AI
 *
 * 实现阿努巴拉克的核心战斗逻辑，包括：
 * - 地面阶段：冻结斩、刺骨之寒、召唤潜地者
 * - 地下阶段：追击尖刺、召唤甲虫
 * - P3 阶段：吸血虫群（30% 血量触发）
 *
 * 战斗流程：
 * 1. 地面阶段（100% - 30% 血量）：每 80 秒进入地下
 * 2. 地下阶段（60 秒）：召唤尖刺和甲虫
 * 3. P3 阶段（30% 以下）：释放吸血虫群，不再下潜
 */
struct boss_anubarak_trial : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_anubarak_trial(Creature* creature) : BossAI(creature, DATA_ANUBARAK)
    {
        Initialize();
    }

    /**
     * @brief 初始化成员变量
     *
     * 重置状态标志到初始值
     */
    void Initialize()
    {
        _intro = true;              ///< 是否首次进入视野（用于触发开场白）
        _reachedPhase3 = false;     ///< 是否已进入 P3 阶段（吸血虫群）
    }

    /**
     * @brief 重置 BOSS 状态
     *
     * 当 BOSS 重置或脱离战斗时调用，负责：
     * - 重置事件调度器
     * - 清理召唤物（霜冻之球）
     * - 移除不可攻击标志
     * - 清空地洞 GUID 列表
     *
     * @note 性能注意事项：会遍历 150 码范围内的所有霜冻之球进行清理
     */
    void Reset() override
    {
        _Reset();
        events.SetPhase(PHASE_MELEE);
        // 调度地面阶段的技能事件
        events.ScheduleEvent(EVENT_FREEZE_SLASH, 15s, 0, PHASE_MELEE);
        events.ScheduleEvent(EVENT_PENETRATING_COLD, 20s, PHASE_MELEE);
        events.ScheduleEvent(EVENT_SUMMON_NERUBIAN, 10s, 0, PHASE_MELEE);
        events.ScheduleEvent(EVENT_SUBMERGE, 80s, 0, PHASE_MELEE);
        events.ScheduleEvent(EVENT_BERSERK, 10min);

        // 英雄模式：调度暗影打击事件
        if (IsHeroic())
            events.ScheduleEvent(EVENT_NERUBIAN_SHADOW_STRIKE, 30s, 0, PHASE_MELEE);

        // 非英雄模式：定期刷新霜冻之球
        if (!IsHeroic())
            events.ScheduleEvent(EVENT_SUMMON_FROST_SPHERE, 20s);

        Initialize();
        me->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_UNINTERACTIBLE);

        // 清理已刷新的霜冻之球
        std::list<Creature*> FrostSphereList;
        me->GetCreatureListWithEntryInGrid(FrostSphereList, NPC_FROST_SPHERE, 150.0f);
        if (!FrostSphereList.empty())
            for (std::list<Creature*>::iterator itr = FrostSphereList.begin(); itr != FrostSphereList.end(); ++itr)
                (*itr)->DespawnOrUnsummon();

        _burrowGUID.clear();
    }

    /**
     * @brief 击杀单位事件
     * @param who 被击杀的单位
     *
     * 当 BOSS 击杀玩家时播放击杀台词
     */
    void KilledUnit(Unit* who) override
    {
        if (who->GetTypeId() == TYPEID_PLAYER)
            Talk(SAY_KILL_PLAYER);
    }

    /**
     * @brief 进入视野事件
     * @param who 进入视野的单位（未使用）
     *
     * 首次有单位进入视野时播放开场白
     * 仅触发一次
     */
    void MoveInLineOfSight(Unit* /*who*/) override
    {
        if (!_intro)
        {
            Talk(SAY_INTRO);
            _intro = false;
        }
    }

    /**
     * @brief 返回出生点事件
     *
     * 当 BOSS 重置返回出生点时调用，执行以下操作：
     * - 设置 BOSS 状态为失败
     * - 召唤 10 只中立甲虫在周围随机移动
     *
     * @note 这些甲虫属于中立阵营（FACTION_PREY），不会主动攻击
     */
    void JustReachedHome() override
    {
        instance->SetBossState(DATA_ANUBARAK, FAIL);
        // 在随机位置召唤中立的虫群甲虫
        for (int i = 0; i < 10; i++)
            if (Creature* scarab = me->SummonCreature(NPC_SCARAB, AnubarakLoc[1].GetPositionX()+urand(0, 50)-25, AnubarakLoc[1].GetPositionY()+urand(0, 50)-25, AnubarakLoc[1].GetPositionZ()))
            {
                scarab->SetFaction(FACTION_PREY);
                scarab->GetMotionMaster()->MoveRandom(10);
            }
    }

    /**
     * @brief 死亡事件
     * @param killer 击杀者（未使用）
     *
     * BOSS 死亡时调用，执行以下操作：
     * - 调用基类的 _JustDied() 方法
     * - 播放死亡台词
     * - 清理所有霜冻之球和尼鲁布潜地者
     */
    void JustDied(Unit* /*killer*/) override
    {
        _JustDied();
        Talk(SAY_DEATH);

        // 死亡时消失霜冻之球和潜地者
        std::list<Creature*> AddList;
        me->GetCreatureListWithEntryInGrid(AddList, NPC_FROST_SPHERE, 150.0f);
        me->GetCreatureListWithEntryInGrid(AddList, NPC_BURROWER, 150.0f);
        if (!AddList.empty())
            for (std::list<Creature*>::iterator itr = AddList.begin(); itr != AddList.end(); ++itr)
                (*itr)->DespawnOrUnsummon();
    }

    /**
     * @brief 召唤生物事件
     * @param summoned 被召唤的生物
     *
     * 处理 BOSS 召唤的生物初始化：
     * - 地洞（Burrow）：设为被动状态，施放翻腾之地效果，隐藏模型
     * - 追击尖刺（Spike）：选择随机玩家目标并开始追击
     *
     * @note 所有召唤物都会被加入 summons 列表进行统一管理
     */
    void JustSummoned(Creature* summoned) override
    {
        switch (summoned->GetEntry())
        {
            case NPC_BURROW:
                // 保存地洞 GUID 用于后续召唤甲虫
                _burrowGUID.push_back(summoned->GetGUID());
                summoned->SetReactState(REACT_PASSIVE);
                summoned->CastSpell(summoned, SPELL_CHURNING_GROUND, false);
                summoned->SetDisplayId(summoned->GetCreatureTemplate()->Modelid2);
                break;
            case NPC_SPIKE:
                // 尖刺设置可见模型并选择随机目标
                summoned->SetDisplayId(summoned->GetCreatureTemplate()->Modelid1);
                if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 0.0f, true))
                {
                    summoned->EngageWithTarget(target);
                    Talk(EMOTE_SPIKE, target);
                }
                break;
            default:
                break;
        }
        summons.Summon(summoned);
    }

    /**
     * @brief 进入战斗事件
     * @param who 仇恨目标
     *
     * 开怪时调用，执行以下操作：
     * - 调用基类的开怪逻辑
     * - 播放开怪台词
     * - 移除不可攻击标志
     * - 让中立的甲虫消失
     * - 召唤 4 个地洞（用于地下阶段召唤甲虫）
     * - 召唤 6 个霜冻之球（初始数量）
     *
     * @note 霜冻之球的 GUID 会被保存以便后续管理和补充
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        Talk(SAY_AGGRO);
        me->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_UNINTERACTIBLE);

        // 让中立的甲虫消失（战斗开始前的装饰性甲虫）
        EntryCheckPredicate pred(NPC_SCARAB);
        summons.DoAction(ACTION_SCARAB_SUBMERGE, pred);

        // 召唤 4 个地洞
        for (int i = 0; i < 4; i++)
            me->SummonCreature(NPC_BURROW, AnubarakLoc[i + 2]);

        // 开怪时召唤 6 个霜冻之球
        for (int i = 0; i < 6; i++)
            if (Unit* summoned = me->SummonCreature(NPC_FROST_SPHERE, SphereSpawn[i]))
                _sphereGUID[i] = summoned->GetGUID();
    }

    /**
     * @brief 更新 AI 逻辑
     * @param diff 距离上次更新的时间差（毫秒）
     *
     * 核心 AI 更新函数，每帧调用一次，负责：
     * - 更新事件调度器
     * - 处理各种技能事件
     * - 管理 BOSS 阶段转换
     * - 触发 P3 阶段（吸血虫群）
     *
     * 技能执行顺序（地面阶段）：
     * 1. 冻结斩：对主坦施放，降低移动速度
     * 2. 刺骨之寒：对多个目标施放冰霜 DOT
     * 3. 召唤尼鲁布潜地者：根据团队规模召唤不同数量
     * 4. 下潜：进入地下阶段
     *
     * 地下阶段：
     * 1. 召唤追击尖刺：追击随机玩家
     * 2. 召唤甲虫：从随机地洞召唤
     * 3. 60 秒后重新出现
     *
     * @note 性能注意事项：避免在施法状态下执行过多逻辑
     */
    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        events.Update(diff);

        // 如果正在施法，跳过事件处理
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        // 执行所有到期的事件
        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_FREEZE_SLASH:
                    // 冻结斩：对主坦施放
                    DoCastVictim(SPELL_FREEZE_SLASH);
                    events.ScheduleEvent(EVENT_FREEZE_SLASH, 15s, 0, PHASE_MELEE);
                    return;
                case EVENT_PENETRATING_COLD:
                {
                    // 刺骨之寒：对多个目标施放
                    CastSpellExtraArgs args;
                    args.AddSpellMod(SPELLVALUE_MAX_TARGETS, RAID_MODE(2, 5, 2, 5));
                    me->CastSpell(nullptr, SPELL_PENETRATING_COLD, args);
                    events.ScheduleEvent(EVENT_PENETRATING_COLD, 20s, 0, PHASE_MELEE);
                    return;
                }
                case EVENT_SUMMON_NERUBIAN:
                    // 召唤尼鲁布潜地者
                    // P3 阶段后非英雄模式不再召唤
                    if (IsHeroic() || !_reachedPhase3)
                    {
                        CastSpellExtraArgs args;
                        args.AddSpellMod(SPELLVALUE_MAX_TARGETS, RAID_MODE(1, 2, 2, 4));
                        me->CastSpell(nullptr, SPELL_SUMMON_BURROWER, args);
                    }
                    events.ScheduleEvent(EVENT_SUMMON_NERUBIAN, 45s, 0, PHASE_MELEE);
                    return;
                case EVENT_NERUBIAN_SHADOW_STRIKE:
                {
                    // 英雄模式专属：让所有潜地者执行暗影打击
                    EntryCheckPredicate pred(NPC_BURROWER);
                    summons.DoAction(ACTION_SHADOW_STRIKE, pred);
                    events.ScheduleEvent(EVENT_NERUBIAN_SHADOW_STRIKE, 30s, 0, PHASE_MELEE);
                    break;
                }
                case EVENT_SUBMERGE:
                    // 下潜事件：进入地下阶段
                    // P3 阶段或狂暴后不再下潜
                    if (!_reachedPhase3 && !me->HasAura(SPELL_BERSERK))
                    {
                        DoCast(me, SPELL_SUBMERGE_ANUBARAK);
                        DoCast(me, SPELL_CLEAR_ALL_DEBUFFS);
                        me->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_UNINTERACTIBLE);
                        Talk(EMOTE_BURROWER);
                        events.SetPhase(PHASE_SUBMERGED);
                        // 调度地下阶段事件
                        events.ScheduleEvent(EVENT_PURSUING_SPIKE, 2s, 0, PHASE_SUBMERGED);
                        events.ScheduleEvent(EVENT_SUMMON_SCARAB, 4s, 0, PHASE_SUBMERGED);
                        events.ScheduleEvent(EVENT_EMERGE, 60s, 0, PHASE_SUBMERGED);
                    }
                    break;
                case EVENT_PURSUING_SPIKE:
                    // 召唤追击尖刺
                    DoCast(SPELL_SPIKE_CALL);
                    break;
                case EVENT_SUMMON_SCARAB:
                {
                    /* 临时解决方案
                     * - 正确实现应该使用 SPELL_SUMMON_BEATLES 法术，但需要更多法术数据
                     * - 当前实现：从随机地洞召唤甲虫
                     */
                    GuidList::iterator i = _burrowGUID.begin();
                    uint32 at = urand(0, _burrowGUID.size()-1);
                    for (uint32 k = 0; k < at; k++)
                        ++i;
                    if (Creature* pBurrow = ObjectAccessor::GetCreature(*me, *i))
                        pBurrow->CastSpell(pBurrow, 66340, false);

                    events.ScheduleEvent(EVENT_SUMMON_SCARAB, 4s, 0, PHASE_SUBMERGED);

                    /* 这个法术可能有更多机制需要考虑
                     * 需要更多抓包数据
                     * DoCast(SPELL_SUMMON_BEATLES);
                     * // 确保在这个阶段不会再次发生
                     * m_uiSummonScarabTimer = 90*IN_MILLISECONDS;
                     */
                    break;
                }
                case EVENT_EMERGE:
                    // 出现事件：从地下重新出现
                    DoCast(SPELL_SPIKE_TELE);
                    summons.DespawnEntry(NPC_SPIKE);  // 清理所有尖刺
                    me->RemoveAurasDueToSpell(SPELL_SUBMERGE_ANUBARAK);
                    me->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_UNINTERACTIBLE);
                    DoCast(me, SPELL_EMERGE_ANUBARAK);
                    Talk(EMOTE_EMERGE);
                    events.SetPhase(PHASE_MELEE);
                    // 重新调度地面阶段事件
                    events.ScheduleEvent(EVENT_FREEZE_SLASH, 15s, 0, PHASE_MELEE);
                    events.ScheduleEvent(EVENT_PENETRATING_COLD, 20s, PHASE_MELEE);
                    events.ScheduleEvent(EVENT_SUMMON_NERUBIAN, 10s, 0, PHASE_MELEE);
                    events.ScheduleEvent(EVENT_SUBMERGE, 80s, 0, PHASE_MELEE);
                    if (IsHeroic())
                        events.ScheduleEvent(EVENT_NERUBIAN_SHADOW_STRIKE, 30s, 0, PHASE_MELEE);
                    return;
                case EVENT_SUMMON_FROST_SPHERE:
                {
                    // 召唤霜冻之球（非英雄模式专属）
                    // 寻找一个已被摧毁的霜冻之球位置并重新召唤
                    uint8 startAt = urand(0, 5);
                    uint8 i = startAt;
                    do
                    {
                        if (Unit* pSphere = ObjectAccessor::GetCreature(*me, _sphereGUID[i]))
                        {
                            if (!pSphere->HasAura(SPELL_FROST_SPHERE))
                            {
                                if (Creature* summon = me->SummonCreature(NPC_FROST_SPHERE, SphereSpawn[i]))
                                    _sphereGUID[i] = summon->GetGUID();
                                break;
                            }
                        }
                        i = (i + 1) % 6;
                    }
                    while
                        (i != startAt);
                    events.ScheduleEvent(EVENT_SUMMON_FROST_SPHERE, 20s, 30s);
                    break;
                }
                case EVENT_BERSERK:
                    // 狂暴：10 分钟后触发
                    DoCast(me, SPELL_BERSERK);
                    break;
                default:
                    break;
            }

            // 如果正在施法，停止后续事件处理
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        // P3 阶段触发：血量低于 30% 时施放吸血虫群
        if (HealthBelowPct(30) && events.IsInPhase(PHASE_MELEE) && !_reachedPhase3)
        {
            _reachedPhase3 = true;
            DoCastAOE(SPELL_LEECHING_SWARM);
            Talk(EMOTE_LEECHING_SWARM);
            Talk(SAY_LEECHING_SWARM);
        }

        // 地面阶段执行近战攻击
        if (events.IsInPhase(PHASE_MELEE))
            DoMeleeAttackIfReady();
    }

    private:
        GuidList _burrowGUID;           ///< 地洞 GUID 列表，用于地下阶段召唤甲虫
        ObjectGuid _sphereGUID[6];      ///< 霜冻之球 GUID 数组，管理 6 个刷新位置
        bool _intro;                    ///< 是否首次进入视野（触发开场白）
        bool _reachedPhase3;            ///< 是否已进入 P3 阶段（吸血虫群）
};

/**
 * @struct npc_swarm_scarab
 * @brief 虫群甲虫 AI
 *
 * 实现虫群甲虫的战斗逻辑：
 * - 在地下阶段从地洞召唤
 * - 定期施放决心 buff 提升伤害
 * - 死亡时对击杀者施放叛徒之王成就相关法术
 *
 * 甲虫会主动攻击玩家，需要被坦克拉住或被 DPS 击杀
 */
struct npc_swarm_scarab : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_swarm_scarab(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
        _instance = creature->GetInstanceScript();
    }

    /**
     * @brief 初始化成员变量
     *
     * 设置决心计时器为随机时间（5-60秒）
     */
    void Initialize()
    {
        _determinationTimer = urand(5 * IN_MILLISECONDS, 60 * IN_MILLISECONDS);
    }

    /**
     * @brief 重置状态
     *
     * 设置尸体延迟消失时间为 0（立即消失）
     * 施放酸性大颚被动技能
     * 将自身加入 BOSS 的召唤列表
     */
    void Reset() override
    {
        me->SetCorpseDelay(0);
        Initialize();
        DoCast(me, SPELL_ACID_MANDIBLE);
        DoZoneInCombat();
        // 如果进入战斗，将自己加入阿努巴拉克的召唤列表
        if (me->IsInCombat())
            if (Creature* anubarak = _instance->GetCreature(DATA_ANUBARAK))
                anubarak->AI()->JustSummoned(me);
    }

    /**
     * @brief 处理动作命令
     * @param actionId 动作 ID
     *
     * 接收 BOSS 发送的动作命令：
     * - ACTION_SCARAB_SUBMERGE：让甲虫消失（战斗开始前清理中立甲虫）
     */
    void DoAction(int32 actionId) override
    {
        switch (actionId)
        {
            case ACTION_SCARAB_SUBMERGE:
                DoCast(SPELL_SUBMERGE_EFFECT);
                me->DespawnOrUnsummon(1s);
                break;
            default:
                break;
        }
    }

    /**
     * @brief 死亡事件
     * @param killer 击杀者
     *
     * 甲虫死亡时对击杀者施放叛徒之王成就相关法术
     * 这与成就"叛徒之王"相关
     */
    void JustDied(Unit* killer) override
    {
        if (killer)
            DoCast(killer, SPELL_TRAITOR_KING);
    }

    /**
     * @brief 更新 AI 逻辑
     * @param diff 距离上次更新的时间差（毫秒）
     *
     * 定期施放决心 buff，提升自身伤害
     * 如果 BOSS 战斗结束，立即消失
     */
    void UpdateAI(uint32 diff) override
    {
        // 如果 BOSS 战斗未进行，甲虫消失
        if (_instance->GetBossState(DATA_ANUBARAK) != IN_PROGRESS)
            me->DisappearAndDie();

        if (!UpdateVictim())
            return;

        /* Bosskillers 不识别此机制 */
        // 定期施放决心 buff
        if (_determinationTimer <= diff)
        {
            DoCast(me, SPELL_DETERMINATION);
            _determinationTimer = urand(10*IN_MILLISECONDS, 60*IN_MILLISECONDS);
        }
        else
            _determinationTimer -= diff;

        DoMeleeAttackIfReady();
    }

    private:
        InstanceScript* _instance;          ///< 副本脚本实例
        uint32 _determinationTimer;         ///< 决心施放计时器
};

/**
 * @struct npc_nerubian_burrower
 * @brief 尼鲁布潜地者 AI
 *
 * 实现尼鲁布潜地者的战斗逻辑：
 * - 拥有暴露弱点和蛛形狂乱被动技能
 * - 可以潜入地下规避伤害（除非站在永冻土上）
 * - 英雄模式下会执行暗影打击（瞬移到目标身后）
 *
 * 战术要点：
 * - 必须将其拉到永冻土上击杀，否则会下潜回血
 * - 英雄模式下需要及时打断暗影打击
 */
struct npc_nerubian_burrower : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_nerubian_burrower(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
        _instance = creature->GetInstanceScript();
    }

    /**
     * @brief 初始化成员变量
     *
     * 设置下潜计时器为 30 秒
     */
    void Initialize()
    {
        _submergeTimer = 30 * IN_MILLISECONDS;
    }

    /**
     * @brief 重置状态
     *
     * 设置尸体延迟消失时间为 10 秒
     * 施放被动技能：暴露弱点、蛛形狂乱、唤醒
     * 将自身加入 BOSS 的召唤列表
     */
    void Reset() override
    {
        me->SetCorpseDelay(10);
        Initialize();
        DoCast(me, SPELL_EXPOSE_WEAKNESS);
        DoCast(me, SPELL_SPIDER_FRENZY);
        DoCast(me, SPELL_AWAKENED);
        DoZoneInCombat();
        // 如果进入战斗，将自己加入阿努巴拉克的召唤列表
        if (me->IsInCombat())
            if (Creature* anubarak = _instance->GetCreature(DATA_ANUBARAK))
                anubarak->AI()->JustSummoned(me);
    }

    /**
     * @brief 处理动作命令
     * @param actionId 动作 ID
     *
     * 接收 BOSS 发送的动作命令：
     * - ACTION_SHADOW_STRIKE：执行暗影打击（英雄模式专属）
     */
    void DoAction(int32 actionId) override
    {
        switch (actionId)
        {
            case ACTION_SHADOW_STRIKE:
                // 如果不在唤醒状态，执行暗影打击
                if (!me->HasAura(SPELL_AWAKENED))
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                        DoCast(target, SPELL_SHADOW_STRIKE);
                break;
            default:
                break;
        }
    }

    /**
     * @brief 更新 AI 逻辑
     * @param diff 距离上次更新的时间差（毫秒）
     *
     * 管理潜地者的下潜和出现机制：
     * - 血量低于 80% 时可以下潜
     * - 如果站在永冻土上，无法下潜
     * - 下潜后 20 秒会重新出现
     * - 出现时会获得唤醒 buff
     *
     * @note 性能注意事项：下潜机制需要检查永冻土 aura
     */
    void UpdateAI(uint32 diff) override
    {
        // 如果 BOSS 战斗未进行，潜地者消失
        if (_instance->GetBossState(DATA_ANUBARAK) != IN_PROGRESS)
            me->DisappearAndDie();

        // 如果没有目标且不在下潜状态，返回
        if (!UpdateVictim() && !me->HasAura(SPELL_SUBMERGE_EFFECT))
            return;

        // 如果正在施法，跳过
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        // 下潜和出现机制
        if ((_submergeTimer <= diff) && HealthBelowPct(80))
        {
            if (me->HasAura(SPELL_SUBMERGE_EFFECT))
            {
                // 当前在下潜状态，准备出现
                me->RemoveAurasDueToSpell(SPELL_SUBMERGE_EFFECT);
                DoCast(me, SPELL_EMERGE_EFFECT);
                DoCast(me, SPELL_AWAKENED);
                me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
            }
            else
            {
                // 当前在地面上，尝试下潜
                // 如果站在永冻土上，无法下潜
                if (!me->HasAura(SPELL_PERMAFROST_HELPER))
                {
                    DoCast(me, SPELL_SUBMERGE_EFFECT);
                    me->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                    DoCast(me, SPELL_PERSISTENT_DIRT, true);
                }
            }
            _submergeTimer = 20*IN_MILLISECONDS;
        }
        else
            _submergeTimer -= diff;

        DoMeleeAttackIfReady();
    }

    private:
        uint32 _submergeTimer;          ///< 下潜/出现计时器
        EventMap _events;               ///< 事件映射（未使用）
        InstanceScript* _instance;      ///< 副本脚本实例
};

/**
 * @struct npc_frost_sphere
 * @brief 霜冻之球 AI
 *
 * 实现霜冻之球的机制：
 * - 在战斗区域上空随机漂浮
 * - 受到足够伤害后落地形成永冻土
 * - 永冻土可以阻止尖刺追击和潜地者下潜
 *
 * 战术要点：
 * - 玩家需要攻击霜冻之球使其落地
 * - 尖刺撞击永冻土后会眩晕 5 秒
 * - 站在永冻土上的潜地者无法下潜
 */
struct npc_frost_sphere : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_frost_sphere(Creature* creature) : ScriptedAI(creature) { }

    /**
     * @brief 重置状态
     *
     * 设置为被动状态
     * 施放霜冻之球浮空效果
     * 切换为隐藏模型（球体外观）
     * 开始随机移动
     */
    void Reset() override
    {
        me->SetReactState(REACT_PASSIVE);
        DoCast(SPELL_FROST_SPHERE);
        me->SetDisplayId(me->GetCreatureTemplate()->Modelid2);
        me->GetMotionMaster()->MoveRandom(20.0f);
    }

    /**
     * @brief 受到伤害事件
     * @param who 攻击者（未使用）
     * @param damage 受到的伤害（引用，可修改）
     * @param damageType 伤害类型（未使用）
     * @param spellInfo 法术信息（未使用）
     *
     * 当霜冻之球受到的伤害达到或超过当前生命值时：
     * - 如果接近地面：直接形成永冻土
     * - 如果在空中：下落到地面后形成永冻土
     *
     * @note 伤害会被设置为 0，球体不会真正死亡
     */
    void DamageTaken(Unit* /*who*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        if (me->GetHealth() <= damage)
        {
            damage = 0;
            // 计算地面高度
            float floorZ = me->GetPositionZ();
            me->UpdateGroundPositionZ(me->GetPositionX(), me->GetPositionY(), floorZ);
            if (fabs(me->GetPositionZ() - floorZ) < 0.1f)
            {
                // 接近地面：直接形成永冻土
                me->GetMotionMaster()->MoveIdle();
                me->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                me->RemoveAurasDueToSpell(SPELL_FROST_SPHERE);
                DoCast(SPELL_PERMAFROST_MODEL);
                DoCast(SPELL_PERMAFROST);
                me->SetObjectScale(2.0f);
            }
            else
            {
                // 在空中：开始下落
                me->GetMotionMaster()->MoveIdle();
                me->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                // 播放下落死亡动画
                me->HandleEmoteCommand(EMOTE_ONESHOT_FLYDEATH);
                me->GetMotionMaster()->MoveFall(POINT_FALL_GROUND);
            }
        }
    }

    /**
     * @brief 移动完成通知
     * @param type 移动类型
     * @param pointId 路径点 ID
     *
     * 当霜冻之球完成下落动作后，形成永冻土
     */
    void MovementInform(uint32 type, uint32 pointId) override
    {
        if (type != EFFECT_MOTION_TYPE)
            return;

        switch (pointId)
        {
            case POINT_FALL_GROUND:
                // 落地后形成永冻土
                me->RemoveAurasDueToSpell(SPELL_FROST_SPHERE);
                DoCast(SPELL_PERMAFROST_MODEL);
                DoCast(SPELL_PERMAFROST_VISUAL);
                DoCast(SPELL_PERMAFROST);
                me->SetObjectScale(2.0f);
                break;
            default:
                break;
        }
    }
};

/**
 * @struct npc_anubarak_spike
 * @brief 追击尖刺 AI
 *
 * 实现追击尖刺的机制：
 * - 地下阶段从地洞追击随机玩家
 * - 分三个阶段逐渐加速
 * - 撞击永冻土后眩晕 5 秒
 * - 触碰玩家造成高额伤害
 *
 * 尖刺速度机制：
 * - 阶段 1（初始）：慢速追击
 * - 阶段 2（7秒后）：中速追击
 * - 阶段 3（14秒后）：高速追击
 *
 * 战术要点：
 * - 被标记的玩家需要引导尖刺撞击永冻土
 * - 尖刺撞击永冻土后会眩晕 5 秒
 */
struct npc_anubarak_spike : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_anubarak_spike(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
    }

    /**
     * @brief 初始化成员变量
     *
     * 设置初始阶段为无移动，阶段切换计时器为 1ms（立即开始第一阶段）
     */
    void Initialize()
    {
        _phase = PHASE_NO_MOVEMENT;
        _phaseSwitchTimer = 1;
    }

    /**
     * @brief 重置状态
     *
     * 初始化阶段状态
     * 确保尖刺有所有玩家的仇恨
     */
    void Reset() override
    {
        Initialize();
        // 确保尖刺有所有玩家的仇恨列表
        DoZoneInCombat();
    }

    /**
     * @brief 判断是否可以攻击目标
     * @param victim 目标单位
     * @return 只有玩家才能被尖刺攻击
     */
    bool CanAIAttack(Unit const* victim) const override
    {
        return victim->GetTypeId() == TYPEID_PLAYER;
    }

    /**
     * @brief 进入战斗事件
     * @param who 仇恨目标
     *
     * 选择随机玩家并开始追击
     */
    void JustEngagedWith(Unit* who) override
    {
        if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 0.0f, true))
        {
            StartChase(target);
            Talk(EMOTE_SPIKE, who);
        }
    }

    /**
     * @brief 受到伤害事件
     * @param who 攻击者（未使用）
     * @param uiDamage 受到的伤害（引用，会被设置为 0）
     * @param damageType 伤害类型（未使用）
     * @param spellInfo 法术信息（未使用）
     *
     * 尖刺免疫所有伤害
     */
    void DamageTaken(Unit* /*who*/, uint32& uiDamage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        uiDamage = 0;
    }

    /**
     * @brief 更新 AI 逻辑
     * @param diff 距离上次更新的时间差（毫秒）
     *
     * 管理尖刺的速度阶段切换：
     * - 初始等待 1ms 后开始第一阶段
     * - 每隔 7 秒提升一次速度
     * - 达到最高速度后不再切换
     *
     * @note 性能注意事项：阶段切换需要频繁检查计时器
     */
    void UpdateAI(uint32 diff) override
    {
        // 如果没有目标，尖刺消失
        if (!UpdateVictim())
        {
            me->DisappearAndDie();
            return;
        }

        // 阶段切换计时器
        if (_phaseSwitchTimer)
        {
            if (_phaseSwitchTimer <= diff)
            {
                switch (_phase)
                {
                    case PHASE_NO_MOVEMENT:
                        // 开始第一阶段：普通速度
                        DoCast(me, SPELL_SPIKE_SPEED1);
                        DoCast(me, SPELL_SPIKE_TRAIL);
                        _phase = PHASE_IMPALE_NORMAL;
                        // 选择新目标
                        if (Unit* target2 = SelectTarget(SelectTargetMethod::Random, 0, 0.0f, true))
                        {
                            StartChase(target2);
                            Talk(EMOTE_SPIKE, target2);
                        }
                        _phaseSwitchTimer = 7*IN_MILLISECONDS;
                        return;
                    case PHASE_IMPALE_NORMAL:
                        // 切换到第二阶段：中速
                        DoCast(me, SPELL_SPIKE_SPEED2);
                        _phase = PHASE_IMPALE_MIDDLE;
                        _phaseSwitchTimer = 7*IN_MILLISECONDS;
                        return;
                    case PHASE_IMPALE_MIDDLE:
                        // 切换到第三阶段：高速
                        DoCast(me, SPELL_SPIKE_SPEED3);
                        _phase = PHASE_IMPALE_FAST;
                        _phaseSwitchTimer = 0;
                        return;
                    default:
                        return;
                }
            }
            else
                _phaseSwitchTimer -= diff;
        }
    }

    /**
     * @brief 进入视野事件
     * @param pWho 进入视野的单位
     *
     * 检测尖刺是否接近霜冻之球（永冻土）
     * 如果接近，撞击永冻土并眩晕
     */
    void MoveInLineOfSight(Unit* pWho) override
    {
        if (!pWho)
            return;

        // 只检测霜冻之球
        if (pWho->GetEntry() != NPC_FROST_SPHERE)
            return;

        // 如果在无移动阶段，不检测
        if (_phase == PHASE_NO_MOVEMENT)
            return;

        // 如果距离霜冻之球小于 7 码，触发撞击
        if (me->IsWithinDist(pWho, 7.0f))
        {
            // 移除当前速度增益
            switch (_phase)
            {
                case PHASE_IMPALE_NORMAL:
                    me->RemoveAurasDueToSpell(SPELL_SPIKE_SPEED1);
                    break;
                case PHASE_IMPALE_MIDDLE:
                    me->RemoveAurasDueToSpell(SPELL_SPIKE_SPEED2);
                    break;
                case PHASE_IMPALE_FAST:
                    me->RemoveAurasDueToSpell(SPELL_SPIKE_SPEED3);
                    break;
                default:
                    break;
            }

            // 施放失败效果（眩晕）
            me->CastSpell(me, SPELL_SPIKE_FAIL, true);

            // 3 秒后消失霜冻之球
            pWho->ToCreature()->DespawnOrUnsummon(3s);

            // 尖刺撞击永冻土后大约 5 秒无法移动
            _phase = PHASE_NO_MOVEMENT;
            _phaseSwitchTimer = 5*IN_MILLISECONDS;
            SetCombatMovement(false);
            me->GetMotionMaster()->MoveIdle();
            me->GetMotionMaster()->Clear();
        }
    }

    /**
     * @brief 开始追击目标
     * @param who 追击目标
     *
     * 对目标施放标记
     * 设置移动速度为初始值
     * 重置仇恨列表并锁定目标
     */
    void StartChase(Unit* who)
    {
        DoCast(who, SPELL_MARK);
        me->SetSpeedRate(MOVE_RUN, 0.5f);
        // 确保尖刺真正追击目标
        me->GetThreatManager().ResetAllThreat();
        DoZoneInCombat();
        AddThreat(who, 1000000.0f);
        AttackStart(who);
    }

    private:
        uint32 _phaseSwitchTimer;              ///< 阶段切换计时器
        PursuingSpikesPhases _phase;           ///< 当前追击阶段
};

/**
 * @class spell_pursuing_spikes
 * @brief 追击尖刺法术脚本
 *
 * 处理追击尖刺的周期性触发效果：
 * - 法术 ID：65920（速度1）、65922（速度2）、65923（速度3）
 * - 周期性检测尖刺是否接触永冻土
 * - 如果接触永冻土，触发尖刺失败效果并消失
 */
// 65920 - Pursuing Spikes
// 65922 - Pursuing Spikes
// 65923 - Pursuing Spikes
class spell_pursuing_spikes : public AuraScript
{
    PrepareAuraScript(spell_pursuing_spikes);

    /**
     * @brief 验证法术信息
     * @param spellInfo 法术信息（未使用）
     * @return 验证永冻土和尖刺失败法术是否存在
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_PERMAFROST, SPELL_SPIKE_FAIL });
    }

    /**
     * @brief 加载脚本
     * @return 验证当前副本是否为十字军试炼
     */
    bool Load() override
    {
        return InstanceHasScript(GetUnitOwner(), ToCrScriptName);
    }

    /**
     * @brief 周期性触发效果
     * @param aurEff 光环效果（未使用）
     *
     * 检测尖刺目标是否有永冻土 aura
     * 如果有，说明尖刺撞击了永冻土，触发失败效果
     */
    void PeriodicTick(AuraEffect const* /*aurEff*/)
    {
        Unit* permafrostCaster = nullptr;
        if (Aura* permafrostAura = GetTarget()->GetAura(sSpellMgr->GetSpellIdForDifficulty(SPELL_PERMAFROST, GetTarget())))
            permafrostCaster = permafrostAura->GetCaster();

        if (permafrostCaster)
        {
            // 阻止默认触发动作
            PreventDefaultAction();

            // 让永冻土 3 秒后消失
            if (Creature* permafrostCasterCreature = permafrostCaster->ToCreature())
                permafrostCasterCreature->DespawnOrUnsummon(3s);

            // 尖刺施放失败效果并消失
            GetTarget()->CastSpell(nullptr, SPELL_SPIKE_FAIL);
            GetTarget()->RemoveAllAuras();
            if (Creature* targetCreature = GetTarget()->ToCreature())
                targetCreature->DisappearAndDie();
        }
    }

    /**
     * @brief 注册光环效果回调
     */
    void Register() override
    {
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_pursuing_spikes::PeriodicTick, EFFECT_0, SPELL_AURA_PERIODIC_TRIGGER_SPELL);
    }
};

/**
 * @class spell_impale
 * @brief 穿刺法术脚本
 *
 * 处理穿刺法术的伤害计算：
 * - 法术 ID：65919
 * - 检测目标是否站在永冻土上
 * - 如果在永冻土上，穿刺不造成伤害
 *
 * 机制说明：尖刺撞击永冻土后会穿透地下，不会对玩家造成伤害
 */
// 65919 - Impale
class spell_impale : public SpellScript
{
    PrepareSpellScript(spell_impale);

    /**
     * @brief 处理伤害计算
     * @param effIndex 效果索引（未使用）
     *
     * 检测目标是否有永冻土 aura
     * 如果有，阻止伤害生效
     */
    void HandleDamageCalc(SpellEffIndex /*effIndex*/)
    {
        Unit* target = GetHitUnit();
        uint32 permafrost = sSpellMgr->GetSpellIdForDifficulty(SPELL_PERMAFROST, target);

        // 确保穿刺在永冻土上不造成伤害
        if (target && target->HasAura(permafrost))
            PreventHitDamage();
    }

    /**
     * @brief 注册法术效果回调
     */
    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_impale::HandleDamageCalc, EFFECT_0, SPELL_EFFECT_SCHOOL_DAMAGE);
    }
};

/**
 * @class spell_anubarak_leeching_swarm
 * @brief 吸血虫群法术脚本
 *
 * 处理阿努巴拉克 P3 阶段的吸血虫群机制：
 * - 法术 ID：66118（10人）、67630（25人）、68646、68647
 * - 每秒吸取目标当前生命值的百分比
 * - 为 BOSS 恢复等量生命值
 * - 最小吸取量为 250 点
 *
 * 机制说明：这是 P3 阶段的核心机制，团队需要快速 DPS 消耗 BOSS 血量
 */
// 66118, 67630, 68646, 68647 - Leeching Swarm
class spell_anubarak_leeching_swarm : public AuraScript
{
    PrepareAuraScript(spell_anubarak_leeching_swarm);

    /**
     * @brief 验证法术信息
     * @param spell 法术信息（未使用）
     * @return 验证吸血虫群的伤害和治疗效果法术是否存在
     */
    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo({ SPELL_LEECHING_SWARM_DMG, SPELL_LEECHING_SWARM_HEAL });
    }

    /**
     * @brief 处理周期性效果
     * @param aurEff 光环效果
     *
     * 每秒触发一次：
     * - 计算吸取的生命值 = 目标当前生命值 * aura 效果值百分比
     * - 最小吸取量 250 点
     * - 对目标造成伤害
     * - 为 BOSS 恢复等量生命值
     */
    void HandleEffectPeriodic(AuraEffect const* aurEff)
    {
        Unit* caster = GetCaster();
        if (Unit* target = GetTarget())
        {
            // 计算吸取的生命值百分比
            int32 lifeLeeched = target->CountPctFromCurHealth(aurEff->GetAmount());
            // 最小吸取量 250 点
            if (lifeLeeched < 250)
                lifeLeeched = 250;
            // 准备法术参数
            CastSpellExtraArgs args(TRIGGERED_FULL_MASK);
            args.AddSpellMod(SPELLVALUE_BASE_POINT0, lifeLeeched);
            // 对目标造成伤害
            caster->CastSpell(target, SPELL_LEECHING_SWARM_DMG, args);
            // 为 BOSS 恢复生命值
            caster->CastSpell(caster, SPELL_LEECHING_SWARM_HEAL, args);
        }
    }

    /**
     * @brief 注册光环效果回调
     */
    void Register() override
    {
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_anubarak_leeching_swarm::HandleEffectPeriodic, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY);
    }
};

/**
 * @brief 注册阿努巴拉克 BOSS 战斗脚本
 *
 * 注册所有相关 AI 和法术脚本：
 * - 阿努巴拉克 BOSS AI
 * - 虫群甲虫 AI
 * - 尼鲁布潜地者 AI
 * - 追击尖刺 AI
 * - 霜冻之球 AI
 * - 追击尖刺法术脚本
 * - 穿刺法术脚本
 * - 吸血虫群法术脚本
 */
void AddSC_boss_anubarak_trial()
{
    RegisterTrialOfTheCrusaderCreatureAI(boss_anubarak_trial);
    RegisterTrialOfTheCrusaderCreatureAI(npc_swarm_scarab);
    RegisterTrialOfTheCrusaderCreatureAI(npc_nerubian_burrower);
    RegisterTrialOfTheCrusaderCreatureAI(npc_anubarak_spike);
    RegisterTrialOfTheCrusaderCreatureAI(npc_frost_sphere);

    RegisterSpellScript(spell_pursuing_spikes);
    RegisterSpellScript(spell_impale);
    RegisterSpellScript(spell_anubarak_leeching_swarm);
}
