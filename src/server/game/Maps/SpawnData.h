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

// ============================================================================
// 模块：SpawnData - 生成数据管理
// ============================================================================
// 职责：
//   定义生物和游戏对象的生成数据结构
//   管理生成对象的类型、位置、时间等元数据
//
// 核心概念：
//   - Spawn：在游戏世界中生成生物或游戏对象
//   - SpawnData：生成的静态配置数据（从数据库加载）
//   - SpawnMetadata：生成的元数据基类
//
// 使用场景：
//   - 从数据库加载生成点配置
//   - 地图初始化时创建生成对象
//   - 管理生成组和关联重生
//
// 数据流向：
//   数据库 -> SpawnData -> Map -> Creature/GameObject 实例
// ============================================================================

#ifndef TRINITY_SPAWNDATA_H
#define TRINITY_SPAWNDATA_H

#include "Position.h"

class Creature;
class GameObject;
class Pool;
struct PoolTemplate;

// ============================================================================
// SpawnObjectType - 生成对象类型枚举
// ============================================================================
// 职责：定义可生成的对象类型
// 用途：类型识别、数据路由、模板特化
// ============================================================================
enum SpawnObjectType
{
    SPAWN_TYPE_CREATURE = 0,        // 生物类型
    SPAWN_TYPE_GAMEOBJECT = 1,      // 游戏对象类型
    NUM_SPAWN_TYPES_WITH_DATA,      // 有数据类型的数量（用于数组大小）
    NUM_SPAWN_TYPES = NUM_SPAWN_TYPES_WITH_DATA // SKIP: 所有类型数量
};

// ============================================================================
// SpawnObjectTypeMask - 生成对象类型掩码
// ============================================================================
// 职责：用于位运算快速检查对象类型
// 用途：批量过滤、类型匹配
// 示例：if (typeMask & SPAWN_TYPEMASK_CREATURE) { /* 是生物 */ }
// ============================================================================
enum SpawnObjectTypeMask
{
    SPAWN_TYPEMASK_CREATURE = (1 << SPAWN_TYPE_CREATURE),      // 生物掩码
    SPAWN_TYPEMASK_GAMEOBJECT = (1 << SPAWN_TYPE_GAMEOBJECT),  // 游戏对象掩码

    // 所有有数据的类型掩码
    SPAWN_TYPEMASK_WITH_DATA = (1 << NUM_SPAWN_TYPES_WITH_DATA)-1,
    // 所有类型掩码
    SPAWN_TYPEMASK_ALL = (1 << NUM_SPAWN_TYPES)-1
};

// ============================================================================
// SpawnGroupFlags - 生成组标志
// ============================================================================
// 职责：定义生成组的特殊行为标志
// 用途：控制生成组的工作模式
// ============================================================================
enum SpawnGroupFlags
{
    SPAWNGROUP_FLAG_NONE                = 0x00,  // 无特殊标志
    SPAWNGROUP_FLAG_SYSTEM              = 0x01,  // 系统生成组（内部使用）
    SPAWNGROUP_FLAG_COMPATIBILITY_MODE  = 0x02,  // 兼容模式（旧版行为）
    SPAWNGROUP_FLAG_MANUAL_SPAWN        = 0x04,  // 手动生成（不自动刷新）
    SPAWNGROUP_FLAG_DYNAMIC_SPAWN_RATE  = 0x08,  // 动态生成速率（根据玩家数量调整）
    SPAWNGROUP_FLAG_ESCORTQUESTNPC      = 0x10,  // 护送任务 NPC

    // 所有有效标志的组合
    SPAWNGROUP_FLAGS_ALL = (SPAWNGROUP_FLAG_SYSTEM | SPAWNGROUP_FLAG_COMPATIBILITY_MODE |
                           SPAWNGROUP_FLAG_MANUAL_SPAWN | SPAWNGROUP_FLAG_DYNAMIC_SPAWN_RATE |
                           SPAWNGROUP_FLAG_ESCORTQUESTNPC)
};

// ============================================================================
// SpawnGroupTemplateData - 生成组模板数据
// ============================================================================
// 职责：存储生成组的配置信息
// 来源：从数据库 spawn_group_template 表加载
// 用途：管理一组相关联的生成点
// ============================================================================
struct SpawnGroupTemplateData
{
    uint32 groupId;                  // 生成组 ID（主键）
    std::string name;                // 生成组名称（调试用）
    uint32 mapId;                    // 所属地图 ID
    SpawnGroupFlags flags;           // 生成组标志
};

// ============================================================================
// SpawnObjectTypeForImpl - 类型到枚举的映射工具
// ============================================================================
// 职责：编译期将 C++ 类型映射到 SpawnObjectType 枚举
// 设计：使用模板特化实现类型萃取
// 用途：支持泛型编程，自动推导生成类型
// ============================================================================
namespace Trinity { namespace Impl {
    // 通用模板：未特化类型会触发静态断言
    template <typename T>
    struct SpawnObjectTypeForImpl {
        static_assert(!std::is_same<T,T>::value, "This type does not have an associated spawn type!");
    };

    // Creature 类型特化：映射到 SPAWN_TYPE_CREATURE
    template <> struct SpawnObjectTypeForImpl<Creature> {
        static constexpr SpawnObjectType value = SPAWN_TYPE_CREATURE;
    };

    // GameObject 类型特化：映射到 SPAWN_TYPE_GAMEOBJECT
    template <> struct SpawnObjectTypeForImpl<GameObject> {
        static constexpr SpawnObjectType value = SPAWN_TYPE_GAMEOBJECT;
    };
}}

// 前向声明
struct SpawnData;

// ============================================================================
// SpawnMetadata - 生成元数据基类
// ============================================================================
// 职责：存储所有生成对象的公共元数据
// 设计：轻量级基类，支持向下转型到 SpawnData
// 用途：在不知道具体类型时，统一处理生成对象元数据
// ============================================================================
struct SpawnMetadata
{
    // ========================================================================
    // 静态工具方法
    // ========================================================================

    // ========================================================================
    // 检查类型是否在掩码中
    // ========================================================================
    // @brief 使用位运算检查类型是否匹配掩码
    // @param type - 要检查的类型
    // @param mask - 类型掩码
    // @return true - 类型在掩码中，false - 不在
    // 用途：快速过滤生成对象类型
    // ========================================================================
    static constexpr bool TypeInMask(SpawnObjectType type, SpawnObjectTypeMask mask) {
        return ((1 << type) & mask);
    }

    // ========================================================================
    // 检查类型是否有数据
    // ========================================================================
    // @brief 检查类型是否有完整的 SpawnData
    // @param type - 要检查的类型
    // @return true - 有数据，false - 无数据
    // 用途：判断是否可以安全转换到 SpawnData
    // ========================================================================
    static constexpr bool TypeHasData(SpawnObjectType type) {
        return (type < NUM_SPAWN_TYPES_WITH_DATA);
    }

    // ========================================================================
    // 检查类型是否有效
    // ========================================================================
    // @brief 检查类型枚举值是否在有效范围内
    // @param type - 要检查的类型
    // @return true - 有效，false - 无效
    // 用途：数据验证
    // ========================================================================
    static constexpr bool TypeIsValid(SpawnObjectType type) {
        return (type < NUM_SPAWN_TYPES);
    }

    // ========================================================================
    // 类型到枚举的自动映射
    // ========================================================================
    // @brief 编译期自动推导类型的 SpawnObjectType
    // @tparam T - C++ 类型（Creature 或 GameObject）
    // @return 对应的 SpawnObjectType 枚举值
    // 示例：
    //   SpawnObjectType type = SpawnMetadata::TypeFor<Creature>;
    // ========================================================================
    template <typename T>
    static constexpr SpawnObjectType TypeFor = Trinity::Impl::SpawnObjectTypeForImpl<T>::value;

    // ========================================================================
    // 转换为 SpawnData
    // ========================================================================
    // @brief 安全地向下转型到 SpawnData
    // @param 无
    // @return SpawnData 指针，如果类型无数据则返回 nullptr
    // 注意：使用 reinterpret_cast，确保类型正确性
    // ========================================================================
    SpawnData const* ToSpawnData() const {
        return TypeHasData(type) ? reinterpret_cast<SpawnData const*>(this) : nullptr;
    }

    // ========================================================================
    // 成员变量
    // ========================================================================

    SpawnObjectType const type;                          // 生成对象类型（只读）
    uint32 spawnId = 0;                                  // 生成点 ID（数据库主键）
    uint32 mapId = MAPID_INVALID;                        // 所属地图 ID
    bool dbData = true;                                  // 是否来自数据库（false 为动态创建）
    SpawnGroupTemplateData const* spawnGroupData = nullptr; // 所属生成组数据（可能为空）

    protected:
        // ====================================================================
        // 构造函数（受保护）
        // ====================================================================
        // @brief 只能由派生类构造
        // @param t - 生成对象类型
        // ====================================================================
        SpawnMetadata(SpawnObjectType t) : type(t) {}
};

// ============================================================================
// SpawnData - 生成数据结构
// ============================================================================
// 职责：存储生成点的完整数据
// 继承：SpawnMetadata - 包含基础元数据
// 用途：
//   - 从数据库加载生成点配置
//   - 创建 Creature 或 GameObject 时提供初始数据
// 数据来源：
//   - creature 表（生物）
//   - gameobject 表（游戏对象）
// ============================================================================
struct SpawnData : public SpawnMetadata
{
    uint32 id = 0;                 // 模板 ID（creature_template 或 gameobject_template 的 entry）
    Position spawnPoint;           // 生成位置（坐标、朝向）
    uint32 phaseMask = 0;          // 相位掩码（控制可见性）
    int32 spawntimesecs = 0;       // 重生时间（秒）
    uint8 spawnMask = 0;           // 生成掩码（区分难度模式）
    uint32 scriptId = 0;           // 脚本 ID（用于自定义行为）

    protected:
        // ====================================================================
        // 构造函数（受保护）
        // ====================================================================
        // @brief 只能由具体的生成数据类构造
        // @param t - 生成对象类型
        // ====================================================================
        SpawnData(SpawnObjectType t) : SpawnMetadata(t) {}
};

// ============================================================================
// LinkedRespawnType - 关联重生类型
// ============================================================================
// 职责：定义对象之间的重生依赖关系
// 用途：
//   - 实现从属关系（如 BOSS 和小怪）
//   - 当主对象死亡时，从属对象自动消失
//   - 当主对象重生时，从属对象自动重生
// 示例：
//   - BOSS 房间的小怪依赖 BOSS 重生
//   - 游戏对象依赖附近的 NPC 重生
// ============================================================================
enum LinkedRespawnType
{
    LINKED_RESPAWN_CREATURE_TO_CREATURE  = 0,  // 生物依赖生物
    LINKED_RESPAWN_CREATURE_TO_GO        = 1,  // 生物依赖游戏对象
    LINKED_RESPAWN_GO_TO_GO              = 2,  // 游戏对象依赖游戏对象
    LINKED_RESPAWN_GO_TO_CREATURE        = 3,  // 游戏对象依赖生物
};

#endif
