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
 * @file boss_flame_leviathan.cpp
 * @brief 烈焰巨兽（Flame Leviathan）Boss 战斗脚本模块
 *
 * 模块职责：
 * 实现奥杜尔副本中烈焰巨兽 Boss 的完整战斗逻辑，包括：
 * - 烈焰巨兽的主要战斗 AI 和技能系统
 * - 载具战斗机制（攻城器械、石毁车、机车）
 * - 四座防御塔的困难模式机制（风暴、火焰、冰霜、生命之塔）
 * - 追击系统和导弹弹幕
 * - 系统关闭和修复循环
 * - 各种载具和 NPC 的 AI（座位、机械升降机、信标等）
 * - 成就系统（Shutout、Unbroken、Orbit 等）
 *
 * 注意事项：
 * - 区域触发器（Area Triggers）代码尚不完整
 * - 布莱恩·铜须的对话系统需要进一步完善
 * - 当玩家到达特定位置时，布莱恩会通过无线电说话
 */

#include "ScriptMgr.h"
#include "CellImpl.h"
#include "CombatAI.h"
#include "Containers.h"
#include "GameObjectAI.h"
#include "GridNotifiersImpl.h"
#include "InstanceScript.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "PassiveAI.h"
#include "ScriptedEscortAI.h"
#include "ScriptedGossip.h"
#include "Spell.h"
#include "SpellAuraEffects.h"
#include "SpellInfo.h"
#include "SpellScript.h"
#include "ulduar.h"
#include "Vehicle.h"

/**
 * @brief 法术 ID 枚举定义
 *
 * 定义烈焰巨兽战斗中使用的所有法术 ID
 */
enum Spells
{
    // 主要战斗技能
    SPELL_PURSUED                  = 62374,  ///< 追击标记 - 标记被追击的目标
    SPELL_GATHERING_SPEED          = 62375,  ///< 加速 - 提高移动速度
    SPELL_BATTERING_RAM            = 62376,  ///< 攻城锤 - 对载具造成伤害并击退
    SPELL_FLAME_VENTS              = 62396,  ///< 烈焰喷发 - 对前方锥形区域造成火焰伤害
    SPELL_MISSILE_BARRAGE          = 62400,  ///< 导弹弹幕 - 持续发射导弹攻击随机目标
    SPELL_SYSTEMS_SHUTDOWN         = 62475,  ///< 系统关闭 - 使烈焰巨兽瘫痪
    SPELL_OVERLOAD_CIRCUIT         = 62399,  ///< 电路过载 - 由载具造成的过载效果
    SPELL_START_THE_ENGINE         = 62472,  ///< 启动引擎 - 战斗开始时施放
    SPELL_SEARING_FLAME            = 62402,  ///< 灼热烈焰 - 对目标造成火焰伤害
    SPELL_BLAZE                    = 62292,  ///< 火焰燃烧 - 地面火焰效果
    SPELL_TAR_PASSIVE              = 62288,  ///< 沥青被动 - 减速效果
    SPELL_SMOKE_TRAIL              = 63575,  ///< 烟雾轨迹 - 视觉效果
    SPELL_ELECTROSHOCK             = 62522,  ///< 电击 - 打断施法
    SPELL_NAPALM                   = 63666,  ///< 凝固汽油弹 - 区域火焰伤害
    SPELL_INVIS_AND_STEALTH_DETECT = 18950,  ///< 被动技能 - 潜行和隐形检测

    // 防御塔相关技能（困难模式）
    SPELL_THORIM_S_HAMMER          = 62911,  ///< 风暴之塔 - 托里姆之锤
    SPELL_MIMIRON_S_INFERNO        = 62909,  ///< 火焰之塔 - 米米尔隆的炼狱
    SPELL_HODIR_S_FURY             = 62533,  ///< 冰霜之塔 - 霍迪尔的愤怒
    SPELL_FREYA_S_WARD             = 62906,  ///< 生命之塔 - 弗雷亚的守护
    SPELL_FREYA_SUMMONS            = 62947,  ///< 生命之塔 - 弗雷亚召唤

    // 防御塔增益效果（提高攻击强度和生命值）
    SPELL_BUFF_TOWER_OF_STORMS     = 65076,  ///< 风暴之塔增益
    SPELL_BUFF_TOWER_OF_FLAMES     = 65075,  ///< 火焰之塔增益
    SPELL_BUFF_TOWER_OF_FR0ST      = 65077,  ///< 冰霜之塔增益
    SPELL_BUFF_TOWER_OF_LIFE       = 64482,  ///< 生命之塔增益

    // 其他技能
    SPELL_LASH                     = 65062,  ///< 鞭笞
    SPELL_FREYA_S_WARD_EFFECT_1    = 62947,  ///< 弗雷亚守护效果 1
    SPELL_FREYA_S_WARD_EFFECT_2    = 62907,  ///< 弗雷亚守护效果 2
    SPELL_AUTO_REPAIR              = 62705,  ///< 自动修复 - 载具修复技能
    AURA_DUMMY_BLUE                = 63294,  ///< 蓝色视觉效果
    AURA_DUMMY_GREEN               = 63295,  ///< 绿色视觉效果
    AURA_DUMMY_YELLOW              = 63292,  ///< 黄色视觉效果
    SPELL_LIQUID_PYRITE            = 62494,  ///< 液态黄铁 - 液体黄铁矿效果
    SPELL_DUSTY_EXPLOSION          = 63360,  ///< 灰尘爆炸
    SPELL_DUST_CLOUD_IMPACT        = 54740,  ///< 灰尘云冲击
    AURA_STEALTH_DETECTION         = 18950,  ///< 潜行检测光环
    SPELL_RIDE_VEHICLE             = 46598,  ///< 骑乘坐骑
};

/**
 * @brief 生物 NPC ID 枚举定义
 *
 * 定义烈焰巨兽战斗中涉及的所有 NPC ID
 */
enum Creatures
{
    NPC_SEAT                       = 33114,  ///< 座位 NPC - 载具的座位
    NPC_MECHANOLIFT                = 33214,  ///< 机械升降机 - 运输液体黄铁矿
    NPC_LIQUID                     = 33189,  ///< 液体 - 液态黄铁矿
    NPC_CONTAINER                  = 33218,  ///< 容器 - 黄铁矿容器
    NPC_THORIM_BEACON              = 33365,  ///< 托里姆信标 - 风暴之塔
    NPC_MIMIRON_BEACON             = 33370,  ///< 米米尔隆信标 - 火焰之塔
    NPC_HODIR_BEACON               = 33212,  ///< 霍迪尔信标 - 冰霜之塔
    NPC_FREYA_BEACON               = 33367,  ///< 弗雷亚信标 - 生命之塔
    NPC_THORIM_TARGET_BEACON       = 33364,  ///< 托里姆目标信标
    NPC_MIMIRON_TARGET_BEACON      = 33369,  ///< 米米尔隆目标信标
    NPC_HODIR_TARGET_BEACON        = 33108,  ///< 霍迪尔目标信标
    NPC_FREYA_TARGET_BEACON        = 33366,  ///< 弗雷亚目标信标
    NPC_ULDUAR_GAUNTLET_GENERATOR  = 33571,  ///< 奥杜尔挑战生成器 - 与防御塔相关的触发器
};

/**
 * @brief 防御塔游戏对象 ID 枚举定义
 *
 * 定义四座防御塔的游戏对象 ID，这些防御塔决定困难模式
 */
enum Towers
{
    GO_TOWER_OF_STORMS    = 194377,  ///< 风暴之塔 - 增加烈焰巨兽的伤害
    GO_TOWER_OF_FLAMES    = 194371,  ///< 火焰之塔 - 增加火焰伤害
    GO_TOWER_OF_FROST     = 194370,  ///< 冰霜之塔 - 增加冰霜伤害
    GO_TOWER_OF_LIFE      = 194375,  ///< 生命之塔 - 增加生命值并召唤小怪
};

/**
 * @brief 事件 ID 枚举定义
 *
 * 定义战斗中使用的所有事件 ID，用于事件调度系统
 */
enum Events
{
    EVENT_PURSUE               = 1,   ///< 追击事件 - 选择并追击目标
    EVENT_MISSILE              = 2,   ///< 导弹事件 - 发射导弹弹幕
    EVENT_VENT                 = 3,   ///< 烈焰喷发事件 - 施放烈焰喷发
    EVENT_SPEED                = 4,   ///< 加速事件 - 提高移动速度
    EVENT_SUMMON               = 5,   ///< 召唤事件 - 召唤机械升降机
    EVENT_SHUTDOWN             = 6,   ///< 系统关闭事件 - 烈焰巨兽瘫痪
    EVENT_REPAIR               = 7,   ///< 修复事件 - 烈焰巨兽修复完成
    EVENT_THORIM_S_HAMMER      = 8,   ///< 托里姆之锤事件 - 风暴之塔技能
    EVENT_MIMIRON_S_INFERNO    = 9,   ///< 米米尔隆炼狱事件 - 火焰之塔技能
    EVENT_HODIR_S_FURY         = 10,  ///< 霍迪尔愤怒事件 - 冰霜之塔技能
    EVENT_FREYA_S_WARD         = 11,  ///< 弗雷亚守护事件 - 生命之塔技能
};

/**
 * @brief 座位索引枚举定义
 *
 * 定义载具中不同座位的索引位置
 */
enum Seats
{
    SEAT_PLAYER    = 0,  ///< 玩家座位 - 主要驾驶位
    SEAT_TURRET    = 1,  ///< 炮塔座位 - 操作炮塔
    SEAT_DEVICE    = 2,  ///< 装置座位 - 操作特殊装置
    SEAT_CANNON    = 7,  ///< 火炮座位 - 操作火炮
};

/**
 * @brief 载具 ID 枚举定义
 *
 * 定义战斗中使用的三种载具 ID
 */
enum Vehicles
{
    VEHICLE_SIEGE         = 33060,  ///< 攻城器械 - 破坏者号，主要攻击载具
    VEHICLE_CHOPPER       = 33062,  ///< 机车 - 摩托车，快速机动载具
    VEHICLE_DEMOLISHER    = 33109,  ///< 石毁车 - 攻城坦克，远程攻击载具
};

/**
 * @brief 杂项数据枚举定义
 *
 * 定义成就数据、生成数量等杂项常量
 */
enum Misc
{
    DATA_SHUTOUT               = 29112912,  ///< Shutout 成就数据 (2911, 2912 是成就 ID)
    DATA_ORBIT_ACHIEVEMENTS    = 1,          ///< Orbit 成就数据
    VEHICLE_SPAWNS             = 5,          ///< 载具生成数量
    FREYA_SPAWNS               = 4           ///< 弗雷亚信标生成数量
};

/**
 * @brief 对话和喊叫枚举定义
 *
 * 定义烈焰巨兽在战斗中的各种对话和表情文本 ID
 */
enum Yells
{
    SAY_AGGRO            = 0,   ///< 开怪对话
    SAY_SLAY             = 1,   ///< 击杀玩家对话
    SAY_DEATH            = 2,   ///< 死亡对话
    SAY_TARGET           = 3,   ///< 选择目标对话
    SAY_HARDMODE         = 4,   ///< 困难模式对话
    SAY_TOWER_NONE       = 5,   ///< 无防御塔对话
    SAY_TOWER_FROST      = 6,   ///< 冰霜之塔对话
    SAY_TOWER_FLAME      = 7,   ///< 火焰之塔对话
    SAY_TOWER_NATURE     = 8,   ///< 生命之塔对话
    SAY_TOWER_STORM      = 9,   ///< 风暴之塔对话
    SAY_PLAYER_RIDING    = 10,  ///< 玩家骑乘对话
    SAY_OVERLOAD         = 11,  ///< 过载对话
    EMOTE_PURSUE         = 12,  ///< 追击表情
    EMOTE_OVERLOAD       = 13,  ///< 过载表情
    EMOTE_REPAIR         = 14   ///< 修复表情
};

/**
 * @brief 杂项数据枚举定义
 *
 * 定义动作 ID、座位数量等杂项数据
 */
enum MiscellanousData
{
    // 其他动作在 Ulduar.h 中定义
    ACTION_START_HARD_MODE    = 5,  ///< 开始困难模式动作
    ACTION_SPAWN_VEHICLES     = 6,  ///< 生成载具动作
    // 根据团队模式确定的座位数量
    TWO_SEATS                 = 2,  ///< 2 个座位（10 人模式）
    FOUR_SEATS                = 4,  ///< 4 个座位（25 人模式）
};

/**
 * @brief 场地位置常量定义
 *
 * 定义战斗场地中的关键位置坐标
 */

/// 战场中心位置 - 烈焰巨兽的主要活动区域
Position const Center = { 354.8771f, -12.90240f, 409.803650f, 0.0f };

/// 炼狱起始位置 - 米米尔隆炼狱技能的起始点
Position const InfernoStart = { 390.93f, -13.91f, 409.81f, 0.0f };

/**
 * @brief 攻城器械生成位置数组
 *
 * 定义 5 辆攻城器械（破坏者号）的生成位置和朝向
 */
Position const PosSiege[VEHICLE_SPAWNS] =
{
    {-814.59f, -64.54f, 429.92f, 5.969f},
    {-784.37f, -33.31f, 429.92f, 5.096f},
    {-808.99f, -52.10f, 429.92f, 5.668f},
    {-798.59f, -44.00f, 429.92f, 5.663f},
    {-812.83f, -77.71f, 429.92f, 0.046f},
};

/**
 * @brief 机车生成位置数组
 *
 * 定义 5 辆机车（摩托车）的生成位置和朝向
 */
Position const PosChopper[VEHICLE_SPAWNS] =
{
    {-717.83f, -106.56f, 430.02f, 0.122f},
    {-717.83f, -114.23f, 430.44f, 0.122f},
    {-717.83f, -109.70f, 430.22f, 0.122f},
    {-718.45f, -118.24f, 430.26f, 0.052f},
    {-718.45f, -123.58f, 430.41f, 0.085f},
};

/**
 * @brief 石毁车生成位置数组
 *
 * 定义 5 辆石毁车（攻城坦克）的生成位置和朝向
 */
Position const PosDemolisher[VEHICLE_SPAWNS] =
{
    {-724.12f, -176.64f, 430.03f, 2.543f},
    {-766.70f, -225.03f, 430.50f, 1.710f},
    {-729.54f, -186.26f, 430.12f, 1.902f},
    {-756.01f, -219.23f, 430.50f, 2.369f},
    {-798.01f, -227.24f, 429.84f, 1.446f},
};

/**
 * @brief 弗雷亚信标生成位置数组
 *
 * 定义生命之塔激活时的 4 个弗雷亚信标生成位置
 */
Position const FreyaBeacons[FREYA_SPAWNS] =
{
    {377.02f, -119.10f, 409.81f, 0.0f},
    {185.62f, -119.10f, 409.81f, 0.0f},
    {377.02f, 54.78f, 409.81f, 0.0f},
    {185.62f, 54.78f, 409.81f, 0.0f},
};

/**
 * @class boss_flame_leviathan
 * @brief 烈焰巨兽生物脚本类
 *
 * 继承自 CreatureScript，负责注册烈焰巨兽 Boss 的 AI 脚本
 */
class boss_flame_leviathan : public CreatureScript
{
    public:
        /**
         * @brief 构造函数
         *
         * 注册 "boss_flame_leviathan" 脚本名称
         */
        boss_flame_leviathan() : CreatureScript("boss_flame_leviathan") { }

        /**
         * @struct boss_flame_leviathanAI
         * @brief 烈焰巨兽 AI 结构
         *
         * 继承自 BossAI，实现烈焰巨兽的核心战斗逻辑
         */
        struct boss_flame_leviathanAI : public BossAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物对象指针
             */
            boss_flame_leviathanAI(Creature* creature) : BossAI(creature, DATA_FLAME_LEVIATHAN)
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
                ActiveTowersCount = 4;  // 默认 4 座塔激活
                Shutdown = 0;           // 关闭计数器归零
                ActiveTowers = false;   // 防御塔是否激活
                towerOfStorms = false;  // 风暴之塔状态
                towerOfLife = false;    // 生命之塔状态
                towerOfFlames = false;  // 火焰之塔状态
                towerOfFrost = false;   // 冰霜之塔状态
                Shutout = true;         // Shutout 成就标志
                Unbroken = true;        // Unbroken 成就标志
            }

            /**
             * @brief 初始化 AI
             *
             * 在生物生成时调用，设置初始状态和标志
             */
            void InitializeAI() override
            {
                if (!me->isDead())
                    Reset();

                Initialize();

                // 施放潜行检测被动技能
                DoCast(SPELL_INVIS_AND_STEALTH_DETECT);

                // 设置为不可攻击、不可交互、眩晕状态
                me->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_UNINTERACTIBLE | UNIT_FLAG_STUNNED);
                me->SetReactState(REACT_PASSIVE);
            }

            // 成员变量定义
            uint8 ActiveTowersCount;  ///< 激活的防御塔数量
            uint8 Shutdown;           ///< 系统关闭计数器
            bool ActiveTowers;        ///< 是否启用防御塔困难模式
            bool towerOfStorms;       ///< 风暴之塔是否激活
            bool towerOfLife;         ///< 生命之塔是否激活
            bool towerOfFlames;       ///< 火焰之塔是否激活
            bool towerOfFrost;        ///< 冰霜之塔是否激活
            bool Shutout;             ///< Shutout 成就标志（无关闭）
            bool Unbroken;            ///< Unbroken 成就标志（无载具被毁）

            /**
             * @brief 重置战斗状态
             *
             * 在战斗结束或重置时调用，清理战斗数据
             */
            void Reset() override
            {
                _Reset();
                // 重置关闭计数器为 0（根据团队模式为 2 或 4）
                Shutdown = 0;
                _pursueTarget.Clear();

                me->SetReactState(REACT_DEFENSIVE);
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
                me->SetReactState(REACT_PASSIVE);

                // 调度战斗事件
                events.ScheduleEvent(EVENT_PURSUE, 1ms);          // 追击目标
                events.ScheduleEvent(EVENT_MISSILE, 1500ms, 4s);  // 导弹弹幕
                events.ScheduleEvent(EVENT_VENT, 20s);            // 烈焰喷发
                events.ScheduleEvent(EVENT_SHUTDOWN, 150s);       // 系统关闭
                events.ScheduleEvent(EVENT_SPEED, 15s);           // 加速
                events.ScheduleEvent(EVENT_SUMMON, 1s);           // 召唤机械升降机

                ActiveTower(); // 激活防御塔
            }

            /**
             * @brief 激活防御塔困难模式
             *
             * 根据激活的防御塔数量，施放相应的增益和调度技能事件
             */
            void ActiveTower()
            {
                if (ActiveTowers)
                {
                    // 风暴之塔 - 增加伤害并召唤托里姆之锤
                    if (towerOfStorms)
                    {
                        me->AddAura(SPELL_BUFF_TOWER_OF_STORMS, me);
                        events.ScheduleEvent(EVENT_THORIM_S_HAMMER, 35s);
                    }

                    // 火焰之塔 - 增加火焰伤害并召唤炼狱
                    if (towerOfFlames)
                    {
                        me->AddAura(SPELL_BUFF_TOWER_OF_FLAMES, me);
                        events.ScheduleEvent(EVENT_MIMIRON_S_INFERNO, 70s);
                    }

                    // 冰霜之塔 - 增加冰霜伤害并召唤霍迪尔的愤怒
                    if (towerOfFrost)
                    {
                        me->AddAura(SPELL_BUFF_TOWER_OF_FR0ST, me);
                        events.ScheduleEvent(EVENT_HODIR_S_FURY, 105s);
                    }

                    // 生命之塔 - 增加生命值并召唤弗雷亚的守护
                    if (towerOfLife)
                    {
                        me->AddAura(SPELL_BUFF_TOWER_OF_LIFE, me);
                        events.ScheduleEvent(EVENT_FREYA_S_WARD, 140s);
                    }

                    // 根据激活的塔播放对话
                    if (!towerOfLife && !towerOfFrost && !towerOfFlames && !towerOfStorms)
                        Talk(SAY_TOWER_NONE);  // 无防御塔
                    else
                        Talk(SAY_HARDMODE);     // 困难模式
                }
                else
                    Talk(SAY_AGGRO);  // 普通模式开怪对话
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
             * @brief 法术命中处理
             * @param caster 施法者（未使用）
             * @param spellInfo 法术信息
             *
             * 当法术命中烈焰巨兽时调用，处理特定法术效果
             */
            void SpellHit(WorldObject* /*caster*/, SpellInfo const* spellInfo) override
            {
                // 启动引擎 - 安装载具附件
                if (spellInfo->Id == SPELL_START_THE_ENGINE)
                    if (Vehicle* vehicleKit = me->GetVehicleKit())
                        vehicleKit->InstallAllAccessories(false);

                // 电击 - 打断引导法术
                if (spellInfo->Id == SPELL_ELECTROSHOCK)
                    me->InterruptSpell(CURRENT_CHANNELED_SPELL);

                // 电路过载 - 增加关闭计数器
                if (spellInfo->Id == SPELL_OVERLOAD_CIRCUIT)
                    ++Shutdown;
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
                switch (type)
                {
                    case DATA_SHUTOUT:
                        return Shutout ? 1 : 0;  // 返回 Shutout 成就状态
                    case DATA_UNBROKEN:
                        return Unbroken ? 1 : 0;  // 返回 Unbroken 成就状态
                    case DATA_ORBIT_ACHIEVEMENTS:
                        if (ActiveTowers) // 仅在困难模式下
                            return ActiveTowersCount;  // 返回激活的防御塔数量
                        break;
                    default:
                        break;
                }

                return 0;
            }

            /**
             * @brief 设置数据
             * @param id 数据 ID
             * @param data 数据值
             *
             * 用于设置成就相关的数据
             */
            void SetData(uint32 id, uint32 data) override
            {
                if (id == DATA_UNBROKEN)
                    Unbroken = data ? true : false;  // 设置 Unbroken 成就状态
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
                // 如果未进入战斗，直接返回
                if (!me->IsEngaged())
                    return;

                // 如果没有战斗目标，进入逃避模式
                if (!me->IsInCombat())
                {
                    EnterEvadeMode(EVADE_REASON_NO_HOSTILES);
                    return;
                }

                // 更新事件队列
                events.Update(diff);

                // 检查是否达到关闭阈值（根据团队模式 2 或 4）
                if (Shutdown == RAID_MODE(TWO_SEATS, FOUR_SEATS))
                {
                    Shutdown = 0;
                    events.ScheduleEvent(EVENT_SHUTDOWN, 4s);
                    me->RemoveAurasDueToSpell(SPELL_OVERLOAD_CIRCUIT);
                    me->InterruptNonMeleeSpells(true);
                    return;
                }

                // 如果正在施法，暂停其他动作
                if (me->HasUnitState(UNIT_STATE_CASTING))
                    return;

                // 处理事件队列
                while (uint32 eventId = events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_PURSUE:  // 追击目标
                            Talk(SAY_TARGET);
                            DoCast(SPELL_PURSUED);  // 在法术脚本中选择目标
                            events.ScheduleEvent(EVENT_PURSUE, 35s);
                            break;
                        case EVENT_MISSILE:  // 导弹弹幕
                            DoCast(me, SPELL_MISSILE_BARRAGE, true);
                            events.ScheduleEvent(EVENT_MISSILE, 2s);
                            break;
                        case EVENT_VENT:  // 烈焰喷发
                            DoCastAOE(SPELL_FLAME_VENTS);
                            events.ScheduleEvent(EVENT_VENT, 20s);
                            break;
                        case EVENT_SPEED:  // 加速
                            DoCastAOE(SPELL_GATHERING_SPEED);
                            events.ScheduleEvent(EVENT_SPEED, 15s);
                            break;
                        case EVENT_SUMMON:  // 召唤机械升降机
                            if (summons.size() < 15)
                                if (Creature* lift = DoSummonFlyer(NPC_MECHANOLIFT, me, 30.0f, 50.0f, 0s))
                                    lift->GetMotionMaster()->MoveRandom(100);
                            events.ScheduleEvent(EVENT_SUMMON, 2s);
                            break;
                        case EVENT_SHUTDOWN:  // 系统关闭
                            Talk(SAY_OVERLOAD);
                            Talk(EMOTE_OVERLOAD);
                            me->CastSpell(me, SPELL_SYSTEMS_SHUTDOWN, true);
                            if (Shutout)
                                Shutout = false;  // 失去 Shutout 成就
                            events.ScheduleEvent(EVENT_REPAIR, 4s);
                            events.DelayEvents(20s, 0);
                            break;
                        case EVENT_REPAIR:  // 修复完成
                            Talk(EMOTE_REPAIR);
                            me->ClearUnitState(UNIT_STATE_STUNNED | UNIT_STATE_ROOT);
                            events.ScheduleEvent(EVENT_SHUTDOWN, 150s);
                            events.CancelEvent(EVENT_REPAIR);
                            break;
                        case EVENT_THORIM_S_HAMMER:  // 风暴之塔 - 托里姆之锤
                            for (uint8 i = 0; i < 7; ++i)
                            {
                                if (Creature* thorim = DoSummon(NPC_THORIM_BEACON, me, float(urand(20, 60)), 20s, TEMPSUMMON_TIMED_DESPAWN))
                                    thorim->GetMotionMaster()->MoveRandom(100);
                            }
                            Talk(SAY_TOWER_STORM);
                            events.CancelEvent(EVENT_THORIM_S_HAMMER);
                            break;
                        case EVENT_MIMIRON_S_INFERNO:  // 火焰之塔 - 米米尔隆的炼狱
                            me->SummonCreature(NPC_MIMIRON_BEACON, InfernoStart);
                            Talk(SAY_TOWER_FLAME);
                            events.CancelEvent(EVENT_MIMIRON_S_INFERNO);
                            break;
                        case EVENT_HODIR_S_FURY:  // 冰霜之塔 - 霍迪尔的愤怒
                            for (uint8 i = 0; i < 7; ++i)
                            {
                                if (Creature* hodir = DoSummon(NPC_HODIR_BEACON, me, 50, 0s))
                                    hodir->GetMotionMaster()->MoveRandom(100);
                            }
                            Talk(SAY_TOWER_FROST);
                            events.CancelEvent(EVENT_HODIR_S_FURY);
                            break;
                        case EVENT_FREYA_S_WARD:  // 生命之塔 - 弗雷亚的守护
                            Talk(SAY_TOWER_NATURE);
                            for (int32 i = 0; i < 4; ++i)
                                me->SummonCreature(NPC_FREYA_BEACON, FreyaBeacons[i]);

                            if (Unit* target = SelectTarget(SelectTargetMethod::Random))
                                DoCast(target, SPELL_FREYA_S_WARD);
                            events.CancelEvent(EVENT_FREYA_S_WARD);
                            break;
                    }

                    // 如果正在施法，暂停后续事件处理
                    if (me->HasUnitState(UNIT_STATE_CASTING))
                        return;
                }

                // 准备好时施放攻城锤
                DoBatteringRamIfReady();
            }

            /**
             * @brief 法术命中目标处理
             * @param target 目标对象
             * @param spellInfo 法术信息
             *
             * 当法术命中目标时调用，用于处理追击目标的选定
             */
            void SpellHitTarget(WorldObject* target, SpellInfo const* spellInfo) override
            {
                Unit* unitTarget = target->ToUnit();
                if (!unitTarget)
                    return;

                // 只处理追击法术
                if (spellInfo->Id != SPELL_PURSUED)
                    return;

                // 记录追击目标并开始攻击
                _pursueTarget = unitTarget->GetGUID();
                AttackStart(unitTarget);

                // 向载具中的玩家发送追击表情
                for (SeatMap::const_iterator itr = unitTarget->GetVehicleKit()->Seats.begin(); itr != unitTarget->GetVehicleKit()->Seats.end(); ++itr)
                {
                    if (Player* passenger = ObjectAccessor::GetPlayer(*me, itr->second.Passenger.Guid))
                    {
                        Talk(EMOTE_PURSUE, passenger);
                        return;
                    }
                }
            }

            /**
             * @brief 执行动作
             * @param action 动作 ID
             *
             * 处理外部触发的动作，如防御塔被摧毁、开始困难模式等
             */
            void DoAction(int32 action) override
            {
                // 防御塔被摧毁，降低战利品模式和减少激活塔数量
                if (action && action <= 4)
                {
                    if (me->HasLootMode(LOOT_MODE_DEFAULT | LOOT_MODE_HARD_MODE_1 | LOOT_MODE_HARD_MODE_2 | LOOT_MODE_HARD_MODE_3 | LOOT_MODE_HARD_MODE_4) && ActiveTowersCount == 4)
                        me->RemoveLootMode(LOOT_MODE_HARD_MODE_4);

                    if (me->HasLootMode(LOOT_MODE_DEFAULT | LOOT_MODE_HARD_MODE_1 | LOOT_MODE_HARD_MODE_2 | LOOT_MODE_HARD_MODE_3) && ActiveTowersCount == 3)
                        me->RemoveLootMode(LOOT_MODE_HARD_MODE_3);

                    if (me->HasLootMode(LOOT_MODE_DEFAULT | LOOT_MODE_HARD_MODE_1 | LOOT_MODE_HARD_MODE_2) && ActiveTowersCount == 2)
                        me->RemoveLootMode(LOOT_MODE_HARD_MODE_2);

                    if (me->HasLootMode(LOOT_MODE_DEFAULT | LOOT_MODE_HARD_MODE_1) && ActiveTowersCount == 1)
                        me->RemoveLootMode(LOOT_MODE_HARD_MODE_1);
                }

                switch (action)
                {
                    case ACTION_TOWER_OF_STORM_DESTROYED:  // 风暴之塔被摧毁
                        if (towerOfStorms)
                        {
                            towerOfStorms = false;
                            --ActiveTowersCount;
                        }
                        break;
                    case ACTION_TOWER_OF_FROST_DESTROYED:  // 冰霜之塔被摧毁
                        if (towerOfFrost)
                        {
                            towerOfFrost = false;
                            --ActiveTowersCount;
                        }
                        break;
                    case ACTION_TOWER_OF_FLAMES_DESTROYED:  // 火焰之塔被摧毁
                        if (towerOfFlames)
                        {
                            towerOfFlames = false;
                            --ActiveTowersCount;
                        }
                        break;
                    case ACTION_TOWER_OF_LIFE_DESTROYED:  // 生命之塔被摧毁
                        if (towerOfLife)
                        {
                            towerOfLife = false;
                            --ActiveTowersCount;
                        }
                        break;
                    case ACTION_START_HARD_MODE:  // 激活困难模式，启用所有防御塔并应用增益
                        ActiveTowers = true;
                        towerOfStorms = true;
                        towerOfLife = true;
                        towerOfFlames = true;
                        towerOfFrost = true;
                        me->SetLootMode(LOOT_MODE_DEFAULT | LOOT_MODE_HARD_MODE_1 | LOOT_MODE_HARD_MODE_2 | LOOT_MODE_HARD_MODE_3 | LOOT_MODE_HARD_MODE_4);
                        break;
                    case ACTION_MOVE_TO_CENTER_POSITION:  // 由门口的 2 个巨人触发
                        if (!me->isDead() && me->HasReactState(REACT_PASSIVE))
                        {
                            me->SetHomePosition(Center);
                            me->GetMotionMaster()->MoveCharge(Center.GetPositionX(), Center.GetPositionY(), Center.GetPositionZ(), 42.0f, ACTION_MOVE_TO_CENTER_POSITION); // 移动到中心位置
                        }
                        break;
                    default:
                        break;
                }
            }

            /**
             * @brief 移动完成通知
             * @param type 移动类型（未使用）
             * @param id 移动 ID
             *
             * 当移动完成时调用，用于激活 Boss 战斗状态
             */
            void MovementInform(uint32 /*type*/, uint32 id) override
            {
                if (id != ACTION_MOVE_TO_CENTER_POSITION)
                    return;
                // 到达中心位置后激活战斗状态
                me->SetReactState(REACT_AGGRESSIVE);
                me->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_UNINTERACTIBLE | UNIT_FLAG_STUNNED);
            }

            private:
                /**
                 * @brief 准备好时施放攻城锤
                 *
                 * 复制自 DoSpellAttackIfReady，区别在于目标选择方式不同
                 * 无法通过 getVictim 选择目标，移除了 spellInfo 检查
                 * 性能注意事项：每帧调用，需保持高效
                 */
                void DoBatteringRamIfReady()
                {
                    if (me->isAttackReady())
                    {
                        Unit* target = ObjectAccessor::GetUnit(*me, _pursueTarget);

                        if (!target)
                        {
                            // 没有目标，重新调度追击事件
                            events.RescheduleEvent(EVENT_PURSUE, 0s);
                            return;
                        }

                        // 如果目标在 30 码范围内，施放攻城锤
                        if (me->IsWithinCombatRange(target, 30.0f))
                        {
                            DoCast(target, SPELL_BATTERING_RAM);
                            me->resetAttackTimer();
                        }
                    }
                }

                ObjectGuid _pursueTarget;  ///< 追击目标的 GUID
        };

        /**
         * @brief 获取 AI 实例
         * @param creature 生物对象
         * @return AI 实例指针
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetUlduarAI<boss_flame_leviathanAI>(creature);
        }
};

/**
 * @class boss_flame_leviathan_seat
 * @brief 烈焰巨兽座位脚本类
 *
 * 实现烈焰巨兽背部的座位系统，玩家可以跳到 Boss 背上操作炮塔和过载装置
 */
class boss_flame_leviathan_seat : public CreatureScript
{
    public:
        /**
         * @brief 构造函数
         */
        boss_flame_leviathan_seat() : CreatureScript("boss_flame_leviathan_seat") { }

        /**
         * @struct boss_flame_leviathan_seatAI
         * @brief 座位 AI 结构
         *
         * 处理玩家登上烈焰巨兽背部座位的逻辑
         */
        struct boss_flame_leviathan_seatAI : public ScriptedAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物对象指针
             */
            boss_flame_leviathan_seatAI(Creature* creature) : ScriptedAI(creature)
            {
                me->SetReactState(REACT_PASSIVE);
                me->SetDisplayId(me->GetCreatureTemplate()->Modelid2);
                instance = creature->GetInstanceScript();
            }

            InstanceScript* instance;  ///< 副本脚本实例

            /**
             * @brief 乘客登载处理
             * @param who 登载的单位
             * @param seatId 座位 ID
             * @param apply true 为登载，false 为离载
             *
             * 处理玩家登上或离开座位的逻辑
             */
            void PassengerBoarded(Unit* who, int8 seatId, bool apply) override
            {
                if (!me->GetVehicle())
                    return;

                // 玩家座位
                if (seatId == SEAT_PLAYER)
                {
                    if (!apply)
                        return;
                    else if (Creature* leviathan = me->GetVehicleCreatureBase())
                        leviathan->AI()->Talk(SAY_PLAYER_RIDING);  // Boss 对话

                    // 激活炮塔
                    if (Unit* turretPassenger = me->GetVehicleKit()->GetPassenger(SEAT_TURRET))
                        if (Creature* turret = turretPassenger->ToCreature())
                        {
                            turret->SetFaction(me->GetVehicleBase()->GetFaction());
                            turret->ReplaceAllUnitFlags(UnitFlags(0)); // 取消不可选择标志
                            turret->AI()->AttackStart(who);
                        }

                    // 激活过载装置
                    if (Unit* devicePassenger = me->GetVehicleKit()->GetPassenger(SEAT_DEVICE))
                        if (Creature* device = devicePassenger->ToCreature())
                        {
                            device->SetNpcFlag(UNIT_NPC_FLAG_SPELLCLICK);
                            device->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                        }

                    me->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                }
                else if (seatId == SEAT_TURRET)
                {
                    if (apply)
                        return;

                    // 炮塔被摧毁后激活过载装置
                    if (Unit* device = ASSERT_NOTNULL(me->GetVehicleKit())->GetPassenger(SEAT_DEVICE))
                    {
                        device->SetNpcFlag(UNIT_NPC_FLAG_SPELLCLICK);
                        device->ReplaceAllUnitFlags(UnitFlags(0)); // 取消不可选择标志
                    }
                }
            }
        };

        /**
         * @brief 获取 AI 实例
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetUlduarAI<boss_flame_leviathan_seatAI>(creature);
        }
};

/**
 * @class boss_flame_leviathan_defense_cannon
 * @brief 烈焰巨兽防御火炮脚本类
 *
 * 实现烈焰巨兽的防御火炮系统，攻击载具中的玩家
 */
class boss_flame_leviathan_defense_cannon : public CreatureScript
{
    public:
        /**
         * @brief 构造函数
         */
        boss_flame_leviathan_defense_cannon() : CreatureScript("boss_flame_leviathan_defense_cannon") { }

        /**
         * @struct boss_flame_leviathan_defense_cannonAI
         * @brief 防御火炮 AI 结构
         *
         * 控制防御火炮发射凝固汽油弹
         */
        struct boss_flame_leviathan_defense_cannonAI : public ScriptedAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物对象指针
             */
            boss_flame_leviathan_defense_cannonAI(Creature* creature) : ScriptedAI(creature)
            {
                Initialize();
            }

            /**
             * @brief 初始化成员变量
             */
            void Initialize()
            {
                NapalmTimer = 5 * IN_MILLISECONDS;  // 凝固汽油弹计时器
            }

            uint32 NapalmTimer;  ///< 凝固汽油弹施放计时器

            /**
             * @brief 重置
             */
            void Reset() override
            {
                Initialize();
                DoCast(me, AURA_STEALTH_DETECTION);  // 施放潜行检测光环
            }

            /**
             * @brief 更新 AI
             * @param diff 时间差（毫秒）
             */
            void UpdateAI(uint32 diff) override
            {
                if (!UpdateVictim())
                    return;

                // 凝固汽油弹计时器
                if (NapalmTimer <= diff)
                {
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                        if (CanAIAttack(target))
                            DoCast(target, SPELL_NAPALM, true);

                    NapalmTimer = 5000;  // 5 秒冷却
                }
                else
                    NapalmTimer -= diff;
            }

            /**
             * @brief 检查是否可以攻击目标
             * @param who 目标单位
             * @return 是否可以攻击
             *
             * 只能攻击载具中的玩家，不能攻击座位上的玩家
             */
            bool CanAIAttack(Unit const* who) const override
            {
                if (who->GetTypeId() != TYPEID_PLAYER || !who->GetVehicle() || who->GetVehicleBase()->GetEntry() == NPC_SEAT)
                    return false;
                return true;
            }
        };

        /**
         * @brief 获取 AI 实例
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetUlduarAI<boss_flame_leviathan_defense_cannonAI>(creature);
        }
};

/**
 * @class boss_flame_leviathan_defense_turret
 * @brief 烈焰巨兽防御炮塔脚本类
 *
 * 实现烈焰巨兽背部的防御炮塔，攻击登上 Boss 背部的玩家
 */
class boss_flame_leviathan_defense_turret : public CreatureScript
{
    public:
        /**
         * @brief 构造函数
         */
        boss_flame_leviathan_defense_turret() : CreatureScript("boss_flame_leviathan_defense_turret") { }

        /**
         * @struct boss_flame_leviathan_defense_turretAI
         * @brief 防御炮塔 AI 结构
         *
         * 继承自 TurretAI，实现炮塔的攻击逻辑
         */
        struct boss_flame_leviathan_defense_turretAI : public TurretAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物对象指针
             */
            boss_flame_leviathan_defense_turretAI(Creature* creature) : TurretAI(creature) { }

            /**
             * @brief 受到伤害处理
             * @param who 攻击者
             * @param damage 伤害值（引用）
             * @param damageType 伤害类型（未使用）
             * @param spellInfo 法术信息（未使用）
             *
             * 如果攻击者不是有效目标，取消伤害
             */
            void DamageTaken(Unit* who, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
            {
                if (!CanAIAttack(who))
                    damage = 0;
            }

            /**
             * @brief 检查是否可以攻击目标
             * @param who 目标单位
             * @return 是否可以攻击
             *
             * 只能攻击座位上的玩家
             */
            bool CanAIAttack(Unit const* who) const override
            {
                if (!who || who->GetTypeId() != TYPEID_PLAYER || !who->GetVehicle() || who->GetVehicleBase()->GetEntry() != NPC_SEAT)
                    return false;
                return true;
            }
        };

        /**
         * @brief 获取 AI 实例
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetUlduarAI<boss_flame_leviathan_defense_turretAI>(creature);
        }
};

/**
 * @class boss_flame_leviathan_overload_device
 * @brief 烈焰巨兽过载装置脚本类
 *
 * 实现烈焰巨兽背部的过载装置，玩家点击后会将玩家弹飞并造成 Boss 过载
 */
class boss_flame_leviathan_overload_device : public CreatureScript
{
    public:
        /**
         * @brief 构造函数
         */
        boss_flame_leviathan_overload_device() : CreatureScript("boss_flame_leviathan_overload_device") { }

        /**
         * @struct boss_flame_leviathan_overload_deviceAI
         * @brief 过载装置 AI 结构
         *
         * 继承自 PassiveAI，处理玩家点击过载装置的逻辑
         */
        struct boss_flame_leviathan_overload_deviceAI : public PassiveAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物对象指针
             */
            boss_flame_leviathan_overload_deviceAI(Creature* creature) : PassiveAI(creature)
            {
            }

            /**
             * @brief 法术点击处理
             * @param clicker 点击者（未使用）
             * @param spellClickHandled 是否已处理法术点击
             *
             * 当玩家点击过载装置时，将玩家弹飞并使其退出载具
             */
            void OnSpellClick(Unit* /*clicker*/, bool spellClickHandled) override
            {
                if (!spellClickHandled)
                    return;

                if (me->GetVehicle())
                {
                    me->RemoveNpcFlag(UNIT_NPC_FLAG_SPELLCLICK);
                    me->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);

                    if (Unit* player = me->GetVehicle()->GetPassenger(SEAT_PLAYER))
                    {
                        // 施放烟雾轨迹效果
                        me->GetVehicleBase()->CastSpell(player, SPELL_SMOKE_TRAIL, true);
                        // 将玩家击退
                        player->GetMotionMaster()->MoveKnockbackFrom(me->GetVehicleBase()->GetPositionX(), me->GetVehicleBase()->GetPositionY(), 30, 30);
                        // 玩家退出载具
                        player->ExitVehicle();
                    }
                }
            }
        };

        /**
         * @brief 获取 AI 实例
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetUlduarAI<boss_flame_leviathan_overload_deviceAI>(creature);
        }
};

/**
 * @class boss_flame_leviathan_safety_container
 * @brief 安全容器脚本类
 *
 * 实现安全容器的行为，容器被摧毁后会落到地面
 */
class boss_flame_leviathan_safety_container : public CreatureScript
{
    public:
        boss_flame_leviathan_safety_container() : CreatureScript("boss_flame_leviathan_safety_container") { }

        struct boss_flame_leviathan_safety_containerAI : public PassiveAI
        {
            boss_flame_leviathan_safety_containerAI(Creature* creature) : PassiveAI(creature)
            {
            }

            /**
             * @brief 死亡处理
             * @param killer 击杀者（未使用）
             *
             * 容器被摧毁后，移动到地面高度
             */
            void JustDied(Unit* /*killer*/) override
            {
                float x, y, z;
                me->GetPosition(x, y, z);
                z = me->GetMap()->GetHeight(me->GetPhaseMask(), x, y, z);
                me->GetMotionMaster()->MovePoint(0, x, y, z);
                me->UpdatePosition(x, y, z, 0);
            }

            void UpdateAI(uint32 /*diff*/) override
            {
            }
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetUlduarAI<boss_flame_leviathan_safety_containerAI>(creature);
        }
};

/**
 * @class npc_mechanolift
 * @brief 机械升降机脚本类
 *
 * 实现机械升降机的 AI，用于运输液体黄铁矿容器
 */
class npc_mechanolift : public CreatureScript
{
    public:
        npc_mechanolift() : CreatureScript("npc_mechanolift") { }

        struct npc_mechanoliftAI : public PassiveAI
        {
            npc_mechanoliftAI(Creature* creature) : PassiveAI(creature)
            {
                Initialize();
                ASSERT(me->GetVehicleKit());
            }

            void Initialize()
            {
                MoveTimer = 0;  // 移动计时器
            }

            uint32 MoveTimer;  ///< 移动计时器

            /**
             * @brief 重置
             *
             * 开始随机移动
             */
            void Reset() override
            {
                Initialize();
                me->GetMotionMaster()->MoveRandom(50);
            }

            /**
             * @brief 死亡处理
             * @param killer 击杀者（未使用）
             *
             * 升降机被摧毁后，施放爆炸效果并生成液体黄铁矿
             */
            void JustDied(Unit* /*killer*/) override
            {
                me->GetMotionMaster()->MoveTargetedHome();
                DoCast(SPELL_DUSTY_EXPLOSION);  // 灰尘爆炸效果

                // 生成液体黄铁矿
                Creature* liquid = DoSummon(NPC_LIQUID, me, 0);
                if (liquid)
                {
                    liquid->CastSpell(liquid, SPELL_LIQUID_PYRITE, true);
                    liquid->CastSpell(liquid, SPELL_DUST_CLOUD_IMPACT, true);
                }
            }

            /**
             * @brief 移动完成通知
             * @param type 移动类型
             * @param id 移动 ID
             *
             * 到达目标点后，拾取容器
             */
            void MovementInform(uint32 type, uint32 id) override
            {
                if (type == POINT_MOTION_TYPE && id == 1)
                    if (Creature* container = me->FindNearestCreature(NPC_CONTAINER, 5, true))
                        container->EnterVehicle(me);  // 容器进入升降机
            }

            /**
             * @brief 更新 AI
             * @param diff 时间差（毫秒）
             *
             * 定期查找并拾取容器
             */
            void UpdateAI(uint32 diff) override
            {
                if (MoveTimer <= diff)
                {
                    if (me->GetVehicleKit()->HasEmptySeat(-1))
                    {
                        Creature* container = me->FindNearestCreature(NPC_CONTAINER, 50, true);
                        if (container && !container->GetVehicle())
                            me->GetMotionMaster()->MovePoint(1, container->GetPositionX(), container->GetPositionY(), container->GetPositionZ());
                    }

                    MoveTimer = 30000; //check next 30 seconds
                }
                else
                    MoveTimer -= diff;
            }
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetUlduarAI<npc_mechanoliftAI>(creature);
        }
};

class npc_pool_of_tar : public CreatureScript
{
    public:
        npc_pool_of_tar() : CreatureScript("npc_pool_of_tar") { }

        struct npc_pool_of_tarAI : public ScriptedAI
        {
            npc_pool_of_tarAI(Creature* creature) : ScriptedAI(creature)
            {
                me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                me->SetReactState(REACT_PASSIVE);
                me->CastSpell(me, SPELL_TAR_PASSIVE, true);
            }

            void DamageTaken(Unit* /*who*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
            {
                damage = 0;
            }

            void SpellHit(WorldObject* /*caster*/, SpellInfo const* spellInfo) override
            {
                if (spellInfo->SchoolMask & SPELL_SCHOOL_MASK_FIRE && !me->HasAura(SPELL_BLAZE))
                    me->CastSpell(me, SPELL_BLAZE, true);
            }

            void UpdateAI(uint32 /*diff*/) override { }
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetUlduarAI<npc_pool_of_tarAI>(creature);
        }
};

class npc_colossus : public CreatureScript
{
    public:
        npc_colossus() : CreatureScript("npc_colossus") { }

        struct npc_colossusAI : public ScriptedAI
        {
            npc_colossusAI(Creature* creature) : ScriptedAI(creature)
            {
                instance = creature->GetInstanceScript();
            }

            InstanceScript* instance;

            void JustDied(Unit* /*killer*/) override
            {
                if (me->GetHomePosition().IsInDist(&Center, 50.f))
                    instance->SetData(DATA_COLOSSUS, instance->GetData(DATA_COLOSSUS)+1);
            }

            void UpdateAI(uint32 /*diff*/) override
            {
                if (!UpdateVictim())
                    return;

                DoMeleeAttackIfReady();
            }
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetUlduarAI<npc_colossusAI>(creature);
        }
};

class npc_thorims_hammer : public CreatureScript
{
    public:
        npc_thorims_hammer() : CreatureScript("npc_thorims_hammer") { }

        struct npc_thorims_hammerAI : public ScriptedAI
        {
            npc_thorims_hammerAI(Creature* creature) : ScriptedAI(creature)
            {
                me->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                me->CastSpell(me, AURA_DUMMY_BLUE, true);
            }

            void MoveInLineOfSight(Unit* who) override

            {
                if (who->GetTypeId() == TYPEID_PLAYER && who->IsVehicle() && me->IsInRange(who, 0, 10, false))
                {
                    if (Creature* trigger = DoSummonFlyer(NPC_THORIM_TARGET_BEACON, me, 20, 0, 1s, TEMPSUMMON_TIMED_DESPAWN))
                        trigger->CastSpell(who, SPELL_THORIM_S_HAMMER, true);
                }
            }

            void UpdateAI(uint32 /*diff*/) override
            {
                if (!me->HasAura(AURA_DUMMY_BLUE))
                    me->CastSpell(me, AURA_DUMMY_BLUE, true);

                UpdateVictim();
            }
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetUlduarAI<npc_thorims_hammerAI>(creature);
        }
};

class npc_mimirons_inferno : public CreatureScript
{
public:
    npc_mimirons_inferno() : CreatureScript("npc_mimirons_inferno") { }

    struct npc_mimirons_infernoAI : public EscortAI
    {
        npc_mimirons_infernoAI(Creature* creature) : EscortAI(creature)
        {
            Initialize();
            me->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_UNINTERACTIBLE);
            me->CastSpell(me, AURA_DUMMY_YELLOW, true);
            me->SetReactState(REACT_PASSIVE);
        }

        void Initialize()
        {
            infernoTimer = 2000;
        }

        void Reset() override
        {
            Initialize();
        }

        uint32 infernoTimer;

        void UpdateAI(uint32 diff) override
        {
            EscortAI::UpdateAI(diff);

            if (!HasEscortState(STATE_ESCORT_ESCORTING))
                Start(false, true, ObjectGuid::Empty, nullptr, false, true);
            else
            {
                if (infernoTimer <= diff)
                {
                    if (Creature* trigger = DoSummonFlyer(NPC_MIMIRON_TARGET_BEACON, me, 20, 0, 1s, TEMPSUMMON_TIMED_DESPAWN))
                    {
                        trigger->CastSpell(me->GetPosition(), SPELL_MIMIRON_S_INFERNO, true);
                        infernoTimer = 2000;
                    }
                }
                else
                    infernoTimer -= diff;

                if (!me->HasAura(AURA_DUMMY_YELLOW))
                    me->CastSpell(me, AURA_DUMMY_YELLOW, true);
            }
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetUlduarAI<npc_mimirons_infernoAI>(creature);
    }
};

class npc_hodirs_fury : public CreatureScript
{
    public:
        npc_hodirs_fury() : CreatureScript("npc_hodirs_fury") { }

        struct npc_hodirs_furyAI : public ScriptedAI
        {
            npc_hodirs_furyAI(Creature* creature) : ScriptedAI(creature)
            {
                me->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                me->CastSpell(me, AURA_DUMMY_GREEN, true);
            }

            void MoveInLineOfSight(Unit* who) override

            {
                if (who->GetTypeId() == TYPEID_PLAYER && who->IsVehicle() && me->IsInRange(who, 0, 5, false))
                {
                    if (Creature* trigger = DoSummonFlyer(NPC_HODIR_TARGET_BEACON, me, 20, 0, 1s, TEMPSUMMON_TIMED_DESPAWN))
                        trigger->CastSpell(who, SPELL_HODIR_S_FURY, true);
                }
            }

            void UpdateAI(uint32 /*diff*/) override
            {
                if (!me->HasAura(AURA_DUMMY_GREEN))
                    me->CastSpell(me, AURA_DUMMY_GREEN, true);

                UpdateVictim();
            }
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetUlduarAI<npc_hodirs_furyAI>(creature);
        }
};

class npc_freyas_ward : public CreatureScript
{
    public:
        npc_freyas_ward() : CreatureScript("npc_freyas_ward") { }

        struct npc_freyas_wardAI : public ScriptedAI
        {
            npc_freyas_wardAI(Creature* creature) : ScriptedAI(creature)
            {
                Initialize();
                me->CastSpell(me, AURA_DUMMY_GREEN, true);
            }

            void Initialize()
            {
                summonTimer = 5000;
            }

            uint32 summonTimer;

            void Reset() override
            {
                Initialize();
            }

            void UpdateAI(uint32 diff) override
            {
                if (summonTimer <= diff)
                {
                    DoCast(SPELL_FREYA_S_WARD_EFFECT_1);
                    DoCast(SPELL_FREYA_S_WARD_EFFECT_2);
                    summonTimer = 20000;
                }
                else
                    summonTimer -= diff;

                if (!me->HasAura(AURA_DUMMY_GREEN))
                    me->CastSpell(me, AURA_DUMMY_GREEN, true);

                UpdateVictim();
            }
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetUlduarAI<npc_freyas_wardAI>(creature);
        }
};

class npc_freya_ward_summon : public CreatureScript
{
    public:
        npc_freya_ward_summon() : CreatureScript("npc_freya_ward_summon") { }

        struct npc_freya_ward_summonAI : public ScriptedAI
        {
            npc_freya_ward_summonAI(Creature* creature) : ScriptedAI(creature)
            {
                Initialize();
                creature->GetMotionMaster()->MoveRandom(100);
            }

            void Initialize()
            {
                lashTimer = 5000;
            }

            uint32 lashTimer;

            void Reset() override
            {
                Initialize();
            }

            void UpdateAI(uint32 diff) override
            {
                if (!UpdateVictim())
                    return;

                if (lashTimer <= diff)
                {
                    DoCast(SPELL_LASH);
                    lashTimer = 20000;
                }
                else
                    lashTimer -= diff;

                DoMeleeAttackIfReady();
            }
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetUlduarAI<npc_freya_ward_summonAI>(creature);
        }
};

enum BrannBronzebeardGossips
{
    GOSSIP_MENU_BRANN_BRONZEBEARD   = 10355,
    GOSSIP_OPTION_BRANN_BRONZEBEARD = 0
};

class npc_brann_bronzebeard_ulduar_intro : public CreatureScript
{
    public:
        npc_brann_bronzebeard_ulduar_intro() : CreatureScript("npc_brann_bronzebeard_ulduar_intro") { }

        struct npc_brann_bronzebeard_ulduar_introAI : public ScriptedAI
        {
            npc_brann_bronzebeard_ulduar_introAI(Creature* creature) : ScriptedAI(creature)
            {
                _instance = creature->GetInstanceScript();
            }

            bool OnGossipSelect(Player* player, uint32 menuId, uint32 gossipListId) override
            {
                if (menuId == GOSSIP_MENU_BRANN_BRONZEBEARD && gossipListId == GOSSIP_OPTION_BRANN_BRONZEBEARD)
                {
                    me->RemoveNpcFlag(UNIT_NPC_FLAG_GOSSIP);
                    player->PlayerTalkClass->SendCloseGossip();
                    if (Creature* loreKeeper = _instance->GetCreature(DATA_LORE_KEEPER_OF_NORGANNON))
                        loreKeeper->RemoveNpcFlag(UNIT_NPC_FLAG_GOSSIP);
                }
                return false;
            }

        private:
            InstanceScript* _instance;
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetUlduarAI<npc_brann_bronzebeard_ulduar_introAI>(creature);
        }
};

enum LoreKeeperGossips
{
    GOSSIP_MENU_LORE_KEEPER   = 10477,
    GOSSIP_OPTION_LORE_KEEPER = 0
};

class npc_lorekeeper : public CreatureScript
{
    public:
        npc_lorekeeper() : CreatureScript("npc_lorekeeper") { }

        struct npc_lorekeeperAI : public ScriptedAI
        {
            npc_lorekeeperAI(Creature* creature) : ScriptedAI(creature)
            {
                _instance = creature->GetInstanceScript();
            }

            void DoAction(int32 action) override
            {
                // Start encounter
                if (action == ACTION_SPAWN_VEHICLES)
                {
                    for (uint8 i = 0; i < RAID_MODE(2, 5); ++i)
                        DoSummon(VEHICLE_SIEGE, PosSiege[i], 3s, TEMPSUMMON_CORPSE_TIMED_DESPAWN);
                    for (uint8 i = 0; i < RAID_MODE(2, 5); ++i)
                        DoSummon(VEHICLE_CHOPPER, PosChopper[i], 3s, TEMPSUMMON_CORPSE_TIMED_DESPAWN);
                    for (uint8 i = 0; i < RAID_MODE(2, 5); ++i)
                        DoSummon(VEHICLE_DEMOLISHER, PosDemolisher[i], 3s, TEMPSUMMON_CORPSE_TIMED_DESPAWN);
                }
            }

            bool OnGossipSelect(Player* player, uint32 menuId, uint32 gossipListId) override
            {
                if (menuId == GOSSIP_MENU_LORE_KEEPER && gossipListId == GOSSIP_OPTION_LORE_KEEPER)
                {
                    me->RemoveNpcFlag(UNIT_NPC_FLAG_GOSSIP);
                    player->PlayerTalkClass->SendCloseGossip();
                    _instance->instance->LoadGrid(364, -16); // make sure leviathan is loaded

                    if (Creature* leviathan = _instance->GetCreature(DATA_FLAME_LEVIATHAN))
                    {
                        leviathan->AI()->DoAction(ACTION_START_HARD_MODE);
                        me->SetVisible(false);
                        DoAction(ACTION_SPAWN_VEHICLES); // spawn the vehicles
                        if (Creature* delorah = _instance->GetCreature(DATA_DELLORAH))
                        {
                            if (Creature* brann = _instance->GetCreature(DATA_BRANN_BRONZEBEARD_INTRO))
                            {
                                brann->RemoveNpcFlag(UNIT_NPC_FLAG_GOSSIP);
                                delorah->GetMotionMaster()->MovePoint(0, brann->GetPositionX() - 4, brann->GetPositionY(), brann->GetPositionZ());
                                /// @todo delorah->AI()->Talk(xxxx, brann->GetGUID()); when reached at branz
                            }
                        }
                    }
                }
                return false;
            }

        private:
            InstanceScript* _instance;
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetUlduarAI<npc_lorekeeperAI>(creature);
        }
};

class go_ulduar_tower : public GameObjectScript
{
    public:
        go_ulduar_tower() : GameObjectScript("go_ulduar_tower") { }

        struct go_ulduar_towerAI : public GameObjectAI
        {
            go_ulduar_towerAI(GameObject* go) : GameObjectAI(go), instance(go->GetInstanceScript()) { }

            InstanceScript* instance;

            void Destroyed(WorldObject* /*attacker*/, uint32 /*eventId*/) override
            {
                switch (me->GetEntry())
                {
                    case GO_TOWER_OF_STORMS:
                        instance->ProcessEvent(me, EVENT_TOWER_OF_STORM_DESTROYED);
                        break;
                    case GO_TOWER_OF_FLAMES:
                        instance->ProcessEvent(me, EVENT_TOWER_OF_FLAMES_DESTROYED);
                        break;
                    case GO_TOWER_OF_FROST:
                        instance->ProcessEvent(me, EVENT_TOWER_OF_FROST_DESTROYED);
                        break;
                    case GO_TOWER_OF_LIFE:
                        instance->ProcessEvent(me, EVENT_TOWER_OF_LIFE_DESTROYED);
                        break;
                }

                if (Creature* trigger = me->FindNearestCreature(NPC_ULDUAR_GAUNTLET_GENERATOR, 15.0f, true))
                    trigger->DisappearAndDie();
            }
        };

        GameObjectAI* GetAI(GameObject* go) const override
        {
            return GetUlduarAI<go_ulduar_towerAI>(go);
        }
};

class achievement_three_car_garage_demolisher : public AchievementCriteriaScript
{
    public:
        achievement_three_car_garage_demolisher() : AchievementCriteriaScript("achievement_three_car_garage_demolisher") { }

        bool OnCheck(Player* source, Unit* /*target*/) override
        {
            if (Creature* vehicle = source->GetVehicleCreatureBase())
            {
                if (vehicle->GetEntry() == VEHICLE_DEMOLISHER)
                    return true;
            }

            return false;
        }
};

class achievement_three_car_garage_chopper : public AchievementCriteriaScript
{
    public:
        achievement_three_car_garage_chopper() : AchievementCriteriaScript("achievement_three_car_garage_chopper") { }

        bool OnCheck(Player* source, Unit* /*target*/) override
        {
            if (Creature* vehicle = source->GetVehicleCreatureBase())
            {
                if (vehicle->GetEntry() == VEHICLE_CHOPPER)
                    return true;
            }

            return false;
        }
};

class achievement_three_car_garage_siege : public AchievementCriteriaScript
{
    public:
        achievement_three_car_garage_siege() : AchievementCriteriaScript("achievement_three_car_garage_siege") { }

        bool OnCheck(Player* source, Unit* /*target*/) override
        {
            if (Creature* vehicle = source->GetVehicleCreatureBase())
            {
                if (vehicle->GetEntry() == VEHICLE_SIEGE)
                    return true;
            }

            return false;
        }
};

class achievement_shutout : public AchievementCriteriaScript
{
    public:
        achievement_shutout() : AchievementCriteriaScript("achievement_shutout") { }

        bool OnCheck(Player* /*source*/, Unit* target) override
        {
            if (target)
                if (Creature* leviathan = target->ToCreature())
                    if (leviathan->AI()->GetData(DATA_SHUTOUT))
                        return true;

            return false;
        }
};

class achievement_unbroken : public AchievementCriteriaScript
{
    public:
        achievement_unbroken() : AchievementCriteriaScript("achievement_unbroken") { }

        bool OnCheck(Player* /*source*/, Unit* target) override
        {
            if (target)
                if (InstanceScript* instance = target->GetInstanceScript())
                    return instance->GetData(DATA_UNBROKEN) != 0;

            return false;
        }
};

class achievement_orbital_bombardment : public AchievementCriteriaScript
{
    public:
        achievement_orbital_bombardment() : AchievementCriteriaScript("achievement_orbital_bombardment") { }

        bool OnCheck(Player* /*source*/, Unit* target) override
        {
            if (!target)
                return false;

            if (Creature* Leviathan = target->ToCreature())
                if (Leviathan->AI()->GetData(DATA_ORBIT_ACHIEVEMENTS) >= 1)
                    return true;

            return false;
        }
};

class achievement_orbital_devastation : public AchievementCriteriaScript
{
    public:
        achievement_orbital_devastation() : AchievementCriteriaScript("achievement_orbital_devastation") { }

        bool OnCheck(Player* /*source*/, Unit* target) override
        {
            if (!target)
                return false;

            if (Creature* Leviathan = target->ToCreature())
                if (Leviathan->AI()->GetData(DATA_ORBIT_ACHIEVEMENTS) >= 2)
                    return true;

            return false;
        }
};

class achievement_nuked_from_orbit : public AchievementCriteriaScript
{
    public:
        achievement_nuked_from_orbit() : AchievementCriteriaScript("achievement_nuked_from_orbit") { }

        bool OnCheck(Player* /*source*/, Unit* target) override
        {
            if (!target)
                return false;

            if (Creature* Leviathan = target->ToCreature())
                if (Leviathan->AI()->GetData(DATA_ORBIT_ACHIEVEMENTS) >= 3)
                    return true;

            return false;
        }
};

class achievement_orbit_uary : public AchievementCriteriaScript
{
    public:
        achievement_orbit_uary() : AchievementCriteriaScript("achievement_orbit_uary") { }

        bool OnCheck(Player* /*source*/, Unit* target) override
        {
            if (!target)
                return false;

            if (Creature* Leviathan = target->ToCreature())
                if (Leviathan->AI()->GetData(DATA_ORBIT_ACHIEVEMENTS) == 4)
                    return true;

            return false;
        }
};

// 62399 - Overload Circuit
class spell_overload_circuit : public AuraScript
{
    PrepareAuraScript(spell_overload_circuit);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_SYSTEMS_SHUTDOWN });
    }

    void PeriodicTick(AuraEffect const* /*aurEff*/)
    {
        if (!GetTarget()->GetMap()->IsDungeon() || int32(GetTarget()->GetAppliedAuras().count(GetId())) < (GetTarget()->GetMap()->Is25ManRaid() ? 4 : 2))
            return;

        GetTarget()->CastSpell(nullptr, SPELL_SYSTEMS_SHUTDOWN, true);
        if (Unit* veh = GetTarget()->GetVehicleBase())
            veh->CastSpell(nullptr, SPELL_SYSTEMS_SHUTDOWN, true);
    }

    void Register() override
    {
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_overload_circuit::PeriodicTick, EFFECT_1, SPELL_AURA_PERIODIC_DUMMY);
    }
};

// 62292 - Blaze
class spell_tar_blaze : public AuraScript
{
    PrepareAuraScript(spell_tar_blaze);

    bool Validate(SpellInfo const* spellInfo) override
    {
        return ValidateSpellInfo({ spellInfo->GetEffect(EFFECT_0).TriggerSpell });
    }

    void PeriodicTick(AuraEffect const* aurEff)
    {
        // should we use custom damage?
        GetTarget()->CastSpell(nullptr, aurEff->GetSpellEffectInfo().TriggerSpell, true);
    }

    void Register() override
    {
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_tar_blaze::PeriodicTick, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY);
    }
};

// 64414 - Load into Catapult
class spell_load_into_catapult : public SpellScriptLoader
{
    enum Spells
    {
        SPELL_PASSENGER_LOADED = 62340,
    };

    public:
        spell_load_into_catapult() : SpellScriptLoader("spell_load_into_catapult") { }

        class spell_load_into_catapult_AuraScript : public AuraScript
        {
            PrepareAuraScript(spell_load_into_catapult_AuraScript);

            void OnApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
            {
                Unit* owner = GetOwner()->ToUnit();
                if (!owner)
                    return;

                owner->CastSpell(owner, SPELL_PASSENGER_LOADED, true);
            }

            void OnRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
            {
                Unit* owner = GetOwner()->ToUnit();
                if (!owner)
                    return;

                owner->RemoveAurasDueToSpell(SPELL_PASSENGER_LOADED);
            }

            void Register() override
            {
                OnEffectApply += AuraEffectApplyFn(spell_load_into_catapult_AuraScript::OnApply, EFFECT_0, SPELL_AURA_CONTROL_VEHICLE, AURA_EFFECT_HANDLE_REAL);
                OnEffectRemove += AuraEffectRemoveFn(spell_load_into_catapult_AuraScript::OnRemove, EFFECT_0, SPELL_AURA_CONTROL_VEHICLE, AURA_EFFECT_HANDLE_REAL_OR_REAPPLY_MASK);
            }
        };

        AuraScript* GetAuraScript() const override
        {
            return new spell_load_into_catapult_AuraScript();
        }
};

// 62705 - Auto-repair
class spell_auto_repair : public SpellScriptLoader
{
    enum Spells
    {
        SPELL_AUTO_REPAIR = 62705,
    };

    public:
        spell_auto_repair() : SpellScriptLoader("spell_auto_repair") { }

        class spell_auto_repair_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_auto_repair_SpellScript);

            void CheckCooldownForTarget(SpellMissInfo missInfo)
            {
                if (missInfo != SPELL_MISS_NONE)
                    return;

                if (GetHitUnit()->HasAuraEffect(SPELL_AUTO_REPAIR, EFFECT_2))   // Check presence of dummy aura indicating cooldown
                {
                    PreventHitEffect(EFFECT_0);
                    PreventHitDefaultEffect(EFFECT_1);
                    PreventHitDefaultEffect(EFFECT_2);
                    //! Currently this doesn't work: if we call PreventHitAura(), the existing aura will be removed
                    //! because of recent aura refreshing changes. Since removing the existing aura negates the idea
                    //! of a cooldown marker, we just let the dummy aura refresh itself without executing the other spelleffects.
                    //! The spelleffects can be executed by letting the dummy aura expire naturally.
                    //! This is a temporary solution only.
                    //PreventHitAura();
                }
            }

            void HandleScript(SpellEffIndex /*eff*/)
            {
                Vehicle* vehicle = GetHitUnit()->GetVehicleKit();
                if (!vehicle)
                    return;

                Unit* driver = vehicle->GetPassenger(0);
                if (!driver)
                    return;

                driver->TextEmote(EMOTE_REPAIR, driver, true);

                InstanceScript* instance = driver->GetInstanceScript();
                if (!instance)
                    return;

                // Actually should/could use basepoints (100) for this spell effect as percentage of health, but oh well.
                vehicle->GetBase()->SetFullHealth();

                // For achievement
                instance->SetData(DATA_UNBROKEN, 0);
            }

            void Register() override
            {
                OnEffectHitTarget += SpellEffectFn(spell_auto_repair_SpellScript::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
                BeforeHit += BeforeSpellHitFn(spell_auto_repair_SpellScript::CheckCooldownForTarget);
            }
        };

        SpellScript* GetSpellScript() const override
        {
            return new spell_auto_repair_SpellScript();
        }
};

// 62475 - Systems Shutdown
class spell_systems_shutdown : public SpellScriptLoader
{
    public:
        spell_systems_shutdown() : SpellScriptLoader("spell_systems_shutdown") { }

        class spell_systems_shutdown_AuraScript : public AuraScript
        {
            PrepareAuraScript(spell_systems_shutdown_AuraScript);

            void OnApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
            {
                Creature* owner = GetOwner()->ToCreature();
                if (!owner)
                    return;

                //! This could probably in the SPELL_EFFECT_SEND_EVENT handler too:
                owner->AddUnitState(UNIT_STATE_STUNNED | UNIT_STATE_ROOT);
                owner->SetUnitFlag(UNIT_FLAG_STUNNED);
                owner->RemoveAurasDueToSpell(SPELL_GATHERING_SPEED);
            }

            void OnRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
            {
                Creature* owner = GetOwner()->ToCreature();
                if (!owner)
                    return;

                owner->RemoveUnitFlag(UNIT_FLAG_STUNNED);
            }

            void Register() override
            {
                OnEffectApply += AuraEffectApplyFn(spell_systems_shutdown_AuraScript::OnApply, EFFECT_0, SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN, AURA_EFFECT_HANDLE_REAL);
                OnEffectRemove += AuraEffectRemoveFn(spell_systems_shutdown_AuraScript::OnRemove, EFFECT_0, SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN, AURA_EFFECT_HANDLE_REAL);
            }
        };

        AuraScript* GetAuraScript() const override
        {
            return new spell_systems_shutdown_AuraScript();
        }
};

class FlameLeviathanPursuedTargetSelector
{
    enum Area
    {
        AREA_FORMATION_GROUNDS = 4652,
    };

    public:
        explicit FlameLeviathanPursuedTargetSelector() { };

        bool operator()(WorldObject* target) const
        {
            //! No players, only vehicles. Pursue is never cast on players.
            Creature* creatureTarget = target->ToCreature();
            if (!creatureTarget)
                return false;

            //! NPC entries must match
            if (creatureTarget->GetEntry() != NPC_SALVAGED_DEMOLISHER && creatureTarget->GetEntry() != NPC_SALVAGED_SIEGE_ENGINE)
                return false;

            //! NPC must be a valid vehicle installation
            Vehicle* vehicle = creatureTarget->GetVehicleKit();
            if (!vehicle)
                return false;

            //! Entity needs to be in appropriate area
            if (target->GetAreaId() != AREA_FORMATION_GROUNDS)
                return false;

            //! Vehicle must be in use by player
            for (SeatMap::const_iterator itr = vehicle->Seats.begin(); itr != vehicle->Seats.end(); ++itr)
                if (itr->second.Passenger.Guid.IsPlayer())
                    return true;

            return false;
        }
};

// 62374 - Pursued
class spell_pursue : public SpellScriptLoader
{
    public:
        spell_pursue() : SpellScriptLoader("spell_pursue") { }

        class spell_pursue_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_pursue_SpellScript);

        private:
            // EFFECT #0 - select target
            void FilterTargets(std::list<WorldObject*>& targets)
            {
                Trinity::Containers::RandomResize(targets, FlameLeviathanPursuedTargetSelector(), 1);
                if (targets.empty())
                {
                    if (Unit* caster = GetCaster())
                        if (Creature* cCaster = caster->ToCreature())
                            cCaster->AI()->EnterEvadeMode(CreatureAI::EVADE_REASON_NO_HOSTILES);
                }
                else
                    _target = targets.front();
            }

            // EFFECT #1 - copy target from effect #0
            void FilterTargetsSubsequently(std::list<WorldObject*>& targets)
            {
                targets.clear();
                if (_target)
                    targets.push_back(_target);
            }

            void Register() override
            {
                OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_pursue_SpellScript::FilterTargets, EFFECT_0, TARGET_UNIT_SRC_AREA_ENEMY);
                OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_pursue_SpellScript::FilterTargetsSubsequently, EFFECT_1, TARGET_UNIT_SRC_AREA_ENEMY);
            }

            WorldObject* _target = nullptr;
        };

        SpellScript* GetSpellScript() const override
        {
            return new spell_pursue_SpellScript();
        }
};

// 62324 - Throw Passenger
class spell_vehicle_throw_passenger : public SpellScriptLoader
{
    public:
        spell_vehicle_throw_passenger() : SpellScriptLoader("spell_vehicle_throw_passenger") { }

        class spell_vehicle_throw_passenger_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_vehicle_throw_passenger_SpellScript);
            void HandleScript(SpellEffIndex /*effIndex*/)
            {
                Spell* baseSpell = GetSpell();
                SpellCastTargets targets = baseSpell->m_targets;
                int32 damage = GetEffectValue();
                if (targets.HasTraj())
                    if (Vehicle* vehicle = GetCaster()->GetVehicleKit())
                        if (Unit* passenger = vehicle->GetPassenger(damage - 1))
                        {
                            // use 99 because it is 3d search
                            std::list<WorldObject*> targetList;
                            Trinity::WorldObjectSpellAreaTargetCheck check(99, GetExplTargetDest(), GetCaster(), GetCaster(), GetSpellInfo(), TARGET_CHECK_DEFAULT, nullptr);
                            Trinity::WorldObjectListSearcher<Trinity::WorldObjectSpellAreaTargetCheck> searcher(GetCaster(), targetList, check);
                            Cell::VisitAllObjects(GetCaster(), searcher, 99.0f);
                            float minDist = 99 * 99;
                            Unit* target = nullptr;
                            for (std::list<WorldObject*>::iterator itr = targetList.begin(); itr != targetList.end(); ++itr)
                            {
                                if (Unit* unit = (*itr)->ToUnit())
                                    if (unit->GetEntry() == NPC_SEAT)
                                        if (Vehicle* seat = unit->GetVehicleKit())
                                            if (!seat->GetPassenger(0))
                                                if (Unit* device = seat->GetPassenger(2))
                                                    if (!device->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
                                                    {
                                                        float dist = unit->GetExactDistSq(targets.GetDstPos());
                                                        if (dist < minDist)
                                                        {
                                                            minDist = dist;
                                                            target = unit;
                                                        }
                                                    }
                            }
                            if (target && target->IsWithinDist2d(targets.GetDstPos(), GetEffectInfo().CalcRadius() * 2)) // now we use *2 because the location of the seat is not correct
                                passenger->EnterVehicle(target, 0);
                            else
                            {
                                passenger->ExitVehicle();
                                passenger->GetMotionMaster()->MoveJump(*targets.GetDstPos(), targets.GetSpeedXY(), targets.GetSpeedZ());
                            }
                        }
            }

            void Register() override
            {
                OnEffectHitTarget += SpellEffectFn(spell_vehicle_throw_passenger_SpellScript::HandleScript, EFFECT_0, SPELL_EFFECT_DUMMY);
            }
        };

        SpellScript* GetSpellScript() const override
        {
            return new spell_vehicle_throw_passenger_SpellScript();
        }
};

void AddSC_boss_flame_leviathan()
{
    new boss_flame_leviathan();
    new boss_flame_leviathan_seat();
    new boss_flame_leviathan_defense_turret();
    new boss_flame_leviathan_defense_cannon();
    new boss_flame_leviathan_overload_device();
    new boss_flame_leviathan_safety_container();
    new npc_mechanolift();
    new npc_pool_of_tar();
    new npc_colossus();
    new npc_thorims_hammer();
    new npc_mimirons_inferno();
    new npc_hodirs_fury();
    new npc_freyas_ward();
    new npc_freya_ward_summon();
    new npc_brann_bronzebeard_ulduar_intro();
    new npc_lorekeeper();
    new go_ulduar_tower();

    new achievement_three_car_garage_demolisher();
    new achievement_three_car_garage_chopper();
    new achievement_three_car_garage_siege();
    new achievement_shutout();
    new achievement_unbroken();
    new achievement_orbital_bombardment();
    new achievement_orbital_devastation();
    new achievement_nuked_from_orbit();
    new achievement_orbit_uary();

    RegisterSpellScript(spell_overload_circuit);
    RegisterSpellScript(spell_tar_blaze);
    new spell_load_into_catapult();
    new spell_auto_repair();
    new spell_systems_shutdown();
    new spell_pursue();
    new spell_vehicle_throw_passenger();
}
