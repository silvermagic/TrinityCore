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
 * @file Map.h
 * @brief 地图系统核心头文件
 *
 * 本模块实现了游戏世界的地图管理系统，是 TrinityCore 的核心组件之一。
 *
 * 主要职责：
 * - 管理游戏世界的空间划分（网格系统）
 * - 处理游戏中对象的添加、移除和位置更新
 * - 管理地图的加载、卸载和动态更新
 * - 处理玩家和生物的可见性和交互范围
 * - 管理实例地图（地下城、团队副本）和战场地图
 * - 处理重生系统和尸体管理
 * - 管理地形数据（高度、水面、区域信息）
 * - 实现天气系统和区域动态效果
 *
 * 核心类：
 * - Map: 地图基类，提供所有地图的通用功能
 * - InstanceMap: 实例地图类（地下城、团队副本）
 * - BattlegroundMap: 战场地图类（战场、竞技场）
 * - GridMap: 网格地图数据管理类
 *
 * 设计模式：
 * - 使用网格（Grid）系统进行空间划分，优化对象查询和更新性能
 * - 采用引用计数管理玩家和对象的生命周期
 * - 使用观察者模式处理对象可见性更新
 *
 * 性能考虑：
 * - 网格按需加载和卸载，减少内存占用
 * - 对象更新基于可见距离进行优化
 * - 使用动态树结构加速碰撞检测和视线检测
 */

#ifndef TRINITY_MAP_H
#define TRINITY_MAP_H

#include "Define.h"

#include "Cell.h"
#include "DynamicTree.h"
#include "GridDefines.h"
#include "GridRefManager.h"
#include "MapRefManager.h"
#include "MPSCQueue.h"
#include "ObjectGuid.h"
#include "Optional.h"
#include "SharedDefines.h"
#include "SpawnData.h"
#include "Timer.h"
#include "Transaction.h"
#include "UniqueTrackablePtr.h"
#include <bitset>
#include <list>
#include <memory>
#include <mutex>

class Battleground;
class BattlegroundMap;
class CreatureGroup;
class GameObjectModel;
class Group;
class InstanceMap;
class InstanceSave;
class InstanceScript;
class MapInstanced;
class Object;
class Player;
class TempSummon;
class Transport;
class Unit;
class Weather;
class WorldObject;
class WorldPacket;
class WorldSession;
struct MapDifficulty;
struct MapEntry;
struct Position;
struct ScriptAction;
struct ScriptInfo;
struct SummonPropertiesEntry;
enum Difficulty : uint8;
enum WeatherState : uint32;

namespace Trinity { struct ObjectUpdater; }
namespace VMAP { enum class ModelIgnoreFlags : uint32; }
namespace G3D { class Plane; }

// ============================================================================
// 脚本动作结构体
// ============================================================================
struct ScriptAction
{
    ObjectGuid sourceGUID;                                  // 源对象GUID
    ObjectGuid targetGUID;                                  // 目标对象GUID
    ObjectGuid ownerGUID;                                   // 源对象的所有者GUID（如果源对象是物品）
    ScriptInfo const* script;                               // 静态脚本数据指针
};

/// 表示地图魔法值（4字节，用于版本信息）
union u_map_magic
{
    char asChar[4];                                         // 非空终止字符串
    uint32 asUInt;                                          // uint32表示
};

// ============================================================================
// 地图文件格式定义
// ============================================================================
struct map_fileheader
{
    u_map_magic mapMagic;                                   // 地图魔法值
    uint32 versionMagic;                                    // 版本魔法值
    u_map_magic buildMagic;                                 // 构建魔法值
    uint32 areaMapOffset;                                   // 区域地图偏移
    uint32 areaMapSize;                                     // 区域地图大小
    uint32 heightMapOffset;                                 // 高度地图偏移
    uint32 heightMapSize;                                   // 高度地图大小
    uint32 liquidMapOffset;                                 // 液体地图偏移
    uint32 liquidMapSize;                                   // 液体地图大小
    uint32 holesOffset;                                     // 洞穴偏移
    uint32 holesSize;                                       // 洞穴大小
};

// 区域地图标志
#define MAP_AREA_NO_AREA      0x0001                         // 无区域

// 区域地图头部
struct map_areaHeader
{
    uint32 fourcc;                                          // 四字符代码
    uint16 flags;                                           // 标志位
    uint16 gridArea;                                        // 网格区域
};

// 高度地图标志
#define MAP_HEIGHT_NO_HEIGHT            0x0001              // 无高度
#define MAP_HEIGHT_AS_INT16             0x0002              // 高度为INT16格式
#define MAP_HEIGHT_AS_INT8              0x0004              // 高度为INT8格式
#define MAP_HEIGHT_HAS_FLIGHT_BOUNDS    0x0008              // 有飞行边界

// 高度地图头部
struct map_heightHeader
{
    uint32 fourcc;                                          // 四字符代码
    uint32 flags;                                           // 标志位
    float  gridHeight;                                      // 网格高度
    float  gridMaxHeight;                                   // 网格最大高度
};

// 液体地图标志
#define MAP_LIQUID_NO_TYPE    0x0001                        // 无液体类型
#define MAP_LIQUID_NO_HEIGHT  0x0002                        // 无液体高度

// 液体地图头部
struct map_liquidHeader
{
    uint32 fourcc;                                          // 四字符代码
    uint8 flags;                                            // 标志位
    uint8 liquidFlags;                                      // 液体标志
    uint16 liquidType;                                      // 液体类型
    uint8  offsetX;                                         // X偏移
    uint8  offsetY;                                         // Y偏移
    uint8  width;                                           // 宽度
    uint8  height;                                          // 高度
    float  liquidLevel;                                     // 液体高度
};

// 液体状态枚举
enum ZLiquidStatus : uint32
{
    LIQUID_MAP_NO_WATER     = 0x00000000,                   // 无水
    LIQUID_MAP_ABOVE_WATER  = 0x00000001,                   // 在水面之上
    LIQUID_MAP_WATER_WALK   = 0x00000002,                   // 在水面上行走
    LIQUID_MAP_IN_WATER     = 0x00000004,                   // 在水中
    LIQUID_MAP_UNDER_WATER  = 0x00000008                    // 在水下
};

// 液体状态组合
#define MAP_LIQUID_STATUS_SWIMMING (LIQUID_MAP_IN_WATER | LIQUID_MAP_UNDER_WATER)  // 游泳状态
#define MAP_LIQUID_STATUS_IN_CONTACT (MAP_LIQUID_STATUS_SWIMMING | LIQUID_MAP_WATER_WALK)  // 接触水状态

// 液体类型定义
#define MAP_LIQUID_TYPE_NO_WATER    0x00                    // 无水
#define MAP_LIQUID_TYPE_WATER       0x01                    // 普通水
#define MAP_LIQUID_TYPE_OCEAN       0x02                    // 海洋
#define MAP_LIQUID_TYPE_MAGMA       0x04                    // 岩浆
#define MAP_LIQUID_TYPE_SLIME       0x08                    // 污泥

// 所有液体类型
#define MAP_ALL_LIQUIDS   (MAP_LIQUID_TYPE_WATER | MAP_LIQUID_TYPE_OCEAN | MAP_LIQUID_TYPE_MAGMA | MAP_LIQUID_TYPE_SLIME)

#define MAP_LIQUID_TYPE_DARK_WATER  0x10                    // 深水

// 液体数据
struct LiquidData
{
    uint32 type_flags;                                      // 类型标志
    uint32 entry;                                           // 条目ID
    float  level;                                           // 液面高度
    float  depth_level;                                     // 深度高度
};

// 位置完整地形状态
struct PositionFullTerrainStatus
{
    // 区域信息
    struct AreaInfo
    {
        AreaInfo(int32 _adtId, int32 _rootId, int32 _groupId, uint32 _flags) : adtId(_adtId), rootId(_rootId), groupId(_groupId), mogpFlags(_flags) { }
        int32 const adtId;                                  // ADT ID
        int32 const rootId;                                 // 根ID
        int32 const groupId;                                // 组ID
        uint32 const mogpFlags;                             // MOGP标志
    };

    PositionFullTerrainStatus() : areaId(0), floorZ(0.0f), outdoors(true), liquidStatus(LIQUID_MAP_NO_WATER) { }
    uint32 areaId;                                          // 区域ID
    float floorZ;                                           // 地面高度
    bool outdoors;                                          // 是否户外
    ZLiquidStatus liquidStatus;                             // 液体状态
    Optional<AreaInfo> areaInfo;                            // 区域信息
    Optional<LiquidData> liquidInfo;                        // 液体信息
};

// ============================================================================
// GridMap 类 - 网格地图数据管理
// ============================================================================
class TC_GAME_API GridMap
{
    uint32  _flags;                                         // 标志位
    union{
        float* m_V9;                                        // V9高度数据（浮点）
        uint16* m_uint16_V9;                                // V9高度数据（uint16）
        uint8* m_uint8_V9;                                  // V9高度数据（uint8）
    };
    union{
        float* m_V8;                                        // V8高度数据（浮点）
        uint16* m_uint16_V8;                                // V8高度数据（uint16）
        uint8* m_uint8_V8;                                  // V8高度数据（uint8）
    };
    G3D::Plane* _minHeightPlanes;                           // 最小高度平面
    // 高度数据
    float _gridHeight;                                      // 网格高度
    float _gridIntHeightMultiplier;                         // 网格整数高度乘数

    // 区域数据
    uint16* _areaMap;                                       // 区域地图

    // 液体数据
    float _liquidLevel;                                     // 液体高度
    uint16* _liquidEntry;                                   // 液体条目
    uint8* _liquidFlags;                                    // 液体标志
    float* _liquidMap;                                      // 液体地图
    uint16 _gridArea;                                       // 网格区域
    uint16 _liquidGlobalEntry;                              // 全局液体条目
    uint8 _liquidGlobalFlags;                               // 全局液体标志
    uint8 _liquidOffX;                                      // 液体X偏移
    uint8 _liquidOffY;                                      // 液体Y偏移
    uint8 _liquidWidth;                                     // 液体宽度
    uint8 _liquidHeight;                                    // 液体高度

    uint16* _holes;                                         // 洞穴数据

    bool loadAreaData(FILE* in, uint32 offset, uint32 size);   // 加载区域数据
    bool loadHeightData(FILE* in, uint32 offset, uint32 size);  // 加载高度数据
    bool loadLiquidData(FILE* in, uint32 offset, uint32 size);  // 加载液体数据
    bool loadHolesData(FILE* in, uint32 offset, uint32 size);   // 加载洞穴数据
    bool isHole(int row, int col) const;                    // 判断是否为洞穴

    // 获取高度函数和指针
    typedef float (GridMap::*GetHeightPtr) (float x, float y) const;
    GetHeightPtr _gridGetHeight;                             // 获取高度函数指针
    float getHeightFromFloat(float x, float y) const;        // 从浮点数据获取高度
    float getHeightFromUint16(float x, float y) const;       // 从uint16数据获取高度
    float getHeightFromUint8(float x, float y) const;        // 从uint8数据获取高度
    float getHeightFromFlat(float x, float y) const;         // 从平面获取高度

public:
    GridMap();
    ~GridMap();
    bool loadData(char const* filename);                     // 加载数据
    void unloadData();                                       // 卸载数据

    uint16 getArea(float x, float y) const;                  // 获取区域ID
    inline float getHeight(float x, float y) const {return (this->*_gridGetHeight)(x, y);}  // 获取高度
    float getMinHeight(float x, float y) const;              // 获取最小高度
    float getLiquidLevel(float x, float y) const;            // 获取液体高度
    ZLiquidStatus GetLiquidStatus(float x, float y, float z, uint8 ReqLiquidType, LiquidData* data = 0, float collisionHeight = 2.03128f);  // 获取液体状态
};

#pragma pack(push, 1)

// 等级要求枚举
enum LevelRequirementVsMode
{
    LEVELREQUIREMENT_HEROIC = 70                             // 英雄模式等级要求
};

// ============================================================================
// 区域动态信息结构体 - 管理区域动态效果（天气、音乐、光照）
// ============================================================================
struct ZoneDynamicInfo
{
    ZoneDynamicInfo();

    uint32 MusicId;                                          // 音乐ID

    std::unique_ptr<Weather> DefaultWeather;                 // 默认天气对象
    WeatherState WeatherId;                                  // 天气状态ID
    float Intensity;                                         // 天气强度

    // 光照覆盖
    struct LightOverride
    {
        uint32 AreaLightId;                                  // 区域光照ID
        uint32 OverrideLightId;                              // 覆盖光照ID
        uint32 TransitionMilliseconds;                       // 过渡时间（毫秒）
    };
    std::vector<LightOverride> LightOverrides;               // 光照覆盖列表
};

#pragma pack(pop)

// 高度和地图相关常量
#define MAX_HEIGHT            100000.0f                     // 最大高度（可用于查找地面高度）
#define INVALID_HEIGHT       -100000.0f                     // 无效高度（用于检查，必须等于VMAP_INVALID_HEIGHT）
#define MAX_FALL_DISTANCE     250000.0f                     // 最大掉落距离（用于查找VMap地面）
#define DEFAULT_HEIGHT_SEARCH     50.0f                     // 默认高度搜索距离
#define MIN_UNLOAD_DELAY      1                             // 最小卸载延迟（立即卸载）
#define MAP_INVALID_ZONE      0xFFFFFFFF                    // 无效区域

struct RespawnInfo; // 前向声明
// 重生信息比较器
struct CompareRespawnInfo
{
    bool operator()(RespawnInfo const* a, RespawnInfo const* b) const;
};
using ZoneDynamicInfoMap = std::unordered_map<uint32 /*zoneId*/, ZoneDynamicInfo>;  // 区域动态信息映射
struct RespawnListContainer;
using RespawnInfoMap = std::unordered_map<ObjectGuid::LowType, RespawnInfo*>;  // 重生信息映射

// ============================================================================
// RespawnInfo 结构体 - 重生信息
// ============================================================================
struct TC_GAME_API RespawnInfo
{
    virtual ~RespawnInfo();

    SpawnObjectType type;                                    // 生成对象类型
    ObjectGuid::LowType spawnId;                             // 生成ID
    uint32 entry;                                            // 条目ID
    time_t respawnTime;                                      // 重生时间
    uint32 gridId;                                           // 网格ID
};
inline bool CompareRespawnInfo::operator()(RespawnInfo const* a, RespawnInfo const* b) const
{
    if (a == b)
        return false;
    if (a->respawnTime != b->respawnTime)
        return (a->respawnTime > b->respawnTime);
    if (a->spawnId != b->spawnId)
        return a->spawnId < b->spawnId;
    ASSERT(a->type != b->type, "Duplicate respawn entry for spawnId (%u,%u) found!", a->type, a->spawnId);
    return a->type < b->type;
}

extern template class TypeUnorderedMapContainer<AllMapStoredObjectTypes, ObjectGuid>;
typedef TypeUnorderedMapContainer<AllMapStoredObjectTypes, ObjectGuid> MapStoredObjectTypesContainer;

// ============================================================================
// Map 类 - 地图核心类，管理游戏空间
// ============================================================================
class TC_GAME_API Map : public GridRefManager<NGridType>
{
    friend class MapReference;
    public:
        // 构造函数和析构函数
        Map(uint32 id, time_t, uint32 InstanceId, uint8 SpawnMode, Map* _parent = nullptr);
        virtual ~Map();

        // 获取地图条目
        MapEntry const* GetEntry() const { return i_mapEntry; }

        // 检查是否可以卸载（当前对普通地图未使用）
        bool CanUnload(uint32 diff)
        {
            if (!m_unloadTimer)
                return false;

            if (m_unloadTimer <= diff)
                return true;

            m_unloadTimer -= diff;
            return false;
        }

        // 将玩家添加到地图
        virtual bool AddPlayerToMap(Player*);
        // 从地图移除玩家
        virtual void RemovePlayerFromMap(Player*, bool);

        // 将对象添加到地图（模板函数）
        template<class T> bool AddToMap(T *);
        // 从地图移除对象（模板函数）
        template<class T> void RemoveFromMap(T *, bool);

        // 访问世界对象附近的网格单元
        void VisitNearbyCellsOf(WorldObject* obj, TypeContainerVisitor<Trinity::ObjectUpdater, GridTypeMapContainer> &gridVisitor, TypeContainerVisitor<Trinity::ObjectUpdater, WorldTypeMapContainer> &worldVisitor);
        // 更新地图
        virtual void Update(uint32);

        // 获取可见范围
        float GetVisibilityRange() const { return m_VisibleDistance; }
        // 初始化可见距离（根据地图类型/ID设置）
        virtual void InitVisibilityDistance();

        // 玩家位置重定位
        void PlayerRelocation(Player*, float x, float y, float z, float orientation);
        // 生物位置重定位
        void CreatureRelocation(Creature* creature, float x, float y, float z, float ang, bool respawnRelocationOnFail = true);
        // 游戏对象位置重定位
        void GameObjectRelocation(GameObject* go, float x, float y, float z, float orientation, bool respawnRelocationOnFail = true);
        // 动态对象位置重定位
        void DynamicObjectRelocation(DynamicObject* go, float x, float y, float z, float orientation);

        // 访问网格单元（模板函数）
        template<class T, class CONTAINER>
        void Visit(Cell const& cell, TypeContainerVisitor<T, CONTAINER>& visitor);

        // 判断是否为移除状态的网格
        bool IsRemovalGrid(float x, float y) const
        {
            GridCoord p = Trinity::ComputeGridCoord(x, y);
            return !getNGrid(p.x_coord, p.y_coord) || getNGrid(p.x_coord, p.y_coord)->GetGridState() == GRID_STATE_REMOVAL;
        }
        bool IsRemovalGrid(Position const& pos) const { return IsRemovalGrid(pos.GetPositionX(), pos.GetPositionY()); }

        // 检查网格是否已加载
        bool IsGridLoaded(uint32 gridId) const { return IsGridLoaded(GridCoord(gridId % MAX_NUMBER_OF_GRIDS, gridId / MAX_NUMBER_OF_GRIDS)); }
        bool IsGridLoaded(float x, float y) const { return IsGridLoaded(Trinity::ComputeGridCoord(x, y)); }
        bool IsGridLoaded(Position const& pos) const { return IsGridLoaded(pos.GetPositionX(), pos.GetPositionY()); }

        // 网格卸载锁控制
        bool GetUnloadLock(GridCoord const& p) const { return getNGrid(p.x_coord, p.y_coord)->getUnloadLock(); }
        void SetUnloadLock(GridCoord const& p, bool on) { getNGrid(p.x_coord, p.y_coord)->setUnloadExplicitLock(on); }
        // 加载网格
        void LoadGrid(float x, float y);
        // 加载所有单元格
        void LoadAllCells();
        // 卸载网格
        bool UnloadGrid(NGridType& ngrid, bool pForce);
        // 标记网格为不可卸载
        void GridMarkNoUnload(uint32 x, uint32 y);
        // 取消网格不可卸载标记
        void GridUnmarkNoUnload(uint32 x, uint32 y);
        // 卸载所有网格
        virtual void UnloadAll();

        // 重置网格过期时间
        void ResetGridExpiry(NGridType &grid, float factor = 1) const
        {
            grid.ResetTimeTracker(time_t(float(i_gridExpiry)*factor));
        }

        // 获取网格过期时间
        time_t GetGridExpiry(void) const { return i_gridExpiry; }
        // 获取地图ID
        uint32 GetId() const;

        // 检查地图文件是否存在
        static bool ExistMap(uint32 mapid, int gx, int gy);
        // 检查虚拟地图文件是否存在
        static bool ExistVMap(uint32 mapid, int gx, int gy);

        // 初始化状态机
        static void InitStateMachine();
        // 删除状态机
        static void DeleteStateMachine();

        // 获取父地图
        Map const* GetParent() const { return m_parentMap; }

        // 获取位置的完整地形状态
        void GetFullTerrainStatusForPosition(uint32 phaseMask, float x, float y, float z, PositionFullTerrainStatus& data, uint8 reqLiquidType, float collisionHeight) const;
        // 获取液体状态
        ZLiquidStatus GetLiquidStatus(uint32 phaseMask, float x, float y, float z, uint8 ReqLiquidType, LiquidData* data = nullptr, float collisionHeight = 2.03128f) const;

        // 获取区域信息
        bool GetAreaInfo(uint32 phaseMask, float x, float y, float z, uint32& mogpflags, int32& adtId, int32& rootId, int32& groupId) const;
        // 获取区域ID
        uint32 GetAreaId(uint32 phaseMask, float x, float y, float z) const;
        uint32 GetAreaId(uint32 phaseMask, Position const& pos) const { return GetAreaId(phaseMask, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ()); }
        // 获取区域ID
        uint32 GetZoneId(uint32 phaseMask, float x, float y, float z) const;
        uint32 GetZoneId(uint32 phaseMask, Position const& pos) const { return GetZoneId(phaseMask, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ()); }
        // 获取区域和子区域ID
        void GetZoneAndAreaId(uint32 phaseMask, uint32& zoneid, uint32& areaid, float x, float y, float z) const;
        void GetZoneAndAreaId(uint32 phaseMask, uint32& zoneid, uint32& areaid, Position const& pos) const { GetZoneAndAreaId(phaseMask, zoneid, areaid, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ()); }

        // 获取水面高度
        float GetWaterLevel(float x, float y) const;
        // 判断是否在水中
        bool IsInWater(uint32 phaseMask, float x, float y, float z, LiquidData* data = nullptr) const;
        // 判断是否在水下
        bool IsUnderWater(uint32 phaseMask, float x, float y, float z) const;

        // 移动所有在移动列表中的生物
        void MoveAllCreaturesInMoveList();
        // 移动所有在移动列表中的游戏对象
        void MoveAllGameObjectsInMoveList();
        // 移动所有在移动列表中的动态对象
        void MoveAllDynamicObjectsInMoveList();
        // 移除所有在移除列表中的对象
        void RemoveAllObjectsInRemoveList();
        // 移除所有玩家
        virtual void RemoveAllPlayers();

        // 生物重生位置重定位（仅在MoveAllCreaturesInMoveList和ObjectGridUnloader中使用）
        bool CreatureRespawnRelocation(Creature* c, bool diffGridOnly);
        // 游戏对象重生位置重定位
        bool GameObjectRespawnRelocation(GameObject* go, bool diffGridOnly);

        // 网格完整性检查（断言打印辅助）
        bool CheckGridIntegrity(Creature* c, bool moved) const;

        // 获取实例ID
        uint32 GetInstanceId() const { return i_InstanceId; }
        // 获取生成模式
        uint8 GetSpawnMode() const { return (i_spawnMode); }

        // 弱引用管理
        Trinity::unique_weak_ptr<Map> GetWeakPtr() const { return m_weakRef; }
        void SetWeakPtr(Trinity::unique_weak_ptr<Map> weakRef) { m_weakRef = std::move(weakRef); }

        // 进入状态枚举
        enum EnterState
        {
            CAN_ENTER = 0,                                      // 可以进入
            CANNOT_ENTER_ALREADY_IN_MAP = 1,                    // 玩家已在地图中
            CANNOT_ENTER_NO_ENTRY,                              // 未找到目标地图ID的地图条目
            CANNOT_ENTER_UNINSTANCED_DUNGEON,                   // 未找到地下城地图的实例模板
            CANNOT_ENTER_DIFFICULTY_UNAVAILABLE,                // 请求的实例难度对目标地图不可用
            CANNOT_ENTER_NOT_IN_RAID,                           // 目标实例是团队实例且玩家不在团队中
            CANNOT_ENTER_CORPSE_IN_DIFFERENT_INSTANCE,          // 玩家已死亡且尸体不在目标实例中
            CANNOT_ENTER_INSTANCE_BIND_MISMATCH,                // 玩家的永久实例存档与其队伍当前实例绑定不兼容
            CANNOT_ENTER_TOO_MANY_INSTANCES,                    // 玩家最近进入了太多实例
            CANNOT_ENTER_MAX_PLAYERS,                           // 目标地图已达到最大玩家数
            CANNOT_ENTER_ZONE_IN_COMBAT,                        // 目标地图上正在进行首领战斗
            CANNOT_ENTER_UNSPECIFIED_REASON                     // 未指定原因
        };
        // 检查玩家是否可以进入
        virtual EnterState CannotEnter(Player* /*player*/) { return CAN_ENTER; }
        // 获取地图名称
        char const* GetMapName() const;

        // 仅对实例地图有效（已设置实际难度的）
        Difficulty GetDifficulty() const { return Difficulty(GetSpawnMode()); }
        bool IsRegularDifficulty() const;
        MapDifficulty const* GetMapDifficulty() const;

        // 地图类型判断
        bool Instanceable() const;                            // 是否可实例化
        bool IsDungeon() const;                               // 是否为地下城
        bool IsNonRaidDungeon() const;                        // 是否为非团队地下城
        bool IsRaid() const;                                  // 是否为团队副本
        bool IsRaidOrHeroicDungeon() const;                   // 是否为团队或英雄地下城
        bool IsHeroic() const;                                // 是否为英雄难度
        bool Is25ManRaid() const;                             // 是否为25人团队
        bool IsBattleground() const;                          // 是否为战场
        bool IsBattleArena() const;                           // 是否为竞技场
        bool IsBattlegroundOrArena() const;                   // 是否为战场或竞技场
        bool GetEntrancePos(int32& mapid, float& x, float& y) const;  // 获取入口位置

        // 将对象添加到移除列表
        void AddObjectToRemoveList(WorldObject* obj);
        // 将对象添加到切换列表
        void AddObjectToSwitchList(WorldObject* obj, bool on);
        // 延迟更新
        virtual void DelayedUpdate(uint32 diff);

        // 标记单元格操作
        void resetMarkedCells() { marked_cells.reset(); }    // 重置标记的单元格
        bool isCellMarked(uint32 pCellId) { return marked_cells.test(pCellId); }  // 检查单元格是否被标记
        void markCell(uint32 pCellId) { marked_cells.set(pCellId); }  // 标记单元格

        // 玩家相关
        bool HavePlayers() const { return !m_mapRefManager.isEmpty(); }  // 是否有玩家
        uint32 GetPlayersCountExceptGMs() const;               // 获取非GM玩家数量
        bool ActiveObjectsNearGrid(NGridType const& ngrid) const;  // 网格附近是否有活跃对象

        // 世界对象管理
        void AddWorldObject(WorldObject* obj) { i_worldObjects.insert(obj); }  // 添加世界对象
        void RemoveWorldObject(WorldObject* obj) { i_worldObjects.erase(obj); }  // 移除世界对象

        // 发送数据包
        void SendToPlayers(WorldPacket const* data) const;    // 发送给所有玩家
        bool SendZoneMessage(uint32 zone, WorldPacket const* packet, WorldSession const* self = nullptr, uint32 team = 0) const;  // 发送区域消息

        typedef MapRefManager PlayerList;
        PlayerList const& GetPlayers() const { return m_mapRefManager; }  // 获取玩家列表

        // 每地图脚本存储
        void ScriptsStart(std::map<uint32, std::multimap<uint32, ScriptInfo>> const& scripts, uint32 id, Object* source, Object* target);
        void ScriptCommandStart(ScriptInfo const& script, uint32 delay, Object* source, Object* target);

        // 添加到活跃对象（必须与AddToWorld一起调用）
        void AddToActive(WorldObject* obj);

        // 从活跃对象移除（必须与RemoveFromWorld一起调用）
        void RemoveFromActive(WorldObject* obj);

        // 切换网格容器
        template<class T> void SwitchGridContainers(T* obj, bool on);
        std::unordered_map<ObjectGuid::LowType /*leaderSpawnId*/, CreatureGroup*> CreatureGroupHolder;  // 生物组容器

        // 更新迭代器回退
        void UpdateIteratorBack(Player* player);

        // 召唤生物
        TempSummon* SummonCreature(uint32 entry, Position const& pos, SummonPropertiesEntry const* properties = nullptr, uint32 duration = 0, WorldObject* summoner = nullptr, uint32 spellId = 0, uint32 vehId = 0, bool visibleOnlyBySummoner = false);
        // 召唤生物组
        void SummonCreatureGroup(uint8 group, std::list<TempSummon*>* list = nullptr);

        // 通过GUID获取各种对象
        Player* GetPlayer(ObjectGuid const& guid);
        Corpse* GetCorpse(ObjectGuid const& guid);
        Creature* GetCreature(ObjectGuid const& guid);
        GameObject* GetGameObject(ObjectGuid const& guid);
        Creature* GetCreatureBySpawnId(ObjectGuid::LowType spawnId) const;
        GameObject* GetGameObjectBySpawnId(ObjectGuid::LowType spawnId) const;
        // 通过生成ID获取世界对象
        WorldObject* GetWorldObjectBySpawnId(SpawnObjectType type, ObjectGuid::LowType spawnId) const
        {
            switch (type)
            {
                case SPAWN_TYPE_CREATURE:
                    return reinterpret_cast<WorldObject*>(GetCreatureBySpawnId(spawnId));
                case SPAWN_TYPE_GAMEOBJECT:
                    return reinterpret_cast<WorldObject*>(GetGameObjectBySpawnId(spawnId));
                default:
                    return nullptr;
            }
        }
        Transport* GetTransport(ObjectGuid const& guid);
        DynamicObject* GetDynamicObject(ObjectGuid const& guid);
        Pet* GetPet(ObjectGuid const& guid);

        // 获取对象存储容器
        MapStoredObjectTypesContainer& GetObjectsStore() { return _objectsStore; }

        typedef std::unordered_multimap<ObjectGuid::LowType, Creature*> CreatureBySpawnIdContainer;
        CreatureBySpawnIdContainer& GetCreatureBySpawnIdStore() { return _creatureBySpawnIdStore; }
        CreatureBySpawnIdContainer const& GetCreatureBySpawnIdStore() const { return _creatureBySpawnIdStore; }

        typedef std::unordered_multimap<ObjectGuid::LowType, GameObject*> GameObjectBySpawnIdContainer;
        GameObjectBySpawnIdContainer& GetGameObjectBySpawnIdStore() { return _gameobjectBySpawnIdStore; }
        GameObjectBySpawnIdContainer const& GetGameObjectBySpawnIdStore() const { return _gameobjectBySpawnIdStore; }

        // 获取单元格中的尸体
        std::unordered_set<Corpse*> const* GetCorpsesInCell(uint32 cellId) const
        {
            auto itr = _corpsesByCell.find(cellId);
            if (itr != _corpsesByCell.end())
                return &itr->second;

            return nullptr;
        }

        // 通过玩家GUID获取尸体
        Corpse* GetCorpseByPlayer(ObjectGuid const& ownerGuid) const
        {
            auto itr = _corpsesByPlayer.find(ownerGuid);
            if (itr != _corpsesByPlayer.end())
                return itr->second;

            return nullptr;
        }

        // 地图类型转换
        MapInstanced* ToMapInstanced() { if (Instanceable()) return reinterpret_cast<MapInstanced*>(this); return nullptr; }
        MapInstanced const* ToMapInstanced() const { if (Instanceable()) return reinterpret_cast<MapInstanced const*>(this); return nullptr; }

        InstanceMap* ToInstanceMap() { if (IsDungeon()) return reinterpret_cast<InstanceMap*>(this); else return nullptr;  }
        InstanceMap const* ToInstanceMap() const { if (IsDungeon()) return reinterpret_cast<InstanceMap const*>(this); return nullptr; }

        BattlegroundMap* ToBattlegroundMap() { if (IsBattlegroundOrArena()) return reinterpret_cast<BattlegroundMap*>(this); else return nullptr;  }
        BattlegroundMap const* ToBattlegroundMap() const { if (IsBattlegroundOrArena()) return reinterpret_cast<BattlegroundMap const*>(this); return nullptr; }

        // 获取水面或地面高度
        float GetWaterOrGroundLevel(uint32 phasemask, float x, float y, float z, float* ground = nullptr, bool swim = false, float collisionHeight = 2.03128f) const;
        // 获取最小高度
        float GetMinHeight(float x, float y) const;
        // 获取高度
        float GetHeight(float x, float y, float z, bool checkVMap = true, float maxSearchDist = DEFAULT_HEIGHT_SEARCH) const;
        // 获取网格高度
        float GetGridHeight(float x, float y) const;
        float GetHeight(Position const& pos, bool vmap = true, float maxSearchDist = DEFAULT_HEIGHT_SEARCH) const { return GetHeight(pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), vmap, maxSearchDist); }
        float GetHeight(uint32 phasemask, float x, float y, float z, bool vmap = true, float maxSearchDist = DEFAULT_HEIGHT_SEARCH) const { return std::max<float>(GetHeight(x, y, z, vmap, maxSearchDist), GetGameObjectFloor(phasemask, x, y, z, maxSearchDist)); }
        float GetHeight(uint32 phasemask, Position const& pos, bool vmap = true, float maxSearchDist = DEFAULT_HEIGHT_SEARCH) const { return GetHeight(phasemask, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), vmap, maxSearchDist); }

        // 视线检测
        bool isInLineOfSight(float x1, float y1, float z1, float x2, float y2, float z2, uint32 phasemask, LineOfSightChecks checks, VMAP::ModelIgnoreFlags ignoreFlags) const;
        // 动态树操作
        void Balance() { _dynamicTree.balance(); }            // 平衡动态树
        void RemoveGameObjectModel(GameObjectModel const& model) { _dynamicTree.remove(model); }  // 移除游戏对象模型
        void InsertGameObjectModel(GameObjectModel const& model) { _dynamicTree.insert(model); }  // 插入游戏对象模型
        bool ContainsGameObjectModel(GameObjectModel const& model) const { return _dynamicTree.contains(model);}  // 检查是否包含游戏对象模型
        // 获取游戏对象地面高度
        float GetGameObjectFloor(uint32 phasemask, float x, float y, float z, float maxSearchDist = DEFAULT_HEIGHT_SEARCH) const
        {
            return _dynamicTree.getHeight(x, y, z, maxSearchDist, phasemask);
        }
        // 获取对象命中位置
        bool getObjectHitPos(uint32 phasemask, float x1, float y1, float z1, float x2, float y2, float z2, float& rx, float &ry, float& rz, float modifyDist);

        /*
            重生时间管理
        */
        // 获取链接重生时间
        time_t GetLinkedRespawnTime(ObjectGuid guid) const;
        // 获取重生时间
        time_t GetRespawnTime(SpawnObjectType type, ObjectGuid::LowType spawnId) const
        {
            auto const& map = GetRespawnMapForType(type);
            auto it = map.find(spawnId);
            return (it == map.end()) ? 0 : it->second->respawnTime;
        }
        time_t GetCreatureRespawnTime(ObjectGuid::LowType spawnId) const { return GetRespawnTime(SPAWN_TYPE_CREATURE, spawnId); }
        time_t GetGORespawnTime(ObjectGuid::LowType spawnId) const { return GetRespawnTime(SPAWN_TYPE_GAMEOBJECT, spawnId); }

        // 更新玩家区域统计
        void UpdatePlayerZoneStats(uint32 oldZone, uint32 newZone);

        // 保存重生时间
        void SaveRespawnTime(SpawnObjectType type, ObjectGuid::LowType spawnId, uint32 entry, time_t respawnTime, uint32 gridId, CharacterDatabaseTransaction dbTrans = nullptr, bool startup = false);
        void SaveRespawnInfoDB(RespawnInfo const& info, CharacterDatabaseTransaction dbTrans = nullptr);
        void LoadRespawnTimes();
        void DeleteRespawnTimes() { UnloadAllRespawnInfos(); DeleteRespawnTimesInDB(GetId(), GetInstanceId()); }
        static void DeleteRespawnTimesInDB(uint16 mapId, uint32 instanceId);

        // 尸体数据管理
        void LoadCorpseData();
        void DeleteCorpseData();
        void AddCorpse(Corpse* corpse);
        void RemoveCorpse(Corpse* corpse);
        Corpse* ConvertCorpseToBones(ObjectGuid const& ownerGuid, bool insignia = false);
        void RemoveOldCorpses();

        // 传输工具相关
        void SendInitTransports(Player* player);
        void SendRemoveTransports(Player* player);
        // 区域动态信息
        void SendZoneDynamicInfo(uint32 zoneId, Player* player) const;
        void SendZoneWeather(uint32 zoneId, Player* player) const;
        void SendZoneWeather(ZoneDynamicInfo const& zoneDynamicInfo, Player* player) const;
        void SendZoneText(uint32 zoneId, const char* text, WorldSession const* self = nullptr, uint32 team = 0) const;

        // 区域效果设置
        void SetZoneMusic(uint32 zoneId, uint32 musicId);          // 设置区域音乐
        Weather* GetOrGenerateZoneDefaultWeather(uint32 zoneId);    // 获取或生成区域默认天气
        void SetZoneWeather(uint32 zoneId, WeatherState weatherId, float intensity);  // 设置区域天气
        void SetZoneOverrideLight(uint32 zoneId, uint32 areaLightId, uint32 overrideLightId, Milliseconds transitionTime);  // 设置区域覆盖光照

        // 更新区域依赖光环
        void UpdateAreaDependentAuras();

        // 生成GUID
        template<HighGuid high>
        inline ObjectGuid::LowType GenerateLowGuid()
        {
            static_assert(ObjectGuidTraits<high>::MapSpecific, "Only map specific guid can be generated in Map context");
            return GetGuidSequenceGenerator(high).Generate();
        }

        // 获取最大GUID
        template<HighGuid high>
        inline ObjectGuid::LowType GetMaxLowGuid()
        {
            static_assert(ObjectGuidTraits<high>::MapSpecific, "Only map specific guid can be retrieved in Map context");
            return GetGuidSequenceGenerator(high).GetNextAfterMaxUsed();
        }

        // 更新对象管理
        void AddUpdateObject(Object* obj)
        {
            _updateObjects.insert(obj);
        }

        void RemoveUpdateObject(Object* obj)
        {
            _updateObjects.erase(obj);
        }

        // 获取活跃非玩家对象数量
        size_t GetActiveNonPlayersCount() const
        {
            return m_activeNonPlayers.size();
        }

        // 获取调试信息
        virtual std::string GetDebugInfo() const;

    private:
        // 加载地图和虚拟地图
        void LoadMapAndVMap(int gx, int gy);
        // 加载虚拟地图
        void LoadVMap(int gx, int gy);
        // 加载地图
        void LoadMap(int gx, int gy, bool reload = false);
        // 加载移动地图（MMap）
        void LoadMMap(int gx, int gy);
        // 获取网格
        GridMap* GetGrid(float x, float y);

        // 设置计时器
        void SetTimer(uint32 t) { i_gridExpiry = t < MIN_GRID_DELAY ? MIN_GRID_DELAY : t; }

        // 发送初始化给自己
        void SendInitSelf(Player* player);

        // 生物单元格重定位
        bool CreatureCellRelocation(Creature* creature, Cell new_cell);
        // 游戏对象单元格重定位
        bool GameObjectCellRelocation(GameObject* go, Cell new_cell);
        // 动态对象单元格重定位
        bool DynamicObjectCellRelocation(DynamicObject* go, Cell new_cell);

        // 初始化对象（模板函数）
        template<class T> void InitializeObject(T* obj);
        // 添加生物到移动列表
        void AddCreatureToMoveList(Creature* c, float x, float y, float z, float ang);
        // 从移动列表移除生物
        void RemoveCreatureFromMoveList(Creature* c);
        // 添加游戏对象到移动列表
        void AddGameObjectToMoveList(GameObject* go, float x, float y, float z, float ang);
        // 从移动列表移除游戏对象
        void RemoveGameObjectFromMoveList(GameObject* go);
        // 添加动态对象到移动列表
        void AddDynamicObjectToMoveList(DynamicObject* go, float x, float y, float z, float ang);
        // 从移动列表移除动态对象
        void RemoveDynamicObjectFromMoveList(DynamicObject* go);

        bool _creatureToMoveLock;                               // 生物移动锁
        std::vector<Creature*> _creaturesToMove;                // 待移动的生物列表

        bool _gameObjectsToMoveLock;                            // 游戏对象移动锁
        std::vector<GameObject*> _gameObjectsToMove;            // 待移动的游戏对象列表

        bool _dynamicObjectsToMoveLock;                         // 动态对象移动锁
        std::vector<DynamicObject*> _dynamicObjectsToMove;      // 待移动的动态对象列表

        // 检查网格是否已加载
        bool IsGridLoaded(GridCoord const&) const;
        // 确保网格已创建
        void EnsureGridCreated(GridCoord const&);
        void EnsureGridCreated_i(GridCoord const&);
        // 确保网格已加载
        bool EnsureGridLoaded(Cell const&);
        // 为活跃对象确保网格已加载
        void EnsureGridLoadedForActiveObject(Cell const&, WorldObject* object);

        // 构建NGrid链接
        void buildNGridLinkage(NGridType* pNGridType) { pNGridType->link(this); }

        // 获取NGrid
        NGridType* getNGrid(uint32 x, uint32 y) const
        {
            ASSERT(x < MAX_NUMBER_OF_GRIDS && y < MAX_NUMBER_OF_GRIDS, "x = %u, y = %u", x, y);
            return i_grids[x][y];
        }

        // 设置NGrid
        void setNGrid(NGridType* grid, uint32 x, uint32 y);
        // 处理脚本
        void ScriptsProcess();

        // 发送对象更新
        void SendObjectUpdates();

    protected:
        // 设置卸载引用锁
        void SetUnloadReferenceLock(GridCoord const& p, bool on) { getNGrid(p.x_coord, p.y_coord)->setUnloadReferenceLock(on); }

        std::mutex _mapLock;                                   // 地图锁
        std::mutex _gridLock;                                  // 网格锁

        // ============================================================================
        // Map 类保护成员变量
        // ============================================================================
        MapEntry const* i_mapEntry;                            // 地图条目
        uint8 i_spawnMode;                                     // 生成模式（难度）
        uint32 i_InstanceId;                                   // 实例ID（0表示非实例）
        Trinity::unique_weak_ptr<Map> m_weakRef;               // 弱引用
        uint32 m_unloadTimer;                                  // 卸载计时器
        float m_VisibleDistance;                               // 可见距离
        DynamicMapTree _dynamicTree;                           // 动态地图树

        MapRefManager m_mapRefManager;                         // 地图上的玩家引用管理器
        MapRefManager::iterator m_mapRefIter;                  // 地图引用迭代器

        int32 m_VisibilityNotifyPeriod;                        // 可见性通知周期

        typedef std::set<WorldObject*> ActiveNonPlayers;
        ActiveNonPlayers m_activeNonPlayers;                   // 活跃的非玩家对象
        ActiveNonPlayers::iterator m_activeNonPlayersIter;     // 活跃非玩家迭代器

        // 即使在非活跃网格中也必须更新的对象（不激活它们）
        typedef std::set<Transport*> TransportsContainer;
        TransportsContainer _transports;                       // 传输工具容器（船只、飞艇等）
        TransportsContainer::iterator _transportsUpdateIter;   // 传输工具更新迭代器

    private:
        // 脚本辅助函数
        Player* _GetScriptPlayerSourceOrTarget(Object* source, Object* target, ScriptInfo const* scriptInfo) const;
        Creature* _GetScriptCreatureSourceOrTarget(Object* source, Object* target, ScriptInfo const* scriptInfo, bool bReverse = false) const;
        GameObject* _GetScriptGameObjectSourceOrTarget(Object* source, Object* target, ScriptInfo const* scriptInfo, bool bReverse = false) const;
        Unit* _GetScriptUnit(Object* obj, bool isSource, ScriptInfo const* scriptInfo) const;
        Player* _GetScriptPlayer(Object* obj, bool isSource, ScriptInfo const* scriptInfo) const;
        Creature* _GetScriptCreature(Object* obj, bool isSource, ScriptInfo const* scriptInfo) const;
        WorldObject* _GetScriptWorldObject(Object* obj, bool isSource, ScriptInfo const* scriptInfo) const;
        void _ScriptProcessDoor(Object* source, Object* target, ScriptInfo const* scriptInfo) const;
        GameObject* _FindGameObject(WorldObject* pWorldObject, ObjectGuid::LowType guid) const;

        // ============================================================================
        // Map 类私有成员变量
        // ============================================================================
        time_t i_gridExpiry;                                   // 网格过期时间

        // 用于快速查找基础地图（如MapInstanced类对象）
        // 用于InstanceMaps和BattlegroundMaps...
        Map* m_parentMap;                                      // 父地图指针

        // === 网格系统 ===
        NGridType* i_grids[MAX_NUMBER_OF_GRIDS][MAX_NUMBER_OF_GRIDS];  // 网格数组（64x64）
        GridMap* GridMaps[MAX_NUMBER_OF_GRIDS][MAX_NUMBER_OF_GRIDS];   // 网格地图数组
        std::bitset<TOTAL_NUMBER_OF_CELLS_PER_MAP*TOTAL_NUMBER_OF_CELLS_PER_MAP> marked_cells;  // 标记的单元格

        // 这些函数用于处理玩家/怪物仇恨反应和可见性计算
        // 为大规模计算高度优化
        void ProcessRelocationNotifies(const uint32 diff);

        bool i_scriptLock;                                     // 脚本锁
        std::set<WorldObject*> i_objectsToRemove;              // 待删除对象集合
        std::map<WorldObject*, bool> i_objectsToSwitch;        // 待切换对象映射
        std::set<WorldObject*> i_worldObjects;                 // 世界对象集合

        typedef std::multimap<time_t, ScriptAction> ScriptScheduleMap;
        ScriptScheduleMap m_scriptSchedule;                    // 脚本调度映射

    public:
        // 处理重生
        void ProcessRespawns();
        // 应用动态模式重生缩放
        void ApplyDynamicModeRespawnScaling(WorldObject const* obj, ObjectGuid::LowType spawnId, uint32& respawnDelay, uint32 mode) const;

    private:
        // 检查重生（返回true表示可以重生，false表示需要重新调度或删除）
        bool CheckRespawn(RespawnInfo* info);
        // 执行重生
        void DoRespawn(SpawnObjectType type, ObjectGuid::LowType spawnId, uint32 gridId);
        // 添加重生信息
        bool AddRespawnInfo(RespawnInfo const& info);
        // 卸载所有重生信息
        void UnloadAllRespawnInfos();
        // 获取重生信息
        RespawnInfo* GetRespawnInfo(SpawnObjectType type, ObjectGuid::LowType spawnId) const;
        // 重生
        void Respawn(RespawnInfo* info, CharacterDatabaseTransaction dbTrans = nullptr);
        // 删除重生信息
        void DeleteRespawnInfo(RespawnInfo* info, CharacterDatabaseTransaction dbTrans = nullptr);
        // 从数据库删除重生信息
        void DeleteRespawnInfoFromDB(SpawnObjectType type, ObjectGuid::LowType spawnId, CharacterDatabaseTransaction dbTrans = nullptr);

    public:
        // 获取重生信息
        void GetRespawnInfo(std::vector<RespawnInfo const*>& respawnData, SpawnObjectTypeMask types) const;
        // 重生
        void Respawn(SpawnObjectType type, ObjectGuid::LowType spawnId, CharacterDatabaseTransaction dbTrans = nullptr)
        {
            if (RespawnInfo* info = GetRespawnInfo(type, spawnId))
                Respawn(info, dbTrans);
        }
        // 移除重生时间
        void RemoveRespawnTime(SpawnObjectType type, ObjectGuid::LowType spawnId, CharacterDatabaseTransaction dbTrans = nullptr, bool alwaysDeleteFromDB = false)
        {
            if (RespawnInfo* info = GetRespawnInfo(type, spawnId))
                DeleteRespawnInfo(info, dbTrans);
            // 某些调用者可能需要确保数据库不包含任何重生时间
            else if (alwaysDeleteFromDB)
                DeleteRespawnInfoFromDB(type, spawnId, dbTrans);
        }
        // 全部消失
        size_t DespawnAll(SpawnObjectType type, ObjectGuid::LowType spawnId);

        // 检查是否应在网格加载时生成
        bool ShouldBeSpawnedOnGridLoad(SpawnObjectType type, ObjectGuid::LowType spawnId) const;
        template <typename T> bool ShouldBeSpawnedOnGridLoad(ObjectGuid::LowType spawnId) const { return ShouldBeSpawnedOnGridLoad(SpawnData::TypeFor<T>, spawnId); }

        // 获取生成组数据
        SpawnGroupTemplateData const* GetSpawnGroupData(uint32 groupId) const;

        // 检查生成组是否活跃
        bool IsSpawnGroupActive(uint32 groupId) const;

        // 启用生成组（使其中的所有生物重生，除非它们有重生计时器）
        // force标志可用于强制生成额外副本，即使旧副本仍然存在
        bool SpawnGroupSpawn(uint32 groupId, bool ignoreRespawn = false, bool force = false, std::vector<WorldObject*>* spawnedObjects = nullptr);

        // 消失生成组中的所有生物（如果已生成），可选择删除其重生计时器，并禁用组
        bool SpawnGroupDespawn(uint32 groupId, bool deleteRespawnTimes = false, size_t* count = nullptr);

        // 禁用生成组（阻止组中任何生物重生直到重新启用）
        // 这不会影响组中已存在的生物
        void SetSpawnGroupInactive(uint32 groupId) { SetSpawnGroupActive(groupId, false); }

        typedef std::function<void(Map*)> FarSpellCallback;
        void AddFarSpellCallback(FarSpellCallback&& callback);

    private:
        // 特定类型的网格添加/移除代码
        template<class T>
        void AddToGrid(T* object, Cell const& cell);

        template<class T>
        void DeleteFromWorld(T*);

        // 添加到活跃对象辅助函数
        void AddToActiveHelper(WorldObject* obj)
        {
            m_activeNonPlayers.insert(obj);
        }

        // 从活跃对象移除辅助函数
        void RemoveFromActiveHelper(WorldObject* obj)
        {
            // Map::Update正在处理活跃对象
            if (m_activeNonPlayersIter != m_activeNonPlayers.end())
            {
                ActiveNonPlayers::iterator itr = m_activeNonPlayers.find(obj);
                if (itr == m_activeNonPlayers.end())
                    return;
                if (itr == m_activeNonPlayersIter)
                    ++m_activeNonPlayersIter;
                m_activeNonPlayers.erase(itr);
            }
            else
                m_activeNonPlayers.erase(obj);
        }

        // === 重生系统 ===
        std::unique_ptr<RespawnListContainer> _respawnTimes;    // 重生时间列表
        RespawnInfoMap       _creatureRespawnTimesBySpawnId;    // 按生成ID索引的生物重生时间
        RespawnInfoMap       _gameObjectRespawnTimesBySpawnId;  // 按生成ID索引的游戏对象重生时间
        RespawnInfoMap& GetRespawnMapForType(SpawnObjectType type)
        {
            switch (type)
            {
                default:
                    ABORT();
                case SPAWN_TYPE_CREATURE:
                    return _creatureRespawnTimesBySpawnId;
                case SPAWN_TYPE_GAMEOBJECT:
                    return _gameObjectRespawnTimesBySpawnId;
            }
        }
        RespawnInfoMap const& GetRespawnMapForType(SpawnObjectType type) const
        {
            switch (type)
            {
                default:
                    ABORT();
                case SPAWN_TYPE_CREATURE:
                    return _creatureRespawnTimesBySpawnId;
                case SPAWN_TYPE_GAMEOBJECT:
                    return _gameObjectRespawnTimesBySpawnId;
            }
        }

        // 设置生成组活跃状态
        void SetSpawnGroupActive(uint32 groupId, bool state);
        std::unordered_set<uint32> _toggledSpawnGroupIds;       // 已切换的生成组ID

        uint32 _respawnCheckTimer;                              // 重生检查计时器
        std::unordered_map<uint32, uint32> _zonePlayerCountMap; // 区域玩家计数映射

        // === 天气系统 ===
        ZoneDynamicInfoMap _zoneDynamicInfo;                    // 区域动态信息（天气等）
        IntervalTimer _weatherUpdateTimer;                      // 天气更新计时器

        // GUID生成器
        ObjectGuidGenerator& GetGuidSequenceGenerator(HighGuid high);

        // === 对象存储 ===
        std::map<HighGuid, std::unique_ptr<ObjectGuidGenerator>> _guidGenerators;  // GUID生成器映射
        MapStoredObjectTypesContainer _objectsStore;            // 对象存储容器
        CreatureBySpawnIdContainer _creatureBySpawnIdStore;     // 按生成ID索引的生物存储
        GameObjectBySpawnIdContainer _gameobjectBySpawnIdStore; // 按生成ID索引的游戏对象存储

        // 尸体存储
        std::unordered_map<uint32/*cellId*/, std::unordered_set<Corpse*>> _corpsesByCell;  // 按单元格索引的尸体
        std::unordered_map<ObjectGuid, Corpse*> _corpsesByPlayer; // 按玩家索引的尸体
        std::unordered_set<Corpse*> _corpseBones;               // 骨骼尸体

        std::unordered_set<Object*> _updateObjects;             // 待更新对象集合

        MPSCQueue<FarSpellCallback> _farSpellCallbacks;         // 远程法术回调队列
};

// ============================================================================
// 实例重置方法枚举
// ============================================================================
enum InstanceResetMethod
{
    INSTANCE_RESET_ALL,                                      // 重置所有
    INSTANCE_RESET_CHANGE_DIFFICULTY,                        // 更改难度
    INSTANCE_RESET_GLOBAL,                                   // 全局重置
    INSTANCE_RESET_GROUP_DISBAND,                            // 队伍解散
    INSTANCE_RESET_GROUP_JOIN,                               // 加入队伍
    INSTANCE_RESET_RESPAWN_DELAY                             // 重生延迟
};

// ============================================================================
// InstanceMap 类 - 实例地图类（地下城、团队副本）
// ============================================================================
class TC_GAME_API InstanceMap : public Map
{
    public:
        InstanceMap(uint32 id, time_t, uint32 InstanceId, uint8 SpawnMode, Map* _parent, TeamId InstanceTeam);
        ~InstanceMap();

        // 将玩家添加到地图
        bool AddPlayerToMap(Player*) override;
        // 从地图移除玩家
        void RemovePlayerFromMap(Player*, bool) override;
        // 更新实例
        void Update(uint32) override;
        // 创建实例数据
        void CreateInstanceData(bool load);
        // 重置实例
        bool Reset(uint8 method);
        // 获取脚本ID
        uint32 GetScriptId() const { return i_script_id; }
        // 获取脚本名称
        std::string const& GetScriptName() const;
        // 获取实例脚本
        InstanceScript* GetInstanceScript() { return i_data; }
        InstanceScript const* GetInstanceScript() const { return i_data; }
        // 永久绑定所有玩家
        void PermBindAllPlayers();
        // 卸载所有
        void UnloadAll() override;
        // 检查玩家是否可以进入
        EnterState CannotEnter(Player* player) override;
        // 发送重置警告
        void SendResetWarnings(uint32 timeLeft) const;
        // 设置重置计划
        void SetResetSchedule(bool on);

        // 检查是否有玩家永久绑定到此实例ID（包括可重新激活的过期绑定）
        // 需要数据库查询，请谨慎使用
        bool HasPermBoundPlayers() const;
        // 获取最大玩家数
        uint32 GetMaxPlayers() const;
        // 获取最大重置延迟
        uint32 GetMaxResetDelay() const;
        // 获取实例中的队伍ID
        TeamId GetTeamIdInInstance() const { return i_script_team; }
        Team GetTeamInInstance() const { return i_script_team == TEAM_ALLIANCE ? ALLIANCE : HORDE; }

        // 初始化可见距离
        virtual void InitVisibilityDistance() override;

        // 获取调试信息
        std::string GetDebugInfo() const override;
    private:
        bool m_resetAfterUnload;                               // 卸载后重置
        bool m_unloadWhenEmpty;                                // 为空时卸载
        InstanceScript* i_data;                                // 实例脚本数据
        uint32 i_script_id;                                    // 脚本ID
        TeamId i_script_team;                                  // 脚本队伍
};

// ============================================================================
// BattlegroundMap 类 - 战场地图类（战场、竞技场）
// ============================================================================
class TC_GAME_API BattlegroundMap : public Map
{
    public:
        BattlegroundMap(uint32 id, time_t, uint32 InstanceId, Map* _parent, uint8 spawnMode);
        ~BattlegroundMap();

        // 将玩家添加到地图
        bool AddPlayerToMap(Player*) override;
        // 从地图移除玩家
        void RemovePlayerFromMap(Player*, bool) override;
        // 检查玩家是否可以进入
        EnterState CannotEnter(Player* player) override;
        // 设置卸载
        void SetUnload();
        // 移除所有玩家
        void RemoveAllPlayers() override;

        // 初始化可见距离
        virtual void InitVisibilityDistance() override;
        // 获取战场对象
        Battleground* GetBG() { return m_bg; }
        void SetBG(Battleground* bg) { m_bg = bg; }
    private:
        Battleground* m_bg;                                    // 战场对象指针
};

// Visit模板函数实现 - 访问网格单元
template<class T, class CONTAINER>
inline void Map::Visit(Cell const& cell, TypeContainerVisitor<T, CONTAINER>& visitor)
{
    const uint32 x = cell.GridX();
    const uint32 y = cell.GridY();
    const uint32 cell_x = cell.CellX();
    const uint32 cell_y = cell.CellY();

    if (!cell.NoCreate())
        EnsureGridLoaded(cell);

    NGridType* grid = getNGrid(x, y);
    if (grid && grid->isGridObjectDataLoaded())
        grid->VisitGrid(cell_x, cell_y, visitor);
}
#endif
