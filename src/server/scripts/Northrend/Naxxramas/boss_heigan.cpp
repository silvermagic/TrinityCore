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
 * @file boss_heigan.cpp
 * @brief 纳克萨玛斯副本 - 海根 Boss 战斗脚本模块
 *
 * 本模块实现了 Boss 海根 (Heigan the Unclean) 的完整战斗逻辑，包括：
 * - 战斗阶段切换（地面战斗阶段和跳舞阶段）
 * - 瘟疫云和疾病技能管理
 * - 房间地面喷发机关控制
 * - 安全跳舞成就判定
 *
 * 海根战斗特点：
 * - Boss 在平台上来回移动，玩家需要在房间区域躲避地面喷发
 * - 战斗分为地面战斗阶段（90秒）和跳舞阶段（45秒）
 * - 跳舞阶段玩家需要按照固定规律在 4 个区域间移动躲避喷发
 * - 如果任何玩家死亡，则"安全跳舞"成就失败
 */

#include "ScriptMgr.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Map.h"
#include "naxxramas.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellScript.h"

/**
 * @brief 海根使用的法术 ID 枚举
 */
enum Spells
{
    SPELL_DECREPIT_FEVER    = 29998,    // 衰败热病 - 10人模式法术ID，25人模式: 55011
    SPELL_SPELL_DISRUPTION  = 29310,    // 法术打乱 - 打断施法
    SPELL_PLAGUE_CLOUD      = 29350,    // 瘟疫云 - 跳舞阶段在平台上的持续伤害
    SPELL_TELEPORT_SELF     = 30211     // 自身传送 - 传送到平台中央
};

/**
 * @brief 海根的台词和表情 ID 枚举
 */
enum Yells
{
    SAY_AGGRO               = 0,        // 开战台词
    SAY_SLAY                = 1,        // 击杀玩家台词
    SAY_TAUNT               = 2,        // 嘲讽台词（跳舞阶段开始）
    SAY_DEATH               = 3,        // 死亡台词

    EMOTE_DANCE             = 4,        // 跳舞开始表情
    EMOTE_DANCE_END         = 5,        // 跳舞结束表情
};

/**
 * @brief 战斗事件 ID 枚举，用于事件调度系统
 */
enum Events
{
    EVENT_DISRUPT = 1,                  // 法术打乱事件
    EVENT_FEVER,                        // 衰败热病事件
    EVENT_ERUPT,                        // 地面喷发事件
    EVENT_DANCE,                        // 跳舞阶段开始事件
    EVENT_DANCE_END                     // 跳舞阶段结束事件
};

/**
 * @brief 战斗阶段枚举
 */
enum Phases
{
    PHASE_FIGHT = 1,                    // 地面战斗阶段 - Boss 在平台上与玩家战斗
    PHASE_DANCE                         // 跳舞阶段 - Boss 传送到平台，玩家躲避喷发
};

/**
 * @brief 其他数据常量枚举
 */
enum Misc
{
    DATA_SAFETY_DANCE               = 19962139  // 安全跳舞成就数据标识
};

/**
 * @brief 地面喷发机关相关常量
 */
static const uint32 firstEruptionDBGUID = 84980;     // 第一个喷发地精对象的数据库 GUID
static const uint8 numSections = 4;                  // 房间分为 4 个区域
static const uint8 numEruptions[numSections] = {     // 每个区域包含的喷发地精对象数量
    15,     // 第1个区域（靠近入口）有 15 个喷发点
    25,     // 第2个区域有 25 个喷发点
    23,     // 第3个区域有 23 个喷发点
    13      // 第4个区域有 13 个喷发点
};

/**
 * @brief 海根 Boss AI 结构体
 *
 * 继承自 BossAI，实现海根的完整战斗逻辑。
 * 海根战斗的核心机制是地面喷发和跳舞阶段切换。
 */
struct boss_heigan : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature Boss 生物对象指针
     */
    boss_heigan(Creature* creature) : BossAI(creature, BOSS_HEIGAN), _safeSection(0), _danceDirection(false), _safetyDance(false) { }

    /**
     * @brief 重置 Boss 状态
     *
     * 当 Boss 脱离战斗或重置时调用，恢复 Boss 到初始状态。
     * 将反应状态设置为主动攻击。
     *
     * @调用时机 Boss 脱离战斗、重置副本、Boss 初始化时
     */
    void Reset() override
    {
        me->SetReactState(REACT_AGGRESSIVE);    // 设置为主动攻击模式
        _Reset();                                // 调用父类重置函数
    }

    /**
     * @brief 击杀单位时的回调函数
     * @param who 被击杀的单位
     *
     * 当海根击杀玩家时，播放击杀台词并标记"安全跳舞"成就失败。
     * 只有当被击杀的是玩家时才触发。
     *
     * @调用时机 Boss 击杀任何单位时
     */
    void KilledUnit(Unit* who) override
    {
        if (who->GetTypeId() == TYPEID_PLAYER)  // 只对玩家死亡有反应
        {
            Talk(SAY_SLAY);                     // 播放击杀台词
            _safetyDance = false;               // 标记成就失败
        }
    }

    /**
     * @brief 获取 Boss 自定义数据
     * @param type 数据类型标识
     * @return 如果是安全跳舞成就查询且成就仍然可能，返回 1；否则返回 0
     *
     * @调用时机 成就系统检查是否完成"安全跳舞"成就时
     */
    uint32 GetData(uint32 type) const override
    {
        return (type == DATA_SAFETY_DANCE && _safetyDance) ? 1u : 0u;
    }

    /**
     * @brief Boss 死亡时的回调函数
     * @param killer 击杀者（未使用）
     *
     * 当海根死亡时调用，执行清理工作并播放死亡台词。
     *
     * @调用时机 Boss 生命值降为 0 时
     */
    void JustDied(Unit* /*killer*/) override
    {
        _JustDied();                            // 调用父类死亡处理
        Talk(SAY_DEATH);                        // 播放死亡台词
    }

    /**
     * @brief Boss 进入战斗时的回调函数
     * @param who 激活 Boss 的目标
     *
     * 初始化战斗阶段，设置事件调度，收集喷发地精对象的 GUID。
     * 这是战斗开始的核心初始化函数。
     *
     * 主要工作：
     * 1. 播放开战台词
     * 2. 初始化安全区域为 0（靠近入口的第一个区域）
     * 3. 调度法术打乱、衰败热病、跳舞阶段和地面喷发事件
     * 4. 标记安全跳舞成就初始状态为 true
     * 5. 遍历地图中所有喷发地精对象并按区域分类存储
     *
     * @调用时机 Boss 被玩家激活进入战斗状态时
     * @性能注意 需要遍历地图中的游戏对象，在大型副本中可能有性能开销
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        Talk(SAY_AGGRO);                        // 播放开战台词

        _safeSection = 0;                       // 初始安全区域为第一个区域
        // 调度地面战斗阶段的各种技能事件
        events.ScheduleEvent(EVENT_DISRUPT, randtime(Seconds(15), Seconds(20)), 0, PHASE_FIGHT);   // 15-20秒后法术打乱
        events.ScheduleEvent(EVENT_FEVER, randtime(Seconds(10), Seconds(20)), 0, PHASE_FIGHT);    // 10-20秒后衰败热病
        events.ScheduleEvent(EVENT_DANCE, Minutes(1) + Seconds(30), 0, PHASE_FIGHT);              // 90秒后开始跳舞阶段
        events.ScheduleEvent(EVENT_ERUPT, 15s);                                                    // 15秒后地面喷发

        _safetyDance = true;                    // 初始时安全跳舞成就仍有可能

        // 收集地图中所有喷发地精对象的 GUID 并按区域分类存储
        std::unordered_multimap<uint32, GameObject*> const& mapGOs = me->GetMap()->GetGameObjectBySpawnIdStore();
        uint32 spawnId = firstEruptionDBGUID;   // 从第一个喷发对象的数据库 GUID 开始

        // 遍历 4 个区域
        for (uint8 section = 0; section < numSections; ++section)
        {
            _eruptTiles[section].clear();       // 清空该区域的喷发对象列表
            // 遍历该区域内的所有喷发对象
            for (uint8 i = 0; i < numEruptions[section]; ++i)
            {
                // 查找具有该 spawnId 的所有游戏对象实例
                auto tileIt = mapGOs.equal_range(spawnId++);
                for (auto it = tileIt.first; it != tileIt.second; ++it)
                    _eruptTiles[section].push_back(it->second->GetGUID());  // 存储 GUID
            }
        }
    }

    /**
     * @brief Boss AI 主更新函数
     * @param diff 距离上次更新的时间间隔（毫秒）
     *
     * 这是 Boss AI 的核心逻辑循环，每帧调用一次。
     * 处理事件调度、技能施放、阶段切换等所有战斗逻辑。
     *
     * 主要处理的事件：
     * - EVENT_DISRUPT: 法术打乱技能，AOE 打断施法
     * - EVENT_FEVER: 衰败热病技能，AOE 持续伤害
     * - EVENT_DANCE: 跳舞阶段开始，Boss 传送到平台并施放瘟疫云
     * - EVENT_DANCE_END: 跳舞阶段结束，恢复地面战斗
     * - EVENT_ERUPT: 地面喷发，按规律在不同区域触发伤害
     *
     * @调用时机 每帧（服务器 tick）调用一次，频率约为 50ms
     * @性能注意 频繁调用，需要保持代码简洁高效
     */
    void UpdateAI(uint32 diff) override
    {
        // 检查是否有有效目标，如果没有则直接返回
        if (!UpdateVictim())
            return;

        // 更新事件调度器
        events.Update(diff);

        // 处理所有待执行的事件
        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_DISRUPT:
                    // 法术打乱：对周围所有玩家施放打断效果
                    DoCastAOE(SPELL_SPELL_DISRUPTION);
                    events.Repeat(Seconds(11));     // 11秒后再次施放
                    break;

                case EVENT_FEVER:
                    // 衰败热病：对周围所有玩家施放持续伤害疾病
                    DoCastAOE(SPELL_DECREPIT_FEVER);
                    events.Repeat(randtime(Seconds(20), Seconds(25)));  // 20-25秒后再次施放
                    break;

                case EVENT_DANCE:
                    // 跳舞阶段开始
                    events.SetPhase(PHASE_DANCE);           // 切换到跳舞阶段
                    Talk(SAY_TAUNT);                        // 播放嘲讽台词
                    Talk(EMOTE_DANCE);                      // 播放跳舞表情
                    _safeSection = 0;                       // 重置安全区域为第一个区域
                    me->SetReactState(REACT_PASSIVE);       // 设置为被动反应状态（不主动攻击）
                    me->AttackStop();                       // 停止攻击
                    me->StopMoving();                       // 停止移动
                    DoCast(SPELL_TELEPORT_SELF);            // 传送到平台中央
                    DoCastAOE(SPELL_PLAGUE_CLOUD);          // 施放瘟疫云（持续伤害）
                    events.ScheduleEvent(EVENT_DANCE_END, Seconds(45), 0, PHASE_DANCE);  // 45秒后结束跳舞
                    events.RescheduleEvent(EVENT_ERUPT, Seconds(10));   // 10秒后开始快速喷发
                    break;

                case EVENT_DANCE_END:
                    // 跳舞阶段结束，返回地面战斗阶段
                    events.SetPhase(PHASE_FIGHT);           // 切换回战斗阶段
                    Talk(EMOTE_DANCE_END);                  // 播放跳舞结束表情
                    _safeSection = 0;                       // 重置安全区域为第一个区域
                    // 重新调度地面战斗阶段的技能
                    events.ScheduleEvent(EVENT_DISRUPT, randtime(Seconds(10), Seconds(25)), 0, PHASE_FIGHT);
                    events.ScheduleEvent(EVENT_FEVER, randtime(Seconds(15), Seconds(20)), 0, PHASE_FIGHT);
                    events.ScheduleEvent(EVENT_DANCE, Minutes(1) + Seconds(30), 0, PHASE_FIGHT);  // 90秒后再次跳舞
                    events.RescheduleEvent(EVENT_ERUPT, Seconds(15));   // 15秒后恢复慢速喷发
                    me->CastStop();                         // 停止施法
                    me->SetReactState(REACT_AGGRESSIVE);    // 恢复主动攻击状态
                    DoZoneInCombat();                       // 将范围内玩家拉入战斗
                    break;

                case EVENT_ERUPT:
                    // 地面喷发：在非安全区域触发喷发伤害
                    TeleportCheaters();                     // 传送作弊者（站在不该站的地方的玩家）

                    // 遍历所有区域，对非安全区域施放喷发
                    for (uint8 section = 0; section < numSections; ++section)
                    {
                        if (section != _safeSection)        // 只喷发非安全区域
                        {
                            // 对该区域内的所有喷发地精对象触发喷发
                            for (ObjectGuid tileGUID : _eruptTiles[section])
                            {
                                if (GameObject* tile = ObjectAccessor::GetGameObject(*me, tileGUID))
                                {
                                    tile->SendCustomAnim(0);                        // 播放喷发动画
                                    CastSpellExtraArgs args;
                                    args.OriginalCaster = me->GetGUID();            // 设置原始施法者为海根
                                    // 触发陷阱法术，对区域内的玩家造成伤害
                                    tile->CastSpell(tile, tile->GetGOInfo()->trap.spellId, args);
                                }
                            }
                        }
                    }

                    // 更新安全区域的位置（喷发会在区域间移动）
                    // 到达边界时改变方向
                    if (_safeSection == 0)
                        _danceDirection = true;             // 在第一个区域时向逆时针方向移动
                    else if (_safeSection == numSections-1)
                        _danceDirection = false;            // 在最后一个区域时向顺时针方向移动

                    // 根据方向更新安全区域索引
                    _danceDirection ? ++_safeSection : --_safeSection;

                    // 根据当前阶段设置喷发间隔
                    // 跳舞阶段快速喷发（3秒），地面阶段慢速喷发（10秒）
                    events.Repeat(events.IsInPhase(PHASE_DANCE) ? Seconds(3) : Seconds(10));
                    break;
            }
        }

        // 如果在地面战斗阶段且准备就绪，执行近战攻击
        DoMeleeAttackIfReady();
    }

    private:
        std::vector<ObjectGuid> _eruptTiles[numSections];   // 每个区域的喷发地精对象 GUID 列表，战斗开始时填充

        uint32 _safeSection;                                // 当前安全区域的索引（0 表示靠近入口的区域）
        bool _danceDirection;                               // 喷发移动方向（true = 逆时针，false = 顺时针）
        bool _safetyDance;                                  // 安全跳舞成就是否仍可能完成（true = 尚无玩家死亡）
};

/**
 * @brief 地面喷发法术脚本（法术 ID: 29371）
 *
 * 处理海根战斗中地面喷发造成的伤害。
 * 如果喷发伤害击杀了玩家，会通知海根 AI 以标记"安全跳舞"成就失败。
 */
class spell_heigan_eruption : public SpellScript
{
    PrepareSpellScript(spell_heigan_eruption);

    /**
     * @brief 处理法术效果命中目标
     * @param eff 法术效果索引（未使用）
     *
     * 当地面喷发法术命中目标时调用。
     * 检查伤害是否足以击杀目标，如果是则通知海根 AI。
     *
     * @调用时机 法术效果命中目标时
     */
    void HandleScript(SpellEffIndex /*eff*/)
    {
        Unit* caster = GetCaster();
        if (!caster || !GetHitUnit())
            return;

        // 如果伤害足以击杀目标
        if (GetHitDamage() >= int32(GetHitUnit()->GetHealth()))
        {
            // 获取副本脚本和海根实例
            if (InstanceScript* instance = caster->GetInstanceScript())
            {
                if (Creature* Heigan = ObjectAccessor::GetCreature(*caster, instance->GetGuidData(DATA_HEIGAN)))
                {
                    // 通知海根 AI 有单位被击杀，用于标记成就失败
                    Heigan->AI()->KilledUnit(GetHitUnit());
                }
            }
        }
    }

    /**
     * @brief 注册法术效果处理函数
     *
     * 将 HandleScript 函数注册到法术的学校伤害效果上。
     */
    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_heigan_eruption::HandleScript, EFFECT_0, SPELL_EFFECT_SCHOOL_DAMAGE);
    }
};

/**
 * @brief 安全跳舞成就脚本
 *
 * 实现"安全跳舞"成就的判定逻辑。
 * 成就要求：在整个战斗过程中没有玩家死亡。
 */
class achievement_safety_dance : public AchievementCriteriaScript
{
    public:
        /**
         * @brief 构造函数
         */
        achievement_safety_dance() : AchievementCriteriaScript("achievement_safety_dance") { }

        /**
         * @brief 检查是否满足成就条件
         * @param player 玩家对象（未使用）
         * @param target 目标单位（应为海根 Boss）
         * @return 如果满足成就条件返回 true，否则返回 false
         *
         * 通过查询海根 AI 的 _safetyDance 标志来判断是否有玩家死亡。
         *
         * @调用时机 成就系统检查该成就是否完成时
         */
        bool OnCheck(Player* /*player*/, Unit* target) override
        {
            if (!target)
                return false;

            if (Creature* Heigan = target->ToCreature())
            {
                // 查询海根 AI 的安全跳舞标志
                if (Heigan->AI()->GetData(DATA_SAFETY_DANCE))
                    return true;
            }

            return false;
        }
};

/**
 * @brief 注册海根 Boss 脚本
 *
 * 将所有海根相关的脚本注册到系统中，包括：
 * - Boss AI
 * - 法术脚本
 * - 成就脚本
 *
 * @调用时机 服务器启动时，由脚本加载系统自动调用
 */
void AddSC_boss_heigan()
{
    RegisterNaxxramasCreatureAI(boss_heigan);           // 注册海根 Boss AI
    RegisterSpellScript(spell_heigan_eruption);         // 注册地面喷发法术脚本
    new achievement_safety_dance();                     // 注册安全跳舞成就脚本
}
