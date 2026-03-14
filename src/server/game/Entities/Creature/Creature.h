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
 * @file Creature.h
 * @brief Creature 类定义 - 游戏中的生物实体系统
 *
 * 本文件定义了 Creature 类,它是游戏中所有生物(NPC、怪物等)的核心基类。
 * Creature 类继承自 Unit、GridObject 和 MapObject,提供了完整的生物行为系统,
 * 包括:
 * - AI 系统集成
 * - 移动和导航
 * - 战斗和仇恨
 * - 生命、死亡和重生
 * - 掉落和战利品
 * - 商贩功能
 * - 编队和群体行为
 * - 法术施放
 *
 * @see Unit
 * @see CreatureAI
 * @see CreatureTemplate
 * @see CreatureData
 */

#ifndef TRINITYCORE_CREATURE_H
#define TRINITYCORE_CREATURE_H

#include "Unit.h"
#include "Common.h"
#include "CreatureData.h"
#include "DatabaseEnvFwd.h"
#include "Duration.h"
#include "Loot.h"
#include "GridObject.h"
#include "MapObject.h"
#include <list>

class CreatureAI;
class CreatureGroup;
class Group;
class Quest;
class Player;
class SpellInfo;
class WorldSession;

enum MovementGeneratorType : uint8;

/**
 * @brief 商贩物品计数结构
 *
 * 用于跟踪商贩NPC出售物品的数量和时间信息。
 * 支持商贩物品的补充机制。
 */
struct VendorItemCount
{
    /**
     * @brief 构造函数
     * @param _item 物品ID
     * @param _count 物品数量
     */
    VendorItemCount(uint32 _item, uint32 _count);

    uint32 itemId;              ///< 物品ID
    uint32 count;               ///< 当前数量
    time_t lastIncrementTime;   ///< 上次增加数量的时间
};

/**
 * @brief 商贩物品计数列表类型
 *
 * 存储商贩NPC所有物品的计数信息列表。
 */
typedef std::list<VendorItemCount> VendorItemCounts;

/**
 * @def CREATURE_Z_ATTACK_RANGE
 * @brief 生物攻击时Z坐标的最大差异值
 *
 * 用于仇恨反应系统中,定义生物在攻击目标时允许的最大高度差。
 * 防止生物攻击距离过远的目标(如在不同高度层的玩家)。
 */
#define CREATURE_Z_ATTACK_RANGE 3

/**
 * @def MAX_VENDOR_ITEMS
 * @brief 商贩物品列表的最大数量限制
 *
 * 定义商贩NPC可以出售物品的最大数量。
 * 这是3.x.x版本客户端协议(SMSG_LIST_INVENTORY)的限制。
 */
#define MAX_VENDOR_ITEMS 150

/**
 * @brief 生物文本重复ID列表类型
 *
 * 存储文本ID的向量,用于处理非重复的随机文本显示。
 */
typedef std::vector<uint8> CreatureTextRepeatIds;

/**
 * @brief 生物文本重复组类型
 *
 * 按组管理文本重复ID的映射表,键为组ID,值为该组的文本ID列表。
 * 用于实现生物对话文本的不重复随机显示机制。
 */
typedef std::unordered_map<uint8, CreatureTextRepeatIds> CreatureTextRepeatGroup;

/**
 * @brief Creature 类 - 游戏中的生物实体基类
 *
 * Creature 类是游戏中所有生物（NPC、怪物等）的基类，继承自 Unit、GridObject 和 MapObject。
 * 它管理生物的属性、AI行为、移动、战斗、重生、尸体处理等核心功能。
 * 该类提供了生物与游戏世界交互的完整接口。
 */
class TC_GAME_API Creature : public Unit, public GridObject<Creature>, public MapObject
{
    public:
        /**
         * @brief 构造函数
         *
         * 创建一个生物实例。
         *
         * @param isWorldObject 是否为世界对象，默认为 false
         */
        explicit Creature(bool isWorldObject = false);

        /**
         * @brief 将生物添加到世界
         *
         * 重写 Unit::AddToWorld 方法。将生物添加到游戏世界中，
         * 初始化网格系统、AI系统等，并通知周围玩家生物的出现。
         */
        void AddToWorld() override;

        /**
         * @brief 将生物从世界中移除
         *
         * 重写 Unit::RemoveFromWorld 方法。将生物从游戏世界中移除，
         * 清理AI、网格系统等，并通知周围玩家生物的消失。
         */
        void RemoveFromWorld() override;

        /**
         * @brief 获取原生对象缩放比例
         *
         * 获取生物的默认缩放比例。
         *
         * @return 原生缩放比例
         */
        float GetNativeObjectScale() const override;

        /**
         * @brief 设置对象缩放比例
         *
         * 设置生物的缩放比例，更新外观大小。
         *
         * @param scale 新的缩放比例
         */
        void SetObjectScale(float scale) override;

        /**
         * @brief 设置显示模型ID
         *
         * 更改生物的显示模型，改变其外观。
         *
         * @param modelId 新的模型ID
         */
        void SetDisplayId(uint32 modelId) override;

        /**
         * @brief 让生物消失并死亡
         *
         * 立即强制生物消失并死亡，通过调用 ForcedDespawn(0) 实现。
         * 通常用于脚本或特殊情况下立即移除生物。
         */
        void DisappearAndDie() { ForcedDespawn(0); }

        /**
         * @brief 创建生物实例
         *
         * 根据给定的参数创建一个新的生物实例，初始化其位置、阶段掩码、
         * 模板数据等基本信息。
         *
         * @param guidlow 低GUID值
         * @param map 所在地图
         * @param phaseMask 阶段掩码
         * @param entry 生物模板ID（Entry）
         * @param pos 生成位置
         * @param data 生物数据（可选）
         * @param vehId 载具ID（可选）
         * @param dynamic 是否为动态生成（可选）
         * @return 创建成功返回 true，失败返回 false
         */
        bool Create(ObjectGuid::LowType guidlow, Map* map, uint32 phaseMask, uint32 entry, Position const& pos, CreatureData const* data = nullptr, uint32 vehId = 0, bool dynamic = false);
        /**
         * @brief 加载生物附加数据
         *
         * 从数据库加载生物的附加数据（CreatureAddon），包括
         * 特殊标志、光环、路径、装备等额外配置信息。
         *
         * @return 加载成功返回 true，失败返回 false
         */
        bool LoadCreaturesAddon();
        /**
         * @brief 选择等级
         *
         * 根据生物模板选择合适的等级，并计算相应的属性值。
         * 用于初始化或更新生物的等级相关属性。
         */
        void SelectLevel();
        /**
         * @brief 更新等级依赖属性
         *
         * 更新所有与等级相关的属性值。
         * 在等级变化时调用。
         */
        void UpdateLevelDependantStats();

        /**
         * @brief 加载装备
         *
         * 加载生物的装备模型，用于显示生物身上的装备。
         *
         * @param id 装备模板ID，默认为1
         * @param force 是否强制加载，默认为 false
         */
        void LoadEquipment(int8 id = 1, bool force = false);
        /**
         * @brief 设置生成时的生命值
         *
         * 将生物的生命值设置为生成时的初始值（通常为最大生命值）。
         * 在生物创建或重生时调用。
         */
        void SetSpawnHealth();
        /**
         * @brief 加载模板根数据
         *
         * 加载生物模板的根数据信息。
         */
        void LoadTemplateRoot();

        /**
         * @brief 获取生成ID
         *
         * 获取生物的生成ID，对于新建或临时生物为0，
         * 对于数据库保存的生物为低GUID。
         *
         * @return 生成ID
         */
        ObjectGuid::LowType GetSpawnId() const { return m_spawnId; }

        /**
         * @brief 更新生物状态
         *
         * 重写 Unit::Update 方法。每帧调用，处理生物的各种定时器、
         * AI更新、移动、生命值恢复等逻辑。这是生物的核心更新循环。
         *
         * @param time 自上次更新以来经过的时间（毫秒）
         */
        void Update(uint32 time) override;                         // overwrited Unit::Update
        /**
         * @brief 获取重生位置
         *
         * 获取生物的重生点坐标，即生物初始生成时的位置。
         *
         * @param x 输出参数：X坐标
         * @param y 输出参数：Y坐标
         * @param z 输出参数：Z坐标
         * @param ori 输出参数：朝向（可选）
         * @param dist 输出参数：距离（可选）
         */
        /**
         * @brief 获取重生位置
         *
         * 获取生物的重生点坐标，即生物初始生成时的位置。
         *
         * @param x 输出参数：X坐标
         * @param y 输出参数：Y坐标
         * @param z 输出参数：Z坐标
         * @param ori 输出参数：朝向（可选）
         * @param dist 输出参数：距离（可选）
         */
        void GetRespawnPosition(float &x, float &y, float &z, float* ori = nullptr, float* dist = nullptr) const;

        /**
         * @brief 检查是否生成在载具上
         *
         * 检查生物是否生成在载具（如船只、电梯）上。
         *
         * @return 在载具上生成返回 true，否则返回 false
         */
        bool IsSpawnedOnTransport() const { return m_creatureData && m_creatureData->mapId != GetMapId(); }

        /**
         * @brief 设置尸体延迟时间
         *
         * 设置生物死亡后尸体消失的延迟时间。
         *
         * @param delay 延迟时间（秒）
         * @param ignoreCorpseDecayRatio 是否忽略尸体衰减比率，默认为 false
         */
        void SetCorpseDelay(uint32 delay, bool ignoreCorpseDecayRatio = false)
        {
            m_corpseDelay = delay;
            if (ignoreCorpseDecayRatio)
                m_ignoreCorpseDecayRatio = true;
        }

        /**
         * @brief 获取尸体延迟时间
         *
         * 获取生物死亡后尸体消失的延迟时间。
         *
         * @return 尸体延迟时间（秒）
         */
        uint32 GetCorpseDelay() const { return m_corpseDelay; }

        /**
         * @brief 检查是否为种族领袖
         *
         * 检查该生物是否为种族领袖NPC。
         *
         * @return 是种族领袖返回 true，否则返回 false
         */
        bool IsRacialLeader() const { return GetCreatureTemplate()->RacialLeader; }

        /**
         * @brief 检查是否为平民
         *
         * 检查该生物是否为平民NPC，平民不会主动攻击玩家。
         *
         * @return 是平民返回 true，否则返回 false
         */
        bool IsCivilian() const { return (GetCreatureTemplate()->flags_extra & CREATURE_FLAG_EXTRA_CIVILIAN) != 0; }

        /**
         * @brief 检查是否为触发器
         *
         * 检查该生物是否为触发器类型，触发器通常不可见且用于特殊用途。
         *
         * @return 是触发器返回 true，否则返回 false
         */
        bool IsTrigger() const { return (GetCreatureTemplate()->flags_extra & CREATURE_FLAG_EXTRA_TRIGGER) != 0; }

        /**
         * @brief 检查是否为守卫
         *
         * 检查该生物是否为守卫NPC。
         *
         * @return 是守卫返回 true，否则返回 false
         */
        bool IsGuard() const { return (GetCreatureTemplate()->flags_extra & CREATURE_FLAG_EXTRA_GUARD) != 0; }

        /**
         * @brief 初始化移动标志
         *
         * 初始化生物的移动能力标志（如行走、游泳、飞行等）。
         */
        void InitializeMovementFlags();

        /**
         * @brief 更新移动标志
         *
         * 根据当前状态更新生物的移动能力标志。
         */
        void UpdateMovementFlags();

        /**
         * @brief 获取移动模板数据
         *
         * 获取生物的移动能力模板配置。
         *
         * @return 移动模板数据常量引用
         */
        CreatureMovementData const& GetMovementTemplate() const;

        /**
         * @brief 检查是否可以行走
         *
         * 检查生物是否具有地面移动能力。
         *
         * @return 可以行走返回 true，否则返回 false
         */
        bool CanWalk() const { return GetMovementTemplate().IsGroundAllowed(); }

        /**
         * @brief 检查是否可以游泳
         *
         * 检查生物是否可以在水中游泳。
         *
         * @return 可以游泳返回 true，否则返回 false
         */
        bool CanSwim() const override;

        /**
         * @brief 检查是否可以进入水中
         *
         * 检查生物是否可以进入水域。
         *
         * @return 可以进入水中返回 true，否则返回 false
         */
        bool CanEnterWater() const override;

        /**
         * @brief 检查是否可以飞行
         *
         * 检查生物是否具有飞行能力。
         *
         * @return 可以飞行返回 true，否则返回 false
         */
        bool CanFly()  const override { return GetMovementTemplate().IsFlightAllowed() || IsFlying(); }

        /**
         * @brief 检查是否可以悬停
         *
         * 检查生物是否可以悬停在空中。
         *
         * @return 可以悬停返回 true，否则返回 false
         */
        bool CanHover() const { return GetMovementTemplate().Ground == CreatureGroundMovementType::Hover || IsHovering(); }

        /**
         * @brief 获取默认移动类型
         *
         * 获取生物的默认移动生成器类型。
         *
         * @return 默认移动类型
         */
        MovementGeneratorType GetDefaultMovementType() const override { return m_defaultMovementType; }

        /**
         * @brief 设置默认移动类型
         *
         * 设置生物的默认移动生成器类型。
         *
         * @param mgt 移动生成器类型
         */
        void SetDefaultMovementType(MovementGeneratorType mgt) { m_defaultMovementType = mgt; }

        /**
         * @brief 检查是否为副本BOSS
         *
         * 检查生物是否标记为副本BOSS。副本BOSS有特殊的机制，
         * 如脱战重置、特殊掉落等。
         *
         * @return 是副本BOSS返回 true，否则返回 false
         */
        /**
         * @brief 检查是否为副本BOSS
         *
         * 检查生物是否标记为副本BOSS。副本BOSS有特殊的机制，
         * 如脱战重置、特殊掉落等。
         *
         * @return 是副本BOSS返回 true，否则返回 false
         */
        bool IsDungeonBoss() const { return (GetCreatureTemplate()->flags_extra & CREATURE_FLAG_EXTRA_DUNGEON_BOSS) != 0; }

        /**
         * @brief 检查是否受递减收益影响
         *
         * 检查该生物是否受到控制效果的递减收益影响。
         *
         * @return 受递减收益影响返回 true，否则返回 false
         */
        bool IsAffectedByDiminishingReturns() const override { return Unit::IsAffectedByDiminishingReturns() || (GetCreatureTemplate()->flags_extra & CREATURE_FLAG_EXTRA_ALL_DIMINISH) != 0; }

        /**
         * @brief 选择攻击目标
         *
         * 为生物选择一个合适的攻击目标（受害者）。该方法会根据威胁值、
         * 距离、视野等因素选择最合适的目标进行攻击。用于AI决策。
         *
         * @return 返回选中的攻击目标 Unit 指针，如果没有合适目标则返回 nullptr
         */
        Unit* SelectVictim();

        /**
         * @brief 设置反应状态
         *
         * 设置生物的反应状态，决定生物对周围环境的反应方式
         * （如被动、攻击性、防御性等）。
         *
         * @param st 反应状态枚举值
         */
        void SetReactState(ReactStates st) { m_reactState = st; }

        /**
         * @brief 获取反应状态
         *
         * 获取生物当前的反应状态。
         *
         * @return 当前的反应状态
         */
        ReactStates GetReactState() const { return m_reactState; }

        /**
         * @brief 检查是否具有指定反应状态
         *
         * 检查生物是否处于指定的反应状态。
         *
         * @param state 要检查的反应状态
         * @return 具有该状态返回 true，否则返回 false
         */
        bool HasReactState(ReactStates state) const { return (m_reactState == state); }
        /**
         * @brief 初始化反应状态
         *
         * 根据生物模板初始化生物的反应状态。
         * 通常在生物创建时调用。
         */
        void InitializeReactState();

        // 使用 Unit 类的免疫方法
        using Unit::IsImmuneToAll;
        using Unit::SetImmuneToAll;

        /**
         * @brief 设置对所有伤害免疫
         *
         * 设置生物对所有伤害类型的免疫状态。
         *
         * @param apply true 表示应用免疫，false 表示移除免疫
         */
        void SetImmuneToAll(bool apply) override { Unit::SetImmuneToAll(apply, HasReactState(REACT_PASSIVE)); }

        using Unit::IsImmuneToPC;
        using Unit::SetImmuneToPC;

        /**
         * @brief 设置对玩家免疫
         *
         * 设置生物对玩家伤害的免疫状态。
         *
         * @param apply true 表示应用免疫，false 表示移除免疫
         */
        void SetImmuneToPC(bool apply) override { Unit::SetImmuneToPC(apply, HasReactState(REACT_PASSIVE)); }

        using Unit::IsImmuneToNPC;
        using Unit::SetImmuneToNPC;

        /**
         * @brief 设置对NPC免疫
         *
         * 设置生物对NPC伤害的免疫状态。
         *
         * @param apply true 表示应用免疫，false 表示移除免疫
         */
        void SetImmuneToNPC(bool apply) override { Unit::SetImmuneToNPC(apply, HasReactState(REACT_PASSIVE)); }

        /**
         * @brief 检查是否可以与战场军官交互
         *
         * 检查玩家是否可以与该战场军官NPC交互。
         *
         * @param player 玩家对象
         * @param msg 是否显示错误消息
         * @return 可以交互返回 true，否则返回 false
         */
        bool isCanInteractWithBattleMaster(Player* player, bool msg) const;

        /**
         * @brief 检查是否可以重置天赋
         *
         * 检查玩家是否可以在该NPC处重置天赋。
         *
         * @param player 玩家对象
         * @param pet 是否为宠物天赋
         * @return 可以重置返回 true，否则返回 false
         */
        bool CanResetTalents(Player* player, bool pet) const;

        /**
         * @brief 检查生物是否可以攻击目标
         *
         * 检查该生物是否可以对指定受害者发起攻击。
         *
         * @param victim 目标单位
         * @param force 是否强制攻击，默认为 true
         * @return 可以攻击返回 true，否则返回 false
         */
        bool CanCreatureAttack(Unit const* victim, bool force = true) const;

        /**
         * @brief 加载模板免疫信息
         *
         * 从生物模板加载免疫信息，设置生物对特定法术或效果的免疫。
         */
        void LoadTemplateImmunities();

        /**
         * @brief 检查是否免疫指定法术
         *
         * 检查生物是否对指定法术免疫。
         *
         * @param spellInfo 法术信息
         * @param caster 施法者
         * @param requireImmunityPurgesEffectAttribute 是否需要免疫清除效果属性
         * @return 免疫返回 true，否则返回 false
         */
        bool IsImmunedToSpell(SpellInfo const* spellInfo, WorldObject const* caster, bool requireImmunityPurgesEffectAttribute = false) const override;

        /**
         * @brief 检查是否免疫法术效果
         *
         * 检查生物是否对指定法术效果免疫。
         *
         * @param spellInfo 法术信息
         * @param spellEffectInfo 法术效果信息
         * @param caster 施法者
         * @param requireImmunityPurgesEffectAttribute 是否需要免疫清除效果属性
         * @return 免疫返回 true，否则返回 false
         */
        bool IsImmunedToSpellEffect(SpellInfo const* spellInfo, SpellEffectInfo const& spellEffectInfo, WorldObject const* caster, bool requireImmunityPurgesEffectAttribute = false) const override;
        /**
         * @brief 检查是否为精英生物
         *
         * 检查生物是否为精英类型，精英生物通常有更强的属性和特殊标识。
         *
         * @return 是精英返回 true，否则返回 false
         */
        bool isElite() const;

        /**
         * @brief 检查是否为世界BOSS
         *
         * 检查生物是否为世界BOSS，世界BOSS有特殊的掉落和机制。
         *
         * @return 是世界BOSS返回 true，否则返回 false
         */
        bool isWorldBoss() const;

        /**
         * @brief 获取针对目标的等级
         *
         * 重写 Unit::GetLevelForTarget 方法，为BOSS等级支持提供特殊处理。
         * BOSS的等级会根据玩家等级动态调整显示（如显示为 "??" 或 "骷髅"）。
         *
         * @param target 目标对象
         * @return 针对该目标的等级
         */
        uint8 GetLevelForTarget(WorldObject const* target) const override;

        /**
         * @brief 检查是否处于脱战模式
         *
         * 检查生物是否正在脱离战斗状态，即处于逃跑/重置过程中。
         *
         * @return 处于脱战模式返回 true，否则返回 false
         */
        bool IsInEvadeMode() const { return HasUnitState(UNIT_STATE_EVADE); }

        /**
         * @brief 检查是否正在躲避攻击
         *
         * 检查生物是否处于脱战模式或无法到达目标状态。
         * 这种情况下生物不会正常攻击。
         *
         * @return 正在躲避攻击返回 true，否则返回 false
         */
        bool IsEvadingAttacks() const { return IsInEvadeMode() || CanNotReachTarget(); }

        /**
         * @brief 销毁AI对象
         *
         * 销毁并清理当前生物关联的AI对象。
         *
         * @return 销毁成功返回 true，失败返回 false
         */
        bool AIM_Destroy();

        /**
         * @brief 创建AI对象
         *
         * 为生物创建并关联一个AI对象。如果未指定AI对象，
         * 将根据生物模板自动选择合适的AI类型。
         *
         * @param ai 可选的自定义AI对象指针，默认为 nullptr
         * @return 创建成功返回 true，失败返回 false
         */
        bool AIM_Create(CreatureAI* ai = nullptr);

        /**
         * @brief 初始化AI
         *
         * 初始化生物的AI系统，设置AI对象并准备AI的初始状态。
         * 通常在生物创建或重生时调用。
         *
         * @param ai 可选的自定义AI对象指针，默认为 nullptr
         * @return 初始化成功返回 true，失败返回 false
         */
        bool AIM_Initialize(CreatureAI* ai = nullptr);
        /**
         * @brief 初始化移动系统
         *
         * 初始化生物的移动生成器，根据默认移动类型设置初始移动行为。
         * 在生物创建或重生后调用。
         */
        void Motion_Initialize();

        /**
         * @brief 获取生物的AI对象
         *
         * 返回该生物关联的 AI 对象指针，用于控制生物的行为逻辑。
         * 如果生物没有AI，则返回 nullptr。
         *
         * @return 返回 CreatureAI 指针
         */
        CreatureAI* AI() const { return reinterpret_cast<CreatureAI*>(GetAI()); }

        /**
         * @brief 设置行走模式
         *
         * 设置生物是否使用行走模式（而非跑步）。
         *
         * @param enable true 为行走，false 为跑步
         * @return 设置成功返回 true，失败返回 false
         */
        bool SetWalk(bool enable) override;

        /**
         * @brief 设置禁用重力
         *
         * 设置生物是否受重力影响。
         *
         * @param disable true 为禁用重力，false 为启用重力
         * @param packetOnly 是否仅发送数据包，默认为 false
         * @param updateAnimTier 是否更新动画层级，默认为 true
         * @return 设置成功返回 true，失败返回 false
         */
        bool SetDisableGravity(bool disable, bool packetOnly = false, bool updateAnimTier = true) override;

        /**
         * @brief 设置游泳模式
         *
         * 设置生物是否进入游泳状态。
         *
         * @param enable true 为启用游泳，false 为禁用
         * @return 设置成功返回 true，失败返回 false
         */
        bool SetSwim(bool enable) override;

        /**
         * @brief 设置飞行能力
         *
         * 设置生物是否可以飞行。
         *
         * @param enable true 为启用飞行，false 为禁用
         * @param packetOnly 是否仅发送数据包，默认为 false
         * @return 设置成功返回 true，失败返回 false
         */
        bool SetCanFly(bool enable, bool packetOnly = false) override;

        /**
         * @brief 设置水上行走
         *
         * 设置生物是否可以在水面上行走。
         *
         * @param enable true 为启用水上行走，false 为禁用
         * @param packetOnly 是否仅发送数据包，默认为 false
         * @return 设置成功返回 true，失败返回 false
         */
        bool SetWaterWalking(bool enable, bool packetOnly = false) override;

        /**
         * @brief 设置缓落效果
         *
         * 设置生物是否受缓落效果影响（减缓下落速度）。
         *
         * @param enable true 为启用缓落，false 为禁用
         * @param packetOnly 是否仅发送数据包，默认为 false
         * @return 设置成功返回 true，失败返回 false
         */
        bool SetFeatherFall(bool enable, bool packetOnly = false) override;

        /**
         * @brief 设置悬停状态
         *
         * 设置生物是否处于悬停状态。
         *
         * @param enable true 为启用悬停，false 为禁用
         * @param packetOnly 是否仅发送数据包，默认为 false
         * @param updateAnimTier 是否更新动画层级，默认为 true
         * @return 设置成功返回 true，失败返回 false
         */
        bool SetHover(bool enable, bool packetOnly = false, bool updateAnimTier = true) override;

        /**
         * @brief 获取盾牌格挡值
         *
         * 获取生物的盾牌格挡数值。
         *
         * @return 盾牌格挡值
         */
        uint32 GetShieldBlockValue() const override;

        /**
         * @brief 获取近战伤害法术学校掩码
         *
         * 获取生物近战攻击的伤害类型掩码（物理、火焰等）。
         *
         * @param attackType 攻击类型，默认为 BASE_ATTACK
         * @param damageIndex 伤害索引，默认为 0
         * @return 法术学校掩码
         */
        SpellSchoolMask GetMeleeDamageSchoolMask(WeaponAttackType /*attackType*/ = BASE_ATTACK, uint8 /*damageIndex*/ = 0) const override { return m_meleeDamageSchoolMask; }

        /**
         * @brief 设置近战伤害学校
         *
         * 设置生物近战攻击的伤害类型（如火焰、冰霜等）。
         *
         * @param school 法术学校类型
         */
        void SetMeleeDamageSchool(SpellSchools school) { m_meleeDamageSchoolMask = SpellSchoolMask(1 << school); }

        /**
         * @brief 检查是否拥有法术
         *
         * 检查生物是否拥有指定ID的法术。
         *
         * @param spellID 法术ID
         * @return 拥有该法术返回 true，否则返回 false
         */
        bool HasSpell(uint32 spellID) const override;

        /**
         * @brief 更新生物模板
         *
         * 更改生物的模板ID，重新加载模板数据。
         * 用于动态改变生物的外观和属性。
         *
         * @param entry 新的生物模板ID
         * @param data 生物数据（可选）
         * @param updateLevel 是否更新等级，默认为 true
         * @return 更新成功返回 true，失败返回 false
         */
        /**
         * @brief 更新生物模板
         *
         * 更改生物的模板ID，重新加载模板数据。
         * 用于动态改变生物的外观和属性。
         *
         * @param entry 新的生物模板ID
         * @param data 生物数据（可选）
         * @param updateLevel 是否更新等级，默认为 true
         * @return 更新成功返回 true，失败返回 false
         */
        bool UpdateEntry(uint32 entry, CreatureData const* data = nullptr, bool updateLevel = true);

        /**
         * @brief 设置阶段掩码
         *
         * 重写 Unit::SetPhaseMask 方法。设置生物的阶段掩码，
         * 决定生物在哪些阶段可见。
         *
         * @param newPhaseMask 新的阶段掩码
         * @param update 是否立即更新，默认为 true
         */
        void SetPhaseMask(uint32 newPhaseMask, bool update) override;

        /**
         * @brief 更新指定属性
         *
         * 重写 Unit::UpdateStats 方法。更新生物的指定属性值（如力量、敏捷等）。
         *
         * @param stat 要更新的属性类型
         * @return 更新成功返回 true，失败返回 false
         */
        bool UpdateStats(Stats stat) override;

        /**
         * @brief 更新所有属性
         *
         * 重写 Unit::UpdateAllStats 方法。更新生物的所有属性值。
         *
         * @return 更新成功返回 true，失败返回 false
         */
        bool UpdateAllStats() override;
        /**
         * @brief 更新抗性
         *
         * 重写 Unit::UpdateResistances 方法。更新生物对指定法术学校的抗性值。
         *
         * @param school 法术学校索引
         */
        void UpdateResistances(uint32 school) override;

        /**
         * @brief 更新护甲值
         *
         * 重写 Unit::UpdateArmor 方法。计算并更新生物的护甲值。
         */
        void UpdateArmor() override;

        /**
         * @brief 更新最大生命值
         *
         * 重写 Unit::UpdateMaxHealth 方法。计算并更新生物的最大生命值。
         */
        void UpdateMaxHealth() override;

        /**
         * @brief 更新最大能量值
         *
         * 重写 Unit::UpdateMaxPower 方法。计算并更新生物指定能量类型的最大值。
         *
         * @param power 能量类型（如法力、怒气等）
         */
        void UpdateMaxPower(Powers power) override;
        /**
         * @brief 更新攻击强度和伤害
         *
         * 重写 Unit::UpdateAttackPowerAndDamage 方法。计算并更新生物的攻击强度和伤害值。
         *
         * @param ranged 是否计算远程攻击，默认为 false
         */
        void UpdateAttackPowerAndDamage(bool ranged = false) override;
        /**
         * @brief 计算最小最大伤害
         *
         * 计算生物指定攻击类型的最小和最大伤害值。
         *
         * @param attType 攻击类型
         * @param normalized 是否使用标准化计算
         * @param addTotalPct 是否添加总百分比
         * @param minDamage 输出参数：最小伤害
         * @param maxDamage 输出参数：最大伤害
         * @param damageIndex 伤害索引
         */
        void CalculateMinMaxDamage(WeaponAttackType attType, bool normalized, bool addTotalPct, float& minDamage, float& maxDamage, uint8 damageIndex) const override;

        /**
         * @brief 设置是否可以双持武器
         *
         * 设置生物是否可以使用双持武器攻击。
         *
         * @param value true 为可以双持，false 为不可双持
         */
        void SetCanDualWield(bool value) override;

        /**
         * @brief 获取原始装备ID
         *
         * 获取生物的原始装备模板ID。
         *
         * @return 原始装备ID，可以为 -1
         */
        int8 GetOriginalEquipmentId() const { return m_originalEquipmentId; }

        /**
         * @brief 获取当前装备ID
         *
         * 获取生物当前使用的装备模板ID。
         *
         * @return 当前装备ID
         */
        uint8 GetCurrentEquipmentId() const { return m_equipmentId; }

        /**
         * @brief 设置当前装备ID
         *
         * 设置生物当前使用的装备模板ID。
         *
         * @param id 装备模板ID
         */
        void SetCurrentEquipmentId(uint8 id) { m_equipmentId = id; }

        /**
         * @brief 获取法术伤害修正值
         *
         * 根据等级获取生物的法术伤害修正系数。
         *
         * @param Rank 等级
         * @return 法术伤害修正值
         */
        float GetSpellDamageMod(int32 Rank) const;

        /**
         * @brief 获取商贩物品列表
         *
         * 获取该商贩NPC出售的物品列表。
         *
         * @return 商贩物品数据指针，如果没有返回 nullptr
         */
        VendorItemData const* GetVendorItems() const;

        /**
         * @brief 获取商贩物品当前数量
         *
         * 获取商贩NPC指定物品的当前库存数量。
         *
         * @param vItem 商贩物品信息
         * @return 当前库存数量
         */
        uint32 GetVendorItemCurrentCount(VendorItem const* vItem);

        /**
         * @brief 更新商贩物品当前数量
         *
         * 更新商贩NPC指定物品的库存数量。
         *
         * @param vItem 商贩物品信息
         * @param used_count 已使用的数量
         * @return 更新后的库存数量
         */
        uint32 UpdateVendorItemCurrentCount(VendorItem const* vItem, uint32 used_count);

        /**
         * @brief 获取生物模板信息
         *
         * 返回生物的模板数据指针，包含生物的基础属性如名称、等级、
         * 生命值、伤害、模型ID、标志等静态数据。
         *
         * @return 返回 CreatureTemplate 常量指针
         */
        CreatureTemplate const* GetCreatureTemplate() const { return m_creatureInfo; }

        /**
         * @brief 获取生物数据
         *
         * 返回生物的动态数据指针，包含生物在游戏世界中的具体实例数据，
         * 如生成位置、移动类型、重生时间等。
         *
         * @return 返回 CreatureData 常量指针
         */
        /**
         * @brief 获取生物数据
         *
         * 返回生物的动态数据指针，包含生物在游戏世界中的具体实例数据，
         * 如生成位置、移动类型、重生时间等。
         *
         * @return 返回 CreatureData 常量指针
         */
        CreatureData const* GetCreatureData() const { return m_creatureData; }

        /**
         * @brief 获取生物附加数据
         *
         * 获取生物的附加数据（CreatureAddon），包含特殊配置。
         *
         * @return 生物附加数据指针
         */
        CreatureAddon const* GetCreatureAddon() const;

        /**
         * @brief 获取AI名称
         *
         * 获取该生物使用的AI类型名称。
         *
         * @return AI名称字符串
         */
        std::string const& GetAIName() const;

        /**
         * @brief 获取脚本名称
         *
         * 获取该生物关联的脚本名称。
         *
         * @return 脚本名称字符串
         */
        std::string GetScriptName() const;

        /**
         * @brief 获取脚本ID
         *
         * 获取该生物关联的脚本ID。
         *
         * @return 脚本ID
         */
        uint32 GetScriptId() const;

        /**
         * @brief 获取本地化名称
         *
         * 重写 WorldObject 方法，根据语言环境索引获取生物的本地化名称。
         *
         * @param locale_idx 语言环境索引
         * @return 本地化名称字符串
         */
        std::string const& GetNameForLocaleIdx(LocaleConstant locale_idx) const override;

        /**
         * @brief 设置死亡状态
         *
         * 重写 Unit::setDeathState 方法。处理生物死亡时的状态转换，
         * 包括初始化尸体、设置重生计时器、处理掉落等死亡相关逻辑。
         *
         * @param s 死亡状态枚举值
         */
        void setDeathState(DeathState s) override;                   // override virtual Unit::setDeathState

        /**
         * @brief 从数据库加载生物
         *
         * 从数据库加载生物数据并初始化生物实例。
         *
         * @param spawnId 生成ID
         * @param map 所在地图
         * @param addToMap 是否添加到地图
         * @param allowDuplicate 是否允许重复
         * @return 加载成功返回 true，失败返回 false
         */
        bool LoadFromDB(ObjectGuid::LowType spawnId, Map* map, bool addToMap, bool allowDuplicate);

        /**
         * @brief 保存到数据库
         *
         * 将生物的当前状态保存到数据库。
         * 在 Pet 类中重写。
         */
        /**
         * @brief 保存到数据库
         *
         * 将生物的当前状态保存到数据库。
         * 在 Pet 类中重写。
         */
        void SaveToDB();

        /**
         * @brief 保存到数据库（带参数）
         *
         * 将生物的当前状态保存到数据库。
         * 在 Pet 类中重写。
         *
         * @param mapid 地图ID
         * @param spawnMask 生成掩码
         * @param phaseMask 阶段掩码
         */
        virtual void SaveToDB(uint32 mapid, uint8 spawnMask, uint32 phaseMask);

        /**
         * @brief 从数据库删除生物
         *
         * 静态方法，从数据库中删除指定生成ID的生物数据。
         *
         * @param spawnId 生成ID
         * @return 删除成功返回 true，失败返回 false
         */
        static bool DeleteFromDB(ObjectGuid::LowType spawnId);

        Loot loot;  ///< 掉落物品对象 - 管理该生物的掉落物品

        /**
         * @brief 启动扒窃补充计时器
         *
         * 启动扒窃物品的补充计时器。
         */
        void StartPickPocketRefillTimer();

        /**
         * @brief 重置扒窃补充计时器
         *
         * 重置扒窃物品的补充计时器。
         */
        void ResetPickPocketRefillTimer() { _pickpocketLootRestore = 0; }

        /**
         * @brief 检查是否可以生成扒窃掉落
         *
         * 检查该生物是否可以生成扒窃掉落物品。
         *
         * @return 可以生成返回 true，否则返回 false
         */
        bool CanGeneratePickPocketLoot() const;

        /**
         * @brief 获取掉落接收者GUID
         *
         * 获取拥有该生物掉落权的玩家GUID。
         *
         * @return 掉落接收者GUID
         */
        ObjectGuid GetLootRecipientGUID() const { return m_lootRecipient; }

        /**
         * @brief 获取掉落接收者
         *
         * 获取拥有该生物掉落权的玩家对象。
         *
         * @return 玩家指针，如果没有则返回 nullptr
         */
        Player* GetLootRecipient() const;

        /**
         * @brief 获取掉落接收者队伍
         *
         * 获取拥有该生物掉落权的队伍对象。
         *
         * @return 队伍指针，如果没有则返回 nullptr
         */
        Group* GetLootRecipientGroup() const;

        /**
         * @brief 检查是否有掉落接收者
         *
         * 检查该生物是否已被玩家或队伍锁定掉落权。
         *
         * @return 有掉落接收者返回 true，否则返回 false
         */
        bool hasLootRecipient() const { return !m_lootRecipient.IsEmpty() || m_lootRecipientGroup; }

        /**
         * @brief 检查是否被玩家锁定
         *
         * 检查该生物是否被指定玩家或其队伍成员锁定。
         *
         * @param player 玩家对象
         * @return 被该玩家或其队伍锁定返回 true，否则返回 false
         */
        bool isTappedBy(Player const* player) const;

        /**
         * @brief 设置掉落接收者
         *
         * 设置该生物的掉落归属玩家或队伍。
         *
         * @param unit 接收掉落的单位
         * @param withGroup 是否包含队伍成员，默认为 true
         */
        void SetLootRecipient (Unit* unit, bool withGroup = true);
        /**
         * @brief 所有掉落物品已从尸体移除
         *
         * 当所有掉落物品都被拾取后调用，用于处理尸体消失等后续逻辑。
         */
        void AllLootRemovedFromCorpse();

        /**
         * @brief 获取掉落模式
         *
         * 获取当前掉落模式位掩码。
         *
         * @return 掉落模式
         */
        uint16 GetLootMode() const { return m_LootMode; }

        /**
         * @brief 检查是否有指定掉落模式
         *
         * 检查掉落模式是否包含指定的标志。
         *
         * @param lootMode 掉落模式标志
         * @return 包含该标志返回 true，否则返回 false
         */
        bool HasLootMode(uint16 lootMode) { return (m_LootMode & lootMode) != 0; }

        /**
         * @brief 设置掉落模式
         *
         * 设置生物的掉落模式位掩码。
         *
         * @param lootMode 掉落模式
         */
        void SetLootMode(uint16 lootMode) { m_LootMode = lootMode; }

        /**
         * @brief 添加掉落模式
         *
         * 向生物的掉落模式添加指定标志。
         *
         * @param lootMode 要添加的掉落模式标志
         */
        void AddLootMode(uint16 lootMode) { m_LootMode |= lootMode; }

        /**
         * @brief 移除掉落模式
         *
         * 从生物的掉落模式移除指定标志。
         *
         * @param lootMode 要移除的掉落模式标志
         */
        void RemoveLootMode(uint16 lootMode) { m_LootMode &= ~lootMode; }

        /**
         * @brief 重置掉落模式
         *
         * 将掉落模式重置为默认值（LOOT_MODE_DEFAULT）。
         */
        void ResetLootMode() { m_LootMode = LOOT_MODE_DEFAULT; }

        /**
         * @var m_spells
         * @brief 生物法术列表
         *
         * 存储生物可使用的法术ID数组。这些法术由AI在战斗中使用。
         * 最多可存储 MAX_CREATURE_SPELLS 个法术ID。
         */
        uint32 m_spells[MAX_CREATURE_SPELLS];

        /**
         * @brief 检查是否可以开始攻击
         *
         * 检查生物是否可以对指定单位发起攻击，考虑距离、视野、
         * 阵营、状态等因素。
         *
         * @param u 目标单位
         * @param force 是否强制攻击（忽略某些条件）
         * @return 可以攻击返回 true，否则返回 false
         */
        bool CanStartAttack(Unit const* u, bool force) const;

        /**
         * @brief 获取攻击距离
         *
         * 计算生物可以发起攻击的最大距离。
         *
         * @param player 目标玩家
         * @return 攻击距离
         */
        float GetAttackDistance(Unit const* player) const;

        /**
         * @brief 获取仇恨范围
         *
         * 获取生物对目标产生仇恨的范围距离。
         *
         * @param target 目标单位
         * @return 仇恨范围距离
         */
        float GetAggroRange(Unit const* target) const;

        /**
         * @brief 发送AI反应消息
         *
         * 向客户端发送AI反应消息，用于触发客户端特效或声音。
         *
         * @param reactionType AI反应类型（如警告、愤怒等）
         */
        void SendAIReaction(AiReaction reactionType);

        /**
         * @brief 选择最近的目标
         *
         * 在指定距离内选择最近的单位作为目标。
         *
         * @param dist 搜索距离，0 表示无限制
         * @param playerOnly 是否仅搜索玩家
         * @return 最近的目标单位指针，如果没有则返回 nullptr
         */
        Unit* SelectNearestTarget(float dist = 0, bool playerOnly = false) const;

        /**
         * @brief 选择攻击距离内的最近目标
         *
         * 在攻击距离内选择最近的单位作为目标。
         *
         * @param dist 搜索距离，0 表示使用默认攻击距离
         * @return 最近的目标单位指针，如果没有则返回 nullptr
         */
        Unit* SelectNearestTargetInAttackDistance(float dist = 0) const;

        /**
         * @brief 选择仇恨范围内的最近敌对单位
         *
         * 在仇恨范围内选择最近的敌对单位。
         *
         * @param useLOS 是否检查视线
         * @param ignoreCivilians 是否忽略平民
         * @return 最近的敌对单位指针，如果没有则返回 nullptr
         */
        Unit* SelectNearestHostileUnitInAggroRange(bool useLOS = false, bool ignoreCivilians = false) const;

        /**
         * @brief 逃跑寻求援助
         *
         * 让生物逃跑并寻找附近的友方单位来援助。
         * 通常在生命值较低时触发。
         */
        void DoFleeToGetAssistance();
        /**
         * @brief 呼叫援助
         *
         * 在指定半径范围内呼叫附近的友方生物来协助战斗。
         *
         * @param fRadius 呼叫半径
         */
        void CallForHelp(float fRadius);

        /**
         * @brief 请求援助
         *
         * 生物在战斗中请求附近友方单位的援助。
         * 设置了相关的标记以防止重复呼叫。
         */
        /**
         * @brief 请求援助
         *
         * 生物在战斗中请求附近友方单位的援助。
         * 设置了相关的标记以防止重复呼叫。
         */
        void CallAssistance();

        /**
         * @brief 设置禁止呼叫援助
         *
         * 设置是否禁止该生物呼叫援助。
         *
         * @param val true 为禁止，false 为允许
         */
        void SetNoCallAssistance(bool val) { m_AlreadyCallAssistance = val; }

        /**
         * @brief 设置禁止搜索援助
         *
         * 设置是否禁止该生物搜索援助。
         *
         * @param val true 为禁止，false 为允许
         */
        void SetNoSearchAssistance(bool val) { m_AlreadySearchedAssistance = val; }

        /**
         * @brief 检查是否已搜索援助
         *
         * 检查该生物是否已经搜索过援助。
         *
         * @return 已搜索援助返回 true，否则返回 false
         */
        bool HasSearchedAssistance() const { return m_AlreadySearchedAssistance; }

        /**
         * @brief 检查是否可以援助
         *
         * 检查该生物是否可以援助指定单位对抗敌人。
         *
         * @param u 被援助的单位
         * @param enemy 敌人单位
         * @param checkfaction 是否检查阵营，默认为 true
         * @return 可以援助返回 true，否则返回 false
         */
        bool CanAssistTo(Unit const* u, Unit const* enemy, bool checkfaction = true) const;

        /**
         * @brief 检查目标是否可接受
         *
         * 检查指定目标是否可以作为有效的攻击目标。
         *
         * @param target 目标单位
         * @return 目标可接受返回 true，否则返回 false
         */
        bool _IsTargetAcceptable(Unit const* target) const;

        /**
         * @brief 检查是否可以忽略假死
         *
         * 检查该生物是否可以忽略玩家的假死技能。
         *
         * @return 可以忽略假死返回 true，否则返回 false
         */
        bool CanIgnoreFeignDeath() const { return (GetCreatureTemplate()->flags_extra & CREATURE_FLAG_EXTRA_IGNORE_FEIGN_DEATH) != 0; }

        /**
         * @brief 移除尸体
         *
         * 移除生物的尸体，开始重生流程。可选择是否设置重生时间，
         * 以及是否对附近玩家销毁尸体显示。
         *
         * @param setSpawnTime 是否设置重生时间，默认为 true
         * @param destroyForNearbyPlayers 是否对附近玩家销毁尸体，默认为 true
         */
        void RemoveCorpse(bool setSpawnTime = true, bool destroyForNearbyPlayers = true);

        /**
         * @brief 消失或取消召唤
         *
         * 使生物消失或取消召唤状态。可以设置延迟消失时间，
         * 以及强制重生计时器。常用于临时召唤生物的移除。
         *
         * @param timeToDespawn 消失延迟时间（毫秒），默认为 0s
         * @param forceRespawnTime 强制重生时间（秒），默认为 0s
         */
        void DespawnOrUnsummon(Milliseconds timeToDespawn = 0s, Seconds forceRespawnTime = 0s);

        /**
         * @brief 获取重生时间
         *
         * 获取生物下次重生的时间点（时间戳）。
         *
         * @return 重生时间的常量引用
         */
        time_t const& GetRespawnTime() const { return m_respawnTime; }

        /**
         * @brief 获取扩展的重生时间
         *
         * 获取生物的重生时间，处理特殊情况。
         *
         * @return 重生时间
         */
        time_t GetRespawnTimeEx() const;

        /**
         * @brief 设置重生时间
         *
         * 设置生物的重生时间。
         *
         * @param respawn 重生延迟时间（秒）
         */
        void SetRespawnTime(uint32 respawn);

        /**
         * @brief 重生生物
         *
         * 立即重生该生物，恢复其生命值、移除尸体状态、
         * 重新初始化AI和移动生成器等。可用于强制重生或正常重生流程。
         *
         * @param force 是否强制重生（忽略某些条件检查），默认为 false
         */
        void Respawn(bool force = false);
        /**
         * @brief 保存重生时间
         *
         * 将生物的重生时间保存到数据库。
         *
         * @param forceDelay 强制延迟时间（秒），默认为0表示使用默认延迟
         */
        void SaveRespawnTime(uint32 forceDelay = 0);

        /**
         * @brief 获取重生延迟时间
         *
         * 返回生物从尸体消失到重生之间的延迟时间（秒）。
         *
         * @return 重生延迟时间（秒）
         */
        uint32 GetRespawnDelay() const { return m_respawnDelay; }

        /**
         * @brief 设置重生延迟时间
         *
         * 设置生物从尸体消失到重生之间的延迟时间（秒）。
         *
         * @param delay 延迟时间（秒）
         */
        void SetRespawnDelay(uint32 delay) { m_respawnDelay = delay; }

        /**
         * @brief 获取闲逛距离
         *
         * 获取生物可以闲逛的最大距离（距离出生点的距离）。
         *
         * @return 闲逛距离
         */
        float GetWanderDistance() const { return m_wanderDistance; }

        /**
         * @brief 设置闲逛距离
         *
         * 设置生物可以闲逛的最大距离。
         *
         * @param dist 闲逛距离
         */
        void SetWanderDistance(float dist) { m_wanderDistance = dist; }

        /**
         * @brief 立即执行边界检查
         *
         * 强制立即执行脱战边界检查。
         */
        void DoImmediateBoundaryCheck() { m_boundaryCheckTime = 0; }

        /**
         * @brief 获取战斗脉冲延迟
         *
         * 获取战斗脉冲的延迟时间（秒）。
         *
         * @return 战斗脉冲延迟时间
         */
        uint32 GetCombatPulseDelay() const { return m_combatPulseDelay; }

        /**
         * @brief 设置战斗脉冲延迟
         *
         * 设置生物将整个区域拉入战斗的频率（仅在副本中生效）。
         *
         * @param delay 延迟时间（秒）
         */
        void SetCombatPulseDelay(uint32 delay)
        {
            m_combatPulseDelay = delay;
            if (m_combatPulseTime == 0 || m_combatPulseTime > delay)
                m_combatPulseTime = delay;
        }

        /**
         * @var m_groupLootTimer
         * @brief 队伍分配计时器(毫秒)
         *
         * 用于队伍分配掉落物品的计时。当队伍使用队伍分配模式时,
         * 此计时器控制物品分配的时间窗口。
         */
        uint32 m_groupLootTimer;

        /**
         * @var lootingGroupLowGUID
         * @brief 正在拾取尸体的队伍低GUID
         *
         * 用于查找正在拾取尸体的队伍,避免多个队伍同时拾取。
         */
        ObjectGuid::LowType lootingGroupLowGUID;

        /**
         * @brief 发送区域受攻击消息
         *
         * 向区域内的玩家发送区域受攻击的消息。
         *
         * @param attacker 攻击者玩家
         */
        void SendZoneUnderAttackMessage(Player* attacker);

        /**
         * @brief 检查是否有指定任务
         *
         * 检查该生物是否提供指定的任务。
         *
         * @param quest_id 任务ID
         * @return 有该任务返回 true，否则返回 false
         */
        bool hasQuest(uint32 quest_id) const override;

        /**
         * @brief 检查是否涉及指定任务
         *
         * 检查该生物是否涉及指定的任务（作为任务目标）。
         *
         * @param quest_id 任务ID
         * @return 涉及该任务返回 true，否则返回 false
         */
        bool hasInvolvedQuest(uint32 quest_id)  const override;

        /**
         * @brief 检查是否可以恢复生命
         *
         * 检查生物是否允许自动恢复生命值。需要同时满足：
         * 1. 生命恢复未被锁定
         * 2. 生命恢复功能已启用
         *
         * @return 可以恢复生命返回 true，否则返回 false
         */
        bool CanRegenerateHealth() const { return !_regenerateHealthLock && _regenerateHealth; }

        /**
         * @brief 设置生命恢复状态
         *
         * 启用或禁用生物的生命恢复功能。
         * 通过设置 _regenerateHealthLock 来控制。
         *
         * @param value true 表示启用生命恢复，false 表示禁用
         */
        /**
         * @brief 设置生命恢复状态
         *
         * 启用或禁用生物的生命恢复功能。
         * 通过设置 _regenerateHealthLock 来控制。
         *
         * @param value true 表示启用生命恢复，false 表示禁用
         */
        void SetRegenerateHealth(bool value) { _regenerateHealthLock = !value; }

        /**
         * @brief 获取宠物自动施法大小
         *
         * 获取宠物自动施法列表的大小。
         *
         * @return 自动施法列表大小
         */
        virtual uint8 GetPetAutoSpellSize() const { return MAX_SPELL_CHARM; }

        /**
         * @brief 获取宠物指定位置的自动施法
         *
         * 获取宠物自动施法列表中指定位置的法术ID。
         *
         * @param pos 位置索引
         * @return 法术ID
         */
        virtual uint32 GetPetAutoSpellOnPos(uint8 pos) const;

        /**
         * @brief 获取宠物追击距离
         *
         * 获取宠物追击目标的最大距离。
         *
         * @return 追击距离
         */
        float GetPetChaseDistance() const;

        /**
         * @brief 设置无法到达目标
         *
         * 标记生物是否无法到达当前目标。
         *
         * @param cannotReach true 表示无法到达，false 表示可以到达
         */
        void SetCannotReachTarget(bool cannotReach);

        /**
         * @brief 检查是否无法到达目标
         *
         * 检查生物是否无法到达当前目标。
         *
         * @return 无法到达返回 true，否则返回 false
         */
        bool CanNotReachTarget() const { return m_cannotReachTarget; }

        /**
         * @brief 设置出生位置
         *
         * 设置生物的出生点坐标，用于脱战归位和重生位置。
         *
         * @param x X坐标
         * @param y Y坐标
         * @param z Z坐标
         * @param o 朝向
         */
        void SetHomePosition(float x, float y, float z, float o) { m_homePosition.Relocate(x, y, z, o); }

        /**
         * @brief 设置出生位置（使用Position对象）
         *
         * 设置生物的出生点，使用Position对象作为参数。
         *
         * @param pos 位置对象
         */
        void SetHomePosition(Position const& pos) { m_homePosition.Relocate(pos); }

        /**
         * @brief 获取出生位置
         *
         * 获取生物的出生点坐标。
         *
         * @param x 输出参数：X坐标
         * @param y 输出参数：Y坐标
         * @param z 输出参数：Z坐标
         * @param ori 输出参数：朝向
         */
        void GetHomePosition(float& x, float& y, float& z, float& ori) const { m_homePosition.GetPosition(x, y, z, ori); }

        /**
         * @brief 获取出生位置对象
         *
         * 返回生物出生位置的Position对象常量引用。
         *
         * @return 出生位置对象的常量引用
         */
        Position const& GetHomePosition() const { return m_homePosition; }

        /**
         * @brief 设置载具出生位置
         *
         * 设置生物在载具上的出生点坐标。
         *
         * @param x X坐标
         * @param y Y坐标
         * @param z Z坐标
         * @param o 朝向
         */
        void SetTransportHomePosition(float x, float y, float z, float o) { m_transportHomePosition.Relocate(x, y, z, o); }

        /**
         * @brief 设置载具出生位置（使用Position对象）
         *
         * 设置生物在载具上的出生点，使用Position对象作为参数。
         *
         * @param pos 位置对象
         */
        void SetTransportHomePosition(Position const& pos) { m_transportHomePosition.Relocate(pos); }

        /**
         * @brief 获取载具出生位置
         *
         * 获取生物在载具上的出生点坐标。
         *
         * @param x 输出参数：X坐标
         * @param y 输出参数：Y坐标
         * @param z 输出参数：Z坐标
         * @param ori 输出参数：朝向
         */
        void GetTransportHomePosition(float& x, float& y, float& z, float& ori) const { m_transportHomePosition.GetPosition(x, y, z, ori); }

        /**
         * @brief 获取载具出生位置对象
         *
         * 返回生物在载具上的出生位置Position对象常量引用。
         *
         * @return 载具出生位置对象的常量引用
         */
        Position const& GetTransportHomePosition() const { return m_transportHomePosition; }

        /**
         * @brief 获取路径点路径ID
         *
         * 获取生物的巡逻路径ID。
         *
         * @return 路径点路径ID
         */
        uint32 GetWaypointPath() const { return _waypointPathId; }

        /**
         * @brief 加载路径
         *
         * 加载指定ID的巡逻路径。
         *
         * @param pathid 路径ID
         */
        void LoadPath(uint32 pathid) { _waypointPathId = pathid; }

        /**
         * @brief 获取当前路径点信息
         *
         * 获取生物当前所在的路径点节点ID和路径ID。
         *
         * @return 包含节点ID和路径ID的pair
         */
        std::pair<uint32, uint32> GetCurrentWaypointInfo() const { return _currentWaypointNodeInfo; }

        /**
         * @brief 更新当前路径点信息
         *
         * 更新生物当前所在的路径点节点信息。
         *
         * @param nodeId 节点ID
         * @param pathId 路径ID
         */
        void UpdateCurrentWaypointInfo(uint32 nodeId, uint32 pathId) { _currentWaypointNodeInfo = { nodeId, pathId }; }

        /**
         * @brief 检查是否正在返回出生点
         *
         * 检查生物当前是否正在返回出生点的过程中。
         * 通常在脱战后发生。
         *
         * @return 正在返回出生点返回 true，否则返回 false
         */
        bool IsReturningHome() const;

        /**
         * @brief 搜索编队
         *
         * 搜索并加入附近的编队，用于编队成员的协调移动。
         */
        /**
         * @brief 搜索编队
         *
         * 搜索并加入附近的编队，用于编队成员的协调移动。
         */
        void SearchFormation();

        /**
         * @brief 获取编队
         *
         * 获取生物所属的编队对象。
         *
         * @return 编队对象指针
         */
        CreatureGroup* GetFormation() { return m_formation; }

        /**
         * @brief 设置编队
         *
         * 设置生物所属的编队。
         *
         * @param formation 编队对象指针
         */
        void SetFormation(CreatureGroup* formation) { m_formation = formation; }

        /**
         * @brief 检查是否为编队领袖
         *
         * 检查该生物是否为其编队的领袖。
         *
         * @return 是编队领袖返回 true，否则返回 false
         */
        bool IsFormationLeader() const;

        /**
         * @brief 发信号通知编队移动
         *
         * 通知编队中的其他成员开始移动。
         */
        void SignalFormationMovement();

        /**
         * @brief 检查编队领袖是否允许移动
         *
         * 检查编队领袖是否被允许移动。
         *
         * @return 允许移动返回 true，否则返回 false
         */
        bool IsFormationLeaderMoveAllowed() const;

        /**
         * @brief 设置禁用声望获取
         *
         * 设置是否禁用击杀该生物时的声望奖励。
         *
         * @param disable true 为禁用，false 为启用
         */
        void SetDisableReputationGain(bool disable) { DisableReputationGain = disable; }

        /**
         * @brief 检查声望获取是否被禁用
         *
         * 检查击杀该生物是否给予声望奖励。
         *
         * @return 被禁用返回 true，否则返回 false
         */
        bool IsReputationGainDisabled() const { return DisableReputationGain; }

        /**
         * @brief 检查伤害是否足够获得掉落和奖励
         *
         * 检查玩家造成的伤害是否足够获得掉落物品和奖励。
         *
         * @return 伤害足够返回 true，否则返回 false
         */
        bool IsDamageEnoughForLootingAndReward() const { return (m_creatureInfo->flags_extra & CREATURE_FLAG_EXTRA_NO_PLAYER_DAMAGE_REQ) || (m_PlayerDamageReq == 0); }

        /**
         * @brief 降低玩家伤害需求
         *
         * 降低玩家需要造成的伤害量才能获得掉落和奖励。
         *
         * @param unDamage 要降低的伤害量
         */
        void LowerPlayerDamageReq(uint32 unDamage);

        /**
         * @brief 重置玩家伤害需求
         *
         * 重置玩家伤害需求为当前生命值的一半。
         */
        void ResetPlayerDamageReq() { m_PlayerDamageReq = GetHealth() / 2; }
        /**
         * @var m_PlayerDamageReq
         * @brief 玩家伤害需求
         *
         * 玩家需要造成的最小伤害量才能获得掉落和奖励。
 * 用于防止玩家"捡漏"他人击杀的生物。
         * 初始值通常为生物生命值的一半。
         */
        uint32 m_PlayerDamageReq;

        /**
         * @brief 获取原始模板ID
         *
         * 获取生物最初的模板ID。
         *
         * @return 原始模板ID
         */
        uint32 GetOriginalEntry() const { return m_originalEntry; }

        /**
         * @brief 设置原始模板ID
         *
         * 设置生物的原始模板ID。
         *
         * @param entry 模板ID
         */
        void SetOriginalEntry(uint32 entry) { m_originalEntry = entry; }

        /**
         * @brief 设置重生兼容模式
         *
         * 设置重生兼容模式，用于兼容旧的重生机制。
         * 很多地方还未准备好动态生成，此功能允许它们暂时继续运行。
         *
         * @param mode true 为启用兼容模式，false 为禁用
         */
        void SetRespawnCompatibilityMode(bool mode = true) { m_respawnCompatibilityMode = mode; }

        /**
         * @brief 获取重生兼容模式
         *
         * 检查是否启用了重生兼容模式。
         *
         * @return 启用返回 true，否则返回 false
         */
        bool GetRespawnCompatibilityMode() { return m_respawnCompatibilityMode; }

        /**
         * @brief 获取伤害修正值
         *
         * 静态方法，根据等级获取伤害修正系数。
         *
         * @param Rank 等级
         * @return 伤害修正值
         */
        static float _GetDamageMod(int32 Rank);

        /**
         * @var m_SightDistance
         * @brief 视野距离
         *
         * 生物能看到目标的距离,影响仇恨反应和AI行为。
         */
        float m_SightDistance;

        /**
         * @var m_CombatDistance
         * @brief 战斗距离
         *
         * 生物可以主动发起战斗的距离。
         */
        float m_CombatDistance;

        /**
         * @var m_isTempWorldObject
         * @brief 临时世界对象标志
         *
         * 当生物被附身时设置为 true。
         * 用于标记该生物是临时的,不应被保存到数据库。
         */
        bool m_isTempWorldObject;

        // Handling caster facing during spellcast
        /**
         * @brief 设置目标
         *
         * 重写 Unit::SetTarget 方法。设置生物的目标单位，
         * 使生物面向该目标。在施法时特别重要。
         *
         * @param guid 目标单位的GUID
         */
        void SetTarget(ObjectGuid guid) override;
        /**
         * @brief 不要重新获取法术焦点目标
         *
         * 告知生物不要重新获取施法焦点目标。
         */
        void DoNotReacquireSpellFocusTarget();

        /**
         * @brief 设置法术焦点
         *
         * 设置生物正在施放的法术和目标，用于施法时的面向控制。
         *
         * @param focusSpell 正在施放的法术对象
         * @param target 法术目标
         */
        void SetSpellFocus(Spell const* focusSpell, WorldObject const* target);

        /**
         * @brief 检查是否有法术焦点
         *
         * 检查生物是否正在施放指定法术（或任何法术）。
         *
         * @param focusSpell 要检查的法术对象，默认为 nullptr 表示检查任何法术
         * @return 有法术焦点返回 true，否则返回 false
         */
        bool HasSpellFocus(Spell const* focusSpell = nullptr) const override;

        /**
         * @brief 释放法术焦点
         *
         * 释放生物的法术焦点，结束施法状态。
         *
         * @param focusSpell 要释放的法术对象，默认为 nullptr
         * @param withDelay 是否使用延迟释放，默认为 true
         */
        void ReleaseSpellFocus(Spell const* focusSpell = nullptr, bool withDelay = true);

        /**
         * @brief 检查施法是否阻止移动
         *
         * 重写 Unit::IsMovementPreventedByCasting 方法。
         * 检查生物当前是否因施法而无法移动。
         *
         * @return 施法阻止移动返回 true，否则返回 false
         */
        bool IsMovementPreventedByCasting() const override;

        /**
         * @brief 获取最后受伤时间
         *
         * 获取生物最后一次受到伤害的时间，用于脱战机制。
         *
         * @return 最后受伤时间戳
         */
        time_t GetLastDamagedTime() const { return _lastDamagedTime; }

        /**
         * @brief 设置最后受伤时间
         *
         * 设置生物最后一次受到伤害的时间。
         *
         * @param val 时间戳
         */
        void SetLastDamagedTime(time_t val) { _lastDamagedTime = val; }

        /**
         * @brief 获取文本重复组
         *
         * 获取指定文本组的重复ID列表。
         *
         * @param textGroup 文本组ID
         * @return 文本重复ID列表
         */
        CreatureTextRepeatIds GetTextRepeatGroup(uint8 textGroup);

        /**
         * @brief 设置文本重复ID
         *
         * 为指定文本组设置重复ID。
         *
         * @param textGroup 文本组ID
         * @param id 重复ID
         */
        void SetTextRepeatId(uint8 textGroup, uint8 id);

        /**
         * @brief 清除文本重复组
         *
         * 清除指定文本组的所有重复ID。
         *
         * @param textGroup 文本组ID
         */
        void ClearTextRepeatGroup(uint8 textGroup);

        /**
         * @brief 检查是否正在护送中
         *
         * 检查生物是否正在执行护送任务。
         *
         * @return 正在护送中返回 true，否则返回 false
         */
        bool IsEscorted() const;

        /**
         * @brief 检查是否可以给予经验值
         *
         * 检查击杀该生物是否可以给予玩家经验值。
         * 某些特殊生物（如守卫、任务生物等）可能不给予经验。
         *
         * @return 可以给予经验返回 true，否则返回 false
         */
        bool CanGiveExperience() const;

        /**
         * @brief 检查是否正在战斗中
         *
         * 重写 Unit::IsEngaged 方法。检查生物是否正处于战斗状态。
         *
         * @return 在战斗中返回 true，否则返回 false
         */
        bool IsEngaged() const override;

        /**
         * @brief 进入战斗时的回调
         *
         * 重写 Unit::AtEngage 方法。当生物进入战斗时调用，
         * 可以在这里执行进入战斗的初始化逻辑。
         *
         * @param target 进入战斗的目标
         */
        void AtEngage(Unit* target) override;

        /**
         * @brief 脱离战斗时的回调
         *
         * 重写 Unit::AtDisengage 方法。当生物脱离战斗时调用，
         * 可以在这里执行脱战的清理逻辑，如重置位置、清除仇恨等。
         */
        void AtDisengage() override;

        /**
         * @brief 检查脱战时是否有游泳标志
         *
         * 检查生物在非战斗状态下是否具有游泳能力标志。
         *
         * @return 有游泳标志返回 true，否则返回 false
         */
        bool HasCanSwimFlagOutOfCombat() const
        {
            return !_isMissingCanSwimFlagOutOfCombat;
        }

        /**
         * @brief 刷新游泳标志
         *
         * 刷新生物的游泳能力标志。
         *
         * @param recheck 是否重新检查，默认为 false
         */
        void RefreshCanSwimFlag(bool recheck = false);

        /**
         * @brief 获取调试信息
         *
         * 获取生物的调试信息字符串。
         *
         * @return 调试信息字符串
         */
        std::string GetDebugInfo() const override;

        /**
         * @brief 离开载具
         *
         * 重写 Unit::ExitVehicle 方法。让生物离开当前所在的载具。
         *
         * @param exitPosition 离开载具后的位置，默认为 nullptr
         */
        void ExitVehicle(Position const* exitPosition = nullptr) override;

    protected:
        /**
         * @brief 从模板创建生物
         *
         * 根据模板ID创建生物实例，初始化基本属性。
         *
         * @param guidlow 低GUID值
         * @param entry 生物模板ID
         * @param data 生物数据（可选）
         * @param vehId 载具ID（可选）
         * @return 创建成功返回 true，失败返回 false
         */
        bool CreateFromProto(ObjectGuid::LowType guidlow, uint32 entry, CreatureData const* data = nullptr, uint32 vehId = 0);

        /**
         * @brief 初始化模板ID
         *
         * 初始化生物的模板ID和相关数据。
         *
         * @param entry 生物模板ID
         * @param data 生物数据（可选）
         * @return 初始化成功返回 true，失败返回 false
         */
        bool InitEntry(uint32 entry, CreatureData const* data = nullptr);

        /**
         * @var m_vendorItemCounts
         * @brief 商贩物品计数映射表
         *
         * 存储商贩NPC每个物品的当前库存数量和补充时间信息。
         * 用于实现商贩物品的有限库存和自动补充机制。
         */
        VendorItemCounts m_vendorItemCounts;

        /**
         * @brief 获取生命修正值
         *
         * 静态方法，根据等级获取生命值修正系数。
         *
         * @param Rank 等级
         * @return 生命修正值
         */
        /**
         * @brief 获取生命修正值
         *
         * 静态方法,根据等级获取生命值修正系数。
         *
         * @param Rank 等级
         * @return 生命修正值
         */
        static float _GetHealthMod(int32 Rank);

        /**
         * @var m_lootRecipient
         * @brief 掉落接收者GUID
         *
         * 拥有该生物掉落权的玩家GUID。当玩家或队伍首先对生物造成伤害时设置。
         * 只有该玩家或其队伍成员可以拾取掉落物品。
         */
        ObjectGuid m_lootRecipient;

        /**
         * @var m_lootRecipientGroup
         * @brief 掉落接收者队伍ID
         *
         * 拥有掉落权的队伍ID。当队伍成员首先对生物造成伤害时设置。
         * 用于队伍共享掉落权机制。
         */
        uint32 m_lootRecipientGroup;

        /**
         * @var _pickpocketLootRestore
         * @brief 扒窃掉落恢复时间
         *
         * 扒窃物品补充的时间点(时间戳)。用于实现扒窃NPC物品的刷新机制。
         */
        time_t _pickpocketLootRestore;

        /**
         * @var m_corpseRemoveTime
         * @brief 尸体移除时间
         *
         * 生物尸体消失的时间点(时间戳)。用于死亡后尸体消失的计时。
         */
        time_t m_corpseRemoveTime;

        /**
         * @var m_respawnTime
         * @brief 重生时间
         *
         * 生物下次重生的时间点(时间戳)。尸体消失后开始计时。
         */
        time_t m_respawnTime;

        /**
         * @var m_respawnDelay
         * @brief 重生延迟
         *
         * 尸体消失到生物重生之间的延迟时间(秒)。
         */
        uint32 m_respawnDelay;

        /**
         * @var m_corpseDelay
         * @brief 尸体延迟
         *
         * 生物死亡到尸体消失之间的延迟时间(秒)。
         */
        uint32 m_corpseDelay;

        /**
         * @var m_ignoreCorpseDecayRatio
         * @brief 是否忽略尸体衰减比率
         *
         * 某些特殊生物需要忽略尸体衰减比率配置。
         */
        bool m_ignoreCorpseDecayRatio;

        /**
         * @var m_wanderDistance
         * @brief 闲逛距离
         *
         * 生物可以闲逛的最大距离(距离出生点)。
         * 用于随机移动生成器限制移动范围。
         */
        float m_wanderDistance;

        /**
         * @var m_boundaryCheckTime
         * @brief 脱战边界检查时间
         *
         * 下次脱战边界检查的剩余时间(毫秒)。
         * 用于检测生物是否离开出生点过远,需要脱战归位。
         */
        uint32 m_boundaryCheckTime;

        /**
         * @var m_combatPulseTime
         * @brief 战斗脉冲时间
         *
         * 下次区域战斗脉冲的剩余时间(毫秒)。
         */
        uint32 m_combatPulseTime;

        /**
         * @var m_combatPulseDelay
         * @brief 战斗脉冲延迟
         *
         * 生物将整个区域拉入战斗的频率(秒),仅在副本中生效。
         * 用于副本BOSS的连锁仇恨机制。
         */
        uint32 m_combatPulseDelay;

        /**
         * @var m_reactState
         * @brief 反应状态
         *
         * AI的反应模式(被动、攻击、防御等)。
         * 注意:不是 charmInfo,是生物本身的反应状态。
         */
        ReactStates m_reactState;

        /**
         * @brief 恢复生命值
         *
         * 处理生物的生命值自动恢复逻辑。
         */
        void RegenerateHealth();

        /**
         * @brief 恢复能量
         *
         * 处理生物的能量值（法力、怒气等）自动恢复逻辑。
         *
         * @param power 能量类型
         */
        void Regenerate(Powers power);

        /**
         * @var m_defaultMovementType
         * @brief 默认移动类型
         *
         * 定义生物的默认移动行为模式,如闲逛、巡逻、静止等。
         * 在生物初始化时由移动生成器使用。
         */
        MovementGeneratorType m_defaultMovementType;

        /**
         * @var m_spawnId
         * @brief 生成ID
         *
         * 生物在数据库中的唯一标识。对于新建或临时生物为0,
         * 对于从数据库加载的生物为低GUID。
         */
        ObjectGuid::LowType m_spawnId;

        /**
         * @var m_equipmentId
         * @brief 当前装备ID
         *
         * 生物当前使用的装备模板ID,决定生物显示的武器和装备外观。
         */
        uint8 m_equipmentId;

        /**
         * @var m_originalEquipmentId
         * @brief 原始装备ID
         *
         * 生物的原始装备模板ID,可以为 -1 表示无装备。
         * 用于装备变化后恢复原始装备。
         */
        int8 m_originalEquipmentId;

        /**
         * @var m_AlreadyCallAssistance
         * @brief 已呼叫援助标志
         *
         * 防止生物重复呼叫援助,避免性能问题。
         */
        bool m_AlreadyCallAssistance;

        /**
         * @var m_AlreadySearchedAssistance
         * @brief 已搜索援助标志
         *
         * 防止生物重复搜索附近援助者,避免性能问题。
         */
        bool m_AlreadySearchedAssistance;

        /**
         * @var m_cannotReachTarget
         * @brief 无法到达目标标志
         *
         * 标记生物是否无法到达当前攻击目标。
         * 用于处理寻路失败的情况。
         */
        bool m_cannotReachTarget;

        /**
         * @var m_cannotReachTimer
         * @brief 无法到达目标计时器
         *
         * 追踪生物无法到达目标的持续时间(毫秒)。
         * 当持续时间过长时可能触发脱战。
         */
        uint32 m_cannotReachTimer;

        /**
         * @var m_meleeDamageSchoolMask
         * @brief 近战伤害法术学校掩码
         *
         * 定义生物近战攻击的伤害类型,如物理、火焰、冰霜等。
         * 大多数生物使用物理伤害。
         */
        SpellSchoolMask m_meleeDamageSchoolMask;

        /**
         * @var m_originalEntry
         * @brief 原始模板ID
         *
         * 生物最初的模板ID,用于在临时变身或变形后恢复原始状态。
         */
        uint32 m_originalEntry;

        /**
         * @var m_homePosition
         * @brief 出生位置
         *
         * 生物的出生点坐标,用于脱战归位和重生位置。
         */
        Position m_homePosition;

        /**
         * @var m_transportHomePosition
         * @brief 载具出生位置
         *
         * 生物在载具(如船只、电梯)上的出生位置。
         */
        Position m_transportHomePosition;

        /**
         * @var DisableReputationGain
         * @brief 禁用声望获取
         *
         * 控制是否禁用击杀该生物时的声望奖励。
         * 某些特殊生物不应给予声望。
         */
        bool DisableReputationGain;

        /**
         * @var m_creatureInfo
         * @brief 生物模板信息
         *
         * 存储生物的基础属性数据,如名称、等级、生命值、模型等。
         * 注意:在难度模式>0时可能与 sObjectMgr->GetCreatureTemplate(GetEntry()) 不同。
         */
        CreatureTemplate const* m_creatureInfo;

        /**
         * @var m_creatureData
         * @brief 生物数据
         *
         * 存储生物实例的动态数据,如位置、移动类型、重生时间等。
         * 从数据库加载并可能包含自定义修改。
         */
        CreatureData const* m_creatureData;

        /**
         * @var m_LootMode
         * @brief 掉落模式位掩码
         *
         * 决定哪些掉落物品可以被拾取,默认为 LOOT_MODE_DEFAULT。
         * 用于条件性掉落机制。
         */
        uint16 m_LootMode;

        /**
         * @brief 检查是否因消失而不可见
         *
         * 检查生物是否因正在消失而不应被看到。
         *
         * @return 因消失不可见返回 true，否则返回 false
         */
        bool IsInvisibleDueToDespawn() const override;

        /**
         * @brief 检查是否总是能看到对象
         *
         * 检查生物是否总是能看到指定对象，忽略视线等限制。
         *
         * @param obj 目标对象
         * @return 总是能看到返回 true，否则返回 false
         */
        bool CanAlwaysSee(WorldObject const* obj) const override;

    private:
        /**
         * @brief 强制消失
         *
         * 强制生物消失，可选择延迟消失时间和强制重生计时器。
         *
         * @param timeMSToDespawn 消失延迟时间（毫秒），默认为 0
         * @param forceRespawnTimer 强制重生时间（秒），默认为 0s
         */
        void ForcedDespawn(uint32 timeMSToDespawn = 0, Seconds forceRespawnTimer = 0s);

        /**
         * @brief 检查无灰色生物仇恨配置
         *
         * 检查配置是否禁止灰色生物（等级过低）对玩家产生仇恨。
         *
         * @param playerLevel 玩家等级
         * @param creatureLevel 生物等级
         * @return 不产生仇恨返回 true，否则返回 false
         */
        bool CheckNoGrayAggroConfig(uint32 playerLevel, uint32 creatureLevel) const;

        /**
         * @var _waypointPathId
         * @brief 路径点路径ID
         *
         * 生物的巡逻路径ID,定义生物的巡逻路线。
         */
        uint32 _waypointPathId;

        /**
         * @var _currentWaypointNodeInfo
         * @brief 当前路径点节点信息
         *
         * 存储当前路径点节点ID和路径ID的pair。
         * 用于追踪生物在巡逻路径上的位置。
         */
        std::pair<uint32/*nodeId*/, uint32/*pathId*/> _currentWaypointNodeInfo;

        /**
         * @var m_formation
         * @brief 编队指针
         *
         * 生物所属的编队对象指针,用于编队移动和协调行为。
         */
        CreatureGroup* m_formation;

        /**
         * @var m_triggerJustAppeared
         * @brief 触发器刚出现标志
         *
         * 用于触发器类型生物的出现处理。
         */
        bool m_triggerJustAppeared;

        /**
         * @var m_respawnCompatibilityMode
         * @brief 重生兼容模式
         *
         * 用于兼容旧的重生机制,很多地方还未准备好动态生成。
         */
        bool m_respawnCompatibilityMode;

        /**
         * @brief 重新获取法术焦点目标
         *
         * 在施法过程中重新获取焦点目标。
         * 用于处理施法期间目标状态变化的情况。
         */
        void ReacquireSpellFocusTarget();

        /**
         * @struct _spellFocusInfo
         * @brief 法术焦点信息结构
         *
         * 存储生物施法时的焦点信息,包括法术对象、延迟、目标和朝向。
         * 用于控制生物在施法期间的目标追踪和面向控制。
         */
        struct
        {
            ::Spell const* Spell = nullptr;                 ///< 正在施放的法术对象
            uint32 Delay = 0;                               ///< 延迟时间(毫秒) - 生物目标应回转的时间(0表示未计划回转)
            ObjectGuid Target;                              ///< 生物在施法时的"真实"目标
            float Orientation = 0.0f;                       ///< 生物在施法时的"真实"朝向
        } _spellFocusInfo;

        /**
         * @var _lastDamagedTime
         * @brief 最后受伤时间
         *
         * 生物最后一次受到伤害的时间戳,用于脱战机制。
         * 当超过一定时间未受伤时触发脱战。
         */
        time_t _lastDamagedTime;

        /**
         * @var m_textRepeat
         * @brief 文本重复组
         *
         * 用于管理生物对话文本的重复显示机制。
         * 存储每个文本组已显示过的文本ID,避免重复。
         */
        CreatureTextRepeatGroup m_textRepeat;

        /**
         * @var _regenerateHealth
         * @brief 是否恢复生命
         *
         * 在创建时设置,决定生物是否会自动恢复生命值。
         * 某些特殊生物可能不希望自动恢复生命。
         */
        bool _regenerateHealth;

        /**
         * @var _regenerateHealthLock
         * @brief 生命恢复锁
         *
         * 用于临时禁用生命恢复功能,如被特殊光环影响时。
         */
        bool _regenerateHealthLock;

        /**
         * @var _isMissingCanSwimFlagOutOfCombat
         * @brief 缺少脱战游泳标志
         *
         * 标记生物是否缺少非战斗状态下的游泳能力标志。
         * 用于游泳标志的刷新逻辑。
         */
        bool _isMissingCanSwimFlagOutOfCombat;
};

/**
 * @brief 援助延迟事件类
 *
 * 用于处理延迟援助呼叫的事件，在指定时间后让附近友方生物援助战斗。
 */
class TC_GAME_API AssistDelayEvent : public BasicEvent
{
    public:
        /**
         * @brief 构造函数
         *
         * @param victim 受害者GUID
         * @param owner 事件拥有者单位
         */
        AssistDelayEvent(ObjectGuid victim, Unit& owner) : BasicEvent(), m_victim(victim), m_owner(owner) { }

        /**
         * @brief 执行事件
         *
         * 执行援助延迟事件的逻辑。
         *
         * @param e_time 事件执行时间
         * @param p_time 经过的处理时间
         * @return 执行成功返回 true
         */
        bool Execute(uint64 e_time, uint32 p_time) override;

        /**
         * @brief 添加援助者
         *
         * 将一个援助者GUID添加到援助列表中。
         *
         * @param guid 援助者GUID
         */
        void AddAssistant(ObjectGuid guid) { m_assistants.push_back(guid); }

    private:
        AssistDelayEvent();  ///< 私有默认构造函数

        ObjectGuid        m_victim;      ///< 受害者GUID
        GuidList          m_assistants;  ///< 援助者GUID列表
        Unit&             m_owner;       ///< 事件拥有者引用
};

/**
 * @brief 强制消失延迟事件类
 *
 * 用于处理延迟强制生物消失的事件。
 */
class TC_GAME_API ForcedDespawnDelayEvent : public BasicEvent
{
    public:
        /**
         * @brief 构造函数
         *
         * @param owner 事件拥有者生物
         * @param respawnTimer 重生计时器（秒）
         */
        ForcedDespawnDelayEvent(Creature& owner, Seconds respawnTimer) : BasicEvent(), m_owner(owner), m_respawnTimer(respawnTimer) { }

        /**
         * @brief 执行事件
         *
         * 执行强制消失延迟事件的逻辑。
         *
         * @param e_time 事件执行时间
         * @param p_time 经过的处理时间
         * @return 执行成功返回 true
         */
        bool Execute(uint64 e_time, uint32 p_time) override;

    private:
        Creature& m_owner;              ///< 事件拥有者生物引用
        Seconds const m_respawnTimer;   ///< 重生计时器（秒）
};

#endif
