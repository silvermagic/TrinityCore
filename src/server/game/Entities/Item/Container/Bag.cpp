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
 * @file Bag.cpp
 * @brief 容器背包类实现文件
 *
 * 本文件实现了 Bag 类,该类继承自 Item 类,用于管理游戏中的容器背包。
 * 容器背包可以存储其他物品,提供物品的增删查改功能,并与数据库进行交互。
 * 主要职责包括:
 * - 容器槽位管理:管理背包内的物品槽位
 * - 物品存储:存储、移除和查询背包内的物品
 * - 数据持久化:保存和加载数据库中的容器数据
 * - 世界同步:将容器及其内容物同步到游戏世界
 */

#include "Common.h"
#include "ObjectMgr.h"
#include "DatabaseEnv.h"

#include "Bag.h"
#include "Log.h"
#include "UpdateData.h"
#include "Player.h"

/**
 * @brief Bag 类默认构造函数
 *
 * 初始化容器对象的基础属性:
 * - 设置对象类型为容器类型(TYPEMASK_CONTAINER)
 * - 设置对象类型ID为容器ID(TYPEID_CONTAINER)
 * - 初始化字段值为容器字段总数
 * - 清空所有背包槽位指针
 */
Bag::Bag(): Item()
{
    // 标记对象为容器类型,用于类型检查和转换
    m_objectType |= TYPEMASK_CONTAINER;
    m_objectTypeId = TYPEID_CONTAINER;

    // 设置容器字段数量,用于网络同步
    m_valuesCount = CONTAINER_END;

    // 初始化背包槽位数组为空指针,防止野指针访问
    memset(m_bagslot, 0, sizeof(Item*) * MAX_BAG_SIZE);
}

/**
 * @brief Bag 类析构函数
 *
 * 清理容器内的所有物品:
 * - 遍历所有背包槽位
 * - 检查并移除仍在世界中的物品(异常情况)
 * - 删除所有物品对象,释放内存
 *
 * @note 如果物品仍处于世界中,会记录致命错误日志并强制移除,
 *       这表示存在逻辑错误,需要排查
 */
Bag::~Bag()
{
    // 遍历所有背包槽位,清理物品
    for (uint8 i = 0; i < MAX_BAG_SIZE; ++i)
        if (Item* item = m_bagslot[i])
        {
            // 检查物品是否仍在游戏世界中(异常情况检测)
            if (item->IsInWorld())
            {
                // 记录致命错误:物品应该在删除前就移出世界
                TC_LOG_FATAL("entities.player.items", "Item {} (slot {}, bag slot {}) in bag {} (slot {}, bag slot {}, m_bagslot {}) is to be deleted but is still in world.",
                    item->GetEntry(), (uint32)item->GetSlot(), (uint32)item->GetBagSlot(),
                    GetEntry(), (uint32)GetSlot(), (uint32)GetBagSlot(), (uint32)i);
                // 强制从世界中移除,防止悬空引用
                item->RemoveFromWorld();
            }
            // 删除物品对象,释放内存
            delete m_bagslot[i];
        }
}

/**
 * @brief 将容器及其内容物添加到游戏世界
 *
 * 先将容器本身添加到世界,然后将背包内的所有物品依次添加到世界。
 * 这确保了世界状态的一致性,客户端可以看到完整的容器内容。
 *
 * @note 调用顺序很重要:必须先添加容器本身,再添加内部物品
 */
void Bag::AddToWorld()
{
    // 首先将容器本身添加到世界
    Item::AddToWorld();

    // 将背包内所有物品添加到世界
    // 只遍历有效的背包槽位范围
    for (uint32 i = 0; i < GetBagSize(); ++i)
        if (m_bagslot[i])
            m_bagslot[i]->AddToWorld();
}

/**
 * @brief 将容器及其内容物从游戏世界中移除
 *
 * 先将背包内的所有物品从世界中移除,然后将容器本身从世界中移除。
 * 移除顺序与添加顺序相反,确保正确清理依赖关系。
 *
 * @note 移除顺序很重要:必须先移除内部物品,再移除容器本身
 */
void Bag::RemoveFromWorld()
{
    // 先从世界中移除背包内所有物品
    for (uint32 i = 0; i < GetBagSize(); ++i)
        if (m_bagslot[i])
            m_bagslot[i]->RemoveFromWorld();

    // 最后将容器本身从世界中移除
    Item::RemoveFromWorld();
}

/**
 * @brief 创建容器对象
 *
 * 初始化容器的基本属性,包括GUID、物品模板、所有者信息、耐久度和槽位数量等。
 * 此函数用于创建新的容器实例。
 *
 * @param guidlow 容器的低GUID值,用于唯一标识对象
 * @param itemid 物品模板ID,定义容器的基础属性
 * @param owner 容器的所有者玩家指针,可为nullptr
 *
 * @return 如果创建成功返回true,如果物品模板无效或槽位数量超过上限返回false
 *
 * @note 验证逻辑:
 *       - 物品模板必须存在
 *       - 容器槽位数量不能超过MAX_BAG_SIZE(20)
 * @note 初始化内容包括:
 *       - 设置容器GUID和基础属性
 *       - 设置所有者信息(如果提供)
 *       - 初始化耐久度和堆叠数量
 *       - 清空所有槽位字段
 */
bool Bag::Create(ObjectGuid::LowType guidlow, uint32 itemid, Player const* owner)
{
    // 获取物品模板,验证容器是否有效
    ItemTemplate const* itemProto = sObjectMgr->GetItemTemplate(itemid);

    // 验证:物品模板必须存在且槽位数量合法
    if (!itemProto || itemProto->ContainerSlots > MAX_BAG_SIZE)
        return false;

    // 创建容器对象的基础GUID,使用HighGuid::Container类型
    Object::_Create(guidlow, 0, HighGuid::Container);

    // 设置物品模板ID和缩放比例
    SetEntry(itemid);
    SetObjectScale(1.0f);

    // 如果有所有者,设置所有者GUID和容器持有者GUID
    if (owner)
    {
        SetGuidValue(ITEM_FIELD_OWNER, owner->GetGUID());
        SetGuidValue(ITEM_FIELD_CONTAINED, owner->GetGUID());
    }

    // 初始化耐久度相关字段
    SetUInt32Value(ITEM_FIELD_MAXDURABILITY, itemProto->MaxDurability);
    SetUInt32Value(ITEM_FIELD_DURABILITY, itemProto->MaxDurability);
    // 设置堆叠数量为1(容器本身不可堆叠)
    SetUInt32Value(ITEM_FIELD_STACK_COUNT, 1);

    // 设置容器的槽位数量
    SetUInt32Value(CONTAINER_FIELD_NUM_SLOTS, itemProto->ContainerSlots);

    // 清空所有槽位(最多20个槽位的字段和指针)
    // 初始化时所有槽位都为空
    for (uint8 i = 0; i < MAX_BAG_SIZE; ++i)
    {
        SetGuidValue(CONTAINER_FIELD_SLOT_1 + (i*2), ObjectGuid::Empty);
        m_bagslot[i] = nullptr;
    }

    return true;
}

/**
 * @brief 将容器数据保存到数据库
 *
 * 调用父类Item的保存方法,将容器的基础信息保存到数据库。
 * 容器内的物品会通过其他机制单独保存。
 *
 * @param trans 数据库事务对象,用于保证数据一致性
 *
 * @note 容器内的物品不在此函数中保存,它们有自己的保存逻辑
 */
void Bag::SaveToDB(CharacterDatabaseTransaction trans)
{
    // 调用父类方法保存容器基础数据
    Item::SaveToDB(trans);
}

/**
 * @brief 从数据库加载容器数据
 *
 * 从数据库加载容器的基础信息,并初始化容器槽位。
 * 容器内的物品会从character_inventory表中单独加载。
 *
 * @param guid 容器的低GUID值
 * @param owner_guid 容器所有者的GUID
 * @param fields 数据库字段数组,包含容器的持久化数据
 * @param entry 物品模板ID
 *
 * @return 如果加载成功返回true,如果父类加载失败返回false
 *
 * @note 加载流程:
 *       1. 调用父类方法加载基础物品数据
 *       2. 设置容器的槽位数量
 *       3. 清空槽位字段,等待从character_inventory表加载物品
 */
bool Bag::LoadFromDB(ObjectGuid::LowType guid, ObjectGuid owner_guid, Field* fields, uint32 entry)
{
    // 调用父类方法加载基础物品数据
    if (!Item::LoadFromDB(guid, owner_guid, fields, entry))
        return false;

    // 获取物品模板(已在父类LoadFromDB中验证过)
    ItemTemplate const* itemProto = GetTemplate();
    // 设置容器的槽位数量
    SetUInt32Value(CONTAINER_FIELD_NUM_SLOTS, itemProto->ContainerSlots);

    // 清空所有槽位字段和内存中的物品指针
    // 这些字段将从character_inventory表中正确加载
    for (uint8 i = 0; i < MAX_BAG_SIZE; ++i)
    {
        SetGuidValue(CONTAINER_FIELD_SLOT_1 + (i * 2), ObjectGuid::Empty);
        delete m_bagslot[i];
        m_bagslot[i] = nullptr;
    }

    return true;
}

/**
 * @brief 从数据库删除容器及其所有物品
 *
 * 递归删除容器内的所有物品,然后删除容器本身。
 * 使用事务确保数据一致性。
 *
 * @param trans 数据库事务对象,确保删除操作的原子性
 *
 * @note 删除顺序:先删除容器内物品,再删除容器本身
 * @warning 此操作不可逆,会永久删除数据
 */
void Bag::DeleteFromDB(CharacterDatabaseTransaction trans)
{
    // 先删除容器内的所有物品
    for (uint8 i = 0; i < MAX_BAG_SIZE; ++i)
        if (m_bagslot[i])
            m_bagslot[i]->DeleteFromDB(trans);

    // 最后删除容器本身
    Item::DeleteFromDB(trans);
}

/**
 * @brief 获取容器中的空闲槽位数量
 *
 * 遍历所有槽位,统计未使用的槽位数量。
 *
 * @return 空闲槽位的数量
 *
 * @note 时间复杂度: O(n), n为容器槽位数量
 */
uint32 Bag::GetFreeSlots() const
{
    uint32 slots = 0;
    // 遍历所有有效槽位,统计空槽位
    for (uint32 i=0; i < GetBagSize(); ++i)
        if (!m_bagslot[i])
            ++slots;

    return slots;
}

/**
 * @brief 从指定槽位移除物品
 *
 * 清除槽位中的物品引用,并更新相关字段。
 * 物品本身不会被删除,只是从容器的槽位中移除引用。
 *
 * @param slot 槽位索引(0到MAX_BAG_SIZE-1)
 * @param update 未使用的参数(保留用于兼容性)
 *
 * @note 此函数只清除引用,不删除物品对象
 * @note 调用此函数会:
 *       - 清除物品的容器引用
 *       - 清空槽位指针
 *       - 清空槽位的GUID字段
 */
void Bag::RemoveItem(uint8 slot, bool /*update*/)
{
    // 断言槽位索引合法
    ASSERT(slot < MAX_BAG_SIZE);

    // 如果槽位有物品,清除物品的容器引用
    if (m_bagslot[slot])
        m_bagslot[slot]->SetContainer(nullptr);

    // 清空槽位指针和GUID字段
    m_bagslot[slot] = nullptr;
    SetGuidValue(CONTAINER_FIELD_SLOT_1 + (slot * 2), ObjectGuid::Empty);
}

/**
 * @brief 将物品存储到指定槽位
 *
 * 将物品放入容器的指定槽位,并设置物品的相关属性(容器引用、所有者等)。
 *
 * @param slot 槽位索引(0到MAX_BAG_SIZE-1)
 * @param pItem 要存储的物品指针
 * @param update 未使用的参数(保留用于兼容性)
 *
 * @note 防护措施:
 *       - 物品指针必须有效
 *       - 物品不能是自己(防止容器自我包含)
 * @note 设置的物品属性:
 *       - 槽位指针引用
 *       - 槽位GUID字段
 *       - 物品的容器GUID
 *       - 物品的所有者GUID
 *       - 物品的容器指针
 *       - 物品的槽位索引
 */
void Bag::StoreItem(uint8 slot, Item* pItem, bool /*update*/)
{
    // 断言槽位索引合法
    ASSERT(slot < MAX_BAG_SIZE);

    // 检查物品有效且不是容器自己(防止自我包含)
    if (pItem && pItem->GetGUID() != GetGUID())
    {
        // 设置槽位指针引用
        m_bagslot[slot] = pItem;
        // 更新槽位的GUID字段,用于网络同步
        SetGuidValue(CONTAINER_FIELD_SLOT_1 + (slot * 2), pItem->GetGUID());
        // 设置物品的容器GUID
        pItem->SetGuidValue(ITEM_FIELD_CONTAINED, GetGUID());
        // 设置物品的所有者为容器的所有者
        pItem->SetGuidValue(ITEM_FIELD_OWNER, GetOwnerGUID());
        // 设置物品的容器指针
        pItem->SetContainer(this);
        // 设置物品的槽位索引
        pItem->SetSlot(slot);
    }
}

/**
 * @brief 为玩家构建创建更新数据块
 *
 * 构建容器及其所有物品的创建更新包,发送给目标玩家。
 * 用于向客户端同步容器的完整状态。
 *
 * @param data 更新数据对象,用于存储构建的更新包
 * @param target 目标玩家,接收更新数据的玩家
 *
 * @note 发送顺序:先发送容器本身,再发送容器内的物品
 *       这样客户端能正确建立父子关系
 */
void Bag::BuildCreateUpdateBlockForPlayer(UpdateData* data, Player* target) const
{
    // 首先构建容器本身的创建更新
    Item::BuildCreateUpdateBlockForPlayer(data, target);

    // 然后构建容器内所有物品的创建更新
    for (uint32 i = 0; i < GetBagSize(); ++i)
        if (m_bagslot[i])
            m_bagslot[i]->BuildCreateUpdateBlockForPlayer(data, target);
}

/**
 * @brief 检查容器是否为空
 *
 * 遍历所有槽位,检查是否存在任何物品。
 *
 * @return 如果容器为空返回true,否则返回false
 *
 * @note 时间复杂度: O(n), n为容器槽位数量
 */
bool Bag::IsEmpty() const
{
    // 遍历所有槽位,检查是否有物品
    for (uint32 i = 0; i < GetBagSize(); ++i)
        if (m_bagslot[i])
            return false;

    return true;
}

/**
 * @brief 获取容器中指定物品的数量
 *
 * 统计容器中特定物品的总数量,包括物品本身的堆叠数量。
 * 如果eItem是宝石,还会统计镶嵌在装备中的宝石数量。
 *
 * @param item 物品模板ID
 * @param eItem 要排除的物品指针(通常用于排除正在操作的物品)
 *
 * @return 物品的总数量
 *
 * @note 统计逻辑:
 *       1. 遍历所有槽位,累加匹配物品的堆叠数量
 *       2. 如果eItem有宝石属性,额外统计镶嵌在装备中的宝石数量
 * @note 时间复杂度: O(n), n为容器槽位数量
 */
uint32 Bag::GetItemCount(uint32 item, Item* eItem) const
{
    Item* pItem;
    uint32 count = 0;

    // 第一遍遍历:统计直接存储的物品数量
    for (uint32 i=0; i < GetBagSize(); ++i)
    {
        pItem = m_bagslot[i];
        // 检查物品有效、不是排除物品、且模板ID匹配
        if (pItem && pItem != eItem && pItem->GetEntry() == item)
            count += pItem->GetCount(); // 累加堆叠数量
    }

    // 如果排除物品是宝石,需要统计镶嵌在装备中的宝石
    if (eItem && eItem->GetTemplate()->GemProperties)
    {
        // 第二遍遍历:检查有插槽的装备
        for (uint32 i=0; i < GetBagSize(); ++i)
        {
            pItem = m_bagslot[i];
            // 检查物品有宝石插槽
            if (pItem && pItem != eItem && pItem->GetTemplate()->Socket[0].Color)
                count += pItem->GetGemCountWithID(item); // 累加装备中镶嵌的该宝石数量
        }
    }

    return count;
}

/**
 * @brief 获取容器中属于指定限制类别的物品数量
 *
 * 统计容器中属于特定物品限制类别(如唯一装备、唯一账号等)的物品总数量。
 * 用于验证玩家是否超过限制类别物品的持有上限。
 *
 * @param limitCategory 限制类别ID
 * @param skipItem 要跳过的物品指针(通常用于排除正在操作的物品)
 *
 * @return 属于该限制类别的物品总数量
 *
 * @note 常见限制类别:
 *       - 唯一装备(unique equipped):玩家最多只能装备一定数量
 *       - 唯一账号(unique account):账号最多只能持有一定数量
 * @note 时间复杂度: O(n), n为容器槽位数量
 */
uint32 Bag::GetItemCountWithLimitCategory(uint32 limitCategory, Item* skipItem) const
{
    uint32 count = 0;

    // 遍历所有槽位,统计匹配限制类别的物品
    for (uint32 i = 0; i < GetBagSize(); ++i)
        if (Item* pItem = m_bagslot[i])
            if (pItem != skipItem) // 跳过指定物品
                if (ItemTemplate const* pProto = pItem->GetTemplate())
                    // 检查物品的限制类别是否匹配
                    if (pProto->ItemLimitCategory == limitCategory)
                        count += m_bagslot[i]->GetCount(); // 累加堆叠数量

    return count;
}

/**
 * @brief 根据物品GUID获取槽位索引
 *
 * 遍历容器槽位,查找指定GUID的物品所在的槽位。
 *
 * @param guid 要查找的物品GUID
 *
 * @return 如果找到物品返回槽位索引(0到GetBagSize()-1),未找到返回NULL_SLOT(255)
 *
 * @note 时间复杂度: O(n), n为容器槽位数量
 * @note 返回NULL_SLOT表示物品不在该容器中
 */
uint8 Bag::GetSlotByItemGUID(ObjectGuid guid) const
{
    // 遍历所有槽位,查找匹配GUID的物品
    for (uint32 i = 0; i < GetBagSize(); ++i)
        if (m_bagslot[i] != 0)
            if (m_bagslot[i]->GetGUID() == guid)
                return i; // 找到物品,返回槽位索引

    return NULL_SLOT; // 未找到,返回无效槽位
}

/**
 * @brief 根据槽位索引获取物品指针
 *
 * 返回指定槽位中的物品指针。如果槽位索引无效或槽位为空,返回nullptr。
 *
 * @param slot 槽位索引(0到GetBagSize()-1)
 *
 * @return 如果槽位有效且包含物品,返回物品指针;否则返回nullptr
 *
 * @note 此函数会验证槽位索引是否在有效范围内
 */
Item* Bag::GetItemByPos(uint8 slot) const
{
    // 检查槽位索引是否有效
    if (slot < GetBagSize())
        return m_bagslot[slot];

    return nullptr;
}

/**
 * @brief 获取容器的调试信息
 *
 * 返回容器的调试字符串,包含父类Item的调试信息。
 * 用于日志记录和问题排查。
 *
 * @return 包含调试信息的字符串
 *
 * @note 调试信息包括:
 *       - 物品GUID
 *       - 物品模板ID
 *       - 所有者信息
 *       - 其他基础属性
 */
std::string Bag::GetDebugInfo() const
{
    std::stringstream sstr;
    // 调用父类方法获取基础调试信息
    sstr << Item::GetDebugInfo();
    return sstr.str();
}
