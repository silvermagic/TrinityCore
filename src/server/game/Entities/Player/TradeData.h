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
 * @file TradeData.h
 * @brief 玩家交易数据管理模块
 *
 * 本模块负责管理玩家之间的交易会话数据，包括：
 * - 交易物品槽位管理（支持6个交易槽位 + 1个非交易槽位）
 * - 交易金额管理
 * - 交易施法管理（对非交易槽位物品施放法术）
 * - 交易状态管理（接受状态、处理流程状态）
 * - 交易数据同步与更新
 *
 * 交易流程：
 * 1. 玩家发起交易请求，创建 TradeData 对象
 * 2. 双方添加物品、金币到交易槽位
 * 3. 可选：对非交易槽位物品施放法术（如附魔）
 * 4. 双方确认交易
 * 5. 交易完成或取消
 */

#ifndef TradeData_h__
#define TradeData_h__

#include "ObjectGuid.h"

/**
 * @brief 交易槽位枚举
 *
 * 定义交易界面中的槽位数量和类型
 */
enum TradeSlots
{
    TRADE_SLOT_COUNT          = 7,  ///< 总槽位数量（6个交易槽位 + 1个非交易槽位）
    TRADE_SLOT_TRADED_COUNT   = 6,  ///< 可交易槽位数量（0-5号槽位，双方可见）
    TRADE_SLOT_NONTRADED      = 6,  ///< 非交易槽位索引（第7个槽位，仅自己可见，用于附魔等操作）
    TRADE_SLOT_INVALID        = -1  ///< 无效槽位标识
};

class Item;
class Player;

/**
 * @class TradeData
 * @brief 玩家交易数据管理类
 *
 * 负责管理单个玩家在一笔交易中的所有数据，包括交易物品、金币、施法信息和交易状态。
 * 每笔交易会在两个玩家身上各创建一个 TradeData 对象，通过 GetTraderData() 获取对方的数据。
 *
 * 关键职责：
 * - 管理交易槽位中的物品（包括交易槽位和非交易槽位）
 * - 管理交易金币数量
 * - 管理对非交易槽位物品施放的法术
 * - 维护交易接受状态和处理流程状态
 * - 自动同步交易数据更新给交易双方
 *
 * 线程安全：
 * - 所有操作应在玩家会话线程中调用
 * - 不支持多线程并发访问
 *
 * 生命周期：
 * - 交易开始时由 Player 对象创建
 * - 交易完成或取消时销毁
 */
class TC_GAME_API TradeData
{
public:
    /**
     * @brief 构造函数，初始化交易数据
     * @param player 发起交易的玩家（本方玩家）
     * @param trader 交易对象玩家（对方玩家）
     *
     * 初始化所有交易数据为默认状态：
     * - 接受状态为 false
     * - 处理流程状态为 false
     * - 金币数量为 0
     * - 法术 ID 为 0
     * - 法术施放物品为空
     */
    TradeData(Player* player, Player* trader) :
        _player(player), _trader(trader), _accepted(false), _acceptProccess(false),
        _money(0), _spell(0), _spellCastItem() { }

    /**
     * @brief 获取交易对象玩家
     * @return 返回对方玩家的指针
     *
     * 调用时机：需要获取交易对象信息时
     */
    Player* GetTrader() const { return _trader; }

    /**
     * @brief 获取交易对象的交易数据
     * @return 返回对方玩家的 TradeData 对象指针
     *
     * 调用时机：需要访问或修改对方交易数据时
     * 性能注意：直接访问对方 Player 对象，时间复杂度 O(1)
     */
    TradeData* GetTraderData() const;

    /**
     * @brief 获取指定槽位的物品
     * @param slot 交易槽位索引
     * @return 返回物品指针，若槽位为空则返回 nullptr
     *
     * 调用时机：需要查看或操作交易槽位中的物品时
     * 性能注意：通过 GUID 查找物品，时间复杂度 O(n)，n 为玩家背包物品数量
     */
    Item* GetItem(TradeSlots slot) const;

    /**
     * @brief 检查交易中是否包含指定物品
     * @param itemGuid 物品的全局唯一标识符
     * @return 若物品在交易槽位中返回 true，否则返回 false
     *
     * 调用时机：验证物品是否已在交易中
     * 性能注意：线性遍历所有槽位，时间复杂度 O(TRADE_SLOT_COUNT)
     */
    bool HasItem(ObjectGuid itemGuid) const;

    /**
     * @brief 根据物品 GUID 查找其所在的交易槽位
     * @param itemGuid 物品的全局唯一标识符
     * @return 返回槽位索引，若未找到返回 TRADE_SLOT_INVALID
     *
     * 调用时机：需要确定物品在哪个交易槽位时
     * 性能注意：线性遍历所有槽位，时间复杂度 O(TRADE_SLOT_COUNT)
     */
    TradeSlots GetTradeSlotForItem(ObjectGuid itemGuid) const;

    /**
     * @brief 设置交易槽位中的物品
     * @param slot 目标槽位索引
     * @param item 要放入的物品指针，可为 nullptr（清空槽位）
     * @param update 是否强制更新，默认为 false
     *
     * 当槽位物品发生变化时：
     * 1. 更新槽位物品 GUID
     * 2. 重置双方的接受状态
     * 3. 发送更新包给双方
     * 4. 若修改的是非交易槽位，清除对方可能施放的法术
     * 5. 清除自己可能施放的法术
     *
     * 调用时机：玩家添加、移除或替换交易槽位物品时
     * 性能注意：会触发网络包发送和状态重置，应避免频繁调用
     */
    void SetItem(TradeSlots slot, Item* item, bool update = false);

    /**
     * @brief 获取对非交易槽位施放的法术 ID
     * @return 返回法术 ID，若未施放法术则返回 0
     *
     * 调用时机：查询对交易物品施放的法术时
     */
    uint32 GetSpell() const { return _spell; }

    /**
     * @brief 设置对非交易槽位施放的法术
     * @param spell_id 法术 ID，设为 0 表示清除法术
     * @param castItem 施法使用的物品（如附魔材料），默认为 nullptr
     *
     * 当法术信息变化时：
     * 1. 更新法术 ID 和施法物品
     * 2. 重置双方的接受状态
     * 3. 发送更新包给物品拥有者（对方）和施法者（自己）
     *
     * 调用时机：玩家对非交易槽位物品施放法术或取消法术时
     * 性能注意：会发送两次更新包，确保双方都能看到法术信息
     */
    void SetSpell(uint32 spell_id, Item* castItem = nullptr);

    /**
     * @brief 获取施法使用的物品
     * @return 返回施法物品指针，若无则返回 nullptr
     *
     * 调用时机：需要获取施法材料信息时
     * 性能注意：通过 GUID 查找物品
     */
    Item*  GetSpellCastItem() const;

    /**
     * @brief 检查是否存在施法物品
     * @return 若有施法物品返回 true，否则返回 false
     *
     * 调用时机：快速判断是否有施法物品
     * 性能注意：时间复杂度 O(1)
     */
    bool HasSpellCastItem() const { return !_spellCastItem.IsEmpty(); }

    /**
     * @brief 获取交易中的金币数量
     * @return 返回金币数量（铜币单位）
     *
     * 调用时机：查询或验证交易金额时
     */
    uint32 GetMoney() const { return _money; }

    /**
     * @brief 设置交易中的金币数量
     * @param money 金币数量（铜币单位）
     *
     * 设置流程：
     * 1. 检查金币数量是否变化
     * 2. 验证玩家是否拥有足够的金币
     * 3. 更新金币数量
     * 4. 重置双方的接受状态
     * 5. 发送更新包给对方
     *
     * 若金币不足，会发送错误消息并取消本次设置
     *
     * 调用时机：玩家修改交易金币数量时
     * 性能注意：会触发金币验证和状态更新，应避免频繁调用
     */
    void SetMoney(uint32 money);

    /**
     * @brief 检查玩家是否已接受交易
     * @return 若已接受返回 true，否则返回 false
     *
     * 调用时机：检查交易确认状态时
     */
    bool IsAccepted() const { return _accepted; }

    /**
     * @brief 设置交易接受状态
     * @param state 接受状态，true 为接受，false 为取消接受
     * @param forTrader 是否发送消息给对方玩家，默认为 false
     *
     * 当取消接受时，会发送 TRADE_STATUS_BACK_TO_TRADE 消息通知相应玩家
     *
     * 调用时机：玩家点击接受或取消交易时
     * 性能注意：取消接受时会发送网络包
     */
    void SetAccepted(bool state, bool forTrader = false);

    /**
     * @brief 检查是否处于交易接受处理流程中
     * @return 若正在处理接受流程返回 true，否则返回 false
     *
     * 用于防止在交易处理过程中重复触发逻辑
     *
     * 调用时机：验证交易状态或防止重复处理时
     */
    bool IsInAcceptProcess() const { return _acceptProccess; }

    /**
     * @brief 设置交易接受处理流程状态
     * @param state 处理流程状态
     *
     * 在交易确认流程开始时设为 true，流程结束或取消时设为 false
     *
     * 调用时机：交易确认流程控制
     */
    void SetInAcceptProcess(bool state) { _acceptProccess = state; }

private:
    /**
     * @brief 发送交易更新数据包
     * @param for_trader 是否发送给对方玩家，true 为发送给对方，false 为发送给自己
     *
     * 调用时机：交易数据发生变化需要同步时，由其他成员函数内部调用
     * 性能注意：会触发网络包发送，应避免频繁调用
     */
    void Update(bool for_trader = true) const;

    Player*    _player;                                ///< 拥有此 TradeData 的玩家（本方玩家）
    Player*    _trader;                                ///< 与本方玩家进行交易的玩家（对方玩家）

    bool       _accepted;                              ///< 本方玩家是否已按下接受按钮
    bool       _acceptProccess;                        ///< 是否正在处理交易接受流程（防止重复处理）

    uint32     _money;                                 ///< 本方玩家放入交易的金币数量（铜币单位）

    uint32     _spell;                                 ///< 对非交易槽位物品施放的法术 ID
    ObjectGuid _spellCastItem;                         ///< 施放法术使用的物品 GUID（如附魔材料）

    ObjectGuid _items[TRADE_SLOT_COUNT];               ///< 交易槽位中的物品 GUID 数组，包括非交易槽位
};

#endif // TradeData_h__
