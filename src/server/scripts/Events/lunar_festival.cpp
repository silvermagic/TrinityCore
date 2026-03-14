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
 * @file lunar_festival.cpp
 * @brief 春节（月度狂欢）事件脚本模块
 *
 * 本模块实现了魔兽世界春节（Lunar Festival）节日活动的核心功能，包括：
 * - 烟花发射系统：各种颜色和类型的烟花效果
 * - 年兽BOSS系统：召唤年兽及其手下进行战斗
 * - 艾露恩蜡烛系统：特殊的节日道具效果
 * - 月神祝福系统：使用特殊烟花获得祝福
 *
 * 春节每年1月下旬至2月中旬举办，主要活动包括：
 * - 在月光林地与长者对话获得祝福
 * - 收集春节硬币兑换奖励
 * - 在午夜燃放烟花召唤年兽
 * - 完成节日成就获得称号和奖励
 *
 * 文化背景：春节基于现实中的农历新年庆祝活动，
 * 年兽的传说源自中国传统文化中驱赶年兽的故事。
 */

#include "GameObject.h"
#include "ScriptMgr.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "SpellScript.h"
#include "TemporarySummon.h"

/**
 * @brief 烟花相关NPC、游戏对象和法术ID枚举
 *
 * 定义了春节烟花系统使用的所有实体和法术ID。
 * 包括各种颜色的烟花NPC、发射器游戏对象和对应的法术效果。
 */
enum Fireworks
{
    // 烟花NPC - 单个烟花
    NPC_OMEN                = 15467,  ///< 年兽 - 春节特殊BOSS
    NPC_MINION_OF_OMEN      = 15466,  ///< 年兽的手下 - 随年兽召唤的小怪
    NPC_FIREWORK_BLUE       = 15879,  ///< 蓝色烟花
    NPC_FIREWORK_GREEN      = 15880,  ///< 绿色烟花
    NPC_FIREWORK_PURPLE     = 15881,  ///< 紫色烟花
    NPC_FIREWORK_RED        = 15882,  ///< 红色烟花
    NPC_FIREWORK_YELLOW     = 15883,  ///< 黄色烟花
    NPC_FIREWORK_WHITE      = 15884,  ///< 白色烟花
    NPC_FIREWORK_BIG_BLUE   = 15885,  ///< 大型蓝色烟花
    NPC_FIREWORK_BIG_GREEN  = 15886,  ///< 大型绿色烟花
    NPC_FIREWORK_BIG_PURPLE = 15887,  ///< 大型紫色烟花
    NPC_FIREWORK_BIG_RED    = 15888,  ///< 大型红色烟花
    NPC_FIREWORK_BIG_YELLOW = 15889,  ///< 大型黄色烟花
    NPC_FIREWORK_BIG_WHITE  = 15890,  ///< 大型白色烟花

    // 烟花群NPC - 多个烟花同时发射
    NPC_CLUSTER_BLUE        = 15872,  ///< 蓝色烟花群
    NPC_CLUSTER_RED         = 15873,  ///< 红色烟花群
    NPC_CLUSTER_GREEN       = 15874,  ///< 绿色烟花群
    NPC_CLUSTER_PURPLE      = 15875,  ///< 紫色烟花群
    NPC_CLUSTER_WHITE       = 15876,  ///< 白色烟花群
    NPC_CLUSTER_YELLOW      = 15877,  ///< 黄色烟花群
    NPC_CLUSTER_BIG_BLUE    = 15911,  ///< 大型蓝色烟花群
    NPC_CLUSTER_BIG_GREEN   = 15912,  ///< 大型绿色烟花群
    NPC_CLUSTER_BIG_PURPLE  = 15913,  ///< 大型紫色烟花群
    NPC_CLUSTER_BIG_RED     = 15914,  ///< 大型红色烟花群
    NPC_CLUSTER_BIG_WHITE   = 15915,  ///< 大型白色烟花群
    NPC_CLUSTER_BIG_YELLOW  = 15916,  ///< 大型黄色烟花群
    NPC_CLUSTER_ELUNE       = 15918,  ///< 艾露恩烟花群 - 特殊烟花

    // 烟花发射器游戏对象
    GO_FIREWORK_LAUNCHER_1  = 180771,  ///< 烟花发射器1
    GO_FIREWORK_LAUNCHER_2  = 180868,  ///< 烟花发射器2
    GO_FIREWORK_LAUNCHER_3  = 180850,  ///< 烟花发射器3
    GO_CLUSTER_LAUNCHER_1   = 180772,  ///< 烟花群发射器1
    GO_CLUSTER_LAUNCHER_2   = 180859,  ///< 烟花群发射器2
    GO_CLUSTER_LAUNCHER_3   = 180869,  ///< 烟花群发射器3
    GO_CLUSTER_LAUNCHER_4   = 180874,  ///< 烟花群发射器4

    // 烟花法术
    SPELL_ROCKET_BLUE       = 26344,   ///< 蓝色火箭烟花
    SPELL_ROCKET_GREEN      = 26345,   ///< 绿色火箭烟花
    SPELL_ROCKET_PURPLE     = 26346,   ///< 紫色火箭烟花
    SPELL_ROCKET_RED        = 26347,   ///< 红色火箭烟花
    SPELL_ROCKET_WHITE      = 26348,   ///< 白色火箭烟花
    SPELL_ROCKET_YELLOW     = 26349,   ///< 黄色火箭烟花
    SPELL_ROCKET_BIG_BLUE   = 26351,   ///< 大型蓝色火箭烟花
    SPELL_ROCKET_BIG_GREEN  = 26352,   ///< 大型绿色火箭烟花
    SPELL_ROCKET_BIG_PURPLE = 26353,   ///< 大型紫色火箭烟花
    SPELL_ROCKET_BIG_RED    = 26354,   ///< 大型红色火箭烟花
    SPELL_ROCKET_BIG_WHITE  = 26355,   ///< 大型白色火箭烟花
    SPELL_ROCKET_BIG_YELLOW = 26356,   ///< 大型黄色火箭烟花
    SPELL_LUNAR_FORTUNE     = 26522,   ///< 月神祝福 - 艾露恩烟花群给予的祝福

    // 其他常量
    ANIM_GO_LAUNCH_FIREWORK = 3,       ///< 游戏对象发射烟花动画ID
    ZONE_MOONGLADE          = 493,     ///< 月光林地区域ID
};

/// 年兽召唤位置坐标
Position omenSummonPos = {7558.993f, -2839.999f, 450.0214f, 4.46f};

/**
 * @struct npc_firework
 * @brief 烟花NPC AI结构
 *
 * 控制各种烟花的发射行为。烟花分为两类：
 * - 单个烟花：发射一次烟花效果
 * - 烟花群：同时发射多个烟花，产生更壮观的视觉效果
 *
 * 特殊功能：
 * - 艾露恩烟花群有机会召唤年兽或年兽的手下
 * - 烟花会与发射器游戏对象互动，产生发射动画
 */
struct npc_firework : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 烟花生物实体
     */
    npc_firework(Creature* creature) : ScriptedAI(creature) { }

    /**
     * @brief 判断当前烟花是否为烟花群类型
     *
     * @return 如果是烟花群返回true，单个烟花返回false
     */
    bool isCluster()
    {
        switch (me->GetEntry())
        {
            case NPC_FIREWORK_BLUE:
            case NPC_FIREWORK_GREEN:
            case NPC_FIREWORK_PURPLE:
            case NPC_FIREWORK_RED:
            case NPC_FIREWORK_YELLOW:
            case NPC_FIREWORK_WHITE:
            case NPC_FIREWORK_BIG_BLUE:
            case NPC_FIREWORK_BIG_GREEN:
            case NPC_FIREWORK_BIG_PURPLE:
            case NPC_FIREWORK_BIG_RED:
            case NPC_FIREWORK_BIG_YELLOW:
            case NPC_FIREWORK_BIG_WHITE:
                return false;  // 单个烟花
            case NPC_CLUSTER_BLUE:
            case NPC_CLUSTER_GREEN:
            case NPC_CLUSTER_PURPLE:
            case NPC_CLUSTER_RED:
            case NPC_CLUSTER_YELLOW:
            case NPC_CLUSTER_WHITE:
            case NPC_CLUSTER_BIG_BLUE:
            case NPC_CLUSTER_BIG_GREEN:
            case NPC_CLUSTER_BIG_PURPLE:
            case NPC_CLUSTER_BIG_RED:
            case NPC_CLUSTER_BIG_YELLOW:
            case NPC_CLUSTER_BIG_WHITE:
            case NPC_CLUSTER_ELUNE:
            default:
                return true;  // 烟花群
        }
    }

    /**
     * @brief 查找最近的烟花发射器
     *
     * 根据烟花类型（单个或群），查找对应类型的发射器游戏对象。
     *
     * @return 找到的发射器游戏对象指针，未找到返回nullptr
     */
    GameObject* FindNearestLauncher()
    {
        GameObject* launcher = nullptr;

        if (isCluster())
        {
            // 烟花群使用群发射器
            GameObject* launcher1 = GetClosestGameObjectWithEntry(me, GO_CLUSTER_LAUNCHER_1, 0.5f);
            GameObject* launcher2 = GetClosestGameObjectWithEntry(me, GO_CLUSTER_LAUNCHER_2, 0.5f);
            GameObject* launcher3 = GetClosestGameObjectWithEntry(me, GO_CLUSTER_LAUNCHER_3, 0.5f);
            GameObject* launcher4 = GetClosestGameObjectWithEntry(me, GO_CLUSTER_LAUNCHER_4, 0.5f);

            if (launcher1)
                launcher = launcher1;
            else if (launcher2)
                launcher = launcher2;
            else if (launcher3)
                launcher = launcher3;
            else if (launcher4)
                launcher = launcher4;
        }
        else
        {
            // 单个烟花使用普通发射器
            GameObject* launcher1 = GetClosestGameObjectWithEntry(me, GO_FIREWORK_LAUNCHER_1, 0.5f);
            GameObject* launcher2 = GetClosestGameObjectWithEntry(me, GO_FIREWORK_LAUNCHER_2, 0.5f);
            GameObject* launcher3 = GetClosestGameObjectWithEntry(me, GO_FIREWORK_LAUNCHER_3, 0.5f);

            if (launcher1)
                launcher = launcher1;
            else if (launcher2)
                launcher = launcher2;
            else if (launcher3)
                launcher = launcher3;
        }

        return launcher;
    }

    /**
     * @brief 根据烟花NPC ID获取对应的烟花法术ID
     *
     * @param entry 烟花NPC的ID
     * @return 对应的法术ID，无效的ID返回0
     */
    uint32 GetFireworkSpell(uint32 entry)
    {
        switch (entry)
        {
            case NPC_FIREWORK_BLUE:
                return SPELL_ROCKET_BLUE;
            case NPC_FIREWORK_GREEN:
                return SPELL_ROCKET_GREEN;
            case NPC_FIREWORK_PURPLE:
                return SPELL_ROCKET_PURPLE;
            case NPC_FIREWORK_RED:
                return SPELL_ROCKET_RED;
            case NPC_FIREWORK_YELLOW:
                return SPELL_ROCKET_YELLOW;
            case NPC_FIREWORK_WHITE:
                return SPELL_ROCKET_WHITE;
            case NPC_FIREWORK_BIG_BLUE:
                return SPELL_ROCKET_BIG_BLUE;
            case NPC_FIREWORK_BIG_GREEN:
                return SPELL_ROCKET_BIG_GREEN;
            case NPC_FIREWORK_BIG_PURPLE:
                return SPELL_ROCKET_BIG_PURPLE;
            case NPC_FIREWORK_BIG_RED:
                return SPELL_ROCKET_BIG_RED;
            case NPC_FIREWORK_BIG_YELLOW:
                return SPELL_ROCKET_BIG_YELLOW;
            case NPC_FIREWORK_BIG_WHITE:
                return SPELL_ROCKET_BIG_WHITE;
            default:
                return 0;
        }
    }

    /**
     * @brief 获取烟花游戏对象ID
     *
     * 根据烟花群类型，确定要召唤的烟花游戏对象ID。
     * 这个游戏对象是实际显示烟花效果的视觉实体。
     *
     * @return 游戏对象ID，无效的烟花群类型返回0
     */
    uint32 GetFireworkGameObjectId()
    {
        uint32 spellId = 0;

        // 根据烟花群类型选择对应的单个烟花法术
        switch (me->GetEntry())
        {
            case NPC_CLUSTER_BLUE:
                spellId = GetFireworkSpell(NPC_FIREWORK_BLUE);
                break;
            case NPC_CLUSTER_GREEN:
                spellId = GetFireworkSpell(NPC_FIREWORK_GREEN);
                break;
            case NPC_CLUSTER_PURPLE:
                spellId = GetFireworkSpell(NPC_FIREWORK_PURPLE);
                break;
            case NPC_CLUSTER_RED:
                spellId = GetFireworkSpell(NPC_FIREWORK_RED);
                break;
            case NPC_CLUSTER_YELLOW:
                spellId = GetFireworkSpell(NPC_FIREWORK_YELLOW);
                break;
            case NPC_CLUSTER_WHITE:
                spellId = GetFireworkSpell(NPC_FIREWORK_WHITE);
                break;
            case NPC_CLUSTER_BIG_BLUE:
                spellId = GetFireworkSpell(NPC_FIREWORK_BIG_BLUE);
                break;
            case NPC_CLUSTER_BIG_GREEN:
                spellId = GetFireworkSpell(NPC_FIREWORK_BIG_GREEN);
                break;
            case NPC_CLUSTER_BIG_PURPLE:
                spellId = GetFireworkSpell(NPC_FIREWORK_BIG_PURPLE);
                break;
            case NPC_CLUSTER_BIG_RED:
                spellId = GetFireworkSpell(NPC_FIREWORK_BIG_RED);
                break;
            case NPC_CLUSTER_BIG_YELLOW:
                spellId = GetFireworkSpell(NPC_FIREWORK_BIG_YELLOW);
                break;
            case NPC_CLUSTER_BIG_WHITE:
                spellId = GetFireworkSpell(NPC_FIREWORK_BIG_WHITE);
                break;
            case NPC_CLUSTER_ELUNE:
                // 艾露恩烟花群随机选择颜色
                spellId = GetFireworkSpell(urand(NPC_FIREWORK_BLUE, NPC_FIREWORK_WHITE));
                break;
        }

        // 从法术信息中提取召唤的游戏对象ID
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);

        if (spellInfo && spellInfo->GetEffect(EFFECT_0).Effect == SPELL_EFFECT_SUMMON_OBJECT_WILD)
            return spellInfo->GetEffect(EFFECT_0).MiscValue;

        return 0;
    }

    /**
     * @brief 重置并触发烟花发射
     *
     * 当烟花NPC刷新时调用，执行以下操作：
     * 1. 找到发射器并播放发射动画
     * 2. 根据类型发射单个烟花或烟花群
     * 3. 特殊情况：在月光林地的艾露恩烟花群有机会召唤年兽
     *
     * 年兽召唤机制：
     * - 在月光林地艾露恩ara湖附近
     * - 40%概率召唤年兽手下
     * - 10%概率召唤年兽BOSS
     * - 50%概率无特殊事件
     */
    void Reset() override
    {
        // 查找并激活发射器
        if (GameObject* launcher = FindNearestLauncher())
        {
            launcher->SendCustomAnim(ANIM_GO_LAUNCH_FIREWORK);
            me->SetOrientation(launcher->GetOrientation() + float(M_PI) / 2);
        }
        else
            return;  // 没有发射器，无法发射

        if (isCluster())
        {
            // 检查是否在月光林地艾露恩ara湖南部，如果是，尝试召唤年兽或手下
            if (me->GetZoneId() == ZONE_MOONGLADE)
            {
                // 只有在没有年兽存在且在召唤位置附近时才尝试召唤
                if (!me->FindNearestCreature(NPC_OMEN, 100.0f) && me->GetDistance2d(omenSummonPos.GetPositionX(), omenSummonPos.GetPositionY()) <= 100.0f)
                {
                    switch (urand(0, 9))
                    {
                        case 0:
                        case 1:
                        case 2:
                        case 3:  // 40%概率召唤年兽手下
                            if (Creature* minion = me->SummonCreature(NPC_MINION_OF_OMEN, me->GetPositionX()+frand(-5.0f, 5.0f), me->GetPositionY()+frand(-5.0f, 5.0f), me->GetPositionZ(), 0.0f, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 20s))
                                minion->AI()->AttackStart(me->SelectNearestPlayer(20.0f));
                            break;
                        case 9:  // 10%概率召唤年兽BOSS
                            me->SummonCreature(NPC_OMEN, omenSummonPos);
                            break;
                        // case 4-8: 50%概率无特殊事件
                    }
                }
            }

            // 艾露恩烟花群施放月神祝福
            if (me->GetEntry() == NPC_CLUSTER_ELUNE)
                DoCast(SPELL_LUNAR_FORTUNE);

            // 召唤4个烟花游戏对象，形成一个正方形排列
            float displacement = 0.7f;
            for (uint8 i = 0; i < 4; i++)
                me->SummonGameObject(GetFireworkGameObjectId(), me->GetPositionX() + (i % 2 == 0 ? displacement : -displacement), me->GetPositionY() + (i > 1 ? displacement : -displacement), me->GetPositionZ() + 4.0f, me->GetOrientation(), QuaternionData(), 1s);
        }
        else
        {
            // 单个烟花：直接施放对应的烟花法术
            me->CastSpell(me->GetPosition(), GetFireworkSpell(me->GetEntry()), true);
        }
    }
};

/*####
# npc_omen
# 年兽
####*/

/**
 * @brief 年兽相关法术和事件ID枚举
 *
 * 定义了年兽BOSS战斗中使用的法术、游戏对象和事件定时器ID。
 */
enum Omen
{
    SPELL_OMEN_CLEAVE           = 15284,  ///< 年兽顺劈斩 - 近战范围AOE攻击
    SPELL_OMEN_STARFALL         = 26540,  ///< 年兽星落 - 召唤流星打击随机目标
    SPELL_OMEN_SUMMON_SPOTLIGHT = 26392,  ///< 召唤巨型聚光灯 - 死亡时召唤
    SPELL_ELUNE_CANDLE          = 26374,  ///< 艾露恩蜡烛 - 玩家可用来影响年兽

    GO_ELUNE_TRAP_1             = 180876,  ///< 艾露恩陷阱1 - 死亡后清理
    GO_ELUNE_TRAP_2             = 180877,  ///< 艾露恩陷阱2 - 死亡后清理

    EVENT_CAST_CLEAVE           = 1,       ///< 定时事件：施放顺劈斩
    EVENT_CAST_STARFALL         = 2,       ///< 定时事件：施放星落
    EVENT_DESPAWN               = 3,       ///< 定时事件：消失
};

/**
 * @struct npc_omen
 * @brief 年兽BOSS AI结构
 *
 * 年兽是春节的特殊世界BOSS，在月光林地被烟花召唤。
 * 传说中年兽害怕红色、火光和巨大的声响，这解释了
 * 为什么烟花可以召唤它。
 *
 * 战斗机制：
 * - 召唤后会走向湖中心
 * - 使用顺劈斩和星落技能攻击玩家
 * - 艾露恩蜡烛可以打断星落并延长冷却时间
 * - 死亡后召唤聚光灯并留下战利品
 */
struct npc_omen : public ScriptedAI
{
    /**
     * @brief 构造函数
     *
     * 初始化年兽，设置免疫玩家攻击，并开始向湖中心移动。
     *
     * @param creature 年兽生物实体
     */
    npc_omen(Creature* creature) : ScriptedAI(creature)
    {
        me->SetImmuneToPC(true);  // 移动过程中免疫玩家攻击
        me->GetMotionMaster()->MovePoint(1, 7549.977f, -2855.137f, 456.9678f);  // 移动到湖中心
    }

    EventMap events;  ///< 事件定时器映射

    /**
     * @brief 移动完成通知
     *
     * 当年兽到达湖中心后，解除免疫并开始攻击最近的玩家。
     *
     * @param type 移动类型
     * @param pointId 路径点ID
     */
    void MovementInform(uint32 type, uint32 pointId) override
    {
        if (type != POINT_MOTION_TYPE)
            return;

        if (pointId == 1)
        {
            // 到达湖中心，设置家园位置并解除免疫
            me->SetHomePosition(me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), me->GetOrientation());
            me->SetImmuneToPC(false);
            // 攻击最近的玩家
            if (Player* player = me->SelectNearestPlayer(40.0f))
                AttackStart(player);
        }
    }

    /**
     * @brief 进入战斗
     *
     * 重置事件定时器并安排技能使用。
     *
     * @param attacker 攻击者（未使用）
     */
    void JustEngagedWith(Unit* /*attacker*/) override
    {
        events.Reset();
        events.ScheduleEvent(EVENT_CAST_CLEAVE, 3s, 5s);    // 3-5秒后施放顺劈斩
        events.ScheduleEvent(EVENT_CAST_STARFALL, 8s, 10s); // 8-10秒后施放星落
    }

    /**
     * @brief 死亡处理
     *
     * 召唤巨型聚光灯，用于清理战场并显示胜利效果。
     *
     * @param killer 击杀者（未使用）
     */
    void JustDied(Unit* /*killer*/) override
    {
        DoCast(SPELL_OMEN_SUMMON_SPOTLIGHT);
    }

    /**
     * @brief 法术命中处理
     *
     * 处理艾露恩蜡烛的特殊效果：打断星落并延长冷却时间。
     *
     * @param caster 施法者（未使用）
     * @param spellInfo 命中的法术信息
     */
    void SpellHit(WorldObject* /*caster*/, SpellInfo const* spellInfo) override
    {
        if (spellInfo->Id == SPELL_ELUNE_CANDLE)
        {
            // 如果年兽正在施放星落，则打断
            if (me->HasAura(SPELL_OMEN_STARFALL))
                me->RemoveAurasDueToSpell(SPELL_OMEN_STARFALL);

            // 重新安排星落，延长冷却时间
            events.RescheduleEvent(EVENT_CAST_STARFALL, 14s, 16s);
        }
    }

    /**
     * @brief 更新AI
     *
     * 处理事件定时器和近战攻击。
     *
     * @param diff 时间差（毫秒）
     */
    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        events.Update(diff);

        switch (events.ExecuteEvent())
        {
            case EVENT_CAST_CLEAVE:
                // 对当前目标施放顺劈斩
                DoCastVictim(SPELL_OMEN_CLEAVE);
                events.ScheduleEvent(EVENT_CAST_CLEAVE, 8s, 10s);
                break;
            case EVENT_CAST_STARFALL:
                // 对随机目标施放星落
                if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                    DoCast(target, SPELL_OMEN_STARFALL);
                events.ScheduleEvent(EVENT_CAST_STARFALL, 14s, 16s);
                break;
        }

        DoMeleeAttackIfReady();
    }
};

/**
 * @struct npc_giant_spotlight
 * @brief 巨型聚光灯AI结构
 *
 * 当年兽死亡后召唤，用于清理战场上的陷阱和年兽尸体。
 * 聚光灯会在5分钟后自动消失，清理所有相关实体。
 */
struct npc_giant_spotlight : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 聚光灯生物实体
     */
    npc_giant_spotlight(Creature* creature) : ScriptedAI(creature) { }

    EventMap events;  ///< 事件定时器映射

    /**
     * @brief 重置
     *
     * 安排5分钟后消失的事件。
     */
    void Reset() override
    {
        events.Reset();
        events.ScheduleEvent(EVENT_DESPAWN, 5min);
    }

    /**
     * @brief 更新AI
     *
     * 处理消失事件，清理所有相关实体。
     *
     * @param diff 时间差（毫秒）
     */
    void UpdateAI(uint32 diff) override
    {
        events.Update(diff);

        if (events.ExecuteEvent() == EVENT_DESPAWN)
        {
            // 清理艾露恩陷阱
            if (GameObject* trap = me->FindNearestGameObject(GO_ELUNE_TRAP_1, 5.0f))
                trap->RemoveFromWorld();

            if (GameObject* trap = me->FindNearestGameObject(GO_ELUNE_TRAP_2, 5.0f))
                trap->RemoveFromWorld();

            // 清理年兽尸体
            if (Creature* omen = me->FindNearestCreature(NPC_OMEN, 5.0f, false))
                omen->DespawnOrUnsummon();

            // 聚光灯自己消失
            me->DespawnOrUnsummon();
        }
    }
};

/**
 * @brief 艾露恩蜡烛法术ID枚举
 *
 * 定义了艾露恩蜡烛使用的各种视觉效果法术。
 * 对年兽使用时有特殊效果，对普通目标使用时是普通效果。
 */
enum EluneCandle
{
    SPELL_ELUNE_CANDLE_OMEN_HEAD   = 26622,  ///< 艾露恩蜡烛-年兽头部 - 特殊视觉效果
    SPELL_ELUNE_CANDLE_OMEN_CHEST  = 26624,  ///< 艾露恩蜡烛-年兽胸部 - 特殊视觉效果
    SPELL_ELUNE_CANDLE_OMEN_HAND_R = 26625,  ///< 艾露恩蜡烛-年兽右手 - 特殊视觉效果
    SPELL_ELUNE_CANDLE_OMEN_HAND_L = 26649,  ///< 艾露恩蜡烛-年兽左手 - 特殊视觉效果
    SPELL_ELUNE_CANDLE_NORMAL      = 26636   ///< 艾露恩蜡烛-普通 - 对非年兽目标使用
};

/**
 * @brief 艾露恩蜡烛法术脚本 (Spell ID: 26374)
 *
 * 艾露恩蜡烛是春节特殊道具，可以用于对抗年兽。
 * 对年兽使用时：
 * - 随机选择一个部位效果（头、胸、左手、右手）
 * - 打断年兽的星落技能
 * - 延长下次星落的冷却时间
 *
 * 对其他目标使用时：
 * - 施放普通蜡烛效果
 *
 * 文化背景：艾露恩蜡烛源自中国传统中用火光和爆竹驱赶年兽的习俗。
 */
// 26374 - Elune's Candle
class spell_lunar_festival_elune_candle : public SpellScript
{
    PrepareSpellScript(spell_lunar_festival_elune_candle);

    /**
     * @brief 验证法术依赖
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_ELUNE_CANDLE_OMEN_HEAD,
            SPELL_ELUNE_CANDLE_OMEN_CHEST,
            SPELL_ELUNE_CANDLE_OMEN_HAND_R,
            SPELL_ELUNE_CANDLE_OMEN_HAND_L,
            SPELL_ELUNE_CANDLE_NORMAL
        });
    }

    /**
     * @brief 处理虚拟效果
     *
     * 根据目标类型选择对应的视觉效果法术。
     *
     * @param effIndex 法术效果索引
     */
    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        uint32 spellId = 0;

        // 判断目标是否为年兽
        if (GetHitUnit()->GetEntry() == NPC_OMEN)
        {
            // 对年兽使用：随机选择一个部位效果
            switch (urand(0, 3))
            {
                case 0:
                    spellId = SPELL_ELUNE_CANDLE_OMEN_HEAD;
                    break;
                case 1:
                    spellId = SPELL_ELUNE_CANDLE_OMEN_CHEST;
                    break;
                case 2:
                    spellId = SPELL_ELUNE_CANDLE_OMEN_HAND_R;
                    break;
                case 3:
                    spellId = SPELL_ELUNE_CANDLE_OMEN_HAND_L;
                    break;
            }
        }
        else
        {
            // 对其他目标使用：普通蜡烛效果
            spellId = SPELL_ELUNE_CANDLE_NORMAL;
        }

        GetCaster()->CastSpell(GetHitUnit(), spellId, true);
    }

    /**
     * @brief 注册法术效果回调
     */
    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_lunar_festival_elune_candle::HandleScript, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

/**
 * @brief 注册所有春节脚本
 *
 * 此函数在服务器启动时被调用，用于注册本文件中定义的所有生物AI和法术脚本。
 * 将脚本与对应的实体ID关联起来，使游戏能够正确处理春节相关的内容。
 */
void AddSC_event_lunar_festival()
{
    // 烟花系统
    RegisterCreatureAI(npc_firework);

    // 年兽BOSS系统
    RegisterCreatureAI(npc_omen);
    RegisterCreatureAI(npc_giant_spotlight);

    // 艾露恩蜡烛系统
    RegisterSpellScript(spell_lunar_festival_elune_candle);
}
