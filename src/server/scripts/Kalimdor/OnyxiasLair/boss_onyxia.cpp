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
 * @file boss_onyxia.cpp
 * @brief 奥妮克希亚首领AI脚本
 *
 * 本模块实现了奥妮克希亚（黑龙公主）的AI逻辑，包括：
 * - 三阶段战斗流程（地面阶段、飞行阶段、最终阶段）
 * - 深呼吸技能（飞行阶段的火焰吐息）
 * - 幼龙召唤机制
 * - 地面喷发效果
 * - 成就系统支持（Many Whelps! Handle It! 和 She Deep Breaths More）
 *
 * 已知问题：
 * - 深呼吸技能的地面视觉效果缺失
 * - 第三阶段幼龙召唤逻辑信息不足
 *
 * @author TrinityCore Team
 * @date 2024
 */

/* ScriptData
SDName: Boss_Onyxia
SD%Complete: 95
SDComment: <Known bugs>
               Ground visual for Deep Breath effect;
               Not summoning whelps on phase 3 (lacks info)
           </Known bugs>
SDCategory: Onyxia's Lair
EndScriptData */

#include "ScriptMgr.h"
#include "CellImpl.h"
#include "GridNotifiersImpl.h"
#include "InstanceScript.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "onyxias_lair.h"
#include "ScriptedCreature.h"
#include "TemporarySummon.h"

/**
 * @brief 奥妮克希亚的喊话和表情枚举
 */
enum Yells
{
    // Say - 喊话
    SAY_AGGRO                   = 0,  // 开战喊话
    SAY_KILL                    = 1,  // 击杀玩家喊话
    SAY_PHASE_2_TRANS           = 2,  // 转换到第二阶段喊话
    SAY_PHASE_3_TRANS           = 3,  // 转换到第三阶段喊话
    // Emote - 表情
    EMOTE_BREATH                = 4   // 深呼吸表情
};

/**
 * @brief 奥妮克希亚使用的法术ID枚举
 */
enum Spells
{
    // Phase 1 spells - 第一阶段法术（地面阶段）
    SPELL_WING_BUFFET           = 18500,  // 翅膀 buffet，击退近战范围敌人
    SPELL_FLAME_BREATH          = 18435,  // 火焰吐息，锥形火焰伤害
    SPELL_CLEAVE                = 68868,  // 顺劈斩，近战AOE
    SPELL_TAIL_SWEEP            = 68867,  // 尾部横扫，背后锥形伤害

    // Phase 2 spells - 第二阶段法术（飞行阶段）
    SPELL_DEEP_BREATH           = 23461,  // 深呼吸，大范围火焰吐息
    SPELL_FIREBALL              = 18392,  // 火球术，随机目标火焰伤害

    //Not much choise about these. We have to make own defintion on the direction/start-end point
    // 深呼吸方向法术 - 用于不同方向的深呼吸攻击
    SPELL_BREATH_NORTH_TO_SOUTH = 17086,                    // 从北向南，数组中出现20次
    SPELL_BREATH_SOUTH_TO_NORTH = 18351,                    // 从南向北，数组中出现11次

    SPELL_BREATH_EAST_TO_WEST   = 18576,                    // 从东向西，数组中出现7次
    SPELL_BREATH_WEST_TO_EAST   = 18609,                    // 从西向东，数组中出现7次

    SPELL_BREATH_SE_TO_NW       = 18564,                    // 从东南向西北，数组中出现12次
    SPELL_BREATH_NW_TO_SE       = 18584,                    // 从西北向东南，数组中出现12次
    SPELL_BREATH_SW_TO_NE       = 18596,                    // 从西南向东北，数组中出现12次
    SPELL_BREATH_NE_TO_SW       = 18617,                    // 从东北向西南，数组中出现12次

    //SPELL_BREATH                = 21131,                  // 数组中出现8次，与其他数组的初始施法不同

    // Phase 3 spells - 第三阶段法术（最终阶段）
    SPELL_BELLOWING_ROAR         = 18431   // 咆哮，范围恐惧效果
};

/**
 * @brief 奥妮克希亚的事件类型枚举
 * 用于事件调度器管理技能冷却和阶段转换
 */
enum Events
{
    EVENT_BELLOWING_ROAR = 1,   // 咆哮事件 - 第三阶段范围恐惧
    EVENT_FLAME_BREATH   = 2,   // 火焰吐息事件 - 第一、三阶段锥形火焰
    EVENT_TAIL_SWEEP     = 3,   // 尾部横扫事件 - 第一、三阶段背后锥形伤害
    EVENT_CLEAVE         = 4,   // 顺劈斩事件 - 第一、三阶段近战AOE
    EVENT_WING_BUFFET    = 5,   // 翅膀 buffet 事件 - 第一、三阶段击退效果
    EVENT_DEEP_BREATH    = 6,   // 深呼吸事件 - 第二阶段大范围火焰吐息
    EVENT_MOVEMENT       = 7,   // 移动事件 - 第二阶段飞行移动
    EVENT_FIREBALL       = 8,   // 火球事件 - 第二阶段随机目标火球
    EVENT_LAIR_GUARD     = 9,   // 巢穴守卫召唤事件 - 第二阶段召唤守卫
    EVENT_WHELP_SPAWN    = 10   // 幼龙生成事件 - 第二阶段召唤幼龙
};

/**
 * @brief 奥妮克希亚移动数据结构
 * 用于第二阶段飞行移动的路径点信息
 */
struct OnyxMove
{
    uint8 LocId;      // 位置ID，用于标识移动点
    uint8 LocIdEnd;   // 结束位置ID，深呼吸后的目标点
    uint32 SpellId;   // 该位置对应的深呼吸法术ID
    float fX, fY, fZ; // 三维坐标
};

/**
 * @brief 预定义的移动路径数据
 * 定义了奥妮克希亚在第二阶段飞行时的8个可能位置
 * 以及每个位置对应的深呼吸法术方向
 */
static OnyxMove MoveData[8]=
{
    {0, 1, SPELL_BREATH_WEST_TO_EAST,   -33.5561f, -182.682f, -56.9457f}, // 西侧位置
    {1, 0, SPELL_BREATH_EAST_TO_WEST,   -31.4963f, -250.123f, -55.1278f}, // 东侧位置
    {2, 4, SPELL_BREATH_NW_TO_SE,         6.8951f, -180.246f, -55.896f}, // 西北位置
    {3, 5, SPELL_BREATH_NE_TO_SW,        10.2191f, -247.912f, -55.896f}, // 东北位置
    {4, 2, SPELL_BREATH_SE_TO_NW,       -63.5156f, -240.096f, -55.477f}, // 东南位置
    {5, 3, SPELL_BREATH_SW_TO_NE,       -58.2509f, -189.020f, -55.790f}, // 西南位置
    {6, 7, SPELL_BREATH_SOUTH_TO_NORTH, -65.8444f, -213.809f, -55.2985f}, // 南侧位置
    {7, 6, SPELL_BREATH_NORTH_TO_SOUTH,  22.8763f, -217.152f, -55.0548f}, // 北侧位置
};

/**
 * @brief 房间中心位置
 * 用于第二阶段深呼吸后的返回点
 */
Position const MiddleRoomLocation = {-23.6155f, -215.357f, -55.7344f, 0.0f};

/**
 * @brief 第二阶段起飞位置
 * 奥妮克希亚从地面起飞的初始位置
 */
Position const Phase2Location = {-80.924f, -214.299f, -82.942f, 0.0f};

/**
 * @brief 第二阶段悬停位置
 * 奥妮克希亚起飞后的悬停高度
 */
Position const Phase2Floating = { -80.924f, -214.299f, -57.942f, 0.0f };

/**
 * @brief 召唤位置数组
 * [0-1]: 幼龙生成位置
 * [2]: 巢穴守卫生成位置
 */
Position const SpawnLocations[3]=
{
    //Whelps - 幼龙生成位置
    {-30.127f, -254.463f, -89.440f, 0.0f},  // 南侧幼龙位置
    {-30.817f, -177.106f, -89.258f, 0.0f},  // 北侧幼龙位置
    //Lair Guard - 巢穴守卫生成位置
    {-145.950f, -212.831f, -68.659f, 0.0f}  // 守卫位置
};

/**
 * @brief 奥妮克希亚首领AI结构体
 *
 * 实现奥妮克希亚的三阶段战斗AI逻辑：
 * - PHASE_START: 第一阶段，地面战斗，使用火焰吐息、顺劈、尾部横扫等技能
 * - PHASE_BREATH: 第二阶段，飞行阶段，使用深呼吸、火球、召唤幼龙和守卫
 * - PHASE_END: 第三阶段，最终地面战斗，增加咆哮技能和地面喷发
 */
struct boss_onyxia : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_onyxia(Creature* creature) : BossAI(creature, DATA_ONYXIA)
    {
        Initialize();
    }

    /**
     * @brief 初始化成员变量
     *
     * 重置所有战斗状态变量到初始值
     * 调用时机：构造函数、Reset函数
     */
    void Initialize()
    {
        Phase = PHASE_START;         // 初始阶段为地面阶段
        MovePoint = urand(0, 5);     // 随机选择初始移动点
        PointData = GetMoveData();   // 获取移动数据
        SummonWhelpCount = 0;        // 幼龙计数器归零
        triggerGUID.Clear();         // 清空触发器GUID
        tankGUID.Clear();            // 清空坦克GUID
        IsMoving = false;            // 移动状态标志归零
    }

    /**
     * @brief 重置首领状态
     *
     * 当首领脱离战斗或重置时调用，恢复所有状态到初始值
     * 包括：
     * - 重置移动状态
     * - 重置反应状态为主动攻击
     * - 停止计时成就
     */
    void Reset() override
    {
        Initialize();

        // 如果战斗移动被禁用，重新启用
        if (!IsCombatMovementAllowed())
            SetCombatMovement(true);

        _Reset();  // 调用基类重置
        me->SetReactState(REACT_AGGRESSIVE);  // 设置为主动攻击状态
        instance->SetData(DATA_ONYXIA_PHASE, Phase);  // 更新实例数据中的阶段
        instance->DoStopTimedAchievement(ACHIEVEMENT_TIMED_TYPE_EVENT, ACHIEV_TIMED_START_EVENT);  // 停止计时成就
    }

    /**
     * @brief 进入战斗回调
     * @param who 进入战斗的目标
     *
     * 当奥妮克希亚进入战斗时调用：
     * - 触发开战喊话
     * - 调度第一阶段技能
     * - 启动计时成就
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        Talk(SAY_AGGRO);  // 开战喊话
        events.ScheduleEvent(EVENT_FLAME_BREATH, 10s, 20s);   // 火焰吐息，10-20秒后
        events.ScheduleEvent(EVENT_TAIL_SWEEP, 15s, 20s);     // 尾部横扫，15-20秒后
        events.ScheduleEvent(EVENT_CLEAVE, 2s, 5s);           // 顺劈斩，2-5秒后
        events.ScheduleEvent(EVENT_WING_BUFFET, 10s, 20s);    // 翅膀 buffet，10-20秒后
        instance->DoStartTimedAchievement(ACHIEVEMENT_TIMED_TYPE_EVENT, ACHIEV_TIMED_START_EVENT);  // 启动计时成就
    }

    /**
     * @brief 召唤生物回调
     * @param summoned 被召唤的生物
     *
     * 处理奥妮克希亚召唤的幼龙和巢穴守卫：
     * - 让召唤物进入战斗
     * - 随机选择攻击目标
     * - 统计幼龙数量（用于成就判定）
     * - 激活巢穴守卫
     */
    void JustSummoned(Creature* summoned) override
    {
        DoZoneInCombat(summoned);  // 让召唤物进入战斗区域
        if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
            summoned->AI()->AttackStart(target);  // 随机选择攻击目标

        switch (summoned->GetEntry())
        {
            case NPC_WHELP:  // 幼龙
                ++SummonWhelpCount;  // 增加幼龙计数
                break;
            case NPC_LAIRGUARD:  // 巢穴守卫
                summoned->setActive(true);        // 激活守卫
                summoned->SetFarVisible(true);    // 设置远距离可见
                break;
        }
        summons.Summon(summoned);  // 添加到召唤列表
    }

    /**
     * @brief 击杀单位回调
     * @param victim 被击杀的单位（未使用）
     *
     * 当奥妮克希亚击杀玩家时触发击杀喊话
     */
    void KilledUnit(Unit* /*victim*/) override
    {
        Talk(SAY_KILL);  // 击杀喊话
    }

    /**
     * @brief 法术命中回调
     * @param caster 施法者（未使用）
     * @param spellInfo 法术信息
     *
     * 当奥妮克希亚被法术命中时调用
     * 用于处理深呼吸法术命中后的移动逻辑：
     * - 检测深呼吸方向法术
     * - 更新移动点和位置数据
     * - 移动到房间中心
     */
    void SpellHit(WorldObject* /*caster*/, SpellInfo const* spellInfo) override
    {
        // 检查是否为深呼吸方向法术
        if (spellInfo->Id == SPELL_BREATH_EAST_TO_WEST ||
            spellInfo->Id == SPELL_BREATH_WEST_TO_EAST ||
            spellInfo->Id == SPELL_BREATH_SE_TO_NW ||
            spellInfo->Id == SPELL_BREATH_NW_TO_SE ||
            spellInfo->Id == SPELL_BREATH_SW_TO_NE ||
            spellInfo->Id == SPELL_BREATH_NE_TO_SW)
        {
            PointData = GetMoveData();          // 获取新的移动数据
            MovePoint = PointData->LocIdEnd;    // 设置结束点

            me->SetSpeedRate(MOVE_FLIGHT, 1.5f);  // 设置飞行速度倍率为1.5
            me->GetMotionMaster()->MovePoint(8, MiddleRoomLocation);  // 移动到房间中心
        }
    }

    /**
     * @brief 移动完成通知回调
     * @param type 移动类型
     * @param id 移动点ID
     *
     * 处理各种移动完成后的逻辑：
     * - ID 8: 深呼吸后返回中心，准备下一次移动
     * - ID 9: 第三阶段降落，恢复正常战斗状态
     * - ID 10: 第二阶段起飞，进入飞行阶段
     * - ID 11: 起飞完成，开始移动到攻击位置
     * - 默认: 标记移动完成
     */
    void MovementInform(uint32 type, uint32 id) override
    {
        if (type == POINT_MOTION_TYPE)
        {
            switch (id)
            {
                case 8:  // 深呼吸后返回中心点
                    PointData = GetMoveData();
                    if (PointData)
                    {
                        me->SetSpeedRate(MOVE_FLIGHT, 1.0f);  // 恢复正常飞行速度
                        me->GetMotionMaster()->MovePoint(PointData->LocId, PointData->fX, PointData->fY, PointData->fZ);
                    }
                    break;
                case 9:  // 第三阶段降落完成
                    me->SetCanFly(false);           // 禁用飞行
                    me->SetDisableGravity(false);   // 启用重力
                    // 杀死触发器生物
                    if (Creature* trigger = ObjectAccessor::GetCreature(*me, triggerGUID))
                        Unit::Kill(me, trigger);
                    me->SetReactState(REACT_AGGRESSIVE);  // 设置为主动攻击
                    // 选择坦克目标（基于第一阶段的坦克，如果不存在则选择最近的目标）
                    if (Unit* tank = ObjectAccessor::GetUnit(*me, tankGUID))
                        me->GetMotionMaster()->MoveChase(tank);
                    else if (Unit* newtarget = SelectTarget(SelectTargetMethod::MinDistance, 0))
                        me->GetMotionMaster()->MoveChase(newtarget);
                    // 调度第三阶段技能
                    events.ScheduleEvent(EVENT_BELLOWING_ROAR, 5s);
                    events.ScheduleEvent(EVENT_FLAME_BREATH, 10s, 20s);
                    events.ScheduleEvent(EVENT_TAIL_SWEEP, 15s, 20s);
                    events.ScheduleEvent(EVENT_CLEAVE, 2s, 5s);
                    events.ScheduleEvent(EVENT_WING_BUFFET, 15s, 30s);
                    break;
                case 10:  // 第二阶段起飞开始
                    me->SetCanFly(true);           // 启用飞行
                    me->SetDisableGravity(true);   // 禁用重力
                    me->SetFacingTo(me->GetOrientation() + float(M_PI));  // 转向
                    // 召唤触发器生物
                    if (Creature * trigger = me->SummonCreature(NPC_TRIGGER, MiddleRoomLocation, TEMPSUMMON_CORPSE_DESPAWN))
                        triggerGUID = trigger->GetGUID();
                    me->GetMotionMaster()->MoveTakeoff(11, Phase2Floating);  // 执行起飞动画
                    me->SetSpeedRate(MOVE_FLIGHT, 1.0f);
                    Talk(SAY_PHASE_2_TRANS);  // 第二阶段转换喊话
                    instance->SetData(DATA_ONYXIA_PHASE, Phase);
                    // 调度第二阶段技能
                    events.ScheduleEvent(EVENT_WHELP_SPAWN, 5s);
                    events.ScheduleEvent(EVENT_LAIR_GUARD, 15s);
                    events.ScheduleEvent(EVENT_DEEP_BREATH, 75s);
                    events.ScheduleEvent(EVENT_MOVEMENT, 10s);
                    events.ScheduleEvent(EVENT_FIREBALL, 18s);
                    break;
                case 11:  // 起飞完成
                    if (PointData)
                        me->GetMotionMaster()->MovePoint(PointData->LocId, PointData->fX, PointData->fY, PointData->fZ);
                    me->GetMotionMaster()->MoveIdle();  // 进入空闲移动状态
                    break;
                default:  // 其他移动点
                    IsMoving = false;  // 标记移动完成
                    break;
            }
        }
    }

    /**
     * @brief 法术命中目标回调
     * @param target 法术命中目标
     * @param spellInfo 法术信息
     *
     * 用于判定深呼吸成就（She Deep Breaths More）
     * 如果深呼吸命中任何玩家，则成就失败
     */
    void SpellHitTarget(WorldObject* target, SpellInfo const* spellInfo) override
    {
        //Workaround - Couldn't find a way to group this spells (All Eruption)
        // 检查是否为深呼吸相关法术（地面喷发效果）
        if (((spellInfo->Id >= 17086 && spellInfo->Id <= 17095) ||
            (spellInfo->Id == 17097) ||
            (spellInfo->Id >= 18351 && spellInfo->Id <= 18361) ||
            (spellInfo->Id >= 18564 && spellInfo->Id <= 18576) ||
            (spellInfo->Id >= 18578 && spellInfo->Id <= 18607) ||
            (spellInfo->Id == 18609) ||
            (spellInfo->Id >= 18611 && spellInfo->Id <= 18628) ||
            (spellInfo->Id >= 21132 && spellInfo->Id <= 21133) ||
            (spellInfo->Id >= 21135 && spellInfo->Id <= 21139) ||
            (spellInfo->Id >= 22191 && spellInfo->Id <= 22202) ||
            (spellInfo->Id >= 22267 && spellInfo->Id <= 22268)) &&
            (target->GetTypeId() == TYPEID_PLAYER))  // 命中玩家
        {
            instance->SetData(DATA_SHE_DEEP_BREATH_MORE, FAIL);  // 成就失败
        }
    }

    /**
     * @brief 获取移动数据
     * @return 对应移动点的移动数据指针，如果未找到则返回nullptr
     *
     * 根据当前移动点ID查找对应的移动数据
     * 用于第二阶段飞行移动
     */
    OnyxMove* GetMoveData()
    {
        uint8 MaxCount = sizeof(MoveData) / sizeof(OnyxMove);

        for (uint8 i = 0; i < MaxCount; ++i)
        {
            if (MoveData[i].LocId == MovePoint)
                return &MoveData[i];
        }

        return nullptr;
    }

    /**
     * @brief 设置下一个随机移动点
     *
     * 随机选择一个新的移动点，避免选择当前点
     * 用于第二阶段飞行移动
     */
    void SetNextRandomPoint()
    {
        uint8 MaxCount = sizeof(MoveData) / sizeof(OnyxMove);

        uint8 iTemp = urand(0, MaxCount - 1);

        // 如果随机结果大于等于当前点，则加1以避免重复
        if (iTemp >= MovePoint)
            ++iTemp;

        MovePoint = iTemp;
    }

    /**
     * @brief 更新AI主循环
     * @param diff 距离上次更新的时间差（毫秒）
     *
     * 核心AI逻辑，根据当前阶段执行不同的战斗策略：
     *
     * 第一阶段（PHASE_START）：
     * - 地面战斗
     * - 使用火焰吐息、顺劈、尾部横扫、翅膀 buffet
     * - 血量低于65%时进入第二阶段
     *
     * 第二阶段（PHASE_BREATH）：
     * - 空中飞行
     * - 使用深呼吸、火球
     * - 召唤幼龙和巢穴守卫
     * - 血量低于40%时进入第三阶段
     *
     * 第三阶段（PHASE_END）：
     * - 地面战斗
     * - 使用第一阶段技能加咆哮
     * - 触发地面喷发效果
     *
     * 性能注意事项：
     * - 使用事件调度器优化技能计时
     * - 避免在施法状态下执行不必要的操作
     */
    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        //Common to PHASE_START && PHASE_END
        // 第一阶段和第三阶段的通用逻辑（地面战斗）
        if (Phase == PHASE_START || Phase == PHASE_END)
        {
            //Specific to PHASE_START || PHASE_END
            // 第一阶段特有：检查是否进入第二阶段
            if (Phase == PHASE_START)
            {
                // 血量低于65%时开始转换到第二阶段
                if (HealthBelowPct(65))
                {
                    // 记录当前坦克，用于第三阶段降落后的目标选择
                    if (Unit* target = me->GetVictim())
                        tankGUID = target->GetGUID();
                    SetCombatMovement(false);         // 禁用战斗移动
                    Phase = PHASE_BREATH;             // 切换到飞行阶段
                    me->SetReactState(REACT_PASSIVE); // 设置为被动反应
                    me->AttackStop();                 // 停止攻击
                    me->GetMotionMaster()->MovePoint(10, Phase2Location);  // 移动到起飞位置
                    return;
                }
            }

            events.Update(diff);

            // 如果正在施法，等待施法完成
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;

            // 处理事件队列
            while (uint32 eventId = events.ExecuteEvent())
            {
                switch (eventId)
                {
                    case EVENT_BELLOWING_ROAR: // Phase PHASE_END - 第三阶段咆哮
                    {
                        DoCastVictim(SPELL_BELLOWING_ROAR);  // 施放咆哮（范围恐惧）
                        // Eruption - 触发地面喷发效果
                        GameObject* Floor = nullptr;
                        Trinity::GameObjectInRangeCheck check(me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), 15);
                        Trinity::GameObjectLastSearcher<Trinity::GameObjectInRangeCheck> searcher(me, Floor, check);
                        Cell::VisitGridObjects(me, searcher, 30.0f);  // 搜索15码范围内的游戏对象
                        if (Floor)
                            instance->SetGuidData(DATA_FLOOR_ERUPTION_GUID, Floor->GetGUID());  // 触发地面喷发
                        events.ScheduleEvent(EVENT_BELLOWING_ROAR, 30s);
                        break;
                    }
                    case EVENT_FLAME_BREATH:   // Phase PHASE_START and PHASE_END - 火焰吐息
                        DoCastVictim(SPELL_FLAME_BREATH);
                        events.ScheduleEvent(EVENT_FLAME_BREATH, 10s, 20s);
                        break;
                    case EVENT_TAIL_SWEEP:     // Phase PHASE_START and PHASE_END - 尾部横扫
                        DoCastAOE(SPELL_TAIL_SWEEP);
                        events.ScheduleEvent(EVENT_TAIL_SWEEP, 15s, 20s);
                        break;
                    case EVENT_CLEAVE:         // Phase PHASE_START and PHASE_END - 顺劈斩
                        DoCastVictim(SPELL_CLEAVE);
                        events.ScheduleEvent(EVENT_CLEAVE, 2s, 5s);
                        break;
                    case EVENT_WING_BUFFET:    // Phase PHASE_START and PHASE_END - 翅膀 buffet
                        DoCastVictim(SPELL_WING_BUFFET);
                        events.ScheduleEvent(EVENT_WING_BUFFET, 15s, 30s);
                        break;
                    default:
                        break;
                }

                // 如果开始施法，退出事件循环
                if (me->HasUnitState(UNIT_STATE_CASTING))
                    return;
            }
            DoMeleeAttackIfReady();  // 执行近战攻击
        }
        else  // 第二阶段（飞行阶段）
        {
            // 血量低于40%时转换到第三阶段
            if (HealthBelowPct(40))
            {
                Phase = PHASE_END;
                instance->SetData(DATA_ONYXIA_PHASE, PHASE_END);
                Talk(SAY_PHASE_3_TRANS);           // 第三阶段转换喊话
                SetCombatMovement(true);           // 启用战斗移动
                IsMoving = false;
                Position const pos = me->GetHomePosition();
                me->GetMotionMaster()->MovePoint(9, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ() + 12.0f);  // 降落到初始位置
                events.ScheduleEvent(EVENT_BELLOWING_ROAR, 30s);
                return;
            }

            // 如果未在移动，面向触发器（用于深呼吸定向）
            if (!me->isMoving())
                if (Creature* trigger = ObjectAccessor::GetCreature(*me, triggerGUID))
                    me->SetFacingToObject(trigger);

            events.Update(diff);

            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;

            while (uint32 eventId = events.ExecuteEvent())
            {
                switch (eventId)
                {
                    case EVENT_DEEP_BREATH:      // Phase PHASE_BREATH - 深呼吸
                        if (!IsMoving)
                        {
                            // 中断非近战法术
                            if (me->IsNonMeleeSpellCast(false))
                                me->InterruptNonMeleeSpells(false);

                            Talk(EMOTE_BREATH);  // 深呼吸表情
                            if (PointData) /// @todo: In what cases is this null? What should we do?
                                DoCast(me, PointData->SpellId);  // 施放深呼吸法术
                            events.ScheduleEvent(EVENT_DEEP_BREATH, 75s);
                        }
                        else
                            events.ScheduleEvent(EVENT_DEEP_BREATH, 1s);  // 如果在移动，延迟1秒再试
                        break;
                    case EVENT_MOVEMENT:         // Phase PHASE_BREATH - 飞行移动
                        if (!IsMoving && !(me->HasUnitState(UNIT_STATE_CASTING)))
                        {
                            SetNextRandomPoint();  // 选择下一个随机移动点
                            PointData = GetMoveData();

                            if (!PointData)
                                return;

                            me->GetMotionMaster()->MovePoint(PointData->LocId, PointData->fX, PointData->fY, PointData->fZ);
                            IsMoving = true;
                            events.ScheduleEvent(EVENT_MOVEMENT, 25s);
                        }
                        else
                            events.ScheduleEvent(EVENT_MOVEMENT, 500ms);  // 如果在移动或施法，延迟0.5秒再试
                        break;
                    case EVENT_FIREBALL:         // Phase PHASE_BREATH - 火球
                        if (!IsMoving)
                        {
                            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                                DoCast(target, SPELL_FIREBALL);
                            events.ScheduleEvent(EVENT_FIREBALL, 8s);
                        }
                        else
                            events.ScheduleEvent(EVENT_FIREBALL, 1s);
                        break;
                    case EVENT_LAIR_GUARD:       // Phase PHASE_BREATH - 召唤巢穴守卫
                        me->SummonCreature(NPC_LAIRGUARD, SpawnLocations[2], TEMPSUMMON_CORPSE_DESPAWN);
                        events.ScheduleEvent(EVENT_LAIR_GUARD, 30s);
                        break;
                    case EVENT_WHELP_SPAWN:      // Phase PHASE_BREATH - 召唤幼龙
                        me->SummonCreature(NPC_WHELP, SpawnLocations[0], TEMPSUMMON_CORPSE_DESPAWN);  // 南侧幼龙
                        me->SummonCreature(NPC_WHELP, SpawnLocations[1], TEMPSUMMON_CORPSE_DESPAWN);  // 北侧幼龙
                        // 检查是否达到幼龙数量上限（10人模式20只，25人模式40只）
                        if (SummonWhelpCount >= RAID_MODE(20, 40))
                        {
                            SummonWhelpCount = 0;
                            events.ScheduleEvent(EVENT_WHELP_SPAWN, 90s);  // 批次召唤完成，90秒后重新开始
                        }
                        else
                            events.ScheduleEvent(EVENT_WHELP_SPAWN, 500ms);  // 继续召唤，间隔0.5秒
                        break;
                    default:
                        break;
                }

                if (me->HasUnitState(UNIT_STATE_CASTING))
                    return;
            }
        }
    }

    private:
        OnyxMove* PointData;        // 当前移动点数据指针
        uint8 Phase;                // 当前战斗阶段
        uint8 MovePoint;            // 当前移动点ID
        uint8 SummonWhelpCount;     // 已召唤的幼龙数量计数器
        ObjectGuid triggerGUID;     // 触发器生物GUID（用于深呼吸定向）
        ObjectGuid tankGUID;        // 坦克玩家GUID（用于第三阶段降落后的目标选择）
        bool IsMoving;              // 是否正在移动的标志
};

void AddSC_boss_onyxia()
{
    RegisterOnyxiasLairCreatureAI(boss_onyxia);
}
