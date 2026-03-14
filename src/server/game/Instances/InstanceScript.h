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
 * @file InstanceScript.h
 * @brief 副本脚本基类模块
 *
 * 本模块提供了副本实例脚本的基础框架,包括:
 * - 首领状态管理(战斗状态、完成状态等)
 * - 门和仆从的控制
 * - 实例数据的保存和加载
 * - 玩家进入/离开事件处理
 * - 成就条件检查
 * - 世界状态更新
 *
 * 所有具体的副本脚本都应继承自InstanceScript类并实现相应的虚函数。
 */

#ifndef TRINITY_INSTANCE_DATA_H
#define TRINITY_INSTANCE_DATA_H

#include "ZoneScript.h"
#include "Common.h"
#include "Duration.h"
#include <map>
#include <set>

#ifdef TRINITY_API_USE_DYNAMIC_LINKING
#include <memory>
#endif

#define OUT_SAVE_INST_DATA             TC_LOG_DEBUG("scripts", "Saving Instance Data for Instance {} (Map {}, Instance Id {})", instance->GetMapName(), instance->GetId(), instance->GetInstanceId())
#define OUT_SAVE_INST_DATA_COMPLETE    TC_LOG_DEBUG("scripts", "Saving Instance Data for Instance {} (Map {}, Instance Id {}) completed.", instance->GetMapName(), instance->GetId(), instance->GetInstanceId())
#define OUT_LOAD_INST_DATA(a)          TC_LOG_DEBUG("scripts", "Loading Instance Data for Instance {} (Map {}, Instance Id {}). Input is '{}'", instance->GetMapName(), instance->GetId(), instance->GetInstanceId(), a)
#define OUT_LOAD_INST_DATA_COMPLETE    TC_LOG_DEBUG("scripts", "Instance Data Load for Instance {} (Map {}, Instance Id: {}) is complete.", instance->GetMapName(), instance->GetId(), instance->GetInstanceId())
#define OUT_LOAD_INST_DATA_FAIL        TC_LOG_ERROR("scripts", "Unable to load Instance Data for Instance {} (Map {}, Instance Id: {}).", instance->GetMapName(), instance->GetId(), instance->GetInstanceId())

namespace WorldPackets
{
    namespace WorldState
    {
        class InitWorldStates;
    }
}

class AreaBoundary;
class Creature;
class GameObject;
class InstanceMap;
struct InstanceSpawnGroupInfo;
class ModuleReference;
class Player;
class Unit;
class WorldPacket;
enum AchievementCriteriaTypes : uint8;
enum AchievementCriteriaTimedTypes : uint8;
enum EncounterCreditType : uint8;

/**
 * @enum EncounterFrameType
 * @brief 首领战斗框类型枚举
 *
 * 用于控制客户端首领战斗框的显示和更新
 */
enum EncounterFrameType
{
    ENCOUNTER_FRAME_ENGAGE              = 0,   ///< 开始战斗,显示首领框
    ENCOUNTER_FRAME_DISENGAGE           = 1,   ///< 结束战斗,隐藏首领框
    ENCOUNTER_FRAME_UPDATE_PRIORITY     = 2,   ///< 更新优先级
    ENCOUNTER_FRAME_ADD_TIMER           = 3,   ///< 添加计时器
    ENCOUNTER_FRAME_ENABLE_OBJECTIVE    = 4,   ///< 启用目标
    ENCOUNTER_FRAME_UPDATE_OBJECTIVE    = 5,   ///< 更新目标
    ENCOUNTER_FRAME_DISABLE_OBJECTIVE   = 6,   ///< 禁用目标
    ENCOUNTER_FRAME_PHASE_SHIFT_CHANGED = 7    ///< 阶段转换改变
};

/**
 * @enum EncounterState
 * @brief 首领战斗状态枚举
 *
 * 表示首领遭遇战的当前状态
 */
enum EncounterState
{
    NOT_STARTED   = 0,   ///< 未开始
    IN_PROGRESS   = 1,   ///< 进行中
    FAIL          = 2,   ///< 失败
    DONE          = 3,   ///< 已完成
    SPECIAL       = 4,   ///< 特殊状态
    TO_BE_DECIDED = 5    ///< 待定(加载中)
};

/**
 * @enum DoorType
 * @brief 门类型枚举
 *
 * 定义副本中不同类型的门及其开关条件
 */
enum DoorType
{
    DOOR_TYPE_ROOM          = 0,    ///< 房间门: 遭遇战未进行时可打开
    DOOR_TYPE_PASSAGE       = 1,    ///< 通道门: 遭遇战完成后可打开
    DOOR_TYPE_SPAWN_HOLE    = 2,    ///< 刷新洞: 遭遇战进行时可打开,通常用于刷新点
    MAX_DOOR_TYPES
};

/**
 * @struct DoorData
 * @brief 门数据结构
 *
 * 用于配置副本中的门与首领的关联关系
 */
struct DoorData
{
    uint32 entry, bossId;   ///< 游戏对象ID, 首领ID
    DoorType type;          ///< 门类型
};

/**
 * @struct BossBoundaryEntry
 * @brief 首领边界条目结构
 *
 * 定义首领战斗区域的边界
 */
struct BossBoundaryEntry
{
    uint32 BossId;                  ///< 首领ID
    AreaBoundary const* Boundary;   ///< 区域边界指针
};

/**
 * @struct BossBoundaryData
 * @brief 首领边界数据容器
 *
 * 存储所有首领的边界数据
 */
struct TC_GAME_API BossBoundaryData
{
    typedef std::vector<BossBoundaryEntry> StorageType;           ///< 存储类型
    typedef StorageType::const_iterator const_iterator;           ///< 常量迭代器

    /**
     * @brief 构造函数
     * @param data 初始化列表
     */
    BossBoundaryData(std::initializer_list<BossBoundaryEntry> data) : _data(data) { }

    /**
     * @brief 析构函数,清理边界对象
     */
    ~BossBoundaryData();

    const_iterator begin() const { return _data.begin(); }   ///< 获取起始迭代器
    const_iterator end() const { return _data.end(); }       ///< 获取结束迭代器

    private:
        StorageType _data;   ///< 边界数据存储
};

/**
 * @struct MinionData
 * @brief 仆从数据结构
 *
 * 用于配置副本中仆从与首领的关联关系
 */
struct MinionData
{
    uint32 entry, bossId;   ///< 生物ID, 首领ID
};

/**
 * @struct ObjectData
 * @brief 对象数据结构
 *
 * 用于配置副本中特殊对象的类型
 */
struct ObjectData
{
    uint32 entry;   ///< 对象ID(生物或游戏对象)
    uint32 type;    ///< 对象类型标识
};

typedef std::vector<AreaBoundary const*> CreatureBoundary;   ///< 生物边界类型

/**
 * @struct BossInfo
 * @brief 首领信息结构
 *
 * 存储首领的所有相关信息,包括状态、门、仆从和边界
 */
struct BossInfo
{
    BossInfo() : state(TO_BE_DECIDED) { }
    EncounterState state;               ///< 首领状态
    GuidSet door[MAX_DOOR_TYPES];       ///< 各类型门的GUID集合
    GuidSet minion;                     ///< 仆从的GUID集合
    CreatureBoundary boundary;          ///< 边界区域列表
};

/**
 * @struct DoorInfo
 * @brief 门信息结构
 *
 * 存储单个门的关联信息
 */
struct DoorInfo
{
    /**
     * @brief 构造函数
     * @param _bossInfo 关联的首领信息指针
     * @param _type 门类型
     */
    explicit DoorInfo(BossInfo* _bossInfo, DoorType _type)
        : bossInfo(_bossInfo), type(_type) { }
    BossInfo* bossInfo;   ///< 关联的首领信息
    DoorType type;        ///< 门类型
};

/**
 * @struct MinionInfo
 * @brief 仆从信息结构
 *
 * 存储单个仆从的关联信息
 */
struct MinionInfo
{
    /**
     * @brief 构造函数
     * @param _bossInfo 关联的首领信息指针
     */
    explicit MinionInfo(BossInfo* _bossInfo) : bossInfo(_bossInfo) { }
    BossInfo* bossInfo;   ///< 关联的首领信息
};

typedef std::multimap<uint32 /*entry*/, DoorInfo> DoorInfoMap;                              ///< 门信息映射(一个对象ID可能对应多个门)
typedef std::pair<DoorInfoMap::const_iterator, DoorInfoMap::const_iterator> DoorInfoMapBounds;  ///< 门信息映射范围

typedef std::map<uint32 /*entry*/, MinionInfo> MinionInfoMap;                               ///< 仆从信息映射
typedef std::map<uint32 /*type*/, ObjectGuid /*guid*/> ObjectGuidMap;                       ///< 对象类型到GUID的映射
typedef std::map<uint32 /*entry*/, uint32 /*type*/> ObjectInfoMap;                          ///< 对象ID到类型的映射

/**
 * @class InstanceScript
 * @brief 副本脚本基类
 *
 * 所有副本脚本都应继承此类,实现特定的副本逻辑。
 * 提供了首领状态管理、门控制、数据保存/加载、玩家事件处理等功能。
 *
 * 生命周期:
 * - 创建: 实例地图创建时
 * - 销毁: 实例地图销毁时
 */
class TC_GAME_API InstanceScript : public ZoneScript
{
    public:
        /**
         * @brief 构造函数
         * @param map 实例地图指针
         */
        explicit InstanceScript(InstanceMap* map);

        virtual ~InstanceScript() { }

        InstanceMap* instance;   ///< 实例地图指针

        /**
         * @brief 创建新的实例(无保存数据时调用)
         *
         * 当实例首次创建或没有任何保存数据时调用此方法。
         * 应在此初始化所有首领状态为NOT_STARTED。
         */
        virtual void Create();

        /**
         * @brief 加载实例保存数据
         * @param data 保存的实例数据字符串
         *
         * 当加载现有实例存档数据时调用此方法。
         * 应在此解析数据并恢复实例状态。
         */
        virtual void Load(char const* data);

        /**
         * @brief 获取实例保存数据
         * @return 实例数据字符串
         *
         * 当需要保存实例数据时调用此方法生成数据字符串。
         */
        virtual std::string GetSaveData();

        /**
         * @brief 将实例数据保存到数据库
         *
         * 调用GetSaveData()并将结果保存到数据库。
         */
        void SaveToDB();

        /**
         * @brief 更新实例
         * @param diff 距上次更新的时间间隔(毫秒)
         *
         * 每个世界更新周期调用,用于处理实例内的定时事件。
         */
        virtual void Update(uint32 /*diff*/) { }

        /**
         * @brief 检查是否有首领战斗正在进行
         * @return 如果有首领战斗进行中返回true
         *
         * 用于地图的CannotEnter函数,防止玩家在首领战斗期间进入实例。
         */
        virtual bool IsEncounterInProgress() const;

        /**
         * @brief 当生物被添加到地图时调用
         * @param creature 生物指针
         *
         * 将生物添加到动态GUID存储中
         */
        virtual void OnCreatureCreate(Creature* creature) override;

        /**
         * @brief 当生物从地图移除时调用
         * @param creature 生物指针
         *
         * 从动态GUID存储中移除生物
         */
        virtual void OnCreatureRemove(Creature* creature) override;

        /**
         * @brief 当游戏对象被添加到地图时调用
         * @param go 游戏对象指针
         *
         * 将游戏对象添加到动态GUID存储中
         */
        virtual void OnGameObjectCreate(GameObject* go) override;

        /**
         * @brief 当游戏对象从地图移除时调用
         * @param go 游戏对象指针
         *
         * 从动态GUID存储中移除游戏对象
         */
        virtual void OnGameObjectRemove(GameObject* go) override;

        /**
         * @brief 根据类型获取对象GUID
         * @param type 对象类型标识
         * @return 对象GUID,不存在返回空GUID
         */
        ObjectGuid GetObjectGuid(uint32 type) const;

        /**
         * @brief 根据类型获取GUID数据
         * @param type 对象类型标识
         * @return 对象GUID
         */
        virtual ObjectGuid GetGuidData(uint32 type) const override;

        /**
         * @brief 根据类型获取生物
         * @param type 对象类型标识
         * @return 生物指针,不存在返回nullptr
         */
        Creature* GetCreature(uint32 type);

        /**
         * @brief 根据类型获取游戏对象
         * @param type 对象类型标识
         * @return 游戏对象指针,不存在返回nullptr
         */
        GameObject* GetGameObject(uint32 type);

        /**
         * @brief 当玩家成功进入实例时调用
         * @param player 玩家指针
         */
        virtual void OnPlayerEnter(Player* /*player*/) { }

        /**
         * @brief 当玩家成功离开实例时调用
         * @param player 玩家指针
         */
        virtual void OnPlayerLeave(Player* /*player*/) { }

        /**
         * @brief 处理游戏对象的打开/关闭
         * @param guid 游戏对象GUID
         * @param open true:打开, false:关闭
         * @param go 游戏对象指针(可选,为nullptr时会通过GUID查找)
         *
         * 使用方式:
         * - 在OnObjectCreate中: HandleGameObject(0, boolen, GO)
         * - 在其他脚本中: HandleGameObject(GUID, boolen, nullptr)
         */
        void HandleGameObject(ObjectGuid guid, bool open, GameObject* go = nullptr);

        /**
         * @brief 使用门或按钮
         * @param guid 游戏对象GUID
         * @param withRestoreTime 恢复时间(毫秒),0表示不自动恢复
         * @param useAlternativeState 是否使用替代状态
         */
        void DoUseDoorOrButton(ObjectGuid guid, uint32 withRestoreTime = 0, bool useAlternativeState = false);

        /**
         * @brief 关闭门或按钮
         * @param guid 游戏对象GUID
         */
        void DoCloseDoorOrButton(ObjectGuid guid);

        /**
         * @brief 重生游戏对象
         * @param guid 游戏对象GUID
         * @param timeToDespawn 多久后消失(默认1分钟)
         *
         * 仅对spawntimesecs为负值的游戏对象有效
         */
        void DoRespawnGameObject(ObjectGuid guid, Seconds timeToDespawn = 1min);

        /**
         * @brief 向实例内所有玩家发送世界状态更新
         * @param worldstateId 世界状态ID
         * @param worldstateValue 世界状态值
         */
        void DoUpdateWorldState(uint32 worldstateId, uint32 worldstateValue);

        /**
         * @brief 向实例内所有玩家发送通知
         * @param format 格式化字符串
         * @param ... 可变参数
         */
        void DoSendNotifyToInstance(char const* format, ...);

        /**
         * @brief 更新实例内所有玩家的成就条件
         * @param type 成就条件类型
         * @param miscValue1 附加值1
         * @param miscValue2 附加值2
         * @param unit 相关单位
         */
        void DoUpdateAchievementCriteria(AchievementCriteriaTypes type, uint32 miscValue1 = 0, uint32 miscValue2 = 0, Unit* unit = nullptr);

        /**
         * @brief 开始实例内所有玩家的计时成就
         * @param type 成就类型
         * @param entry 条目ID
         */
        void DoStartTimedAchievement(AchievementCriteriaTimedTypes type, uint32 entry);

        /**
         * @brief 停止实例内所有玩家的计时成就
         * @param type 成就类型
         * @param entry 条目ID
         */
        void DoStopTimedAchievement(AchievementCriteriaTimedTypes type, uint32 entry);

        /**
         * @brief 移除实例内所有玩家身上的指定法术光环
         * @param spell 法术ID
         * @param includePets 是否包含宠物
         * @param includeControlled 是否包含控制单位
         */
        void DoRemoveAurasDueToSpellOnPlayers(uint32 spell, bool includePets = false, bool includeControlled = false);

        /**
         * @brief 移除指定玩家身上的指定法术光环
         * @param player 玩家指针
         * @param spell 法术ID
         * @param includePets 是否包含宠物
         * @param includeControlled 是否包含控制单位
         */
        void DoRemoveAurasDueToSpellOnPlayer(Player* player, uint32 spell, bool includePets = false, bool includeControlled = false);

        /**
         * @brief 对实例内所有玩家施放法术
         * @param spell 法术ID
         * @param includePets 是否包含宠物
         * @param includeControlled 是否包含控制单位
         */
        void DoCastSpellOnPlayers(uint32 spell, bool includePets = false, bool includeControlled = false);

        /**
         * @brief 对指定玩家施放法术
         * @param player 玩家指针
         * @param spell 法术ID
         * @param includePets 是否包含宠物
         * @param includeControlled 是否包含控制单位
         */
        void DoCastSpellOnPlayer(Player* player, uint32 spell, bool includePets = false, bool includeControlled = false);

        /**
         * @brief 服务器是否允许跨阵营组队
         * @return 是否允许
         */
        static bool ServerAllowsTwoSideGroups();

        /**
         * @brief 设置首领状态
         * @param id 首领ID
         * @param state 新状态
         * @return 状态是否成功改变
         */
        virtual bool SetBossState(uint32 id, EncounterState state);

        /**
         * @brief 获取首领状态
         * @param id 首领ID
         * @return 首领状态
         */
        EncounterState GetBossState(uint32 id) const { return id < bosses.size() ? bosses[id].state : TO_BE_DECIDED; }

        /**
         * @brief 获取首领状态名称
         * @param state 状态值
         * @return 状态名称字符串
         */
        static char const* GetBossStateName(uint8 state);

        /**
         * @brief 获取首领边界
         * @param id 首领ID
         * @return 边界指针,不存在返回nullptr
         */
        CreatureBoundary const* GetBossBoundary(uint32 id) const { return id < bosses.size() ? &bosses[id].boundary : nullptr; }

        /**
         * @brief 检查成就条件是否满足
         * @param criteria_id 成就条件ID
         * @param source 触发玩家
         * @param target 目标单位
         * @param miscvalue1 附加值
         * @return 是否满足条件
         *
         * 注意: 如果可以使用AchievementCriteriaRequirementType中已有的要求类型检查,请勿使用此方法
         */
        virtual bool CheckAchievementCriteriaMeet(uint32 /*criteria_id*/, Player const* /*source*/, Unit const* /*target*/ = nullptr, uint32 /*miscvalue1*/ = 0);

        /**
         * @brief 检查首领前置要求(击杀某首领才能击杀另一首领)
         * @param bossId 首领ID
         * @param player 玩家指针
         * @return 是否满足要求
         */
        virtual bool CheckRequiredBosses(uint32 /*bossId*/, Player const* /*player*/ = nullptr) const { return true; }

        /**
         * @brief 击杀生物时更新遭遇状态
         * @param creatureId 生物ID
         * @param source 来源单位
         */
        void UpdateEncounterStateForKilledCreature(uint32 creatureId, Unit* source);

        /**
         * @brief 施放法术时更新遭遇状态
         * @param spellId 法术ID
         * @param source 来源单位
         */
        void UpdateEncounterStateForSpellCast(uint32 spellId, Unit* source);

        /**
         * @brief 设置已完成的遭遇掩码
         * @param newMask 新掩码
         *
         * 仅在加载时使用
         */
        void SetCompletedEncountersMask(uint32 newMask) { completedEncounters = newMask; }

        /**
         * @brief 获取已完成的遭遇掩码
         * @return 遭遇掩码
         *
         * 用于数据包发送
         */
        uint32 GetCompletedEncounterMask() const { return completedEncounters; }

        /**
         * @brief 发送遭遇单位更新
         * @param type 遭遇框类型
         * @param unit 单位指针
         * @param param1 参数1
         * @param param2 参数2
         */
        void SendEncounterUnit(EncounterFrameType type, Unit const* unit = nullptr, uint8 param1 = 0, uint8 param2 = 0);

        /**
         * @brief 填充初始世界状态
         * @param packet 世界状态数据包
         */
        virtual void FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& /*packet*/) { }

        /**
         * @brief 获取遭遇数量
         * @return 首领数量
         */
        uint32 GetEncounterCount() const { return bosses.size(); }

        /**
         * @brief 标记区域触发器已完成
         * @param id 区域触发器ID
         *
         * 仅用于继承自OnlyOnceAreaTriggerScript的区域触发器
         */
        void MarkAreaTriggerDone(uint32 id) { _activatedAreaTriggers.insert(id); }

        /**
         * @brief 重置区域触发器状态
         * @param id 区域触发器ID
         */
        void ResetAreaTriggerDone(uint32 id) { _activatedAreaTriggers.erase(id); }

        /**
         * @brief 检查区域触发器是否已完成
         * @param id 区域触发器ID
         * @return 是否已完成
         */
        bool IsAreaTriggerDone(uint32 id) const { return _activatedAreaTriggers.find(id) != _activatedAreaTriggers.end(); }

    protected:
        /**
         * @brief 设置保存数据的头部标识
         * @param dataHeaders 头部字符串(每个字符代表一个标识)
         */
        void SetHeaders(std::string const& dataHeaders);

        /**
         * @brief 设置首领数量
         * @param number 首领数量
         */
        void SetBossNumber(uint32 number) { bosses.resize(number); }

        /**
         * @brief 加载首领边界数据
         * @param data 边界数据
         */
        void LoadBossBoundaries(BossBoundaryData const& data);

        /**
         * @brief 加载门数据
         * @param data 门数据数组(以entry=0结尾)
         */
        void LoadDoorData(DoorData const* data);

        /**
         * @brief 加载仆从数据
         * @param data 仆从数据数组(以entry=0结尾)
         */
        void LoadMinionData(MinionData const* data);

        /**
         * @brief 加载对象数据
         * @param creatureData 生物数据数组
         * @param gameObjectData 游戏对象数据数组
         */
        void LoadObjectData(ObjectData const* creatureData, ObjectData const* gameObjectData);

        /**
         * @brief 添加生物对象
         * @param obj 生物指针
         * @param add true:添加, false:移除
         */
        void AddObject(Creature* obj, bool add);

        /**
         * @brief 添加游戏对象
         * @param obj 游戏对象指针
         * @param add true:添加, false:移除
         */
        void AddObject(GameObject* obj, bool add);

        /**
         * @brief 添加对象到映射
         * @param obj 世界对象指针
         * @param type 对象类型
         * @param add true:添加, false:移除
         */
        void AddObject(WorldObject* obj, uint32 type, bool add);

        /**
         * @brief 添加门到实例
         * @param door 门对象指针
         * @param add true:添加, false:移除
         */
        virtual void AddDoor(GameObject* door, bool add);

        /**
         * @brief 添加仆从到实例
         * @param minion 仆从指针
         * @param add true:添加, false:移除
         */
        void AddMinion(Creature* minion, bool add);

        /**
         * @brief 更新门状态
         * @param door 门对象指针
         *
         * 根据关联的首领状态更新门的开关状态
         */
        virtual void UpdateDoorState(GameObject* door);

        /**
         * @brief 更新仆从状态
         * @param minion 仆从指针
         * @param state 首领状态
         *
         * 根据首领状态更新仆从的行为
         */
        void UpdateMinionState(Creature* minion, EncounterState state);

        /**
         * @brief 更新生成组
         *
         * 根据首领状态和阵营条件更新实例中的生成组状态
         */
        void UpdateSpawnGroups();

        /**
         * @brief 获取首领信息
         * @param id 首领ID
         * @return 首领信息指针
         *
         * 暴露私有数据,仅在特殊情况下修改。
         * 修改返回的BossInfo数据时要非常小心以避免问题。
         */
        BossInfo* GetBossInfo(uint32 id);

        /**
         * @brief 读取保存数据头部
         * @param data 输入数据流
         * @return 头部是否匹配
         */
        bool ReadSaveDataHeaders(std::istringstream& data);

        /**
         * @brief 读取保存数据中的首领状态
         * @param data 输入数据流
         */
        void ReadSaveDataBossStates(std::istringstream& data);

        /**
         * @brief 读取额外的保存数据
         * @param data 输入数据流
         *
         * 派生类可重写此方法以加载自定义数据
         */
        virtual void ReadSaveDataMore(std::istringstream& /*data*/) { }

        /**
         * @brief 写入保存数据头部
         * @param data 输出数据流
         */
        void WriteSaveDataHeaders(std::ostringstream& data);

        /**
         * @brief 写入首领状态到保存数据
         * @param data 输出数据流
         */
        void WriteSaveDataBossStates(std::ostringstream& data);

        /**
         * @brief 写入额外的保存数据
         * @param data 输出数据流
         *
         * 派生类可重写此方法以保存自定义数据
         */
        virtual void WriteSaveDataMore(std::ostringstream& /*data*/) { }

        /**
         * @brief 检查是否跳过必需首领检查
         * @param player 玩家指针
         * @return 是否跳过检查
         */
        bool _SkipCheckRequiredBosses(Player const* player = nullptr) const;

    private:
        /**
         * @brief 加载对象数据到映射(静态方法)
         * @param creatureData 对象数据数组
         * @param objectInfo 对象信息映射
         */
        static void LoadObjectData(ObjectData const* creatureData, ObjectInfoMap& objectInfo);

        /**
         * @brief 更新遭遇状态
         * @param type 遭遇信用类型
         * @param creditEntry 信用条目
         * @param source 来源单位
         */
        void UpdateEncounterState(EncounterCreditType type, uint32 creditEntry, Unit* source);

        std::vector<char> headers;                              ///< 保存数据头部标识
        std::vector<BossInfo> bosses;                           ///< 首领信息数组
        DoorInfoMap doors;                                      ///< 门信息映射
        MinionInfoMap minions;                                  ///< 仆从信息映射
        ObjectInfoMap _creatureInfo;                            ///< 生物对象信息映射
        ObjectInfoMap _gameObjectInfo;                          ///< 游戏对象信息映射
        ObjectGuidMap _objectGuids;                             ///< 对象类型到GUID的映射
        uint32 completedEncounters;                             ///< 已完成的遭遇掩码,位索引为DungeonEncounter.dbc首领编号,用于数据包
        std::vector<InstanceSpawnGroupInfo> const* const _instanceSpawnGroups;  ///< 实例生成组信息
        std::unordered_set<uint32> _activatedAreaTriggers;      ///< 已激活的区域触发器ID集合

    #ifdef TRINITY_API_USE_DYNAMIC_LINKING
        std::shared_ptr<ModuleReference> module_reference;      ///< 关联脚本模块的强引用
    #endif // #ifndef TRINITY_API_USE_DYNAMIC_LINKING

        friend class debug_commandscript;   ///< 调试命令友元类
};

#endif // TRINITY_INSTANCE_DATA_H
