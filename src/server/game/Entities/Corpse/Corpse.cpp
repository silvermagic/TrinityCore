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
 * @file Corpse.cpp
 * @brief 尸体系统实现文件
 *
 * 本文件实现了尸体（Corpse）相关的核心功能，包括：
 * - 尸体对象的创建、销毁和管理
 * - 尸体数据的数据库持久化（保存、加载、删除）
 * - 尸体在世界中的生命周期管理
 * - 尸体的过期检测和自动清理
 *
 * 尸体类型说明：
 * - CORPSE_BONES：骨骼类型，玩家释放灵魂后留下的骨骼，60分钟后自动消失
 * - CORPSE_RESURRECTABLE：可复活尸体，玩家死亡后未释放灵魂前的尸体，允许玩家跑尸体复活，3天后过期
 *
 * 相关数据库表：
 * - character_corpse：存储尸体详细信息
 *
 * @see Corpse.h
 * @see Player
 * @see WorldObject
 */

// 标准库和基础类型头文件
#include "Common.h"

// 游戏实体相关
#include "Corpse.h"
#include "Player.h"

// 数据存储和缓存
#include "CharacterCache.h"
#include "DBCStores.h"

// 游戏世界相关
#include "Map.h"
#include "World.h"
#include "ObjectAccessor.h"

// 网络和更新系统
#include "UpdateData.h"
#include "UpdateMask.h"

// 工具类
#include "GameTime.h"
#include "Log.h"
#include "DatabaseEnv.h"

/**
 * @brief 尸体构造函数
 *
 * 初始化尸体对象的基础属性，包括对象类型标识、更新标志和创建时间。
 *
 * @details
 * 构造过程：
 * 1. 调用父类 WorldObject 构造函数
 *    - 参数 type != CORPSE_BONES 决定是否创建客户端对象
 *    - 骨骼类型（CORPSE_BONES）不需要客户端对象，因为只是静态模型
 *    - 可复活尸体需要客户端对象以支持交互
 * 2. 设置对象类型掩码和类型 ID
 * 3. 设置更新标志（用于客户端同步）
 *    - UPDATEFLAG_LOWGUID：低 GUID 标志
 *    - UPDATEFLAG_STATIONARY_POSITION：静止位置标志
 *    - UPDATEFLAG_POSITION：位置更新标志
 * 4. 初始化创建时间为当前游戏时间
 * 5. 清空战利品接收者指针
 *
 * @param type 尸体类型
 *             - CORPSE_BONES：骨骼类型，玩家释放灵魂后留下
 *             - CORPSE_RESURRECTABLE：可复活尸体，玩家死亡后未释放灵魂
 *
 * @note 尸体对象的内存由对象管理系统管理
 * @see CorpseType
 * @see WorldObject
 */
Corpse::Corpse(CorpseType type) : WorldObject(type != CORPSE_BONES), m_type(type)
{
    // 设置对象类型标识
    m_objectType |= TYPEMASK_CORPSE;
    m_objectTypeId = TYPEID_CORPSE;

    // 设置更新标志，用于客户端对象更新同步
    m_updateFlag = (UPDATEFLAG_LOWGUID | UPDATEFLAG_STATIONARY_POSITION | UPDATEFLAG_POSITION);

    // 设置字段数量
    m_valuesCount = CORPSE_END;

    // 记录创建时间
    m_time = GameTime::GetGameTime();

    // 初始化战利品接收者为空
    lootRecipient = nullptr;
}

/**
 * @brief 尸体析构函数
 *
 * 清理尸体对象占用的资源。
 *
 * @details
 * 析构函数当前为空实现，因为：
 * 1. 尸体对象的所有资源都由父类和成员变量自动管理
 * 2. 没有动态分配的内存需要手动释放
 * 3. 如果尸体在世界中，RemoveFromWorld() 会在对象销毁前被调用
 *
 * @note 空析构函数保持为内联实现以提高性能
 */
Corpse::~Corpse() { }

/**
 * @brief 将尸体添加到游戏世界
 *
 * 将尸体对象注册到地图的对象存储系统中，使其可被其他实体查找和访问。
 *
 * @details
 * 执行流程：
 * 1. 检查尸体是否已在世界中（避免重复添加）
 * 2. 如果不在世界中，将尸体插入地图的对象存储（ObjectsStore）
 *    - 使用 GUID 作为键值进行快速查找
 *    - 支持通过 GUID 快速定位尸体对象
 * 3. 调用父类 Object::AddToWorld() 完成基础注册
 *
 * 对象存储的作用：
 * - 提供基于 GUID 的快速对象查找
 * - 支持地图范围内的对象遍历
 * - 用于对象生命周期管理
 *
 * @note 必须在尸体成功创建后调用此方法
 * @warning 如果尸体已在世界中，重复调用会导致插入失败但不影响功能
 * @see RemoveFromWorld
 * @see Object::AddToWorld
 * @see Map::GetObjectsStore
 */
void Corpse::AddToWorld()
{
    // 将尸体注册到地图的对象存储中，用于 GUID 查找
    if (!IsInWorld())
        GetMap()->GetObjectsStore().Insert<Corpse>(GetGUID(), this);

    // 调用父类方法完成世界注册
    Object::AddToWorld();
}

/**
 * @brief 将尸体从游戏世界中移除
 *
 * 从地图的对象存储系统中注销尸体对象，使其不再可被查找和访问。
 *
 * @details
 * 执行流程：
 * 1. 检查尸体是否在世界中（避免重复移除）
 * 2. 如果在世界中，从地图的对象存储中移除尸体
 *    - 使用 GUID 作为键值进行删除
 *    - 移除后尸体对象仍存在，但不可通过 GUID 查找
 * 3. 调用父类 WorldObject::RemoveFromWorld() 完成清理
 *
 * 调用时机：
 * - 尸体过期被清理时
 * - 玩家释放灵魂后，可复活尸体转为骨骼时
 * - 玩家复活时，清理尸体对象
 * - 服务器关闭或地图卸载时
 *
 * @warning 调用此方法后，尸体对象不应再被引用
 * @note 此方法不会删除尸体对象本身，只是将其从世界中注销
 * @see AddToWorld
 * @see WorldObject::RemoveFromWorld
 */
void Corpse::RemoveFromWorld()
{
    // 从地图的对象存储中移除尸体
    if (IsInWorld())
        GetMap()->GetObjectsStore().Remove<Corpse>(GetGUID());

    // 调用父类方法完成世界移除
    WorldObject::RemoveFromWorld();
}

/**
 * @brief 创建尸体对象（基础版本）
 *
 * 使用指定的低 GUID 创建尸体对象，仅初始化对象 GUID。
 *
 * @details
 * 此方法用于从数据库加载尸体时创建基础对象，不设置位置和所有者信息。
 * 执行流程：
 * 1. 调用 Object::_Create 设置对象 GUID
 *    - guidlow：尸体的低 GUID 值（数据库主键）
 *    - entry：0（尸体无 entry ID）
 *    - HighGuid::Corpse：高 GUID 类型标识
 *
 * @param guidlow 尸体的低 GUID 值，通常从数据库读取
 *
 * @return true 始终返回 true，表示创建成功
 *
 * @note 此方法通常与 LoadCorpseFromDB 配合使用
 * @see LoadCorpseFromDB
 * @see Object::_Create
 */
bool Corpse::Create(ObjectGuid::LowType guidlow)
{
    // 创建基础对象，设置 GUID
    Object::_Create(guidlow, 0, HighGuid::Corpse);
    return true;
}

/**
 * @brief 创建尸体对象（完整版本）
 *
 * 在玩家死亡时创建尸体对象，包括位置定位、所有者设置和网格计算。
 *
 * @details
 * 此方法在玩家死亡时调用，创建一个完整的尸体对象。执行流程：
 * 1. 验证所有者指针有效性
 * 2. 将尸体定位到玩家当前位置
 *    - 继承玩家的 X、Y、Z 坐标
 *    - 继承玩家的朝向（orientation）
 * 3. 验证位置有效性
 *    - 检查坐标是否在合理范围内
 *    - 如果无效则记录错误并返回 false
 * 4. 创建世界对象
 *    - 设置 GUID 和高 GUID 类型
 *    - 继承玩家的相位掩码（用于多相位显示）
 * 5. 设置尸体基础属性
 *    - 缩放比例为 1.0
 *    - 所有者 GUID
 * 6. 计算网格坐标
 *    - 用于地图分区管理
 *    - 优化对象查找和更新性能
 *
 * @param guidlow 尸体的低 GUID 值，用于唯一标识尸体
 * @param owner 尸体的所有者（玩家指针），必须非空
 *
 * @return true 创建成功
 * @return false 创建失败（位置无效）
 *
 * @note 调用此方法前，应确保玩家对象有效且位置正确
 * @warning 如果 owner 为空，会触发断言失败
 * @see Player::CreateCorpse
 * @see WorldObject::_Create
 */
bool Corpse::Create(ObjectGuid::LowType guidlow, Player* owner)
{
    // 验证所有者指针有效性
    ASSERT(owner);

    // 将尸体定位到玩家的当前位置
    Relocate(owner->GetPositionX(), owner->GetPositionY(), owner->GetPositionZ(), owner->GetOrientation());

    // 验证位置是否有效
    if (!IsPositionValid())
    {
        TC_LOG_ERROR("entities.player", "Corpse (guidlow {}, owner {}) not created. Suggested coordinates isn't valid (X: {} Y: {})",
            guidlow, owner->GetName(), owner->GetPositionX(), owner->GetPositionY());
        return false;
    }

    // 创建世界对象，继承玩家的相位掩码
    WorldObject::_Create(guidlow, HighGuid::Corpse, owner->GetPhaseMask());

    // 设置尸体基础属性
    SetObjectScale(1.0f);
    SetGuidValue(CORPSE_FIELD_OWNER, owner->GetGUID());

    // 计算尸体所在的网格坐标，用于地图分区管理
    _cellCoord = Trinity::ComputeCellCoord(GetPositionX(), GetPositionY());

    return true;
}

/**
 * @brief 将尸体数据保存到数据库
 *
 * 将尸体的所有信息持久化到数据库，确保服务器重启后尸体数据不丢失。
 *
 * @details
 * 执行流程：
 * 1. 开启数据库事务，确保操作的原子性
 * 2. 先删除旧记录（防止数据不一致和重复）
 * 3. 构建插入语句，保存所有尸体字段：
 *    - 基础信息：所有者 GUID、位置坐标（X/Y/Z）、朝向、地图 ID
 *    - 显示信息：模型 ID、装备缓存（19个装备槽）、字节字段（种族、性别、外观）
 *    - 社交信息：公会 ID
 *    - 状态信息：标志、动态标志
 *    - 时间信息：创建时间
 *    - 环境信息：尸体类型、实例 ID、相位掩码
 * 4. 提交事务
 *
 * 数据库字段说明：
 * - guid：所有者 GUID（玩家 GUID）
 * - posX/Y/Z：尸体位置坐标
 * - orientation：尸体朝向角度
 * - mapId：所在地图 ID
 * - displayId：尸体显示模型 ID（继承自玩家）
 * - itemCache：装备缓存字符串，包含所有装备槽信息
 * - bytes1：字节字段 1（种族、性别、职业、能量类型）
 * - bytes2：字节字段 2（外观信息：皮肤、面部、发型等）
 * - guildId：公会 ID
 * - flags：尸体标志（如是否可释放灵魂等）
 * - dynFlags：动态标志（如是否显示战利袋等）
 * - time：创建时间戳
 * - corpseType：尸体类型（骨骼/可复活）
 * - instanceId：实例 ID（用于副本尸体）
 * - phaseMask：相位掩码（用于多相位显示）
 *
 * @note 为防止数据不一致，采用先删后插策略
 * @note 此方法不会立即执行 SQL，而是使用事务批量提交
 * @see LoadCorpseFromDB
 * @see DeleteFromDB
 */
void Corpse::SaveToDB()
{
    // 防止数据库数据不一致和重复记录
    // 使用事务确保删除和插入操作的原子性
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    DeleteFromDB(trans);

    // 准备插入语句
    uint16 index = 0;
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_CORPSE);

    // 绑定参数：基础位置信息
    stmt->setUInt32(index++, GetOwnerGUID().GetCounter());                            // guid - 所有者 GUID
    stmt->setFloat (index++, GetPositionX());                                         // posX - X 坐标
    stmt->setFloat (index++, GetPositionY());                                         // posY - Y 坐标
    stmt->setFloat (index++, GetPositionZ());                                         // posZ - Z 坐标
    stmt->setFloat (index++, GetOrientation());                                       // orientation - 朝向
    stmt->setUInt16(index++, GetMapId());                                             // mapId - 地图 ID

    // 绑定参数：显示信息
    stmt->setUInt32(index++, GetUInt32Value(CORPSE_FIELD_DISPLAY_ID));                // displayId - 显示模型 ID
    stmt->setString(index++, _ConcatFields(CORPSE_FIELD_ITEM, EQUIPMENT_SLOT_END));   // itemCache - 装备缓存
    stmt->setUInt32(index++, GetUInt32Value(CORPSE_FIELD_BYTES_1));                   // bytes1 - 字段 1（种族、性别等）
    stmt->setUInt32(index++, GetUInt32Value(CORPSE_FIELD_BYTES_2));                   // bytes2 - 字段 2（外观信息）
    stmt->setUInt32(index++, GetUInt32Value(CORPSE_FIELD_GUILD));                     // guildId - 公会 ID

    // 绑定参数：状态和标志
    stmt->setUInt8 (index++, GetUInt32Value(CORPSE_FIELD_FLAGS));                     // flags - 标志
    stmt->setUInt8 (index++, GetUInt32Value(CORPSE_FIELD_DYNAMIC_FLAGS));             // dynFlags - 动态标志

    // 绑定参数：时间和环境信息
    stmt->setUInt32(index++, uint32(m_time));                                         // time - 创建时间
    stmt->setUInt8 (index++, GetType());                                              // corpseType - 尸体类型
    stmt->setUInt32(index++, GetInstanceId());                                        // instanceId - 实例 ID
    stmt->setUInt32(index++, GetPhaseMask());                                         // phaseMask - 相位掩码

    trans->Append(stmt);

    // 提交事务
    CharacterDatabase.CommitTransaction(trans);
}

/**
 * @brief 从数据库删除尸体记录（使用当前尸体的所有者）
 *
 * 根据当前尸体的所有者 GUID 从数据库中删除对应的尸体记录。
 *
 * @details
 * 此方法是 DeleteFromDB(ObjectGuid const&, CharacterDatabaseTransaction) 的重载版本，
 * 自动使用当前尸体的所有者 GUID。常用于：
 * - 玩家释放灵魂后，删除可复活尸体
 * - 玩家复活时，清理尸体记录
 * - 尸体过期时，从数据库删除
 *
 * @param trans 数据库事务对象，用于批量操作
 *              - 如果为 nullptr，立即执行删除
 *              - 如果非空，追加到事务中延迟执行
 *
 * @note 此方法调用另一个 DeleteFromDB 重载版本
 * @see DeleteFromDB(ObjectGuid const&, CharacterDatabaseTransaction)
 */
void Corpse::DeleteFromDB(CharacterDatabaseTransaction trans)
{
    DeleteFromDB(GetOwnerGUID(), trans);
}

/**
 * @brief 从数据库删除尸体记录（指定所有者 GUID）
 *
 * 根据指定的所有者 GUID 从数据库中删除对应的尸体记录。
 *
 * @details
 * 此方法是静态删除功能的实际实现，支持事务操作。
 * 执行流程：
 * 1. 准备删除语句（CHAR_DEL_CORPSE）
 * 2. 绑定所有者 GUID 参数
 * 3. 根据事务对象决定执行方式：
 *    - trans 为空：立即执行删除
 *    - trans 非空：追加到事务中延迟执行
 *
 * 数据库操作：
 * - SQL: DELETE FROM corpse WHERE guid = ?
 * - 使用所有者 GUID 作为条件，确保每个玩家只有一个尸体记录
 *
 * 使用场景：
 * - 玩家上线时清理旧尸体
 * - 玩家复活后删除尸体记录
 * - GM 命令删除尸体
 * - 服务器维护时批量清理过期尸体
 *
 * @param ownerGuid 尸体所有者的 GUID（玩家 GUID）
 * @param trans 数据库事务对象
 *              - nullptr：立即执行 SQL
 *              - 非空：追加到事务中，与其他操作一起提交
 *
 * @note 静态方法，可独立于尸体对象调用
 * @warning 此方法不会删除内存中的尸体对象，仅删除数据库记录
 * @see SaveToDB
 */
void Corpse::DeleteFromDB(ObjectGuid const& ownerGuid, CharacterDatabaseTransaction trans)
{
    // 准备删除语句
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_CORPSE);

    // 绑定所有者 GUID 参数
    stmt->setUInt32(0, ownerGuid.GetCounter());

    // 执行或追加到事务
    CharacterDatabase.ExecuteOrAppend(trans, stmt);
}

/**
 * @brief 获取尸体的阵营
 *
 * 从尸体的种族信息中继承阵营 ID，用于阵营相关的交互判断。
 *
 * @details
 * 阵营系统说明：
 * - 阵营（Faction）决定了实体之间的敌友关系
 * - 尸体继承玩家的种族阵营，保持与原玩家相同的阵营关系
 * - 用于判断其他玩家是否可以对尸体进行交互（如复活）
 *
 * 执行流程：
 * 1. 从 CORPSE_FIELD_BYTES_1 字段提取种族信息
 *    - 字段布局：[0: 能量类型, 1: 种族, 2: 性别, 3: 职业]
 * 2. 查询 ChrRaces DBC 表，获取种族对应的阵营 ID
 * 3. 返回阵营 ID，如果种族信息无效则返回 0
 *
 * 阵营 ID 示例：
 * - 联盟通用阵营：11 (Alliance)
 * - 部落通用阵营：85 (Horde)
 * - 人类：12, 兽人：85, 亡灵：85, 等
 *
 * @return uint32 阵营 ID
 *         - 有效值：对应种族的阵营 ID
 *         - 0：种族信息无效或种族不存在
 *
 * @note 尸体的阵营在创建时从玩家继承，之后不会改变
 * @see Player::GetFaction
 * @see ChrRacesEntry
 */
uint32 Corpse::GetFaction() const
{
    // 从玩家种族继承阵营
    // CORPSE_FIELD_BYTES_1 字段布局：[能量类型, 种族, 性别, 职业]
    uint32 const race = GetByteValue(CORPSE_FIELD_BYTES_1, 1);

    // 查询种族 DBC 数据，获取阵营 ID
    ChrRacesEntry const* rEntry = sChrRacesStore.LookupEntry(race);

    // 返回阵营 ID，如果种族不存在则返回 0
    return rEntry ? rEntry->FactionID : 0;
}

/**
 * @brief 重置幽灵时间
 *
 * 将尸体的创建时间更新为当前游戏时间，延长尸体存在时间。
 *
 * @details
 * 使用场景：
 * 1. 玩家进入灵魂医者复活流程
 *    - 延长可复活尸体的存在时间
 *    - 确保玩家有足够时间跑尸体
 * 2. 防止尸体在关键操作期间过期
 *    - 如玩家正在查看尸体信息时
 *    - 玩家正在跑向尸体途中
 *
 * 时间管理机制：
 * - m_time 记录尸体创建或最后重置时间
 * - IsExpired() 使用 m_time 判断是否过期
 * - 骨骼类型：60分钟后过期
 * - 可复活类型：3天后过期
 *
 * @note 此方法不会影响尸体的其他属性，仅重置时间戳
 * @see IsExpired
 * @see GameTime::GetGameTime
 */
void Corpse::ResetGhostTime()
{
    // 更新创建时间为当前游戏时间
    m_time = GameTime::GetGameTime();
}

/**
 * @brief 从数据库加载尸体数据
 *
 * 从数据库查询结果中加载尸体数据并初始化尸体对象。
 *
 * @details
 * 此方法在服务器启动或地图加载时调用，从数据库恢复尸体对象。
 * 执行流程：
 * 1. 提取基础位置信息
 *    - 所有者 GUID（用于关联玩家）
 *    - 坐标：X、Y、Z 和朝向
 *    - 地图 ID
 * 2. 创建基础对象
 *    - 设置 GUID 和类型
 *    - 初始化对象缩放
 * 3. 加载显示信息
 *    - 模型 ID：决定尸体的外观
 *    - 装备缓存：显示玩家死时的装备
 *    - 字节字段：种族、性别、外观等
 *    - 公会 ID：显示公会信息
 * 4. 加载状态信息
 *    - 标志：控制尸体行为
 *    - 动态标志：控制客户端显示
 * 5. 设置时间和环境信息
 *    - 创建时间：用于过期判断
 *    - 实例 ID：用于副本尸体
 *    - 相位掩码：用于多相位显示
 * 6. 定位尸体
 *    - 设置实例和地图 ID
 *    - 应用相位掩码
 *    - 移动到目标位置
 * 7. 验证位置有效性
 *    - 检查坐标是否在合理范围内
 *    - 如果无效则记录错误并返回 false
 * 8. 计算网格坐标
 *    - 用于地图分区管理
 *
 * 数据库字段映射：
 * - [0]  posX: X 坐标
 * - [1]  posY: Y 坐标
 * - [2]  posZ: Z 坐标
 * - [3]  orientation: 朝向
 * - [4]  mapId: 地图 ID
 * - [5]  displayId: 显示模型 ID
 * - [6]  itemCache: 装备缓存字符串
 * - [7]  bytes1: 字节字段 1（种族、性别、职业等）
 * - [8]  bytes2: 字节字段 2（外观信息）
 * - [9]  guildId: 公会 ID
 * - [10] flags: 标志
 * - [11] dynFlags: 动态标志
 * - [12] time: 创建时间
 * - [13] corpseType: 尸体类型
 * - [14] instanceId: 实例 ID
 * - [15] phaseMask: 相位掩码
 * - [16] guid: 所有者 GUID
 *
 * @param guid 尸体的低 GUID 值（数据库主键）
 * @param fields 数据库查询结果字段数组
 *
 * @return true 加载成功
 * @return false 加载失败（位置无效或装备信息无效）
 *
 * @note 此方法不会将尸体添加到世界，需要额外调用 AddToWorld()
 * @note 装备信息无效不会导致加载失败，只会记录错误日志
 * @see SaveToDB
 * @see Object::_LoadIntoDataField
 */
bool Corpse::LoadCorpseFromDB(ObjectGuid::LowType guid, Field* fields)
{
    // 数据库字段索引映射：
    // [0]posX, [1]posY, [2]posZ, [3]orientation, [4]mapId,
    // [5]displayId, [6]itemCache, [7]bytes1, [8]bytes2, [9]guildId,
    // [10]flags, [11]dynFlags, [12]time, [13]corpseType, [14]instanceId,
    // [15]phaseMask, [16]guid

    // SQL 查询示例：
    // SELECT posX, posY, posZ, orientation, mapId, displayId, itemCache, bytes1, bytes2, guildId,
    //        flags, dynFlags, time, corpseType, instanceId, phaseMask, guid
    // FROM corpse WHERE mapId = ? AND instanceId = ?

    // ==================== 提取基础位置信息 ====================
    ObjectGuid::LowType ownerGuid = fields[16].GetUInt32();  // 所有者 GUID
    float posX   = fields[0].GetFloat();                      // X 坐标
    float posY   = fields[1].GetFloat();                      // Y 坐标
    float posZ   = fields[2].GetFloat();                      // Z 坐标
    float o      = fields[3].GetFloat();                      // 朝向
    uint32 mapId = fields[4].GetUInt16();                     // 地图 ID

    // ==================== 创建基础对象 ====================
    Object::_Create(guid, 0, HighGuid::Corpse);

    // ==================== 设置尸体显示信息 ====================
    SetObjectScale(1.0f);
    SetUInt32Value(CORPSE_FIELD_DISPLAY_ID, fields[5].GetUInt32());

    // 加载装备缓存
    // 装备信息用于在尸体上显示玩家死时穿戴的装备
    if (!_LoadIntoDataField(fields[6].GetString(), CORPSE_FIELD_ITEM, EQUIPMENT_SLOT_END))
    {
        TC_LOG_ERROR("entities.player", "Corpse ({}, owner: {}) is not created, given equipment info is not valid ('{}')",
            GetGUID().ToString(), GetOwnerGUID().ToString(), fields[6].GetString());
    }

    // 设置其他显示字段
    SetUInt32Value(CORPSE_FIELD_BYTES_1, fields[7].GetUInt32());   // 种族、性别、职业等
    SetUInt32Value(CORPSE_FIELD_BYTES_2, fields[8].GetUInt32());   // 外观信息
    SetUInt32Value(CORPSE_FIELD_GUILD, fields[9].GetUInt32());     // 公会 ID

    // 设置状态标志
    SetUInt32Value(CORPSE_FIELD_FLAGS, fields[10].GetUInt8());
    SetUInt32Value(CORPSE_FIELD_DYNAMIC_FLAGS, fields[11].GetUInt8());

    // 设置所有者 GUID
    SetGuidValue(CORPSE_FIELD_OWNER, ObjectGuid(HighGuid::Player, ownerGuid));

    // ==================== 设置时间和环境信息 ====================
    m_time = time_t(fields[12].GetUInt32());  // 创建时间

    uint32 instanceId  = fields[14].GetUInt32();  // 实例 ID
    uint32 phaseMask   = fields[15].GetUInt32();  // 相位掩码

    // ==================== 定位尸体 ====================
    SetLocationInstanceId(instanceId);  // 设置实例 ID
    SetLocationMapId(mapId);            // 设置地图 ID
    SetPhaseMask(phaseMask, false);     // 设置相位掩码（不更新客户端）
    Relocate(posX, posY, posZ, o);      // 移动到目标位置

    // ==================== 验证位置有效性 ====================
    if (!IsPositionValid())
    {
        TC_LOG_ERROR("entities.player", "Corpse ({}, owner: {}) is not created, given coordinates are not valid (X: {}, Y: {}, Z: {})",
            GetGUID().ToString(), GetOwnerGUID().ToString(), posX, posY, posZ);
        return false;
    }

    // ==================== 计算网格坐标 ====================
    // 网格坐标用于地图分区管理，优化对象查找和更新性能
    _cellCoord = Trinity::ComputeCellCoord(GetPositionX(), GetPositionY());

    return true;
}

/**
 * @brief 检查尸体是否已过期
 *
 * 判断尸体是否应该被清理，根据尸体类型和创建时间进行判断。
 *
 * @details
 * 过期判断逻辑：
 * 1. 检查所有者角色是否存在
 *    - 如果角色已被删除，尸体视为过期
 *    - 通过角色缓存快速判断角色是否存在
 * 2. 根据尸体类型检查过期时间
 *    - 骨骼类型（CORPSE_BONES）：60分钟后过期
 *    - 可复活类型（CORPSE_RESURRECTABLE）：3天后过期
 * 3. 比较创建时间与当前时间差值
 *
 * 过期时间设计理由：
 * - 骨骼类型（60分钟）：玩家释放灵魂后留下的骨骼，生命周期较短
 *   - 减少服务器内存占用
 *   - 骨骼仅作为视觉元素，无实际交互功能
 * - 可复活类型（3天）：玩家死亡后未释放灵魂的尸体，需要保留较长时间
 *   - 给予玩家足够时间跑尸体复活
 *   - 避免玩家因短期无法上线而丢失复活机会
 *   - 3天时间足够玩家处理现实事务后返回游戏
 *
 * 清理流程：
 * - 世界服务器定期调用此方法检查所有尸体
 * - 过期的尸体会被自动清理
 * - 清理时会从世界和数据库中删除尸体
 *
 * @param t 当前时间戳（通常是 GameTime::GetGameTime()）
 *
 * @return true 尸体已过期，应被清理
 *         - 所有者角色已被删除
 *         - 骨骼类型超过60分钟
 *         - 可复活类型超过3天
 * @return false 尸体仍在有效期内
 *
 * @note 此方法是常量方法，不会修改尸体状态
 * @see ResetGhostTime
 * @see CharacterCache::HasCharacterCacheEntry
 */
bool Corpse::IsExpired(time_t t) const
{
    // 检查所有者角色是否已被删除
    // 如果角色不存在，尸体视为过期
    if (!sCharacterCache->HasCharacterCacheEntry(GetOwnerGUID()))
        return true;

    // 根据尸体类型检查过期时间
    if (m_type == CORPSE_BONES)
    {
        // 骨骼类型：60分钟后过期
        return m_time < t - 60 * MINUTE;
    }
    else
    {
        // 可复活类型：3天后过期
        return m_time < t - 3 * DAY;
    }
}
