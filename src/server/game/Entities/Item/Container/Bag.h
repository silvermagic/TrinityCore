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
 * @file Bag.h
 * @brief 背包容器类定义文件
 *
 * 本文件定义了 Bag 类，用于实现游戏中的背包容器功能。
 * 背包是一种特殊的物品容器，可以存储其他物品，提供物品管理功能，
 * 包括物品的存储、移除、查询和数据库持久化操作。
 */

#ifndef TRINITY_BAG_H
#define TRINITY_BAG_H

/**
 * @brief 背包最大槽位数量
 *
 * 背包最多支持 36 个槽位，计算公式为：
 * (CONTAINER_END - CONTAINER_FIELD_SLOT_1) / 2
 * 该限制源自客户端协议版本 2.0.12
 */
#define MAX_BAG_SIZE 36                                     // 2.0.12

#include "Item.h"

/**
 * @class Bag
 * @brief 背包容器类，继承自 Item 基类
 *
 * Bag 类实现了游戏中的背包容器功能，是一种特殊的 Item 类型。
 * 背包可以存储其他物品，提供物品槽位管理功能。
 *
 * 继承关系：
 * - 继承自 Item 类，具备物品的基本属性和行为
 * - 重写了多个虚函数以实现背包特有的逻辑
 *
 * 主要职责：
 * - 管理背包内的物品槽位（最多 36 个槽位）
 * - 提供物品的存储、移除和查询接口
 * - 实现背包数据的数据库持久化
 * - 处理背包在世界中的添加和移除
 */
class TC_GAME_API Bag : public Item
{
    public:
        /**
         * @brief 构造函数
         *
         * 初始化背包对象，清空所有槽位指针
         */
        Bag();

        /**
         * @brief 析构函数
         *
         * 清理背包资源，注意：不会删除槽位中的物品
         */
        ~Bag();

        /**
         * @brief 将背包添加到游戏世界
         *
         * 重写 Item::AddToWorld()，在背包添加到世界时，
         * 同时将背包内所有物品也添加到世界中。
         * 这确保背包及其内容的完整性和可见性。
         */
        void AddToWorld() override;

        /**
         * @brief 将背包从游戏世界中移除
         *
         * 重写 Item::RemoveFromWorld()，在背包从世界移除时，
         * 同时将背包内所有物品也从世界中移除。
         * 这确保背包及其内容的正确清理。
         */
        void RemoveFromWorld() override;

        /**
         * @brief 创建背包对象
         *
         * 重写 Item::Create()，初始化背包特有的字段和槽位。
         * 设置背包的槽位数量（从物品模板获取）并初始化所有槽位为空。
         *
         * @param guidlow 物品的低阶 GUID（数据库 ID）
         * @param itemid 物品模板 ID
         * @param owner 背包所有者指针（const，不会被修改）
         * @return 创建成功返回 true，失败返回 false
         *
         * @note 背包的槽位数量由物品模板的 ContainerSlots 字段决定
         */
        bool Create(ObjectGuid::LowType guidlow, uint32 itemid, Player const* owner) override;

        /**
         * @brief 在指定槽位存储物品
         *
         * 将物品存入背包的指定槽位。这是背包物品管理的核心函数。
         *
         * @param slot 目标槽位索引（0 到 GetBagSize()-1）
         * @param pItem 要存储的物品指针（可以为 nullptr，表示清空槽位）
         * @param update 是否立即更新客户端数据
         *               - true: 发送更新包给客户端
         *               - false: 仅修改服务端数据
         *
         * @warning 此函数不会检查槽位有效性，调用者需确保槽位在有效范围内
         * @warning 此函数不会修改物品的父容器指针，调用者需自行处理
         */
        void StoreItem(uint8 slot, Item* pItem, bool update);

        /**
         * @brief 从指定槽位移除物品
         *
         * 从背包的指定槽位移除物品。注意：此函数仅清空槽位指针，
         * 不会删除物品对象本身。
         *
         * @param slot 要清空的槽位索引（0 到 GetBagSize()-1）
         * @param update 是否立即更新客户端数据
         *               - true: 发送更新包给客户端，将槽位设置为空
         *               - false: 仅修改服务端数据
         *
         * @warning 移除后槽位指针将变为 nullptr，但物品对象仍然存在
         * @see StoreItem()
         */
        void RemoveItem(uint8 slot, bool update);

        /**
         * @brief 根据槽位获取物品
         *
         * 获取背包指定槽位中的物品指针。
         *
         * @param slot 槽位索引（0 到 GetBagSize()-1）
         * @return 返回槽位中的物品指针，如果槽位为空或超出范围则返回 nullptr
         *
         * @note 时间复杂度 O(1)，直接数组访问
         */
        Item* GetItemByPos(uint8 slot) const;

        /**
         * @brief 统计指定物品 ID 的数量
         *
         * 统计背包中某个物品 ID 的总数量，包括堆叠数量。
         *
         * @param item 物品模板 ID
         * @param eItem 要排除的物品指针（可选，用于排除某个特定物品实例）
         *              - 默认 nullptr: 统计所有匹配物品
         *              - 非空: 不统计该物品实例
         * @return 返回物品总数（考虑堆叠数量）
         *
         * @note 时间复杂度 O(n)，n 为背包槽位数量
         */
        uint32 GetItemCount(uint32 item, Item* eItem = nullptr) const;

        /**
         * @brief 统计指定限制类别的物品数量
         *
         * 统计背包中属于某个限制类别（如唯一装备类别）的物品数量。
         * 用于实现物品的唯一性检查。
         *
         * @param limitCategory 限制类别 ID（来自 ItemTemplate::ItemLimitCategory）
         * @param skipItem 要跳过的物品指针（可选）
         *                 - 默认 nullptr: 统计所有匹配物品
         *                 - 非空: 不统计该物品实例
         * @return 返回匹配限制类别的物品总数
         *
         * @note 时间复杂度 O(n)，n 为背包槽位数量
         */
        uint32 GetItemCountWithLimitCategory(uint32 limitCategory, Item* skipItem = nullptr) const;

        /**
         * @brief 根据物品 GUID 查找槽位
         *
         * 在背包中查找指定 GUID 的物品，返回其所在槽位索引。
         *
         * @param guid 要查找的物品 GUID
         * @return 如果找到物品，返回槽位索引（0 到 GetBagSize()-1）；
         *         如果未找到，返回 NULL_SLOT（255）
         *
         * @note 时间复杂度 O(n)，n 为背包槽位数量
         */
        uint8 GetSlotByItemGUID(ObjectGuid guid) const;

        /**
         * @brief 检查背包是否为空
         *
         * 检查背包中是否没有任何物品。
         *
         * @return 如果所有槽位都为空返回 true，否则返回 false
         *
         * @note 时间复杂度 O(n)，n 为背包槽位数量
         */
        bool IsEmpty() const;

        /**
         * @brief 获取背包中的空闲槽位数量
         *
         * 统计背包中当前空闲（未放置物品）的槽位数量。
         *
         * @return 空闲槽位的数量
         *
         * @note 时间复杂度 O(n)，n 为背包槽位数量
         * @note 常用于检查背包是否还有空间容纳新物品
         */
        uint32 GetFreeSlots() const;

        /**
         * @brief 获取背包的总槽位数量
         *
         * 获取背包的容量（槽位总数），不包括背包本身占用的装备槽。
         * 该值从物品模板的 ContainerSlots 字段获取。
         *
         * @return 背包的槽位总数（通常为 0 到 36）
         *
         * @note 时间复杂度 O(1)，直接读取字段值
         * @see Create()
         */
        uint32 GetBagSize() const { return GetUInt32Value(CONTAINER_FIELD_NUM_SLOTS); }

        /**
         * @brief 数据库操作 - 保存背包数据
         *
         * 重写 Item::SaveToDB()，将背包及其内部物品的数据保存到数据库。
         *
         * @param trans 数据库事务对象，用于保证数据一致性
         *              - 如果为 nullptr，则立即执行
         *              - 如果非空，则加入事务队列
         *
         * @note 会递归调用背包内所有物品的 SaveToDB() 方法
         * @note 保存内容包括背包本身的数据和所有槽位物品的数据
         */
        void SaveToDB(CharacterDatabaseTransaction trans) override;

        /**
         * @brief 数据库操作 - 从数据库加载背包数据
         *
         * 重写 Item::LoadFromDB()，从数据库加载背包数据，包括背包内的物品。
         *
         * @param guid 物品的低阶 GUID（数据库 ID）
         * @param owner_guid 所有者的 GUID
         * @param fields 数据库查询结果字段数组
         * @param entry 物品模板 ID
         * @return 加载成功返回 true，失败返回 false
         *
         * @note 会同时加载背包内所有物品的数据
         * @see Item::LoadFromDB()
         */
        bool LoadFromDB(ObjectGuid::LowType guid, ObjectGuid owner_guid, Field* fields, uint32 entry) override;

        /**
         * @brief 数据库操作 - 从数据库删除背包数据
         *
         * 重写 Item::DeleteFromDB()，从数据库删除背包及其内部物品的数据。
         *
         * @param trans 数据库事务对象，用于保证数据一致性
         *              - 如果为 nullptr，则立即执行
         *              - 如果非空，则加入事务队列
         *
         * @warning 此操作不可逆，会永久删除背包及所有内部物品的数据库记录
         * @note 会递归调用背包内所有物品的 DeleteFromDB() 方法
         */
        void DeleteFromDB(CharacterDatabaseTransaction trans) override;

        /**
         * @brief 构建创建更新数据块
         *
         * 重写 Item::BuildCreateUpdateBlockForPlayer()，为玩家构建背包的创建更新数据包。
         * 该数据包用于将背包对象发送给客户端，使其能够在客户端创建并显示背包。
         *
         * @param data 更新数据对象，用于存储构建的数据包
         * @param target 目标玩家指针，数据将发送给该玩家
         *
         * @note 此函数在背包首次对玩家可见时调用
         * @note 会同时发送背包内所有物品的创建数据
         */
        void BuildCreateUpdateBlockForPlayer(UpdateData* data, Player* target) const override;

        /**
         * @brief 获取调试信息字符串
         *
         * 重写 Item::GetDebugInfo()，生成包含背包详细信息的调试字符串。
         * 主要用于日志记录和调试目的。
         *
         * @return 包含背包 GUID、条目 ID、槽位数量等信息的格式化字符串
         *
         * @note 主要用于开发和调试，不应在生产环境中频繁调用
         */
        std::string GetDebugInfo() const override;

    protected:
        /**
         * @brief 背包物品槽位数组
         *
         * 存储背包内各个槽位的物品指针。数组大小固定为 MAX_BAG_SIZE (36)，
         * 但实际使用的槽位数量由 GetBagSize() 决定。
         *
         * 索引说明：
         * - 有效索引范围：[0, GetBagSize()-1]
         * - 未使用的槽位指针保持为 nullptr
         * - 槽位索引对应客户端显示的背包格子位置
         *
         * 生命周期管理：
         * - Bag 对象不拥有槽位中物品的所有权
         * - 背包销毁时不会自动删除槽位中的物品
         * - 物品的生命周期由玩家或容器系统管理
         */
        Item* m_bagslot[MAX_BAG_SIZE];
};

/**
 * @brief 工厂函数：根据物品模板创建物品或背包对象
 *
 * 根据物品模板的 InventoryType 字段，决定创建 Bag 对象还是普通 Item 对象。
 * 这是创建物品实例的标准入口函数。
 *
 * @param proto 物品模板常量指针，包含物品的类型信息
 * @return 返回新创建的 Item 指针：
 *         - 如果 InventoryType == INVTYPE_BAG，返回 new Bag
 *         - 否则返回 new Item
 *
 * @note 调用者负责管理返回对象的生命周期（使用 delete 释放）
 * @note 返回类型为 Item*，但实际对象可能是 Bag 实例（多态）
 *
 * 使用示例：
 * @code
 * ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
 * Item* item = NewItemOrBag(proto);
 * // 使用 item...
 * delete item; // 由调用者负责释放
 * @endcode
 */
inline Item* NewItemOrBag(ItemTemplate const* proto)
{
    return (proto->InventoryType == INVTYPE_BAG) ? new Bag : new Item;
}
#endif
