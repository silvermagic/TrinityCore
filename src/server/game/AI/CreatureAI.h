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
 * @file CreatureAI.h
 * @brief 生物AI基类定义
 *
 * 本文件定义了CreatureAI类，这是所有生物AI的基类。
 * 提供了生物行为的核心框架，包括战斗、法术响应、移动、任务交互等功能。
 *
 * 核心职责：
 * - 定义生物行为的虚函数接口（战斗、死亡、法术等）
 * - 提供威胁管理和交战状态管理
 * - 实现边界系统，限制生物活动范围
 * - 提供召唤、对话、任务等交互接口
 *
 * @see UnitAI 基类
 * @see Creature 生物实体类
 */

#ifndef TRINITY_CREATUREAI_H
#define TRINITY_CREATUREAI_H

#include "Common.h"
#include "ObjectDefines.h"
#include "Optional.h"
#include "QuestDef.h"
#include "UnitAI.h"

class AreaBoundary;
class Creature;
class DynamicObject;
class GameObject;
class PlayerAI;
class WorldObject;
struct Position;

/**
 * @brief 生物边界容器类型
 *
 * 用于存储生物活动区域的边界限制
 */
typedef std::vector<AreaBoundary const*> CreatureBoundary;

#define TIME_INTERVAL_LOOK   5000   ///< 视线检测的时间间隔(毫秒)
#define VISIBILITY_RANGE    10000   ///< 可见性范围(码)

/**
 * @brief AI权限优先级枚举
 *
 * 定义了不同AI行为的优先级层级，数值越大优先级越高
 */
enum Permitions : int32
{
    PERMIT_BASE_NO               = -1,   ///< 无权限
    PERMIT_BASE_IDLE             = 1,    ///< 空闲状态权限
    PERMIT_BASE_REACTIVE         = 100,  ///< 反应型行为权限
    PERMIT_BASE_PROACTIVE        = 200,  ///< 主动型行为权限
    PERMIT_BASE_FACTION_SPECIFIC = 400,  ///< 阵营特定权限
    PERMIT_BASE_SPECIAL          = 800   ///< 特殊权限
};

/**
 * @brief 法术目标选择类型枚举
 *
 * 用于SelectSpell函数中选择法术的目标类型
 */
enum SelectTargetType
{
    SELECT_TARGET_DONTCARE = 0,  ///< 不限制目标类型
    SELECT_TARGET_SELF,          ///< 仅自身施法
    SELECT_TARGET_SINGLE_ENEMY,  ///< 仅单个敌人
    SELECT_TARGET_AOE_ENEMY,     ///< 仅范围敌人
    SELECT_TARGET_ANY_ENEMY,     ///< 范围或单个敌人
    SELECT_TARGET_SINGLE_FRIEND, ///< 仅单个友方
    SELECT_TARGET_AOE_FRIEND,    ///< 仅范围友方
    SELECT_TARGET_ANY_FRIEND     ///< 范围或单个友方
};

/**
 * @brief 法术效果选择类型枚举
 *
 * 用于SelectSpell函数中选择法术的效果类型
 */
enum SelectEffect
{
    SELECT_EFFECT_DONTCARE = 0, ///< 不限制效果类型
    SELECT_EFFECT_DAMAGE,       ///< 伤害效果
    SELECT_EFFECT_HEALING,      ///< 治疗效果
    SELECT_EFFECT_AURA          ///< 光环效果
};

/**
 * @brief 装备切换枚举
 *
 * 用于控制生物装备的切换行为
 */
enum SCEquip
{
    EQUIP_NO_CHANGE = -1,  ///< 不改变装备
    EQUIP_UNEQUIP   = 0    ///< 卸下装备
};

/**
 * @brief 生物AI基类
 *
 * CreatureAI是所有生物AI的基类，继承自UnitAI。
 * 提供了生物行为的基本框架，包括：
 * - 战斗行为（进入/脱离战斗、攻击、死亡等）
 * - 法术施放响应
 * - 移动和寻路
 * - 任务和对话交互
 * - 巡逻和边界系统
 *
 * 所有生物的AI都应该继承此类并重写相应的虚函数来实现具体行为。
 *
 * 继承关系：
 * - 继承自 UnitAI（提供基础AI功能）
 * - 被具体AI类继承（如BossAI、ScriptedAI等）
 *
 * 生命周期：
 * 1. 生物刷新或加载时，通过Creature::CreateAI创建AI实例
 * 2. 生物更新时，定期调用UpdateAI
 * 3. 触发事件时调用相应的回调函数（如JustDied、JustEngagedWith等）
 * 4. 生物消失或死亡后销毁AI实例
 *
 * 线程安全：
 * - 所有AI操作都在生物的地图线程中执行
 * - 不需要额外的线程同步
 *
 * @see UnitAI
 * @see Creature
 */
class TC_GAME_API CreatureAI : public UnitAI
{
    protected:
        Creature* const me;  ///< 拥有此AI的生物对象指针（生命周期由Creature管理，永不失效）

        /**
         * @brief 更新当前攻击目标
         *
         * 检查当前威胁列表，选择合适的攻击目标。
         * 如果威胁列表为空或当前目标无效，会返回false。
         *
         * 调用时机：通常在UpdateAI中每帧调用，确保攻击目标始终有效。
         *
         * 性能说明：O(n)复杂度，n为威胁列表大小。
         * 需要遍历威胁列表选择最高威胁目标。
         *
         * @return true 如果成功更新并保持攻击目标
         * @return false 如果没有有效攻击目标（威胁列表为空或所有目标无效）
         */
        bool UpdateVictim();

        /**
         * @brief 在指定位置召唤生物
         *
         * 在指定的坐标位置召唤一个指定ID的生物。
         * 召唤成功后会触发JustSummoned回调。
         *
         * 调用时机：常用于Boss战斗中召唤小怪、召唤图腾等场景。
         *
         * @param entry 要召唤的生物模板ID（对应creature_template表）
         * @param pos 召唤位置（包含坐标、朝向等信息）
         * @param despawnTime 消失时间（默认30秒）
         * @param summonType 召唤类型（默认TIMED_DESPAWN）
         *                  - TEMPSUMMON_TIMED_DESPAWN: 定时消失
         *                  - TEMPSUMMON_CORPSE_TIMED_DESPAWN: 尸体保留后定时消失
         *                  - TEMPSUMMON_DEAD_DESPAWN: 死亡后立即消失
         *                  - TEMPSUMMON_MANUAL_DESPAWN: 需要手动消失
         * @return Creature* 召唤出的生物对象指针，失败返回nullptr
         */
        Creature* DoSummon(uint32 entry, Position const& pos, Milliseconds despawnTime = 30s, TempSummonType summonType = TEMPSUMMON_CORPSE_TIMED_DESPAWN);

        /**
         * @brief 在对象附近召唤生物
         *
         * 在指定对象的给定半径范围内随机位置召唤生物。
         * 使用随机角度和距离计算召唤位置，适合召唤多个小怪的场景。
         *
         * @param entry 要召唤的生物模板ID
         * @param obj 参照对象（召唤位置以此为圆心）
         * @param radius 召唤半径（默认5.0码，随机分布在圆周上）
         * @param despawnTime 消失时间（默认30秒）
         * @param summonType 召唤类型（默认TIMED_DESPAWN）
         * @return Creature* 召唤出的生物对象指针，失败返回nullptr
         */
        Creature* DoSummon(uint32 entry, WorldObject* obj, float radius = 5.0f, Milliseconds despawnTime = 30s, TempSummonType summonType = TEMPSUMMON_CORPSE_TIMED_DESPAWN);

        /**
         * @brief 在空中召唤飞行生物
         *
         * 在指定对象附近的空中位置召唤飞行生物。
         * 自动设置飞行高度，适合召唤飞行单位（如飞行坐骑、飞行宠物等）。
         *
         * @param entry 要召唤的生物模板ID
         * @param obj 参照对象（召唤位置以此为圆心）
         * @param flightZ 飞行高度（相对于地面）
         * @param radius 召唤半径（默认5.0码）
         * @param despawnTime 消失时间（默认30秒）
         * @param summonType 召唤类型（默认TIMED_DESPAWN）
         * @return Creature* 召唤出的生物对象指针，失败返回nullptr
         */
        Creature* DoSummonFlyer(uint32 entry, WorldObject* obj, float flightZ, float radius = 5.0f, Milliseconds despawnTime = 30s, TempSummonType summonType = TEMPSUMMON_CORPSE_TIMED_DESPAWN);

    public:
        /**
         * @brief 脱离战斗原因枚举
         *
         * 定义了生物脱离战斗(Evade)的各种原因，
         * 用于日志记录和调试。
         */
        enum EvadeReason
        {
            EVADE_REASON_NO_HOSTILES,       ///< 威胁列表为空，没有敌对目标
            EVADE_REASON_BOUNDARY,          ///< 生物移动到了战斗边界之外
            EVADE_REASON_NO_PATH,           ///< 生物超过5秒无法到达目标
            EVADE_REASON_SEQUENCE_BREAK,    ///< Boss的前置战斗未完成（仅Boss使用）
            EVADE_REASON_OTHER,             ///< 其他原因
        };

        /**
         * @brief 构造函数
         *
         * 初始化生物AI实例，设置基础状态。
         *
         * 初始化内容：
         * - 绑定生物对象指针（me）
         * - 初始化交战状态为false
         * - 初始化视线检测锁为false
         * - 初始化边界系统
         *
         * @param creature 拥有此AI的生物对象（必须非空）
         */
        explicit CreatureAI(Creature* creature);

        /**
         * @brief 虚析构函数
         *
         * 清理AI资源。
         * 注意：不删除_me指针（生命周期由Creature管理）。
         */
        virtual ~CreatureAI();

        /**
         * @brief 检查生物是否处于交战状态
         *
         * 交战状态表示生物正在与玩家或其他单位战斗。
         * 此状态由EngagementStart/EngagementOver管理。
         *
         * @return true 如果生物正在与玩家或其他单位交战
         * @return false 如果生物处于非战斗状态
         */
        bool IsEngaged() const { return _isEngaged; }

        /**
         * @brief 让生物说话
         *
         * 从生物的文本模板中选择指定的文本ID进行说话，
         * 可以指定私聊目标。
         *
         * 文本来源：creature_text表
         *
         * 说话类型：
         * - SAY: 普通说话（附近玩家可见）
         * - YELL: 喊叫（更大范围可见）
         * - EMOTE: 表情文本
         * - WHISPER: 私聊（仅目标玩家可见）
         *
         * @param id 文本模板ID（对应creature_text.GroupID）
         * @param whisperTarget 私聊目标（可选，仅WHISPER类型需要）
         */
        void Talk(uint8 id, WorldObject const* whisperTarget = nullptr);

        /// @name 反应函数
        /// @{

        /**
         * @brief 安全的视线检测处理
         *
         * 当IsVisible(Unit* who)返回true时，在每个who移动时调用，
         * 用于处理进入可见区域的反应。
         *
         * 安全机制：
         * - 使用_moveInLOSLocked防止递归调用
         * - 如果已锁定则跳过处理
         *
         * 调用时机：在生物更新周期中检测到新单位进入视线范围时。
         *
         * 默认行为：
         * - 如果是敌对单位且可以攻击，调用MoveInLineOfSight
         * - 如果是潜行单位，可能触发TriggerAlert
         *
         * @param who 进入视线的单位
         */
        void MoveInLineOfSight_Safe(Unit* who);

        /**
         * @brief 触发警戒状态
         *
         * 触发生物的"警戒"状态，用于检测潜行单位。
         * 会使生物表现出警觉行为（如转身看向潜行单位）。
         *
         * 行为表现：
         * - 播放警戒表情
         * - 转身面向潜行单位
         * - 播放警戒音效（如果有）
         *
         * 注意：不会真正进入战斗，只是表现出警觉。
         *
         * @param who 被检测到的单位
         */
        void TriggerAlert(Unit const* who) const;

        /**
         * @brief 进入脱离战斗模式
         *
         * 当生物没有攻击者或目标时调用，用于停止攻击。
         * 会重置生物状态，可能使其返回出生点。
         *
         * 脱离战斗流程：
         * 1. 调用_EnterEvadeMode执行核心逻辑
         * 2. 清空威胁列表
         * 3. 停止所有攻击动作
         * 4. 可能传送回出生点或开始移动回出生点
         * 5. 调用JustReachedHome回调（到达出生点后）
         *
         * 子类可以重写此函数实现自定义的脱离战斗行为，
         * 但通常应该调用父类的实现。
         *
         * @param why 脱离战斗的原因（默认OTHER）
         *            - EVADE_REASON_NO_HOSTILES: 没有敌对目标
         *            - EVADE_REASON_BOUNDARY: 超出战斗边界
         *            - EVADE_REASON_NO_PATH: 无法到达目标
         *            - EVADE_REASON_SEQUENCE_BREAK: Boss战序列中断
         *            - EVADE_REASON_OTHER: 其他原因
         */
        virtual void EnterEvadeMode(EvadeReason why = EVADE_REASON_OTHER);

        /**
         * @brief 进入战斗时的反应
         *
         * 当生物进入战斗状态时调用（从UnitAI重写）。
         * 此函数在JustEngagedWith之前调用。
         *
         * 执行流程：
         * 1. 设置交战状态标志
         * 2. 停止所有移动
         * 3. 调用JustEngagedWith（子类实现）
         *
         * @param who 使生物进入战斗的单位
         */
        void JustEnteredCombat(Unit* /*who*/) override;

        /**
         * @brief 开始威胁某个单位时的反应
         *
         * 当一个新的非离线单位被添加到威胁列表时调用。
         * 默认实现：如果未交战，则开始交战。
         *
         * 常见场景：
         * - 玩家攻击生物
         * - 生物被范围法术影响
         * - 玩家进入生物的警戒范围
         *
         * @param who 开始威胁此AI的单位
         */
        virtual void JustStartedThreateningMe(Unit* who) { if (!IsEngaged()) EngagementStart(who); }

        /**
         * @brief 初次交战时的反应
         *
         * 当生物首次与敌人交战时调用。
         * 此函数总是在JustEnteredCombat之后调用。
         *
         * 典型用途（BossAI）：
         * - 播放开场对白
         * - 召唤小怪
         * - 施放开场法术
         * - 设置Boss战斗阶段
         *
         * 子类应重写此函数以实现Boss的开怪逻辑。
         *
         * @param who 交战目标（通常是第一个攻击者或威胁最高的目标）
         */
        virtual void JustEngagedWith(Unit* /*who*/) { }

        /**
         * @brief 生物死亡时的回调
         *
         * 当生物被杀死时调用。
         * 子类可以重写此函数实现死亡时的特殊逻辑（如掉落、任务更新等）。
         *
         * 执行时机：在Unit::Kill中被调用，此时：
         * - 生物已标记为死亡
         * - 尸体尚未消失
         * - 战斗状态已清除
         *
         * 典型用途：
         * - 播放死亡对白
         * - 给予玩家任务进度
         * - 触发相关事件
         * - 保存战斗数据
         *
         * @param killer 击杀者（可能为nullptr，如自杀或环境伤害）
         */
        virtual void JustDied(Unit* /*killer*/) { }

        /**
         * @brief 杀死单位时的回调
         *
         * 当生物杀死一个单位时调用。
         * 常用于Boss战斗中的特殊处理。
         *
         * 典型用途：
         * - Boss击杀玩家时播放嘲讽
         * - 击杀特定目标后改变战斗阶段
         * - 召唤增援
         *
         * @param victim 被杀死的单位
         */
        virtual void KilledUnit(Unit* /*victim*/) { }

        /**
         * @brief 召唤生物成功的回调
         *
         * 当生物成功召唤另一个生物时调用。
         * 在DoSummon创建生物后立即触发。
         *
         * 典型用途：
         * - 设置召唤物的初始状态
         * - 将召唤物添加到管理列表
         * - 让召唤物攻击特定目标
         *
         * @param summon 被召唤的生物
         */
        virtual void JustSummoned(Creature* /*summon*/) { }

        /**
         * @brief 被召唤时的回调
         *
         * 当生物被召唤出来时调用。
         * 在Creature::Update中初始化AI后调用。
         *
         * 典型用途：
         * - 初始化召唤物的状态
         * - 设置召唤物的归属关系
         * - 立即执行某些动作（如施法、移动）
         *
         * @param summoner 召唤者（可能是玩家、生物或游戏对象）
         */
        virtual void IsSummonedBy(WorldObject* /*summoner*/) { }

        /**
         * @brief 召唤物消失时的回调
         *
         * 当此生物召唤的单位消失时调用。
         * 消失原因包括：定时消失、手动消失、尸体腐烂等。
         *
         * 典型用途：
         * - 从管理列表中移除召唤物
         * - 触发召唤物消失后的逻辑
         *
         * @param summon 消失的召唤物
         */
        virtual void SummonedCreatureDespawn(Creature* /*summon*/) { }

        /**
         * @brief 召唤物死亡时的回调
         *
         * 当此生物召唤的单位死亡时调用。
         * 与JustDied不同，这是召唤者接收到的通知。
         *
         * 典型用途：
         * - Boss的小怪被击杀后改变战斗阶段
         * - 检查是否所有召唤物都已死亡
         *
         * @param summon 死亡的召唤物
         * @param killer 击杀者
         */
        virtual void SummonedCreatureDies(Creature* /*summon*/, Unit* /*killer*/) { }

        /**
         * @brief 被法术击中时的回调
         *
         * 当生物被法术击中时调用。
         * 在法术效果应用之后触发。
         *
         * 典型用途：
         * - 响应特定法术（如被治疗时感谢）
         * - 触发特殊效果（如被火球击中后激怒）
         * - 任务进度更新
         *
         * 注意：此函数在法术处理过程中调用，避免在此函数中施放新法术。
         *
         * @param caster 施法者（可能是玩家、生物或游戏对象）
         * @param spellInfo 法术信息（包含法术ID、效果等）
         */
        virtual void SpellHit(WorldObject* /*caster*/, SpellInfo const* /*spellInfo*/) { }

        /**
         * @brief 法术击中目标时的回调
         *
         * 当生物施放的法术击中目标时调用。
         * 这是施法者接收到的通知。
         *
         * 典型用途：
         * - 确认法术命中后触发连锁效果
         * - 更新战斗逻辑
         *
         * @param target 被击中的目标
         * @param spellInfo 法术信息
         */
        virtual void SpellHitTarget(WorldObject* /*target*/, SpellInfo const* /*spellInfo*/) { }

        /**
         * @brief 法术施放完成时的回调
         *
         * 当生物施放的法术完成时调用（成功施放）。
         * 在法术施放流程的最后阶段触发。
         *
         * 典型用途：
         * - 施放成功后的冷却管理
         * - 触发后续动作
         * - 记录战斗日志
         *
         * @param spell 完成的法术信息
         */
        virtual void OnSpellCast(SpellInfo const* /*spell*/) { }

        /**
         * @brief 法术施放失败时的回调
         *
         * 当生物施放的法术失败时调用。
         * 常见失败原因：被打断、目标无效、法力不足等。
         *
         * 典型用途：
         * - 处理施法失败后的应对策略
         * - 选择备用法术
         *
         * @param spell 失败的法术信息
         */
        virtual void OnSpellFailed(SpellInfo const* /*spell*/) { }

        /**
         * @brief 法术开始施放时的回调
         *
         * 当生物开始施放法术时调用。
         * 在施法开始时立即触发。
         *
         * 典型用途：
         * - 播放施法对白
         * - 记录施法开始时间
         * - 设置施法标志
         *
         * @param spell 开始施放的法术信息
         */
        virtual void OnSpellStart(SpellInfo const* /*spell*/) { }

        /**
         * @brief 引导法术结束时的回调
         *
         * 当生物引导的法术结束时调用。
         * 无论引导是正常结束还是被打断都会触发。
         *
         * 典型用途：
         * - 清理引导状态
         * - 触发引导完成后的效果
         *
         * @param spell 结束的引导法术信息
         */
        virtual void OnChannelFinished(SpellInfo const* /*spell*/) { }

        /**
         * @brief 检查是否正在护送任务中
         *
         * 用于判断NPC是否正在进行护送任务。
         * 护送任务中的NPC有特殊的行为规则。
         *
         * 典型用途：
         * - 护送任务中不会主动攻击
         * - 护送任务失败会触发失败事件
         * - 护送任务中可能被攻击
         *
         * @return true 如果正在护送
         * @return false 如果不在护送
         */
        virtual bool IsEscorted() const { return false; }

        /**
         * @brief 生物出现在世界中时的回调
         *
         * 当生物出现在世界时调用，包括：
         * - 刷新（从数据库或模板创建）
         * - 重生（死亡后重新生成）
         * - 动态地图加载
         * - 传送到达
         *
         * 默认实现调用JustReachedHome()。
         *
         * 执行时机：在生物完全初始化并加入地图后调用。
         *
         * 典型用途：
         * - 初始化生物状态
         * - 设置初始位置
         * - 开始巡逻
         */
        virtual void JustAppeared();

        /**
         * @brief 移动完成时的回调
         *
         * 当到达航点或点移动完成时调用。
         *
         * 移动类型包括：
         * - POINT_MOTION_TYPE: 点移动
         * - WAYPOINT_MOTION_TYPE: 航点移动
         * - CHARGE_MOTION_TYPE: 冲锋
         * - FLEEING_MOTION_TYPE: 恐惧逃跑
         * 等
         *
         * 典型用途：
         * - 到达巡逻点后执行动作
         * - 完成移动后触发事件
         * - 航点巡逻逻辑
         *
         * @param type 移动类型（枚举值见MotionMaster.h）
         * @param id 点位ID（由具体的移动生成器定义）
         */
        virtual void MovementInform(uint32 /*type*/, uint32 /*id*/) { }

        /**
         * @brief 被魅惑状态改变时的回调
         *
         * 当生物被魅惑或魅惑解除时调用。
         * 魅惑状态会改变生物的所有权关系。
         *
         * 魅惑类型：
         * - 玩家魅惑生物（如牧师的精神控制）
         * - 生物魅惑玩家（需要实现GetAIForCharmedPlayer）
         *
         * @param isNew true如果是新魅惑，false如果是解除
         */
        void OnCharmed(bool isNew) override;

        /**
         * @brief 返回出生点后的回调
         *
         * 当脱离战斗并返回到出生点后调用。
         * 用于重置Boss状态或执行清理工作。
         *
         * 执行时机：
         * 1. EnterEvadeMode触发脱离战斗
         * 2. 生物传送或移动回出生点
         * 3. 到达后调用JustReachedHome
         *
         * 典型用途：
         * - 重置Boss战斗阶段
         * - 清除战斗中的临时效果
         * - 恢复初始状态（生命值、法力值等）
         * - 重新开始巡逻
         */
        virtual void JustReachedHome() { }

        /**
         * @brief 使区域内的单位进入战斗
         *
         * 使指定区域内的所有敌对单位进入战斗状态。
         * 常用于Boss战斗的开始，激活区域内所有相关敌人。
         *
         * 工作原理：
         * 1. 获取指定生物（或自身）附近的敌对单位列表
         * 2. 对每个敌对单位添加威胁值
         * 3. 触发进入战斗流程
         *
         * 搜索范围：由creature_template的DetectionRange决定。
         *
         * 性能说明：O(n)复杂度，n为附近单位数量。
         * 需要遍历附近所有单位。
         *
         * @param creature 指定的生物（nullptr则使用自身）
         */
        void DoZoneInCombat(Creature* creature = nullptr);

        /**
         * @brief 接收表情时的回调
         *
         * 当玩家对生物使用文本表情时调用。
         * 文本表情如：/wave, /bow, /cheer 等。
         *
         * 典型用途：
         * - 任务交互（如用/kiss表达爱意）
         * - 特殊NPC响应玩家表情
         * - 成就相关（如对某些NPC使用特定表情）
         *
         * @param player 使用表情的玩家
         * @param emoteId 表情ID（对应emotes文本表）
         */
        virtual void ReceiveEmote(Player* /*player*/, uint32 /*emoteId*/) { }

        /**
         * @brief 主人被攻击时的回调
         *
         * 当召唤者/主人被攻击时调用。
         * 用于宠物和守卫AI，使召唤物保护主人。
         *
         * 典型行为：
         * - 宠物转向攻击攻击者
         * - 守卫协助主人防御
         *
         * @param attacker 攻击者
         */
        virtual void OwnerAttackedBy(Unit* attacker) { OnOwnerCombatInteraction(attacker); }

        /**
         * @brief 主人攻击目标时的回调
         *
         * 当召唤者/主人攻击某目标时调用。
         * 用于宠物和守卫AI，使召唤物协助主人攻击。
         *
         * 典型行为：
         * - 宠物攻击主人指定的目标
         * - 守卫协助主人进攻
         *
         * @param target 被攻击的目标
         */
        virtual void OwnerAttacked(Unit* target) { OnOwnerCombatInteraction(target); }

        /// @}

        /// @name 触发动作请求
        /// @{

        // Called when creature attack expected (if creature can and no have current victim)
        //virtual void AttackStart(Unit*) { }

        // Called at World update tick
        //virtual void UpdateAI(const uint32 /*diff*/) { }

        /// @}

        /// @name 状态检查
        /// @{

        // Is unit visible for MoveInLineOfSight
        //virtual bool IsVisible(Unit*) const { return false; }

        /**
         * @brief 尸体被移除时的回调
         *
         * 当生物的尸体被移除时调用。
         * 可以修改重生延迟时间。
         *
         * 尸体移除时机：
         * - 尸体腐烂时间到达
         * - 被搜刮完毕
         * - 世界清理
         *
         * 典型用途：
         * - 延长或缩短重生时间
         * - 根据击杀者调整重生策略
         * - 特殊Boss的重生逻辑
         *
         * @param respawnDelay 重生延迟时间（可修改，单位：秒）
         */
        virtual void CorpseRemoved(uint32& /*respawnDelay*/) { }

        // Called when victim entered water and creature can not enter water
        //virtual bool CanReachByRangeAttack(Unit*) { return false; }

        /// @}

        /// @name 对话系统
        /// @{

        /**
         * @brief 获取对话状态
         *
         * 当请求玩家与生物之间的对话状态时调用。
         * 用于确定任务标记等。
         *
         * 返回值说明：
         * - DIALOG_STATUS_NONE: 无对话
         * - DIALOG_STATUS_UNAVAILABLE: 有对话但不可用
         * - DIALOG_STATUS_CHAT: 普通对话
         * - DIALOG_STATUS_INCOMPLETE: 任务进行中
         * - DIALOG_STATUS_REWARD_REP: 任务可完成（有奖励）
         * - DIALOG_STATUS_AVAILABLE: 任务可接受
         * - DIALOG_STATUS_REWARD: 任务可完成
         *
         * 典型用途：
         * - 自定义任务NPC的标记显示
         * - 根据玩家状态调整对话选项
         *
         * @param player 请求对话的玩家
         * @return Optional<QuestGiverStatus> 任务给予者状态（空表示使用默认值）
         */
        virtual Optional<QuestGiverStatus> GetDialogStatus(Player* /*player*/) { return {}; }

        /**
         * @brief 打开对话菜单时的回调
         *
         * 当玩家打开与生物的对话菜单时调用。
         * 对话菜单是右键点击NPC时显示的菜单。
         *
         * 典型用途：
         * - 发送对话选项包
         * - 触发对话事件
         * - 任务NPC的初始对话
         *
         * 返回值：
         * - true: 事件已处理，不再执行默认逻辑
         * - false: 继续执行默认的对话菜单逻辑
         *
         * @param player 打开对话的玩家
         * @return true 如果处理了事件
         * @return false 如果未处理（使用默认行为）
         */
        virtual bool OnGossipHello(Player* /*player*/) { return false; }

        /**
         * @brief 选择对话选项时的回调
         *
         * 当玩家在生物的对话菜单中选择一个选项时调用。
         *
         * 参数说明：
         * - menuId: 对话菜单ID（对应gossip_menu表）
         * - gossipListId: 选项在列表中的索引（从0开始）
         *
         * 典型用途：
         * - 处理玩家选择的对话选项
         * - 传送、购买物品、学习技能等
         * - 触发任务或事件
         *
         * @param player 选择的玩家
         * @param menuId 菜单ID
         * @param gossipListId 对话列表ID（选项索引）
         * @return true 如果处理了事件
         * @return false 如果未处理
         */
        virtual bool OnGossipSelect(Player* /*player*/, uint32 /*menuId*/, uint32 /*gossipListId*/) { return false; }

        /**
         * @brief 选择带代码输入的对话选项时的回调
         *
         * 当玩家选择需要输入代码的对话选项时调用。
         * 常用于需要玩家输入特定字符串的场景。
         *
         * 典型用途：
         * - GM命令NPC
         * - 密码验证
         * - 自定义输入（如兑换码）
         *
         * @param player 选择的玩家
         * @param menuId 菜单ID
         * @param gossipListId 对话列表ID
         * @param code 玩家输入的代码字符串
         * @return true 如果处理了事件
         * @return false 如果未处理
         */
        virtual bool OnGossipSelectCode(Player* /*player*/, uint32 /*menuId*/, uint32 /*gossipListId*/, char const* /*code*/) { return false; }

        /**
         * @brief 接受任务时的回调
         *
         * 当玩家从生物接受任务时调用。
         * 在任务被添加到玩家任务列表后触发。
         *
         * 典型用途：
         * - 触发任务相关事件
         * - 给予任务物品
         * - 开始护送任务
         *
         * @param player 接受任务的玩家
         * @param quest 接受的任务对象
         */
        virtual void OnQuestAccept(Player* /*player*/, Quest const* /*quest*/) { }

        /**
         * @brief 完成任务并获得奖励时的回调
         *
         * 当玩家完成任务并获得奖励时调用。
         * 在奖励发放后触发。
         *
         * opt参数说明：
         * - 0: 无选择或默认奖励
         * - 1+: 选择的奖励物品索引（从1开始）
         *
         * 典型用途：
         * - 触发任务完成事件
         * - 开始后续任务链
         * - 更新相关状态
         *
         * @param player 完成任务的玩家
         * @param quest 完成的任务对象
         * @param opt 选择的奖励物品索引（0表示无选择）
         */
        virtual void OnQuestReward(Player* /*player*/, Quest const* /*quest*/, uint32 /*opt*/) { }

        /// @}

        /// @name 航点系统
        /// @{

        /**
         * @brief 航点开始时的回调
         *
         * 当开始向某个航点移动时调用。
         * 在生物开始移动到航点时立即触发。
         *
         * 航点系统说明：
         * - nodeId: 航点在路径中的序号（从0开始）
         * - pathId: 航点路径ID（对应waypoints表或waypoint_data表）
         *
         * 典型用途：
         * - 在移动到特定航点前执行准备动作
         * - 记录巡逻进度
         *
         * @param nodeId 航点节点ID（路径中的序号）
         * @param pathId 航点路径ID
         */
        virtual void WaypointStarted(uint32 /*nodeId*/, uint32 /*pathId*/) { }

        /**
         * @brief 到达航点时的回调
         *
         * 当到达某个航点时调用。
         * 在生物到达航点位置后触发。
         *
         * 典型用途：
         * - 在特定航点执行动作（如说话、施法等）
         * - 改变巡逻行为
         * - 触发事件
         *
         * 常见操作：
         * - 播放表情动画
         * - 与环境交互（如打开门）
         * - 与玩家交互（如任务NPC巡逻点）
         *
         * @param nodeId 航点节点ID
         * @param pathId 航点路径ID
         */
        virtual void WaypointReached(uint32 /*nodeId*/, uint32 /*pathId*/) { }

        /**
         * @brief 航点路径结束时的回调
         *
         * 当完成整个航点路径时调用。
         * 在最后一个航点到达后触发。
         *
         * 典型用途：
         * - 循环巡逻（重新开始）
         * - 完成巡逻后的特殊处理
         * - 触发路径完成事件
         *
         * 路径行为：
         * - 循环路径：会重新从第一个航点开始
         * - 单次路径：停在最后一个航点
         *
         * @param nodeId 最后的航点节点ID
         * @param pathId 航点路径ID
         */
        virtual void WaypointPathEnded(uint32 /*nodeId*/, uint32 /*pathId*/) { }

        /// @}

        /// @name 其他功能
        /// @{

        /**
         * @brief 乘客登载时的回调
         *
         * 当单位登上此生物（作为载具）或离开时调用。
         * 载具系统包括坐骑、载具、飞行工具等。
         *
         * 座位系统：
         * - 不同的座位有不同的功能（驾驶位、乘客位、炮台等）
         * - seatId对应vehicle_seat_entry表
         *
         * 典型用途：
         * - 检查乘客类型
         * - 修改载具状态
         * - 触发登载/离开事件
         *
         * @param passenger 乘客单位（玩家或生物）
         * @param seatId 座位ID（负数表示无效座位）
         * @param apply true为登载，false为离开
         */
        virtual void PassengerBoarded(Unit* /*passenger*/, int8 /*seatId*/, bool /*apply*/) { }

        /**
         * @brief 法术点击时的回调
         *
         * 当玩家点击生物施放法术时调用。
         * 法术点击是一种特殊的交互方式，点击生物即可触发法术效果。
         *
         * 法术点击配置：
         * - 在npc_spellclick_spells表中定义
         * - 可以设置施放条件
         *
         * 典型用途：
         * - 响应法术点击事件
         * - 执行额外逻辑（如任务进度）
         *
         * @param clicker 点击者（通常是玩家）
         * @param spellClickHandled 法术点击是否已被处理
         *                          - true: 法术已施放
         *                          - false: 法术未施放（条件不满足等）
         */
        virtual void OnSpellClick(Unit* /*clicker*/, bool /*spellClickHandled*/) { }

        /**
         * @brief 是否始终能看到某对象
         *
         * 检查此生物是否始终能看到指定的世界对象，
         * 不考虑可见性规则。
         *
         * 典型用途：
         * - 特殊的视觉效果（如Boss始终看到玩家）
         * - 任务相关对象（如护送目标始终可见）
         * - 隐藏NPC的特殊检测规则
         *
         * 性能说明：此函数可能频繁调用，避免复杂计算。
         *
         * @param obj 目标对象
         * @return true 如果始终可见（跳过正常可见性检查）
         * @return false 如果需要正常可见性检查
         */
        virtual bool CanSeeAlways(WorldObject const* /*obj*/) { return false; }

        /**
         * @brief 获取魅惑玩家的AI
         *
         * 当玩家被此生物魅惑时调用。
         * 如果返回PlayerAI指针，玩家将使用该AI而不是默认的魅惑AI。
         *
         * 典型用途：
         * - 自定义被魅惑玩家的行为
         * - 特殊的载具控制（如载具战）
         *
         * 内存管理：
         * - 返回的PlayerAI对象由Unit::RemoveCharmedBy负责销毁
         * - 不要在AI中手动删除返回的对象
         *
         * @note 对象销毁由Unit::RemoveCharmedBy处理
         *
         * @param who 被魅惑的玩家
         * @return PlayerAI* 自定义AI指针，nullptr使用默认魅惑AI
         */
        virtual PlayerAI* GetAIForCharmedPlayer(Player* /*who*/) { return nullptr; }

        /**
         * @brief 可视化边界
         *
         * 用于战斗设计/调试，生成边界的可视化效果。
         * 通过在边界区域生成临时游戏对象来显示边界范围。
         *
         * 可视化效果：
         * - 边界线：显示边界轮廓
         * - 填充：显示边界内部区域
         *
         * 性能警告：此函数开销较大，仅用于调试目的。
         * 不要在正式版本中使用。
         *
         * @param duration 可视化持续时间（秒）
         * @param owner 所有者单位（nullptr使用自身）
         * @param fill 是否填充边界区域
         * @return int32 操作结果（生成的可视化对象数量）
         */
        int32 VisualizeBoundary(Seconds duration, Unit* owner = nullptr, bool fill = false) const;

        /// @}

        /// @name 边界系统
        /// @{

        /**
         * @brief 检查是否在房间/边界内
         *
         * 检查生物是否在其活动边界内。
         * 如果生物在边界外，可能会触发脱离战斗。
         *
         * 边界系统说明：
         * - 边界用于限制生物的活动范围（如Boss房间）
         * - 正向边界：生物只能在边界内活动
         * - 反向边界：生物不能在边界内活动
         *
         * 调用时机：
         * - UpdateAI中定期检查
         * - 移动时检查
         * - 脱离战斗判断时检查
         *
         * 失败处理：
         * - 如果不在边界内，触发EnterEvadeMode(EVADE_REASON_BOUNDARY)
         *
         * @return true 如果在边界内（或无边界限制）
         * @return false 如果在边界外
         */
        virtual bool CheckInRoom();

        /**
         * @brief 获取边界
         *
         * 获取生物的活动边界。
         *
         * @return CreatureBoundary const* 边界指针，nullptr表示无边界
         */
        CreatureBoundary const* GetBoundary() const { return _boundary; }

        /**
         * @brief 设置边界
         *
         * 设置生物的活动边界限制。
         * 生物离开边界时会触发脱离战斗。
         *
         * 边界类型：
         * - 正向边界（negateBoundary=false）：
         *   生物只能在边界内活动，离开边界触发脱离战斗
         * - 反向边界（negateBoundary=true）：
         *   生物不能在边界内活动，进入边界触发脱离战斗
         *
         * 典型用途：
         * - Boss房间限制
         * - 事件区域限制
         * - 特殊地形限制
         *
         * 内存管理：边界对象由外部管理，AI不负责销毁。
         *
         * @param boundary 边界数据指针（可以为nullptr清除边界）
         * @param negativeBoundaries 是否为反向边界（默认false）
         */
        void SetBoundary(CreatureBoundary const* boundary, bool negativeBoundaries = false);

        /**
         * @brief 静态方法：检查位置是否在边界内
         *
         * 检查指定位置是否在给定的边界内。
         * 提供静态接口，供其他类使用。
         *
         * 边界检查算法：
         * - 遍历所有边界条件
         * - 所有条件都满足才返回true
         *
         * 性能说明：O(n)复杂度，n为边界数量。
         *
         * @param boundary 边界数据
         * @param who 要检查的位置
         * @return true 如果在边界内
         * @return false 如果在边界外
         */
        static bool IsInBounds(CreatureBoundary const& boundary, Position const* who);

        /**
         * @brief 检查位置是否在边界内
         *
         * 检查指定位置是否在此生物的边界内。
         * 这是实例方法，使用当前AI的边界设置。
         *
         * 边界处理：
         * - 无边界时返回true
         * - 反向边界时取反结果
         *
         * @param who 要检查的位置（nullptr使用生物当前位置）
         * @return true 如果在边界内
         * @return false 如果在边界外或无边界
         */
        bool IsInBoundary(Position const* who = nullptr) const;

        /// @}

    protected:
        /**
         * @brief 开始交战
         *
         * 标记生物进入交战状态。
         * 设置_isEngaged标志为true。
         *
         * 调用时机：
         * - JustStartedThreateningMe中
         * - JustEnteredCombat中
         *
         * 注意：此函数仅设置状态标志，不执行其他逻辑。
         *
         * @param who 交战目标（用于日志记录）
         */
        void EngagementStart(Unit* who);

        /**
         * @brief 结束交战
         *
         * 标记生物退出交战状态。
         * 设置_isEngaged标志为false。
         *
         * 调用时机：
         * - EnterEvadeMode中
         * - JustDied中
         *
         * 注意：此函数仅清除状态标志，不执行其他逻辑。
         */
        void EngagementOver();

        /**
         * @brief 视线检测处理
         *
         * 处理单位进入视线范围的逻辑。
         * 子类可重写以实现自定义行为。
         *
         * 默认行为：
         * - 检查是否可以攻击
         * - 如果可以攻击，调用AttackStart
         *
         * 重写建议：
         * - 保持简洁，避免复杂逻辑
         * - 如果重写，确保处理攻击逻辑
         *
         * @param who 进入视线的单位
         */
        virtual void MoveInLineOfSight(Unit* /*who*/);

        /**
         * @brief 内部脱离战斗处理
         *
         * 执行脱离战斗的核心逻辑。
         * 此函数执行实际的脱离战斗操作。
         *
         * 执行流程：
         * 1. 检查是否可以脱离战斗（不死生物、正在战斗等）
         * 2. 清空威胁列表
         * 3. 停止攻击和移动
         * 4. 清除目标
         * 5. 重置战斗状态
         *
         * 不自动返回出生点，由EnterEvadeMode决定是否返回。
         *
         * @param why 脱离战斗原因
         * @return true 如果成功进入脱离战斗模式
         * @return false 如果未能进入（如不能脱离战斗）
         */
        bool _EnterEvadeMode(EvadeReason why = EVADE_REASON_OTHER);

        CreatureBoundary const* _boundary;   ///< 活动边界指针（由外部管理，AI不负责销毁）
        bool _negateBoundary;                ///< 是否为反向边界标志（true: 边界内禁止，false: 边界外禁止）

    private:
        /**
         * @brief 主人战斗交互处理
         *
         * 处理主人被攻击或攻击目标时的逻辑。
         * 内部辅助函数，被OwnerAttackedBy和OwnerAttacked调用。
         *
         * 执行逻辑：
         * 1. 检查目标是否有效
         * 2. 检查是否可以攻击目标
         * 3. 添加威胁值
         * 4. 如果未交战，开始交战
         *
         * @param target 相关目标（攻击者或被攻击目标）
         */
        void OnOwnerCombatInteraction(Unit* target);

        bool _isEngaged;        ///< 是否处于交战状态（true: 正在战斗，false: 非战斗）
        bool _moveInLOSLocked;  ///< 视线检测锁定标志（防止MoveInLineOfSight_Safe递归调用）
};

#endif
