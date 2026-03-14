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
 * @file Corpse.h
 * @brief 尸体实体类的头文件
 *
 * 本文件定义了游戏中的尸体(Corpse)实体类，用于表示玩家死亡后留下的尸体对象。
 * 尸体系统是游戏死亡机制的核心组成部分，支持玩家复活、战利品拾取等功能。
 *
 * 主要功能：
 * - 管理玩家死亡后的尸体生成和存储
 * - 支持PVE和PVP环境下的尸体复活机制
 * - 处理尸体过期和转化为骨骼的逻辑
 * - 支持战场中的尸体战利品拾取
 * - 与数据库交互进行尸体的持久化存储
 *
 * @see Corpse
 * @see CorpseType
 * @see CorpseFlags
 */

#ifndef TRINITYCORE_CORPSE_H
#define TRINITYCORE_CORPSE_H

#include "Object.h"
#include "GridObject.h"
#include "DatabaseEnvFwd.h"
#include "GridDefines.h"
#include "Loot.h"

/**
 * @brief 尸体类型枚举
 *
 * 定义游戏中不同类型的尸体状态，用于区分尸体的复活能力和所处环境。
 * 尸体类型决定了玩家是否可以在该尸体处复活，以及复活的规则。
 *
 * @note 尸体类型会影响客户端的显示行为和复活对话框的显示
 */
enum CorpseType
{
    CORPSE_BONES             = 0,  ///< 骨头类型（已过期的尸体，不可复活）<br>尸体经过一段时间后会转化为骨骼，此时玩家无法再通过该尸体复活
    CORPSE_RESURRECTABLE_PVE = 1,  ///< PVE环境可复活尸体<br>玩家在PVE环境下死亡后留下的尸体，可以通过灵魂医者或尸体本身复活
    CORPSE_RESURRECTABLE_PVP = 2   ///< PVP环境可复活尸体<br>玩家在PVP环境下死亡后留下的尸体，复活规则可能与PVE有所不同
};

/// 尸体类型的最大数量，用于数组大小定义和边界检查
#define MAX_CORPSE_TYPE        3

/// 尸体回收半径（39码），该值等于客户端复活对话框的显示半径<br>玩家在此半径内可以与尸体交互进行复活
#define CORPSE_RECLAIM_RADIUS 39

/**
 * @brief 尸体标志枚举
 *
 * 定义尸体的各种显示和行为属性，用于控制尸体在游戏世界中的外观和交互特性。
 * 这些标志是位掩码，可以组合使用多个标志。
 *
 * @note 这些标志存储在数据库中，影响客户端对尸体的渲染和交互行为
 *
 * @see Corpse::GetDynamicFlags
 * @see Corpse::SetDynamicFlags
 */
enum CorpseFlags
{
    CORPSE_FLAG_NONE        = 0x00,  ///< 无标志，默认状态
    CORPSE_FLAG_BONES       = 0x01,  ///< 骨头标志（尸体已过期）<br>设置此标志后，客户端会显示骨骼模型而非完整尸体模型
    CORPSE_FLAG_UNK1        = 0x02,  ///< 未知标志1<br>保留的未使用标志位
    CORPSE_FLAG_UNK2        = 0x04,  ///< 未知标志2<br>保留的未使用标志位
    CORPSE_FLAG_HIDE_HELM   = 0x08,  ///< 隐藏头盔<br>尸体不显示玩家死亡时佩戴的头盔装备
    CORPSE_FLAG_HIDE_CLOAK  = 0x10,  ///< 隐藏披风<br>尸体不显示玩家死亡时佩戴的披风装备
    CORPSE_FLAG_LOOTABLE    = 0x20   ///< 可拾取标志<br>尸体上有可拾取的物品（如战场徽章），客户端会显示拾取提示
};

/**
 * @brief 尸体实体类
 *
 * 继承自 WorldObject 和 GridObject<Corpse>，用于表示玩家死亡后留下的尸体对象。
 * 尸体是游戏中重要的实体类型，承载着死亡机制的核心功能。
 *
 * @details
 * 尸体系统的主要特性：
 * - **多种类型**：支持骨头（已过期）、PVE可复活、PVP可复活三种类型
 * - **生命周期管理**：尸体会在一定时间后过期，转化为不可复活的骨骼
 * - **战利品系统**：尸体可以携带战利品，特别是在战场环境中
 * - **数据库持久化**：尸体数据会保存到数据库，支持跨会话存在
 * - **网格系统**：尸体被纳入地图网格系统，支持空间查询和可见性管理
 *
 * 典型使用场景：
 * 1. 玩家死亡时创建尸体对象，记录死亡位置和时间
 * 2. 玩家灵魂医者或尸体处复活时，验证尸体状态
 * 3. 战场中玩家死亡，尸体可能掉落战利品徽章
 * 4. 尸体过期后自动清理或转换为骨骼
 *
 * @note 尸体对象只在玩家死亡时创建，NPC死亡不会创建Corpse对象
 * @note 每个玩家最多只有一个活跃的尸体对象
 *
 * @see WorldObject
 * @see GridObject
 * @see CorpseType
 * @see CorpseFlags
 * @see Player
 */
class TC_GAME_API Corpse : public WorldObject, public GridObject<Corpse>
{
    public:
        /**
         * @brief 构造函数
         *
         * 创建一个新的尸体对象实例。
         *
         * @param type 尸体类型，默认为 CORPSE_BONES（骨头）
         *             - CORPSE_BONES: 已过期的尸体，不可复活
         *             - CORPSE_RESURRECTABLE_PVE: PVE环境中可复活的尸体
         *             - CORPSE_RESURRECTABLE_PVP: PVP环境中可复活的尸体
         *
         * @note 尸体对象通常在玩家死亡时由系统创建，不应该手动实例化
         */
        explicit Corpse(CorpseType type = CORPSE_BONES);

        /**
         * @brief 析构函数
         *
         * 清理尸体对象资源，包括战利品数据和相关内存。
         * 当尸体从世界中移除或对象生命周期结束时自动调用。
         */
        ~Corpse();

        /**
         * @brief 将尸体添加到世界
         *
         * 重写 WorldObject 的虚函数。将尸体注册到世界对象列表和网格系统中，
         * 使其可以被其他实体看到和交互。
         *
         * @details
         * 执行流程：
         * 1. 调用父类的 AddToWorld() 进行基础注册
         * 2. 将尸体添加到地图网格的更新列表
         * 3. 向附近的客户端广播尸体创建消息
         *
         * @note 必须在尸体完全初始化后（Create()调用后）才能调用此方法
         * @see RemoveFromWorld()
         */
        void AddToWorld() override;

        /**
         * @brief 从世界中移除尸体
         *
         * 重写 WorldObject 的虚函数。从世界对象列表和网格系统中注销尸体，
         * 使其不再被其他实体看到和交互。
         *
         * @details
         * 执行流程：
         * 1. 从地图网格的更新列表中移除尸体
         * 2. 向附近的客户端广播尸体移除消息
         * 3. 调用父类的 RemoveFromWorld() 完成基础注销
         *
         * @note 移除后尸体对象仍然存在，但不再参与世界交互
         * @see AddToWorld()
         */
        void RemoveFromWorld() override;

        /**
         * @brief 创建尸体对象（基础版本）
         *
         * 初始化尸体对象的基础属性，设置GUID和对象类型。
         *
         * @param guidlow 低GUID值，用于生成唯一的对象标识符
         * @return 创建成功返回 true，失败返回 false
         *
         * @note 此版本不设置所有者，通常用于系统内部创建
         * @see Create(ObjectGuid::LowType, Player*)
         */
        bool Create(ObjectGuid::LowType guidlow);

        /**
         * @brief 创建尸体对象（完整版本）
         *
         * 初始化尸体对象并设置所有者信息，这是最常用的创建方法。
         * 在玩家死亡时调用，创建与玩家关联的尸体。
         *
         * @param guidlow 低GUID值，用于生成唯一的对象标识符
         * @param owner 尸体的所有者（玩家指针），记录死亡玩家的信息
         * @return 创建成功返回 true，失败返回 false
         *
         * @details
         * 创建过程会：
         * 1. 设置尸体的基础属性（位置、朝向等）
         * 2. 复制玩家的外观信息（装备显示）
         * 3. 设置所有者GUID和阵营信息
         * 4. 初始化尸体创建时间
         *
         * @pre owner 必须是有效的 Player 指针
         * @see Player::CreateCorpse()
         */
        bool Create(ObjectGuid::LowType guidlow, Player* owner);

        /**
         * @brief 将尸体数据保存到数据库
         *
         * 将尸体的当前状态持久化到角色数据库（characters数据库）。
         * 包括位置、朝向、所有者、创建时间等所有尸体属性。
         *
         * @details
         * 保存时机：
         * - 尸体创建时
         * - 服务器关闭时
         * - 定期自动保存
         *
         * @note 使用 INSERT REPLACE 语法，如果尸体已存在则更新
         * @see LoadCorpseFromDB()
         */
        void SaveToDB();

        /**
         * @brief 从数据库加载尸体数据
         *
         * 从数据库记录中恢复尸体对象的状态。
         * 通常在服务器启动或玩家登录时调用，恢复持久化的尸体。
         *
         * @param guid 尸体的低GUID值
         * @param fields 数据库字段数组，包含尸体的所有持久化数据
         * @return 加载成功返回 true，失败返回 false
         *
         * @details
         * 加载的数据包括：
         * - 所有者GUID和实例ID
         * - 位置坐标（X, Y, Z）和朝向（O）
         * - 所在地图ID
         * - 显示信息（装备外观）
         * - 尸体类型和标志
         * - 创建时间
         *
         * @pre fields 必须包含正确数量的字段
         * @see SaveToDB()
         */
        bool LoadCorpseFromDB(ObjectGuid::LowType guid, Field* fields);

        /**
         * @brief 从数据库删除尸体（实例方法）
         *
         * 从角色数据库中删除当前尸体记录。
         * 通常在尸体被拾取或过期时调用。
         *
         * @param trans 数据库事务对象，允许在事务中执行删除操作
         *              如果为 nullptr，则立即执行删除
         *
         * @note 此方法只删除数据库记录，不销毁内存中的对象
         * @see DeleteFromDB(ObjectGuid const&, CharacterDatabaseTransaction)
         */
        void DeleteFromDB(CharacterDatabaseTransaction trans);

        /**
         * @brief 从数据库删除指定所有者的尸体（静态方法）
         *
         * 根据所有者GUID从数据库中删除尸体记录。
         * 这是一个静态方法，可以在没有尸体对象实例的情况下调用。
         *
         * @param ownerGuid 尸体所有者的GUID
         * @param trans 数据库事务对象，允许在事务中执行删除操作
         *              如果为 nullptr，则立即执行删除
         *
         * @details
         * 使用场景：
         * - 玩家成功复活后删除旧尸体
         * - 玩家删除角色时清理尸体
         * - GM命令清理玩家尸体
         *
         * @see DeleteFromDB(CharacterDatabaseTransaction)
         */
        static void DeleteFromDB(ObjectGuid const& ownerGuid, CharacterDatabaseTransaction trans);

        /**
         * @brief 获取尸体所有者的GUID
         *
         * 返回创建该尸体的玩家（所有者）的对象GUID。
         * 用于关联尸体和死亡玩家的关系。
         *
         * @return 所有者的ObjectGuid引用
         *
         * @note 重写自 WorldObject::GetOwnerGUID()
         */
        ObjectGuid GetOwnerGUID() const override { return GetGuidValue(CORPSE_FIELD_OWNER); }

        /**
         * @brief 获取尸体的阵营
         *
         * 返回尸体所属的阵营ID。通常与所有者玩家的阵营一致。
         * 阵营决定了PVP交互规则和敌对关系。
         *
         * @return 阵营模板ID
         *
         * @note 重写自 WorldObject::GetFaction()
         * @see FactionTemplateEntry
         */
        uint32 GetFaction() const override;

        /**
         * @brief 获取尸体变成鬼魂的时间
         *
         * 返回尸体创建时的时间戳，即玩家死亡变成鬼魂状态的时间。
         * 用于计算尸体是否过期。
         *
         * @return 时间戳的常量引用（Unix时间戳格式）
         *
         * @see ResetGhostTime()
         * @see IsExpired()
         */
        time_t const& GetGhostTime() const { return m_time; }

        /**
         * @brief 重置鬼魂时间
         *
         * 将尸体的创建时间更新为当前时间。
         * 通常在延长尸体存在时间或特殊复活场景中使用。
         *
         * @see GetGhostTime()
         */
        void ResetGhostTime();

        /**
         * @brief 获取尸体类型
         *
         * 返回尸体的类型枚举值，标识尸体是骨头、PVE可复活或PVP可复活。
         *
         * @return 尸体类型枚举值（CorpseType）
         *
         * @see CorpseType
         */
        CorpseType GetType() const { return m_type; }

        /**
         * @brief 获取单元格坐标
         *
         * 返回尸体所在的网格单元格坐标。
         * 用于地图网格系统中的空间分区和查询优化。
         *
         * @return 单元格坐标（CellCoord）的常量引用
         *
         * @see SetCellCoord()
         * @see GridSystem
         */
        CellCoord const& GetCellCoord() const { return _cellCoord; }

        /**
         * @brief 设置单元格坐标
         *
         * 设置尸体所在的网格单元格坐标。
         * 通常在尸体位置更新或加载时调用。
         *
         * @param cellCoord 单元格坐标对象
         *
         * @see GetCellCoord()
         */
        void SetCellCoord(CellCoord const& cellCoord) { _cellCoord = cellCoord; }

        Loot loot;                    ///< 战利品对象<br>存储尸体上的可拾取物品，主要用于战场中的徽章掉落
        Player* lootRecipient;        ///< 战利品接收者<br>指向有权拾取该尸体战利品的玩家指针

        /**
         * @brief 检查尸体是否已过期
         *
         * 根据当前时间和尸体创建时间，判断尸体是否已经超过存在时限。
         * 过期的尸体会转换为骨骼或被系统清理。
         *
         * @param t 当前时间戳
         * @return 已过期返回 true，未过期返回 false
         *
         * @details
         * 过期时间规则：
         * - PVE尸体：3分钟内可复活，之后过期
         * - PVP尸体：可能有不同的过期时间规则
         * - 骨骼：已经过期，不会再次过期
         *
         * @see GetGhostTime()
         */
        bool IsExpired(time_t t) const;

    private:
        CorpseType m_type;            ///< 尸体类型<br>标识尸体是骨头（CORPSE_BONES）、PVE可复活（CORPSE_RESURRECTABLE_PVE）或PVP可复活（CORPSE_RESURRECTABLE_PVP）
        time_t m_time;                ///< 尸体创建时间戳<br>记录玩家死亡变成鬼魂状态的时间，用于计算尸体是否过期
        CellCoord _cellCoord;         ///< 网格单元格坐标<br>尸体在地图网格系统中的位置，用于空间分区和可见性管理
};
#endif
