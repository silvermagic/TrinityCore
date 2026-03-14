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
 * @file Item.h
 * @brief 物品类定义头文件
 *
 * 本文件定义了游戏中所有物品的核心数据结构和接口。
 *
 * 主要组件：
 *   1. ItemSetEffect 结构体
 *      - 套装效果数据存储
 *      - 套装物品计数和奖励法术管理
 *
 *   2. EnchantmentOffset 枚举
 *      - 附魔数据字段偏移定义
 *      - 用于访问附魔ID、持续时间、充能次数
 *
 *   3. ItemUpdateState 枚举
 *      - 物品数据库同步状态
 *      - 控制物品如何与数据库同步
 *
 *   4. Item 类
 *      - 游戏中所有物品的基类
 *      - 管理物品实例的所有运行时数据
 *
 * Item 类主要功能模块：
 *   - 生命周期管理：创建、加载、保存、删除
 *   - 绑定系统：灵魂绑定、账号绑定、可交易状态
 *   - 附魔系统：永久附魔、临时附魔、宝石镶嵌
 *   - 耐久度系统：耐久度损耗、修复费用计算
 *   - 退还系统：购买后退还在时限内的物品
 *   - 灵魂绑定交易：拾取绑定物品限时交易
 *   - 随机属性：随机前缀/后缀属性管理
 *   - 战利品系统：可打开物品的战利品管理
 *
 * 继承关系：
 *   Object -> Item -> Bag（背包类物品）
 *
 * 数据库表：
 *   - item_instance: 物品实例数据
 *   - item_refund_instance: 退款数据
 *   - item_soulbound_trade_data: 灵魂绑定交易数据
 *   - character_inventory: 物品位置信息
 *
 * 关键设计模式：
 *   - 状态模式：ItemUpdateState 控制数据库同步策略
 *   - 观察者模式：物品状态变化通知更新队列
 *   - 工厂方法：CreateItem 静态方法创建物品实例
 *
 * 线程安全：
 *   - 物品对象主要在玩家线程中操作
 *   - 数据库操作使用事务保证原子性
 */

#ifndef TRINITYCORE_ITEM_H
#define TRINITYCORE_ITEM_H

#include "Object.h"
#include "Common.h"
#include "DatabaseEnvFwd.h"
#include "ItemDefines.h"
#include "ItemEnchantmentMgr.h"
#include "ItemTemplate.h"
#include "Loot.h"

class SpellInfo;
class Bag;
class Unit;

/**
 * @brief 套装效果结构体
 *
 * 用于存储物品套装的相关信息，包括套装ID、物品数量和套装提供的法术效果。
 */
struct ItemSetEffect
{
    uint32 setid;               ///< 套装ID
    uint32 item_count;          ///< 当前已装备的套装物品数量
    SpellInfo const* spells[8]; ///< 套装提供的法术效果数组（最多8个）
};

#define MAX_GEM_SOCKETS               MAX_ITEM_PROTO_SOCKETS// (BONUS_ENCHANTMENT_SLOT-SOCK_ENCHANTMENT_SLOT) and item proto size, equal value expected

/**
 * @brief 附魔偏移量枚举
 *
 * 定义附魔数据在字段中的偏移位置，用于访问附魔ID、持续时间和次数。
 */
enum EnchantmentOffset
{
    ENCHANTMENT_ID_OFFSET       = 0,  ///< 附魔ID偏移
    ENCHANTMENT_DURATION_OFFSET = 1,  ///< 附魔持续时间偏移
    ENCHANTMENT_CHARGES_OFFSET  = 2   ///< 附魔使用次数偏移（WLK版本扩展）
};

#define MAX_ENCHANTMENT_OFFSET    3

/**
 * @brief 物品更新状态枚举
 *
 * 定义物品在数据库同步过程中的各种状态。
 */
enum ItemUpdateState
{
    ITEM_UNCHANGED                               = 0, ///< 物品未更改
    ITEM_CHANGED                                 = 1, ///< 物品已更改，需要保存
    ITEM_NEW                                     = 2, ///< 新创建的物品，需要插入
    ITEM_REMOVED                                 = 3  ///< 物品已删除，需要从数据库移除
};

/**
 * @brief 检查物品是否可以放入背包
 * @param proto 物品模板
 * @param pBagProto 背包模板
 * @return 是否可以放入
 */
bool ItemCanGoIntoBag(ItemTemplate const* proto, ItemTemplate const* pBagProto);

/**
 * @brief 物品类
 *
 * 继承自Object类，是游戏中所有物品的基类。
 * 管理物品的基本属性、附魔、耐久度、套装效果、退还系统等功能。
 * 包括装备、消耗品、容器等各种类型的物品都使用此类或其派生类。
 */
class TC_GAME_API Item : public Object
{
    friend void AddItemToUpdateQueueOf(Item* item, Player* player);
    friend void RemoveItemFromUpdateQueueOf(Item* item, Player* player);

    public:
        /**
         * @brief 创建物品
         * @param itemEntry 物品ID
         * @param count 物品数量
         * @param owner 拥有者玩家（可选）
         * @return 创建的物品指针，失败返回nullptr
         */
        static Item* CreateItem(uint32 itemEntry, uint32 count, Player const* player = nullptr);

        /**
         * @brief 克隆物品
         * @param count 克隆的物品数量
         * @param player 目标玩家（可选）
         * @return 克隆的物品指针
         */
        Item* CloneItem(uint32 count, Player const* player = nullptr) const;

        /**
         * @brief 构造函数
         */
        Item();

        /**
         * @brief 创建物品对象
         * @param guidlow 低位GUID
         * @param itemId 物品ID
         * @param owner 拥有者玩家
         * @return 创建是否成功
         */
        virtual bool Create(ObjectGuid::LowType guidlow, uint32 itemId, Player const* owner);

        /**
         * @brief 获取物品模板
         * @return 物品模板指针
         */
        ItemTemplate const* GetTemplate() const;

        /**
         * @brief 获取拥有者GUID
         * @return 拥有者的GUID
         */
        ObjectGuid GetOwnerGUID()    const { return GetGuidValue(ITEM_FIELD_OWNER); }

        /**
         * @brief 设置拥有者GUID
         * @param guid 拥有者的GUID
         */
        void SetOwnerGUID(ObjectGuid guid) { SetGuidValue(ITEM_FIELD_OWNER, guid); }

        /**
         * @brief 获取拥有者玩家
         * @return 拥有者玩家指针，不存在则返回nullptr
         */
        Player* GetOwner()const;

        /**
         * @brief 设置绑定状态
         * @param val true为绑定，false为解绑
         */
        void SetBinding(bool val) { ApplyModFlag(ITEM_FIELD_FLAGS, ITEM_FIELD_FLAG_SOULBOUND, val); }

        /**
         * @brief 检查物品是否已灵魂绑定
         * @return 是否已绑定
         */
        bool IsSoulBound() const { return HasFlag(ITEM_FIELD_FLAGS, ITEM_FIELD_FLAG_SOULBOUND); }

        /**
         * @brief 检查物品是否为账号绑定
         * @return 是否账号绑定
         */
        bool IsBoundAccountWide() const { return GetTemplate()->HasFlag(ITEM_FLAG_IS_BOUND_TO_ACCOUNT); }

        /**
         * @brief 检查物品是否绑定给了其他玩家
         * @param player 要检查的玩家
         * @return 是否绑定给其他玩家
         */
        bool IsBindedNotWith(Player const* player) const;

        /**
         * @brief 检查物品是否因附魔而绑定
         * @return 是否因附魔绑定
         */
        bool IsBoundByEnchant() const;

        /**
         * @brief 保存物品数据到数据库
         * @param trans 数据库事务
         */
        virtual void SaveToDB(CharacterDatabaseTransaction trans);

        /**
         * @brief 从数据库加载物品数据
         * @param guid 物品GUID
         * @param owner_guid 拥有者GUID
         * @param fields 数据库字段
         * @param entry 物品ID
         * @return 加载是否成功
         */
        virtual bool LoadFromDB(ObjectGuid::LowType guid, ObjectGuid owner_guid, Field* fields, uint32 entry);

        /**
         * @brief 从数据库删除物品（静态方法）
         * @param trans 数据库事务
         * @param itemGuid 物品GUID
         */
        static void DeleteFromDB(CharacterDatabaseTransaction trans, ObjectGuid::LowType itemGuid);

        /**
         * @brief 从数据库删除物品
         * @param trans 数据库事务
         */
        virtual void DeleteFromDB(CharacterDatabaseTransaction trans);

        /**
         * @brief 从背包数据库表中删除物品记录（静态方法）
         * @param trans 数据库事务
         * @param itemGuid 物品GUID
         */
        static void DeleteFromInventoryDB(CharacterDatabaseTransaction trans, ObjectGuid::LowType itemGuid);

        /**
         * @brief 从背包数据库表中删除物品记录
         * @param trans 数据库事务
         */
        void DeleteFromInventoryDB(CharacterDatabaseTransaction trans);

        /**
         * @brief 保存退还数据到数据库
         */
        void SaveRefundDataToDB();

        /**
         * @brief 从数据库删除退还数据
         * @param trans 数据库事务指针
         */
        void DeleteRefundDataFromDB(CharacterDatabaseTransaction* trans);

        /**
         * @brief 转换为背包对象
         * @return 如果是背包则返回Bag指针，否则返回nullptr
         */
        Bag* ToBag() { if (IsBag()) return reinterpret_cast<Bag*>(this); else return nullptr; }

        /**
         * @brief 转换为背包对象（常量版本）
         * @return 如果是背包则返回Bag指针，否则返回nullptr
         */
        Bag const* ToBag() const { if (IsBag()) return reinterpret_cast<Bag const*>(this); else return nullptr; }

        /**
         * @brief 检查物品是否可退还
         * @return 是否可退还
         */
        bool IsRefundable() const { return HasFlag(ITEM_FIELD_FLAGS, ITEM_FIELD_FLAG_REFUNDABLE); }

        /**
         * @brief 检查物品是否可交易（拾取绑定物品在时限内可交易）
         * @return 是否可交易
         */
        bool IsBOPTradeable() const { return HasFlag(ITEM_FIELD_FLAGS, ITEM_FIELD_FLAG_BOP_TRADEABLE); }

        /**
         * @brief 检查物品是否已包装
         * @return 是否已包装
         */
        bool IsWrapped() const { return HasFlag(ITEM_FIELD_FLAGS, ITEM_FIELD_FLAG_WRAPPED); }

        /**
         * @brief 检查物品是否锁定
         * @return 是否锁定
         */
        bool IsLocked() const { return !HasFlag(ITEM_FIELD_FLAGS, ITEM_FIELD_FLAG_UNLOCKED); }

        /**
         * @brief 检查物品是否为背包
         * @return 是否为背包
         */
        bool IsBag() const { return GetTemplate()->InventoryType == INVTYPE_BAG; }

        /**
         * @brief 检查物品是否为货币代币
         * @return 是否为货币代币
         */
        bool IsCurrencyToken() const { return GetTemplate()->IsCurrencyToken(); }

        /**
         * @brief 检查是否为非空背包
         * @return 是否为非空背包
         */
        bool IsNotEmptyBag() const;

        /**
         * @brief 检查物品是否已损坏（耐久度为0）
         * @return 是否已损坏
         */
        bool IsBroken() const { return GetUInt32Value(ITEM_FIELD_MAXDURABILITY) > 0 && GetUInt32Value(ITEM_FIELD_DURABILITY) == 0; }

        /**
         * @brief 检查物品是否可以交易
         * @param mail 是否通过邮件
         * @param trade 是否通过交易窗口
         * @return 是否可以交易
         */
        bool CanBeTraded(bool mail = false, bool trade = false) const;

        /**
         * @brief 设置物品是否在交易中
         * @param b 是否在交易中
         */
        void SetInTrade(bool b = true) { mb_in_trade = b; }

        /**
         * @brief 检查物品是否在交易中
         * @return 是否在交易中
         */
        bool IsInTrade() const { return mb_in_trade; }

        /**
         * @brief 计算修理耐久度的费用
         * @param discount 折扣比例
         * @return 修理费用（铜币）
         */
        uint32 CalculateDurabilityRepairCost(float discount) const;

        /**
         * @brief 检查玩家是否有附魔所需的技能
         * @param player 玩家指针
         * @return 是否有所需技能
         */
        bool HasEnchantRequiredSkill(Player const* player) const;

        /**
         * @brief 获取附魔所需等级
         * @return 所需等级
         */
        uint32 GetEnchantRequiredLevel() const;

        /**
         * @brief 检查物品是否符合法术要求
         * @param spellInfo 法术信息
         * @return 是否符合要求
         */
        bool IsFitToSpellRequirements(SpellInfo const* spellInfo) const;

        /**
         * @brief 检查物品是否受限于其他地图或区域
         * @param cur_mapId 当前地图ID
         * @param cur_zoneId 当前区域ID
         * @return 是否受限于其他地图或区域
         */
        bool IsLimitedToAnotherMapOrZone(uint32 cur_mapId, uint32 cur_zoneId) const;

        /**
         * @brief 检查宝石是否匹配插槽
         * @return 是否匹配
         */
        bool GemsFitSockets() const;

        /**
         * @brief 获取物品数量
         * @return 物品堆叠数量
         */
        uint32 GetCount() const { return GetUInt32Value(ITEM_FIELD_STACK_COUNT); }

        /**
         * @brief 设置物品数量
         * @param value 新的数量值
         */
        void SetCount(uint32 value);

        /**
         * @brief 获取最大堆叠数量
         * @return 最大堆叠数量
         */
        uint32 GetMaxStackCount() const { return GetTemplate()->GetMaxStackSize(); }

        /**
         * @brief 获取指定ID宝石的数量
         * @param GemID 宝石ID
         * @return 宝石数量
         */
        uint8 GetGemCountWithID(uint32 GemID) const;

        /**
         * @brief 获取指定限制类别宝石的数量
         * @param limitCategory 限制类别
         * @return 宝石数量
         */
        uint8 GetGemCountWithLimitCategory(uint32 limitCategory) const;

        /**
         * @brief 检查物品是否可以与指定模板的物品部分合并
         * @param proto 物品模板
         * @return 合并结果代码
         */
        InventoryResult CanBeMergedPartlyWith(ItemTemplate const* proto) const;

        /**
         * @brief 获取物品所在槽位
         * @return 槽位索引
         */
        uint8 GetSlot() const {return m_slot;}

        /**
         * @brief 获取物品所在的容器（背包）
         * @return 容器指针，如果不在容器中则返回nullptr
         */
        Bag* GetContainer() { return m_container; }

        /**
         * @brief 获取物品所在的背包槽位
         * @return 背包槽位
         */
        uint8 GetBagSlot() const;

        /**
         * @brief 设置物品所在槽位
         * @param slot 槽位索引
         */
        void SetSlot(uint8 slot) { m_slot = slot; }

        /**
         * @brief 获取物品位置（背包槽位+物品槽位的组合）
         * @return 位置值（高字节为背包槽位，低字节为物品槽位）
         */
        uint16 GetPos() const { return uint16(GetBagSlot()) << 8 | GetSlot(); }

        /**
         * @brief 设置物品所在的容器（背包）
         * @param container 容器指针
         */
        void SetContainer(Bag* container) { m_container = container; }

        /**
         * @brief 检查物品是否在背包中
         * @return 是否在背包中
         */
        bool IsInBag() const { return m_container != nullptr; }

        /**
         * @brief 检查物品是否已装备
         * @return 是否已装备
         */
        bool IsEquipped() const;

        /**
         * @brief 获取物品所需的技能
         * @return 技能ID
         */
        uint32 GetSkill();

        /**
         * @brief 获取物品施放的技能
         * @return 技能ID
         */
        uint32 GetSpell();

        // RandomPropertyId (signed but stored as unsigned)
        /**
         * @brief 获取物品随机属性ID
         * @return 随机属性ID（有符号值，但存储为无符号）
         */
        int32 GetItemRandomPropertyId() const { return GetInt32Value(ITEM_FIELD_RANDOM_PROPERTIES_ID); }

        /**
         * @brief 获取物品后缀因子
         * @return 后缀因子值
         */
        uint32 GetItemSuffixFactor() const { return GetUInt32Value(ITEM_FIELD_PROPERTY_SEED); }

        /**
         * @brief 设置物品随机属性
         * @param randomPropId 随机属性ID
         */
        void SetItemRandomProperties(int32 randomPropId);

        /**
         * @brief 更新物品后缀因子
         */
        void UpdateItemSuffixFactor();

        /**
         * @brief 设置附魔
         * @param slot 附魔槽位
         * @param id 附魔ID
         * @param duration 持续时间
         * @param charges 使用次数
         * @param caster 施法者GUID
         */
        void SetEnchantment(EnchantmentSlot slot, uint32 id, uint32 duration, uint32 charges, ObjectGuid caster = ObjectGuid::Empty);

        /**
         * @brief 设置附魔持续时间
         * @param slot 附魔槽位
         * @param duration 持续时间
         * @param owner 拥有者玩家
         */
        void SetEnchantmentDuration(EnchantmentSlot slot, uint32 duration, Player* owner);

        /**
         * @brief 设置附魔使用次数
         * @param slot 附魔槽位
         * @param charges 使用次数
         */
        void SetEnchantmentCharges(EnchantmentSlot slot, uint32 charges);

        /**
         * @brief 清除附魔
         * @param slot 附魔槽位
         */
        void ClearEnchantment(EnchantmentSlot slot);

        /**
         * @brief 获取附魔ID
         * @param slot 附魔槽位
         * @return 附魔ID
         */
        uint32 GetEnchantmentId(EnchantmentSlot slot)       const { return GetUInt32Value(ITEM_FIELD_ENCHANTMENT_1_1 + slot*MAX_ENCHANTMENT_OFFSET + ENCHANTMENT_ID_OFFSET);}

        /**
         * @brief 获取附魔持续时间
         * @param slot 附魔槽位
         * @return 持续时间
         */
        uint32 GetEnchantmentDuration(EnchantmentSlot slot) const { return GetUInt32Value(ITEM_FIELD_ENCHANTMENT_1_1 + slot*MAX_ENCHANTMENT_OFFSET + ENCHANTMENT_DURATION_OFFSET);}

        /**
         * @brief 获取附魔使用次数
         * @param slot 附魔槽位
         * @return 使用次数
         */
        uint32 GetEnchantmentCharges(EnchantmentSlot slot)  const { return GetUInt32Value(ITEM_FIELD_ENCHANTMENT_1_1 + slot*MAX_ENCHANTMENT_OFFSET + ENCHANTMENT_CHARGES_OFFSET);}

        /**
         * @brief 获取物品文本
         * @return 文本字符串
         */
        std::string const& GetText() const { return m_text; }

        /**
         * @brief 设置物品文本
         * @param text 文本内容
         */
        void SetText(std::string const& text) { m_text = text; }

        /**
         * @brief 发送宝石插槽更新
         */
        void SendUpdateSockets();

        /**
         * @brief 发送时间更新
         * @param owner 拥有者玩家
         */
        void SendTimeUpdate(Player* owner);

        /**
         * @brief 更新物品持续时间
         * @param owner 拥有者玩家
         * @param diff 经过的时间（毫秒）
         */
        void UpdateDuration(Player* owner, uint32 diff);

        // spell charges (signed but stored as unsigned)
        /**
         * @brief 获取法术使用次数
         * @param index 索引（0-5）
         * @return 使用次数（有符号值，但存储为无符号）
         */
        int32 GetSpellCharges(uint8 index/*0..5*/ = 0) const { return GetInt32Value(ITEM_FIELD_SPELL_CHARGES + index); }

        /**
         * @brief 设置法术使用次数
         * @param index 索引（0-5）
         * @param value 使用次数
         */
        void SetSpellCharges(uint8 index/*0..5*/, int32 value) { SetInt32Value(ITEM_FIELD_SPELL_CHARGES + index, value); }

        Loot loot;              ///< 物品战利品数据
        bool m_lootGenerated;   ///< 战利品是否已生成

        // Update States
        /**
         * @brief 获取物品更新状态
         * @return 更新状态
         */
        ItemUpdateState GetState() const { return uState; }

        /**
         * @brief 设置物品更新状态
         * @param state 新状态
         * @param forplayer 相关玩家
         */
        void SetState(ItemUpdateState state, Player* forplayer = nullptr);

        /**
         * @brief 检查物品是否在更新队列中
         * @return 是否在更新队列中
         */
        bool IsInUpdateQueue() const { return uQueuePos != -1; }

        /**
         * @brief 获取在更新队列中的位置
         * @return 队列位置
         */
        uint16 GetQueuePos() const { return uQueuePos; }

        /**
         * @brief 强制设置更新状态
         * @param state 新状态
         */
        void FSetState(ItemUpdateState state)               // forced
        {
            uState = state;
        }

        /**
         * @brief 检查物品是否触发指定任务
         * @param quest_id 任务ID
         * @return 是否触发
         */
        bool hasQuest(uint32 quest_id) const override { return GetTemplate()->StartQuest == quest_id; }

        /**
         * @brief 检查物品是否涉及指定任务
         * @param quest_id 任务ID
         * @return 是否涉及（物品不涉及任务，始终返回false）
         */
        bool hasInvolvedQuest(uint32 /*quest_id*/) const override { return false; }

        /**
         * @brief 检查物品是否为药水
         * @return 是否为药水
         */
        bool IsPotion() const { return GetTemplate()->IsPotion(); }

        /**
         * @brief 检查物品是否为武器羊皮纸
         * @return 是否为武器羊皮纸
         */
        bool IsWeaponVellum() const { return GetTemplate()->IsWeaponVellum(); }

        /**
         * @brief 检查物品是否为护甲羊皮纸
         * @return 是否为护甲羊皮纸
         */
        bool IsArmorVellum() const { return GetTemplate()->IsArmorVellum(); }

        /**
         * @brief 检查物品是否为魔法制造的消耗品
         * @return 是否为魔法制造的消耗品
         */
        bool IsConjuredConsumable() const { return GetTemplate()->IsConjuredConsumable(); }

        // Item Refund system
        /**
         * @brief 设置物品为不可退还
         * @param owner 拥有者玩家
         * @param changestate 是否更改状态
         * @param trans 数据库事务指针
         */
        void SetNotRefundable(Player* owner, bool changestate = true, CharacterDatabaseTransaction* trans = nullptr);

        /**
         * @brief 设置退还接收者
         * @param pGuidLow 接收者的低位GUID
         */
        void SetRefundRecipient(ObjectGuid::LowType pGuidLow) { m_refundRecipient = pGuidLow; }

        /**
         * @brief 设置已支付的金钱
         * @param money 金钱数量（铜币）
         */
        void SetPaidMoney(uint32 money) { m_paidMoney = money; }

        /**
         * @brief 设置已支付的扩展成本
         * @param iece 扩展成本ID
         */
        void SetPaidExtendedCost(uint32 iece) { m_paidExtendedCost = iece; }

        /**
         * @brief 获取退还接收者
         * @return 接收者的低位GUID
         */
        uint32 GetRefundRecipient() const { return m_refundRecipient; }

        /**
         * @brief 获取已支付的金钱
         * @return 金钱数量（铜币）
         */
        uint32 GetPaidMoney() const { return m_paidMoney; }

        /**
         * @brief 获取已支付的扩展成本
         * @return 扩展成本ID
         */
        uint32 GetPaidExtendedCost() const { return m_paidExtendedCost; }

        /**
         * @brief 更新已游玩时间
         * @param owner 拥有者玩家
         */
        void UpdatePlayedTime(Player* owner);

        /**
         * @brief 获取已游玩时间
         * @return 游玩时间（秒）
         */
        uint32 GetPlayedTime();

        /**
         * @brief 检查退还是否已过期
         * @return 是否已过期
         */
        bool IsRefundExpired();

        // Soulbound trade system
        /**
         * @brief 设置灵魂绑定物品为可交易状态
         * @param allowedLooters 允许交易的玩家GUID集合
         */
        void SetSoulboundTradeable(GuidSet const& allowedLooters);

        /**
         * @brief 清除灵魂绑定物品的可交易状态
         * @param currentOwner 当前拥有者
         */
        void ClearSoulboundTradeable(Player* currentOwner);

        /**
         * @brief 检查灵魂绑定物品交易是否过期
         * @return 是否已过期
         */
        bool CheckSoulboundTradeExpire();

        /**
         * @brief 构建更新数据
         * @param updateDataMap 更新数据映射
         */
        void BuildUpdate(UpdateDataMapType&) override;

        /**
         * @brief 添加到对象更新队列
         * @return 是否成功
         */
        bool AddToObjectUpdate() override;

        /**
         * @brief 从对象更新队列移除
         */
        void RemoveFromObjectUpdate() override;

        /**
         * @brief 获取脚本ID
         * @return 脚本ID
         */
        uint32 GetScriptId() const { return GetTemplate()->ScriptId; }

        /**
         * @brief 获取调试信息
         * @return 调试信息字符串
         */
        std::string GetDebugInfo() const override;

    private:
        std::string m_text;             ///< 物品文本内容（如礼物附带的文字）
        uint8 m_slot;                   ///< 物品所在槽位索引
        Bag* m_container;               ///< 物品所在的容器（背包）指针
        ItemUpdateState uState;         ///< 物品更新状态
        int16 uQueuePos;                ///< 在更新队列中的位置（-1表示不在队列中）
        bool mb_in_trade;               ///< 物品是否正在交易窗口中
        time_t m_lastPlayedTimeUpdate;  ///< 上次游玩时间更新的时间戳
        ObjectGuid::LowType m_refundRecipient;  ///< 退还接收者的低位GUID
        uint32 m_paidMoney;             ///< 购买时支付的金钱（铜币）
        uint32 m_paidExtendedCost;      ///< 购买时支付的扩展成本ID
        GuidSet allowedGUIDs;           ///< 允许交易的玩家GUID集合（用于拾取绑定物品限时交易）
};
#endif
