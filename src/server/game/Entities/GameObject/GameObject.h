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
 * @file GameObject.h
 * @brief 游戏对象实体定义头文件
 *
 * 本文件定义了游戏中所有交互对象的核心类 GameObject，包括：
 * - 门、按钮、宝箱等可交互对象
 * - 采矿点、草药点等采集对象
 * - 陷阱、运输工具等特殊对象
 * - 可破坏建筑等战场对象
 *
 * 游戏对象是游戏中除玩家、生物、物品之外的主要交互实体，
 * 支持多种状态转换、动画效果、战利品系统等功能。
 */

#ifndef TRINITYCORE_GAMEOBJECT_H
#define TRINITYCORE_GAMEOBJECT_H

#include "Object.h"
#include "GridObject.h"
#include "GameObjectData.h"
#include "Loot.h"
#include "MapObject.h"
#include "SharedDefines.h"

class GameObjectAI;
class GameObjectModel;
class Group;
class OPvPCapturePoint;
class Transport;
class Unit;
struct TransportAnimation;
enum SpellTargetCheckTypes : uint8;
enum TriggerCastFlags : uint32;

/**
 * @brief 游戏对象值联合体 - 存储不同类型游戏对象的特定数据
 *
 * 职责：
 *   为不同类型的游戏对象提供类型特定的数据存储
 *   使用联合体节省内存，同一时间只存储一种类型的数据
 *
 * 使用说明：
 *   根据 GetGoType() 返回的类型访问对应的联合体成员
 *   例如：陷阱类型访问 Trap 成员，运输工具类型访问 Transport 成员
 */
union GameObjectValue
{
    /// 陷阱类型数据 (GAMEOBJECT_TYPE_TRAP = 6)
    struct
    {
        SpellTargetCheckTypes TargetSearcherCheckType;  ///< 目标搜索检查类型 - 用于确定陷阱触发条件
    } Trap;

    /// 运输工具类型数据 (GAMEOBJECT_TYPE_TRANSPORT = 11)
    struct
    {
        uint32 PathProgress;                            ///< 路径进度 - 当前在路径上的位置
        TransportAnimation const* AnimationInfo;        ///< 动画信息指针 - 运输工具的运动动画数据
        uint32 CurrentSeg;                              ///< 当前路段 - 路径段的索引
    } Transport;

    /// 钓鱼点类型数据 (GAMEOBJECT_TYPE_FISHINGHOLE = 25)
    struct
    {
        uint32 MaxOpens;                                ///< 最大开启次数 - 钓鱼点可用次数限制
    } FishingHole;

    /// 占领点类型数据 (GAMEOBJECT_TYPE_CAPTURE_POINT = 29)
    struct
    {
        OPvPCapturePoint *OPvPObj;                      ///< 户外PvP占领点对象指针 - 关联的战场占领点逻辑
    } CapturePoint;

    /// 可破坏建筑类型数据 (GAMEOBJECT_TYPE_DESTRUCTIBLE_BUILDING = 33)
    struct
    {
        uint32 Health;                                  ///< 当前生命值 - 建筑当前血量
        uint32 MaxHealth;                               ///< 最大生命值 - 建筑满血量
    } Building;
};

/**
 * @brief 战利品状态枚举 - 定义游戏对象的生命周期状态
 *
 * 状态转换流程：
 * @code
 * 容器类型:  [GO_NOT_READY] -> GO_READY (关闭) -> GO_ACTIVATED (打开) -> GO_JUST_DEACTIVATED -> GO_READY -> ...
 * 钓鱼浮标:  [GO_NOT_READY] -> GO_READY (关闭) -> GO_ACTIVATED (打开) -> GO_JUST_DEACTIVATED -> <删除>
 * 门(关闭态):[GO_NOT_READY] -> GO_READY (关闭) -> GO_ACTIVATED (打开) -> GO_JUST_DEACTIVATED -> GO_READY (关闭) -> ...
 * 门(打开态):[GO_NOT_READY] -> GO_READY (打开) -> GO_ACTIVATED (关闭) -> GO_JUST_DEACTIVATED -> GO_READY (打开) -> ...
 * @endcode
 *
 * 状态说明：
 * - GO_NOT_READY: 初始状态，对象刚创建或正在初始化
 * - GO_READY: 就绪状态，对象可以被激活交互
 * - GO_ACTIVATED: 激活状态，对象正在被使用（如宝箱已打开）
 * - GO_JUST_DEACTIVATED: 刚停用状态，对象刚完成使用，准备返回就绪或删除
 */
enum LootState
{
    GO_NOT_READY = 0,        ///< 未就绪 - 初始状态，对象尚未准备好交互
    GO_READY,                ///< 就绪 - 可以被激活（但可能已消失，直到重新生成）
    GO_ACTIVATED,            ///< 已激活 - 正在使用中（如宝箱已打开、门已开启）
    GO_JUST_DEACTIVATED      ///< 刚停用 - 刚完成使用，准备返回就绪状态或删除
};

/**
 * @def FISHING_BOBBER_READY_TIME
 * @brief 钓鱼浮标准备时间（秒）
 *
 * 钓鱼时，浮标抛出后需要等待此时间才能检测鱼咬钩。
 * 这个延迟模拟了现实中钓鱼需要等待的过程。
 */
#define FISHING_BOBBER_READY_TIME 5

/**
 * @brief 游戏对象类 - 所有游戏对象（如门、宝箱、陷阱等）的基类
 *
 * 职责：
 *   管理游戏中各种交互对象（门、宝箱、陷阱、采矿点、任务物品等）
 *   处理游戏对象的状态转换、重生、交互、动画等核心功能
 *   支持多种游戏对象类型，如容器、门、按钮、陷阱、运输工具、可破坏建筑等
 *
 * 继承关系：
 *   - WorldObject: 提供世界坐标、区域感知、可见性等基础功能
 *   - GridObject<GameObject>: 提供网格系统访问能力，用于对象查找和更新
 *   - MapObject: 提供地图关联能力，用于实例管理
 *
 * 主要功能模块：
 *   - 状态管理: GetGoState()/SetGoState() 控制对象的开关状态
 *   - 战利品系统: loot 成员和相关函数管理掉落物品
 *   - 重生机制: Respawn()/DespawnOrUnsummon() 管理对象生命周期
 *   - AI系统: AI() 返回智能行为控制器
 *   - 碰撞检测: m_model 提供物理碰撞支持
 *
 * 使用示例：
 * @code
 * // 创建游戏对象
 * GameObject* go = new GameObject();
 * go->Create(guidlow, entry, map, phaseMask, pos, rotation, animprogress, state);
 *
 * // 使用游戏对象
 * go->Use(player);
 *
 * // 设置状态
 * go->SetGoState(GO_STATE_ACTIVE);
 * @endcode
 */
class TC_GAME_API GameObject : public WorldObject, public GridObject<GameObject>, public MapObject
{
    public:
        /// 构造函数 - 初始化游戏对象
        explicit GameObject();
        /// 析构函数 - 清理游戏对象资源
        ~GameObject();

        /// 构建值更新数据包
        /// @param updatetype 更新类型
        /// @param data 数据缓冲区
        /// @param target 目标玩家
        void BuildValuesUpdate(uint8 updatetype, ByteBuffer* data, Player const* target) const override;

        /// 添加到游戏世界
        void AddToWorld() override;
        /// 从游戏世界移除
        void RemoveFromWorld() override;
        /// 删除前的清理工作
        /// @param finalCleanup 是否为最终清理
        void CleanupsBeforeDelete(bool finalCleanup = true) override;

        /// 获取动态标志
        /// @return 动态标志值
        uint32 GetDynamicFlags() const override { return GetUInt32Value(GAMEOBJECT_DYNAMIC); }
        /// 替换所有动态标志
        /// @param flag 新的动态标志
        void ReplaceAllDynamicFlags(uint32 flag) override { SetUInt32Value(GAMEOBJECT_DYNAMIC, flag); }

        /// 创建游戏对象
        /// @param guidlow GUID低位
        /// @param name_id 模板ID
        /// @param map 地图指针
        /// @param phaseMask 相位掩码
        /// @param pos 位置
        /// @param rotation 旋转四元数
        /// @param animprogress 动画进度
        /// @param go_state 游戏对象状态
        /// @param artKit 艺术套件ID
        /// @param dynamic 是否动态生成
        /// @param spawnid 生成ID
        /// @return 创建成功返回true
        bool Create(ObjectGuid::LowType guidlow, uint32 name_id, Map* map, uint32 phaseMask, Position const& pos, QuaternionData const& rotation, uint32 animprogress, GOState go_state, uint32 artKit = 0, bool dynamic = false, ObjectGuid::LowType spawnid = 0);
        /// 更新游戏对象（核心更新函数）
        /// @param p_time 经过的毫秒数
        void Update(uint32 p_time) override;
        /// 获取游戏对象模板信息
        /// @return 游戏对象模板指针
        GameObjectTemplate const* GetGOInfo() const { return m_goInfo; }
        /// 获取模板扩展信息
        /// @return 模板扩展信息指针
        GameObjectTemplateAddon const* GetTemplateAddon() const { return m_goTemplateAddon; }
        /// 获取游戏对象覆盖数据
        /// @return 覆盖数据指针
        GameObjectOverride const* GetGameObjectOverride() const;
        /// 获取游戏对象数据
        /// @return 游戏对象数据指针
        GameObjectData const* GetGameObjectData() const { return m_goData; }
        /// 获取游戏对象值
        /// @return 游戏对象值指针
        GameObjectValue const* GetGOValue() const { return &m_goValue; }

        /// 检查是否为运输工具
        /// @return 是运输工具返回true
        bool IsTransport() const;
        /// 检查是否为动态运输工具
        /// @return 是动态运输工具返回true
        bool IsDynTransport() const;
        /// 检查是否为可破坏建筑
        /// @return 是可破坏建筑返回true
        bool IsDestructibleBuilding() const;

        /// 获取生成ID
        /// @return 生成ID
        ObjectGuid::LowType GetSpawnId() const { return m_spawnId; }

        /// 设置本地旋转角度（欧拉角）
        /// @param z_rot Z轴旋转角度
        /// @param y_rot Y轴旋转角度
        /// @param x_rot X轴旋转角度
        void SetLocalRotationAngles(float z_rot, float y_rot, float x_rot);
        /// 设置本地旋转（四元数）
        /// @param qx 四元数X分量
        /// @param qy 四元数Y分量
        /// @param qz 四元数Z分量
        /// @param qw 四元数W分量
        void SetLocalRotation(float qx, float qy, float qz, float qw);
        /// 设置父级旋转（用于运输工具路径变换）
        /// @param rotation 旋转四元数
        void SetParentRotation(QuaternionData const& rotation);
        /// 获取本地旋转
        /// @return 本地旋转四元数
        QuaternionData const& GetLocalRotation() const { return m_localRotation; }
        /// 获取打包的本地旋转
        /// @return 打包的旋转值
        int64 GetPackedLocalRotation() const { return m_packedRotation; }

        /// 获取世界旋转
        /// @return 世界旋转四元数
        QuaternionData GetWorldRotation() const;

        /// 获取指定语言环境的名称（重写WorldObject函数用于本地化）
        /// @param locale 语言环境
        /// @return 本地化名称
        std::string const& GetNameForLocaleIdx(LocaleConstant locale) const override;

        /// 保存到数据库
        void SaveToDB();
        /// 保存到数据库（指定参数）
        /// @param mapid 地图ID
        /// @param spawnMask 生成掩码
        /// @param phaseMask 相位掩码
        void SaveToDB(uint32 mapid, uint8 spawnMask, uint32 phaseMask);
        /// 从数据库加载
        /// @param spawnId 生成ID
        /// @param map 地图指针
        /// @param addToMap 是否添加到地图
        /// @return 加载成功返回true
        bool LoadFromDB(ObjectGuid::LowType spawnId, Map* map, bool addToMap, bool = true);
        /// 从数据库删除
        /// @param spawnId 生成ID
        /// @return 删除成功返回true
        static bool DeleteFromDB(ObjectGuid::LowType spawnId);

        /// 设置所有者GUID
        /// @param owner 所有者GUID
        void SetOwnerGUID(ObjectGuid owner)
        {
            // 如果已找到所有者且与预期不同，则中断操作
            if (!owner.IsEmpty() && !GetOwnerGUID().IsEmpty() && GetOwnerGUID() != owner)
            {
                ABORT();
            }
            m_spawnedByDefault = false;  // 所有有拥有者的对象都会在延迟后消失
            SetGuidValue(OBJECT_FIELD_CREATED_BY, owner);
        }
        /// 获取所有者GUID
        /// @return 所有者GUID
        ObjectGuid GetOwnerGUID() const override { return GetGuidValue(OBJECT_FIELD_CREATED_BY); }

        /// 设置召唤法术ID
        /// @param id 法术ID
        void SetSpellId(uint32 id)
        {
            m_spawnedByDefault = false;  // 所有召唤的对象都会在延迟后消失
            m_spellId = id;
        }
        /// 获取召唤法术ID
        /// @return 法术ID
        uint32 GetSpellId() const { return m_spellId;}

        /// 获取重生时间
        /// @return 重生时间（秒）
        time_t GetRespawnTime() const { return m_respawnTime; }
        /// 获取扩展重生时间
        /// @return 扩展重生时间
        time_t GetRespawnTimeEx() const;

        /// 设置重生时间
        /// @param respawn 重生时间（秒）
        void SetRespawnTime(int32 respawn);
        /// 立即重生游戏对象
        void Respawn();
        /// 检查是否已生成
        /// @return 已生成返回true
        bool isSpawned() const
        {
            return m_respawnDelayTime == 0 ||
                (m_respawnTime > 0 && !m_spawnedByDefault) ||
                (m_respawnTime == 0 && m_spawnedByDefault);
        }
        /// 检查是否默认生成
        /// @return 是默认生成返回true
        bool isSpawnedByDefault() const { return m_spawnedByDefault; }
        /// 设置是否默认生成
        /// @param b 是否默认生成
        void SetSpawnedByDefault(bool b) { m_spawnedByDefault = b; }
        /// 获取重生延迟
        /// @return 重生延迟（秒）
        uint32 GetRespawnDelay() const { return m_respawnDelayTime; }
        /// 刷新游戏对象
        void Refresh();
        /// 消失或取消召唤
        /// @param delay 延迟时间
        /// @param forceRespawnTime 强制重生时间
        void DespawnOrUnsummon(Milliseconds delay = 0ms, Seconds forceRespawnTime = 0s);
        /// 删除游戏对象
        void Delete();
        /// 获取钓鱼战利品
        /// @param loot 战利品对象
        /// @param loot_owner 战利品拥有者
        void getFishLoot(Loot* loot, Player* loot_owner);
        /// 获取钓鱼垃圾战利品
        /// @param loot 战利品对象
        /// @param loot_owner 战利品拥有者
        void getFishLootJunk(Loot* loot, Player* loot_owner);

        /// 检查是否拥有指定标志
        /// @param flags 游戏对象标志
        /// @return 拥有该标志返回true
        bool HasFlag(GameObjectFlags flags) const { return Object::HasFlag(GAMEOBJECT_FLAGS, flags); }
        /// 设置游戏对象标志
        /// @param flags 游戏对象标志
        void SetFlag(GameObjectFlags flags) { Object::SetFlag(GAMEOBJECT_FLAGS, flags); }
        /// 移除游戏对象标志
        /// @param flags 游戏对象标志
        void RemoveFlag(GameObjectFlags flags) { Object::RemoveFlag(GAMEOBJECT_FLAGS, flags); }
        /// 替换所有标志
        /// @param flags 新的标志值
        void ReplaceAllFlags(GameObjectFlags flags) { SetUInt32Value(GAMEOBJECT_FLAGS, flags); }

        /// 设置等级
        /// @param level 等级值
        void SetLevel(uint32 level) { SetUInt32Value(GAMEOBJECT_LEVEL, level); }
        /// 获取游戏对象类型
        /// @return 游戏对象类型
        GameobjectTypes GetGoType() const { return GameobjectTypes(GetByteValue(GAMEOBJECT_BYTES_1, 1)); }
        /// 设置游戏对象类型
        /// @param type 游戏对象类型
        void SetGoType(GameobjectTypes type) { SetByteValue(GAMEOBJECT_BYTES_1, 1, type); }
        /// 获取游戏对象状态
        /// @return 游戏对象状态
        GOState GetGoState() const { return GOState(GetByteValue(GAMEOBJECT_BYTES_1, 0)); }
        /// 设置游戏对象状态
        /// @param state 游戏对象状态
        void SetGoState(GOState state);
        /// 获取运输工具周期
        /// @return 周期时间
        virtual uint32 GetTransportPeriod() const;
        /// 获取艺术套件ID
        /// @return 艺术套件ID
        uint8 GetGoArtKit() const { return GetByteValue(GAMEOBJECT_BYTES_1, 2); }
        /// 设置艺术套件ID
        /// @param artkit 艺术套件ID
        void SetGoArtKit(uint8 artkit);
        /// 获取动画进度
        /// @return 动画进度
        uint8 GetGoAnimProgress() const { return GetByteValue(GAMEOBJECT_BYTES_1, 3); }
        /// 设置动画进度
        /// @param animprogress 动画进度
        void SetGoAnimProgress(uint8 animprogress) { SetByteValue(GAMEOBJECT_BYTES_1, 3, animprogress); }
        /// 静态方法：设置艺术套件ID
        /// @param artkit 艺术套件ID
        /// @param go 游戏对象指针
        /// @param lowguid 低GUID（默认为0）
        static void SetGoArtKit(uint8 artkit, GameObject* go, ObjectGuid::LowType lowguid = 0);

        /// 设置相位掩码
        /// @param newPhaseMask 新的相位掩码
        /// @param update 是否更新
        void SetPhaseMask(uint32 newPhaseMask, bool update) override;

        /// 启用/禁用碰撞
        /// @param enable 是否启用
        void EnableCollision(bool enable);

        /// 使用游戏对象（核心交互函数）
        /// @param user 使用者
        void Use(Unit* user);

        /// 获取战利品状态
        /// @return 战利品状态
        LootState getLootState() const { return m_lootState; }
        /// 设置战利品状态
        /// @param s 新状态
        /// @param unit 相关单位（仅在s=GO_ACTIVATED时使用）
        void SetLootState(LootState s, Unit* unit = nullptr);

        /// 获取战利品模式
        /// @return 战利品模式位掩码
        uint16 GetLootMode() const { return m_LootMode; }
        /// 检查是否拥有指定战利品模式
        /// @param lootMode 战利品模式
        /// @return 拥有该模式返回true
        bool HasLootMode(uint16 lootMode) const { return (m_LootMode & lootMode) != 0; }
        /// 设置战利品模式
        /// @param lootMode 战利品模式
        void SetLootMode(uint16 lootMode) { m_LootMode = lootMode; }
        /// 添加战利品模式
        /// @param lootMode 战利品模式
        void AddLootMode(uint16 lootMode) { m_LootMode |= lootMode; }
        /// 移除战利品模式
        /// @param lootMode 战利品模式
        void RemoveLootMode(uint16 lootMode) { m_LootMode &= ~lootMode; }
        /// 重置战利品模式为默认
        void ResetLootMode() { m_LootMode = LOOT_MODE_DEFAULT; }
        /// 设置战利品生成时间
        void SetLootGenerationTime();
        /// 获取战利品生成时间
        /// @return 生成时间
        uint32 GetLootGenerationTime() const { return m_lootGenerationTime; }

        /// 添加到技能提升列表
        /// @param PlayerGuidLow 玩家GUID
        void AddToSkillupList(ObjectGuid const& PlayerGuidLow) { m_SkillupList.insert(PlayerGuidLow); }
        /// 检查是否在技能提升列表中
        /// @param playerGuid 玩家GUID
        /// @return 在列表中返回true
        bool IsInSkillupList(ObjectGuid const& playerGuid) const
        {
            return m_SkillupList.count(playerGuid) > 0;
        }
        /// 清空技能提升列表
        void ClearSkillupList() { m_SkillupList.clear(); }

        /// 添加唯一使用记录
        /// @param player 使用者
        void AddUniqueUse(Player* player);
        /// 增加使用次数
        void AddUse() { ++m_usetimes; }

        /// 获取使用次数
        /// @return 使用次数
        uint32 GetUseCount() const { return m_usetimes; }
        /// 获取唯一使用计数
        /// @return 唯一使用者数量
        uint32 GetUniqueUseCount() const { return uint32(m_unique_users.size()); }

        /// 保存重生时间
        /// @param forceDelay 强制延迟（默认为0）
        void SaveRespawnTime(uint32 forceDelay = 0);

        Loot        loot;  ///< 战利品对象 - 存储该游戏对象掉落的战利品内容

        /// 获取战利品接收者
        /// @return 战利品接收者指针
        Player* GetLootRecipient() const;
        /// 获取战利品接收队伍
        /// @return 队伍指针
        Group* GetLootRecipientGroup() const;
        /// 设置战利品接收者
        /// @param unit 单位指针
        /// @param group 队伍指针
        void SetLootRecipient(Unit* unit, Group* group = nullptr);
        /// 检查玩家是否允许拾取战利品
        /// @param player 玩家指针
        /// @return 允许返回true
        bool IsLootAllowedFor(Player const* player) const;
        /// 检查是否有战利品接收者
        /// @return 有接收者返回true
        bool HasLootRecipient() const { return !m_lootRecipient.IsEmpty() || m_lootRecipientGroup; }
        uint32 m_groupLootTimer;            ///< (毫秒)队伍拾取计时器 - 队伍分配战利品时的倒计时
        ObjectGuid::LowType lootingGroupLowGUID;  ///< 正在拾取的队伍低GUID - 标识当前正在拾取的队伍

        /// 获取关联的陷阱
        /// @return 陷阱游戏对象指针
        GameObject* GetLinkedTrap();
        /// 设置关联陷阱
        /// @param linkedTrap 陷阱游戏对象指针
        void SetLinkedTrap(GameObject* linkedTrap) { m_linkedTrap = linkedTrap->GetGUID(); }

        /// 检查是否有指定任务
        /// @param quest_id 任务ID
        /// @return 有任务返回true
        bool hasQuest(uint32 quest_id) const override;
        /// 检查是否涉及指定任务
        /// @param quest_id 任务ID
        /// @return 涉及任务返回true
        bool hasInvolvedQuest(uint32 quest_id) const override;
        /// 为任务激活游戏对象
        /// @param target 目标玩家
        /// @return 可激活返回true
        bool ActivateToQuest(Player const* target) const;
        /// 使用门或按钮
        /// @param time_to_restore 恢复时间（0表示使用gameobject.spawntimesecs）
        /// @param alternative 是否使用替代状态
        /// @param user 使用者
        void UseDoorOrButton(uint32 time_to_restore = 0, bool alternative = false, Unit* user = nullptr);
        /// 重置门或按钮
        void ResetDoorOrButton();
        /// 激活游戏对象（触发法术效果）
        /// @param action 激活动作
        /// @param spellCaster 施法者
        /// @param spellId 法术ID
        /// @param effectIndex 效果索引
        void ActivateObject(GameObjectActions action, WorldObject* spellCaster = nullptr, uint32 spellId = 0, int32 effectIndex = -1);

        /// 触发关联的游戏对象（陷阱）
        /// @param trapEntry 陷阱条目ID
        /// @param target 目标单位
        void TriggeringLinkedGameObject(uint32 trapEntry, Unit* target);

        /// 检查是否从不显示
        /// @param allowServersideObjects 是否允许服务器端对象
        /// @return 从不显示返回true
        bool IsNeverVisible(bool allowServersideObjects) const override;
        /// 检查是否始终对观察者可见
        /// @param seer 观察者
        /// @return 始终可见返回true
        bool IsAlwaysVisibleFor(WorldObject const* seer) const override;
        /// 检查是否因消失而不可见
        /// @return 因消失而不可见返回true
        bool IsInvisibleDueToDespawn() const override;

        /// 获取目标等级
        /// @param target 目标
        /// @return 等级值
        uint8 GetLevelForTarget(WorldObject const* target) const override;

        /// 在范围内查找钓鱼洞
        /// @param range 搜索范围
        /// @return 钓鱼洞游戏对象指针
        GameObject* LookupFishingHoleAround(float range);

        /// 发送自定义动画
        /// @param anim 动画ID
        void SendCustomAnim(uint32 anim);
        /// 检查是否在范围内
        /// @param x X坐标
        /// @param y Y坐标
        /// @param z Z坐标
        /// @param radius 半径
        /// @return 在范围内返回true
        bool IsInRange(float x, float y, float z, float radius) const;

        /// 修改生命值（可破坏建筑）
        /// @param change 生命值变化量
        /// @param attackerOrHealer 攻击者或治疗者
        /// @param spellId 法术ID
        void ModifyHealth(int32 change, WorldObject* attackerOrHealer = nullptr, uint32 spellId = 0);
        /// 设置可破坏状态
        /// @param state 可破坏状态
        /// @param attackerOrHealer 攻击者或治疗者
        /// @param setHealth 是否设置默认生命值
        void SetDestructibleState(GameObjectDestructibleState state, WorldObject* attackerOrHealer = nullptr, bool setHealth = false);
        /// 获取可破坏状态
        /// @return 可破坏状态
        GameObjectDestructibleState GetDestructibleState() const
        {
            if (HasFlag(GO_FLAG_DESTROYED))
                return GO_DESTRUCTIBLE_DESTROYED;
            if (HasFlag(GO_FLAG_DAMAGED))
                return GO_DESTRUCTIBLE_DAMAGED;
            return GO_DESTRUCTIBLE_INTACT;
        }

        /// 事件通知
        /// @param eventId 事件ID
        /// @param invoker 触发者
        void EventInform(uint32 eventId, WorldObject* invoker = nullptr);

        /// 设置重生兼容模式（临时解决方案）
        /// @param mode 是否启用兼容模式
        void SetRespawnCompatibilityMode(bool mode = true) { m_respawnCompatibilityMode = mode; }
        /// 获取重生兼容模式
        /// @return 是否启用兼容模式
        bool GetRespawnCompatibilityMode() {return m_respawnCompatibilityMode; }

        /// 获取脚本ID
        /// @return 脚本ID
        uint32 GetScriptId() const;
        /// 获取AI指针
        /// @return AI指针
        GameObjectAI* AI() const { return m_AI; }

        /// 获取AI名称
        /// @return AI名称字符串
        std::string const& GetAIName() const;
        /// 设置显示ID
        /// @param displayid 显示ID
        void SetDisplayId(uint32 displayid);
        /// 获取显示ID
        /// @return 显示ID
        uint32 GetDisplayId() const { return GetUInt32Value(GAMEOBJECT_DISPLAYID); }

        /// 获取阵营
        /// @return 阵营ID
        uint32 GetFaction() const override { return GetUInt32Value(GAMEOBJECT_FACTION); }
        /// 设置阵营
        /// @param faction 阵营ID
        void SetFaction(uint32 faction) override { SetUInt32Value(GAMEOBJECT_FACTION, faction); }

        GameObjectModel* m_model;  ///< 游戏对象模型指针 - 用于碰撞检测和渲染
        /// 获取重生位置
        /// @param x X坐标输出参数
        /// @param y Y坐标输出参数
        /// @param z Z坐标输出参数
        /// @param ori 朝向输出参数（可选）
        void GetRespawnPosition(float &x, float &y, float &z, float* ori = nullptr) const;

        /// 转换为运输工具指针
        /// @return 运输工具指针（如果不是运输工具返回nullptr）
        Transport* ToTransport() { if (GetGOInfo()->type == GAMEOBJECT_TYPE_MO_TRANSPORT) return reinterpret_cast<Transport*>(this); else return nullptr; }
        Transport const* ToTransport() const { if (GetGOInfo()->type == GAMEOBJECT_TYPE_MO_TRANSPORT) return reinterpret_cast<Transport const*>(this); else return nullptr; }

        /// 获取静止位置X坐标
        float GetStationaryX() const override { if (GetGOInfo()->type != GAMEOBJECT_TYPE_MO_TRANSPORT) return m_stationaryPosition.GetPositionX(); return GetPositionX(); }
        /// 获取静止位置Y坐标
        float GetStationaryY() const override { if (GetGOInfo()->type != GAMEOBJECT_TYPE_MO_TRANSPORT) return m_stationaryPosition.GetPositionY(); return GetPositionY(); }
        /// 获取静止位置Z坐标
        float GetStationaryZ() const override { if (GetGOInfo()->type != GAMEOBJECT_TYPE_MO_TRANSPORT) return m_stationaryPosition.GetPositionZ(); return GetPositionZ(); }
        /// 获取静止朝向
        float GetStationaryO() const override { if (GetGOInfo()->type != GAMEOBJECT_TYPE_MO_TRANSPORT) return m_stationaryPosition.GetOrientation(); return GetOrientation(); }
        /// 重定位静止位置
        void RelocateStationaryPosition(float x, float y, float z, float o) { m_stationaryPosition.Relocate(x, y, z, o); }

        /// 获取交互距离
        /// @return 交互距离
        float GetInteractionDistance() const;

        /// 更新模型位置
        void UpdateModelPosition();

        /// 检查是否在交互距离内
        /// @param pos 位置
        /// @param radius 半径
        /// @return 在交互距离内返回true
        bool IsAtInteractDistance(Position const& pos, float radius) const;
        bool IsAtInteractDistance(Player const* player, SpellInfo const* spell = nullptr) const;

        /// 检查是否在地图距离内
        /// @param player 玩家
        /// @return 在距离内返回true
        bool IsWithinDistInMap(Player const* player) const;
        using WorldObject::IsWithinDistInMap;

        /// 获取解锁法术
        /// @param player 玩家
        /// @return 法术信息指针
        SpellInfo const* GetSpellForLock(Player const* player) const;

        /// 销毁AI
        void AIM_Destroy();
        /// 初始化AI
        /// @return 初始化成功返回true
        bool AIM_Initialize();

        /// 获取调试信息
        /// @return 调试信息字符串
        std::string GetDebugInfo() const override;

    protected:
        /// 创建模型
        void CreateModel();
        /// 更新模型（当显示ID改变时调用）
        void UpdateModel();

        // ==================== 成员变量 ====================

        uint32      m_spellId;               ///< 召唤该游戏对象的法术ID（0表示非法术召唤）
        time_t      m_respawnTime;           ///< 下次重生时间（秒），如果有所有者则为消失时间
        uint32      m_respawnDelayTime;      ///< 重生延迟时间（秒），如果为0则当前GO状态不依赖计时器
        uint32      m_despawnDelay;          ///< 消失延迟（毫秒）
        Seconds     m_despawnRespawnTime;    ///< 延迟消失后的重生时间覆盖值
        LootState   m_lootState;             ///< 战利品状态（就绪/激活/停用等）
        ObjectGuid  m_lootStateUnitGUID;     ///< 传递给SetLootState的单位GUID
        bool        m_spawnedByDefault;      ///< 是否默认生成（true表示正常刷新，false表示动态生成）
        time_t      m_restockTime;           ///< 补货时间（用于有限次使用的容器）
        time_t      m_cooldownTime;          ///< 内部反应延迟时间（陷阱：法术冷却，门/按钮：重置时间）
        GOState     m_prevGoState;           ///< 重置时应该设置的状态（用于恢复原始状态）

        GuidSet m_SkillupList;               ///< 技能提升玩家GUID集合（记录已获得技能提升的玩家）

        ObjectGuid m_ritualOwnerGUID;        ///< 仪式拥有者GUID（用于GAMEOBJECT_TYPE_RITUAL类型）
        GuidSet m_unique_users;              ///< 唯一使用者GUID集合（用于计数唯一使用次数）
        uint32 m_usetimes;                   ///< 总使用次数

        typedef std::map<uint32, ObjectGuid> ChairSlotAndUser;
        ChairSlotAndUser ChairListSlots;     ///< 椅子槽位和使用者映射（用于GAMEOBJECT_TYPE_CHAIR类型）

        ObjectGuid::LowType m_spawnId;       ///< 生成ID（新对象或临时对象为0，保存的对象为lowguid）
        GameObjectTemplate const* m_goInfo;  ///< 游戏对象模板指针（不可变）
        GameObjectTemplateAddon const* m_goTemplateAddon;  ///< 模板扩展信息指针（可为nullptr）
        GameObjectData const* m_goData;      ///< 游戏对象数据指针（数据库加载的数据，可为nullptr）
        GameObjectValue m_goValue;           ///< 游戏对象值联合体（存储类型特定的数据）

        int64 m_packedRotation;              ///< 打包的旋转值（用于网络传输）
        QuaternionData m_localRotation;      ///< 本地旋转四元数
        Position m_stationaryPosition;       ///< 静止位置（用于运输工具等移动对象的基准位置）

        ObjectGuid m_lootRecipient;          ///< 战利品接收者GUID
        uint32 m_lootRecipientGroup;         ///< 战利品接收队伍ID
        uint16 m_LootMode;                   ///< 战利品模式位掩码（默认LOOT_MODE_DEFAULT）
        uint32 m_lootGenerationTime;         ///< 战利品生成时间戳

        ObjectGuid m_linkedTrap;             ///< 关联陷阱GUID（用于采矿点等触发陷阱的对象）

    private:
        /// 从所有者移除
        void RemoveFromOwner();
        /// 切换门或按钮状态
        /// @param activate 是否激活
        /// @param alternative 是否使用替代状态
        void SwitchDoorOrButton(bool activate, bool alternative = false);
        /// 更新打包旋转
        void UpdatePackedRotation();

        /// 检查是否在距离内（重写Object::_IsWithinDist，考虑GO大小）
        /// @param obj 对象
        /// @param dist2compare 比较距离
        /// @param is3D 是否3D距离
        /// @param incOwnRadius 是否包含自身半径
        /// @param incTargetRadius 是否包含目标半径
        /// @return 在距离内返回true
        bool _IsWithinDist(WorldObject const* obj, float dist2compare, bool /*is3D*/, bool /*incOwnRadius*/, bool /*incTargetRadius*/) const override
        {
            // 以下检查确实检查3D距离
            return IsInRange(obj->GetPositionX(), obj->GetPositionY(), obj->GetPositionZ(), dist2compare);
        }

        GameObjectAI* m_AI;                  ///< AI指针 - 智能行为控制器（可为nullptr）
        bool m_respawnCompatibilityMode;     ///< 重生兼容模式标志 - 用于临时解决方案
};
#endif
