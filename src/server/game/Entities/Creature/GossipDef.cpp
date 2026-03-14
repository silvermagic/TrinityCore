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
 * @file GossipDef.cpp
 * @brief 闲聊菜单系统实现文件
 *
 * 本文件实现了游戏中的闲聊(对话)菜单系统，包括：
 * - GossipMenu：管理 NPC 对话菜单选项
 * - QuestMenu：管理任务菜单
 * - PlayerMenu：玩家菜单的统一接口，整合闲聊和任务菜单
 *
 * 闲聊系统是玩家与 NPC 交互的核心机制，支持：
 * - 菜单选项的动态添加和本地化
 * - 任务发布和奖励展示
 * - 兴趣点(POI)导航
 * - 密码保护和金钱验证功能
 */

#include "GossipDef.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "QuestPackets.h"
#include "World.h"
#include "WorldSession.h"

/**
 * @brief GossipMenu 构造函数
 *
 * 初始化闲聊菜单对象，设置默认值：
 * - 菜单 ID 为 0
 * - 区域语言为默认语言
 * - 发送者 GUID 为空
 */
GossipMenu::GossipMenu()
{
    _menuId = 0;
    _locale = DEFAULT_LOCALE;
    _senderGUID.Clear();
}

/**
 * @brief GossipMenu 析构函数
 *
 * 清理菜单资源，调用 ClearMenu() 清空所有菜单项和数据
 */
GossipMenu::~GossipMenu()
{
    ClearMenu();
}

/**
 * @brief 添加闲聊菜单项
 *
 * 向闲聊菜单添加一个新的选项，支持自定义图标、消息文本和验证功能
 *
 * @param menuItemId 菜单项 ID，-1 表示自动分配 ID
 * @param icon 菜单项图标类型（参见 GossipOptionIcon 枚举）
 * @param message 菜单项显示文本
 * @param sender 发送者标识符，用于脚本区分不同的菜单来源
 * @param action 动作类型，传递给 OnGossipHello 回调的自定义动作值
 * @param boxMessage 弹出确认框的提示消息（用于密码或金钱验证）
 * @param boxMoney 需要支付的金钱数量（以铜币为单位）
 * @param coded 是否需要密码验证（true 时弹出密码输入框）
 * @return uint32 返回实际使用的菜单项 ID
 *
 * @note 菜单项数量不能超过 GOSSIP_MAX_MENU_ITEMS (32) 个
 * @note 当 menuItemId 为 -1 时，系统会自动查找可用的 ID：
 *       1. 优先从数据库中获取当前菜单 ID 的最大值
 *       2. 然后检查现有菜单项，确保 ID 不冲突
 *       3. 最终分配一个唯一的菜单项 ID
 *
 * 性能说明：
 * - 自动 ID 分配需要遍历数据库记录和现有菜单项，相对较慢
 * - 如果知道确切的 ID，建议直接指定以提高性能
 */
uint32 GossipMenu::AddMenuItem(int32 menuItemId, GossipOptionIcon icon, std::string const& message, uint32 sender, uint32 action, std::string const& boxMessage, uint32 boxMoney, bool coded /*= false*/)
{
    // 断言检查：菜单项数量不能超过最大限制
    ASSERT(_menuItems.size() <= GOSSIP_MAX_MENU_ITEMS);

    // 脚本情况：自动查找可用的菜单项 ID
    if (menuItemId == -1)
    {
        menuItemId = 0;
        if (_menuId)
        {
            // 从数据库获取当前菜单 ID 的所有菜单项，找到最大的 OptionID
            // 这样可以确保脚本添加的菜单项 ID 不会与数据库中的冲突
            auto [itr, end] = sObjectMgr->GetGossipMenuItemsMapBounds(_menuId);
            itr = std::max_element(itr, end, [](GossipMenuItemsContainer::value_type const& a, GossipMenuItemsContainer::value_type const& b)
            {
                return a.second.OptionID < b.second.OptionID;
            });
            if (itr != end)
                menuItemId = itr->second.OptionID + 1;
        }

        // 进一步检查当前已存在的菜单项，确保 ID 唯一
        if (!_menuItems.empty())
        {
            for (GossipMenuItemContainer::const_iterator itr = _menuItems.begin(); itr != _menuItems.end(); ++itr)
            {
                if (int32(itr->first) > menuItemId)
                    break;

                menuItemId = itr->first + 1;
            }
        }
    }

    // 创建并填充菜单项数据
    GossipMenuItem& menuItem = _menuItems[menuItemId];

    menuItem.MenuItemIcon    = icon;          // 菜单项图标
    menuItem.Message         = message;       // 显示文本
    menuItem.IsCoded         = coded;         // 是否密码保护
    menuItem.Sender          = sender;        // 发送者标识
    menuItem.OptionType      = action;        // 动作类型
    menuItem.BoxMessage      = boxMessage;    // 确认框消息
    menuItem.BoxMoney        = boxMoney;      // 所需金钱
    return menuItemId;
}

/**
 * @brief 从数据库添加本地化的闲聊菜单项
 *
 * 根据菜单 ID 和菜单项 ID 从数据库加载闲聊选项，自动处理本地化文本
 *
 * @param menuId 闲聊菜单 ID（对应 gossip_menu 表）
 * @param menuItemId 菜单项 ID（对应 gossip_menu_option 表）
 * @param sender 发送者标识符，用于脚本区分菜单来源
 * @param action 自定义动作值，传递给 OnGossipHello 回调
 *
 * @note 此方法会自动处理本地化：
 *       1. 优先使用广播文本(BroadcastText)进行本地化
 *       2. 如果没有广播文本，则从 gossip_menu_option_locale 表获取翻译
 *       3. 支持 BoxMessage 和 OptionText 两种文本的本地化
 *
 * @see AddMenuItem(int32, GossipOptionIcon, std::string const&, uint32, uint32, std::string const&, uint32, bool)
 */
void GossipMenu::AddMenuItem(uint32 menuId, uint32 menuItemId, uint32 sender, uint32 action)
{
    // 从数据库查找指定菜单 ID 的所有菜单项
    GossipMenuItemsMapBounds bounds = sObjectMgr->GetGossipMenuItemsMapBounds(menuId);
    auto itr = std::find_if(bounds.first, bounds.second, [menuItemId](std::pair<uint32 const, GossipMenuItems> const& itemPair)
    {
        return itemPair.second.OptionID == menuItemId;
    });

    // 如果找不到菜单项，直接返回
    if (itr == bounds.second)
        return;

    // 存储需要本地化的文本
    std::string strOptionText, strBoxText;
    BroadcastText const* optionBroadcastText = sObjectMgr->GetBroadcastText(itr->second.OptionBroadcastTextID);
    BroadcastText const* boxBroadcastText = sObjectMgr->GetBroadcastText(itr->second.BoxBroadcastTextID);

    // 处理选项文本(OptionText)的本地化
    if (optionBroadcastText)
        strOptionText = optionBroadcastText->GetText(GetLocale());
    else
        strOptionText = itr->second.OptionText;

    // 处理确认框文本(BoxText)的本地化
    if (boxBroadcastText)
        strBoxText = boxBroadcastText->GetText(GetLocale());
    else
        strBoxText = itr->second.BoxText;

    // 如果当前语言不是默认语言，尝试从本地化表获取翻译
    if (GetLocale() != DEFAULT_LOCALE)
    {
        if (!optionBroadcastText)
        {
            // 从 gossip_menu_option_locale 表获取选项文本的本地化版本
            if (GossipMenuItemsLocale const* gossipMenuLocale = sObjectMgr->GetGossipMenuItemsLocale(menuId, menuItemId))
                ObjectMgr::GetLocaleString(gossipMenuLocale->OptionText, GetLocale(), strOptionText);
        }

        if (!boxBroadcastText)
        {
            // 从 gossip_menu_option_locale 表获取确认框文本的本地化版本
            if (GossipMenuItemsLocale const* gossipMenuLocale = sObjectMgr->GetGossipMenuItemsLocale(menuId, menuItemId))
                ObjectMgr::GetLocaleString(gossipMenuLocale->BoxText, GetLocale(), strBoxText);
        }
    }

    // 使用重载的 AddMenuItem 方法添加菜单项，菜单项 ID -1 也用于 ADD_GOSSIP_ITEM 宏
    AddMenuItem(itr->second.OptionID, itr->second.OptionIcon, strOptionText, sender, action, strBoxText, itr->second.BoxMoney, itr->second.BoxCoded);
    // 添加菜单项的附加数据（关联菜单和 POI）
    AddGossipMenuItemData(itr->second.OptionID, itr->second.ActionMenuID, itr->second.ActionPoiID);
}

/**
 * @brief 添加闲聊菜单项的附加数据
 *
 * 为菜单项添加关联数据，包括跳转菜单 ID 和兴趣点 ID
 *
 * @param menuItemId 菜单项 ID
 * @param gossipActionMenuId 点击此菜单项后跳转到的菜单 ID
 * @param gossipActionPoi 点击此菜单项后显示的兴趣点 ID
 *
 * @note 此方法用于存储菜单项的动作数据，与 GossipMenuItem 本身分离存储
 */
void GossipMenu::AddGossipMenuItemData(uint32 menuItemId, uint32 gossipActionMenuId, uint32 gossipActionPoi)
{
    GossipMenuItemData& itemData = _menuItemData[menuItemId];

    itemData.GossipActionMenuId  = gossipActionMenuId;  // 关联的菜单 ID
    itemData.GossipActionPoi     = gossipActionPoi;     // 关联的兴趣点 ID
}

/**
 * @brief 获取菜单项的发送者标识
 *
 * @param menuItemId 菜单项 ID
 * @return uint32 发送者标识，如果菜单项不存在则返回 0
 *
 * @note 发送者标识用于脚本中区分不同来源的菜单选项
 */
uint32 GossipMenu::GetMenuItemSender(uint32 menuItemId) const
{
    GossipMenuItemContainer::const_iterator itr = _menuItems.find(menuItemId);
    if (itr == _menuItems.end())
        return 0;

    return itr->second.Sender;
}

/**
 * @brief 获取菜单项的动作类型
 *
 * @param menuItemId 菜单项 ID
 * @return uint32 动作类型值，如果菜单项不存在则返回 0
 *
 * @note 动作类型是自定义值，传递给脚本回调使用
 */
uint32 GossipMenu::GetMenuItemAction(uint32 menuItemId) const
{
    GossipMenuItemContainer::const_iterator itr = _menuItems.find(menuItemId);
    if (itr == _menuItems.end())
        return 0;

    return itr->second.OptionType;
}

/**
 * @brief 检查菜单项是否需要密码验证
 *
 * @param menuItemId 菜单项 ID
 * @return bool 如果需要密码验证返回 true，否则返回 false
 *
 * @note 密码验证的菜单项在点击时会弹出密码输入框
 */
bool GossipMenu::IsMenuItemCoded(uint32 menuItemId) const
{
    GossipMenuItemContainer::const_iterator itr = _menuItems.find(menuItemId);
    if (itr == _menuItems.end())
        return false;

    return itr->second.IsCoded;
}

/**
 * @brief 清空菜单
 *
 * 清除所有菜单项和菜单项数据，重置菜单到初始状态
 */
void GossipMenu::ClearMenu()
{
    _menuItems.clear();
    _menuItemData.clear();
}

/**
 * @brief PlayerMenu 构造函数
 *
 * 创建玩家菜单对象，初始化会话和语言设置
 *
 * @param session 玩家的世界会话指针，用于获取玩家信息和发送数据包
 *
 * @note 构造时会自动设置闲聊菜单的语言为玩家会话的数据库语言
 */
PlayerMenu::PlayerMenu(WorldSession* session) : _session(session)
{
    if (_session)
        _gossipMenu.SetLocale(_session->GetSessionDbLocaleIndex());
}

/**
 * @brief PlayerMenu 析构函数
 *
 * 清理菜单资源，调用 ClearMenus() 清空所有菜单
 */
PlayerMenu::~PlayerMenu()
{
    ClearMenus();
}

/**
 * @brief 清空所有菜单
 *
 * 清空闲聊菜单和任务菜单，重置到初始状态
 */
void PlayerMenu::ClearMenus()
{
    _gossipMenu.ClearMenu();
    _questMenu.ClearMenu();
}

/**
 * @brief 发送闲聊菜单数据包给客户端
 *
 * 构建并发送 SMSG_GOSSIP_MESSAGE 数据包，包含闲聊选项和任务列表
 *
 * @param titleTextId 标题文本 ID（对应页面标题）
 * @param objectGUID 发送闲聊菜单的对象 GUID（通常是 NPC）
 *
 * 数据包结构：
 * - ObjectGUID: NPC 的 GUID
 * - MenuId: 菜单 ID（2.4.0 版本新增）
 * - TitleTextId: 标题文本 ID
 * - GossipItems: 闲聊菜单项列表（最多 16 个）
 *   - MenuItemId: 菜单项 ID
 *   - Icon: 图标类型
 *   - IsCoded: 是否密码保护
 *   - BoxMoney: 所需金钱
 *   - Message: 选项文本
 *   - BoxMessage: 确认框文本
 * - QuestItems: 任务菜单项列表（最多 32 个）
 *   - QuestID: 任务 ID
 *   - QuestIcon: 任务图标
 *   - QuestLevel: 任务等级
 *   - QuestFlags: 任务标志
 *   - IsAutoComplete: 是否自动完成（影响图标显示）
 *   - Title: 任务标题（支持本地化）
 *
 * @note 任务列表会根据玩家语言进行本地化处理
 */
void PlayerMenu::SendGossipMenu(uint32 titleTextId, ObjectGuid objectGUID)
{
    _gossipMenu.SetSenderGUID(objectGUID);

    WorldPacket data(SMSG_GOSSIP_MESSAGE, 100);         // 预估数据包大小
    data << uint64(objectGUID);
    data << uint32(_gossipMenu.GetMenuId());            // 2.4.0 版本新增字段
    data << uint32(titleTextId);
    data << uint32(_gossipMenu.GetMenuItemCount());     // 最大数量 0x10 (16)

    // 写入闲聊菜单项
    for (GossipMenuItemContainer::const_iterator itr = _gossipMenu.GetMenuItems().begin(); itr != _gossipMenu.GetMenuItems().end(); ++itr)
    {
        GossipMenuItem const& item = itr->second;
        data << uint32(itr->first);
        data << uint8(item.MenuItemIcon);
        data << uint8(item.IsCoded);                    // 是否弹出密码输入框
        data << uint32(item.BoxMoney);                  // 打开菜单所需的金钱，2.0.3 版本
        data << item.Message;                           // 闲聊选项文本
        data << item.BoxMessage;                        // 确认框文本（与金钱相关），2.0.3 版本
    }

    // 写入任务菜单项
    size_t count_pos = data.wpos();
    data << uint32(0);                                  // 任务数量占位符，最大 0x20 (32)
    uint32 count = 0;
    for (uint8 i = 0; i < _questMenu.GetMenuItemCount(); ++i)
    {
        QuestMenuItem const& item = _questMenu.GetItem(i);
        uint32 questID = item.QuestId;
        if (Quest const* quest = sObjectMgr->GetQuestTemplate(questID))
        {
            ++count;
            data << uint32(questID);
            data << uint32(item.QuestIcon);
            data << int32(quest->GetQuestLevel());
            data << uint32(quest->GetFlags()); // 3.3.3 版本的任务标志
            // 3.3.3 版本图标变化：0=黄色感叹号，1=蓝色问号
            data << uint8(quest->IsAutoComplete() && quest->IsRepeatable() && !quest->IsDailyOrWeekly() && !quest->IsMonthly());
            std::string title = quest->GetTitle();

            // 任务标题本地化处理
            LocaleConstant localeConstant = _session->GetSessionDbLocaleIndex();
            if (localeConstant != LOCALE_enUS)
                if (QuestLocale const* localeData = sObjectMgr->GetQuestLocale(questID))
                    ObjectMgr::GetLocaleString(localeData->Title, localeConstant, title);

            data << title;                                  // 最大长度 0x200
        }
    }

    // 回填实际的任务数量
    data.put<uint8>(count_pos, count);
    _session->SendPacket(&data);
}

/**
 * @brief 发送关闭闲聊菜单数据包
 *
 * 发送 SMSG_GOSSIP_COMPLETE 数据包，通知客户端关闭闲聊窗口
 *
 * @note 此方法会清空发送者 GUID
 */
void PlayerMenu::SendCloseGossip()
{
    _gossipMenu.SetSenderGUID(ObjectGuid::Empty);

    WorldPacket data(SMSG_GOSSIP_COMPLETE, 0);
    _session->SendPacket(&data);
}

/**
 * @brief 发送兴趣点(POI)数据包
 *
 * 发送 SMSG_GOSSIP_POI 数据包，在地图上显示一个兴趣点标记
 *
 * @param id 兴趣点 ID
 *
 * 数据包结构：
 * - Flags: POI 标志
 * - PositionX: X 坐标
 * - PositionY: Y 坐标
 * - Icon: 图标类型
 * - Importance: 重要性等级
 * - Name: POI 名称（支持本地化）
 *
 * @note 如果 POI 不存在，会记录错误日志并忽略请求
 */
void PlayerMenu::SendPointOfInterest(uint32 id) const
{
    PointOfInterest const* poi = sObjectMgr->GetPointOfInterest(id);
    if (!poi)
    {
        TC_LOG_ERROR("sql.sql", "Request to send non-existing POI (Id: {}), ignored.", id);
        return;
    }

    std::string name = poi->Name;
    // POI 名称本地化处理
    LocaleConstant localeConstant = _session->GetSessionDbLocaleIndex();
    if (localeConstant != LOCALE_enUS)
        if (PointOfInterestLocale const* localeData = sObjectMgr->GetPointOfInterestLocale(id))
            ObjectMgr::GetLocaleString(localeData->Name, localeConstant, name);

    WorldPacket data(SMSG_GOSSIP_POI, 4 + 4 + 4 + 4 + 4 + 10);  // 预估数据包大小
    data << uint32(poi->Flags);
    data << float(poi->PositionX);
    data << float(poi->PositionY);
    data << uint32(poi->Icon);
    data << uint32(poi->Importance);
    data << name;

    _session->SendPacket(&data);
}

/*********************************************************/
/***                    任务系统                        ***/
/*********************************************************/

/**
 * @brief QuestMenu 构造函数
 *
 * 初始化任务菜单，预分配容器空间以提高性能
 *
 * @note 预留 16 个元素空间，这是大多数情况下的菜单大小，
 *       可以减少 push_back 时的内存重新分配次数
 */
QuestMenu::QuestMenu()
{
    _questMenuItems.reserve(16);                                   // 预分配空间以优化性能
}

/**
 * @brief QuestMenu 析构函数
 *
 * 清理任务菜单资源
 */
QuestMenu::~QuestMenu()
{
    ClearMenu();
}

/**
 * @brief 添加任务菜单项
 *
 * 向任务菜单添加一个新的任务选项
 *
 * @param QuestId 任务 ID
 * @param Icon 任务图标类型
 *
 * @note 任务菜单项数量不能超过 GOSSIP_MAX_MENU_ITEMS (32) 个
 * @note 如果任务模板不存在，则不会添加
 */
void QuestMenu::AddMenuItem(uint32 QuestId, uint8 Icon)
{
    // 验证任务模板是否存在
    if (!sObjectMgr->GetQuestTemplate(QuestId))
        return;

    // 断言检查：菜单项数量不能超过最大限制
    ASSERT(_questMenuItems.size() <= GOSSIP_MAX_MENU_ITEMS);

    QuestMenuItem questMenuItem;

    questMenuItem.QuestId        = QuestId;    // 任务 ID
    questMenuItem.QuestIcon      = Icon;       // 任务图标

    _questMenuItems.push_back(questMenuItem);
}

/**
 * @brief 检查任务是否存在于菜单中
 *
 * @param questId 任务 ID
 * @return bool 如果任务存在于菜单中返回 true，否则返回 false
 */
bool QuestMenu::HasItem(uint32 questId) const
{
    for (QuestMenuItemList::const_iterator i = _questMenuItems.begin(); i != _questMenuItems.end(); ++i)
        if (i->QuestId == questId)
            return true;

    return false;
}

/**
 * @brief 清空任务菜单
 *
 * 清除所有任务菜单项，重置到初始状态
 */
void QuestMenu::ClearMenu()
{
    _questMenuItems.clear();
}

/**
 * @brief 发送任务给予者任务列表数据包
 *
 * 构建并发送 SMSG_QUESTGIVER_QUEST_LIST 数据包，显示 NPC 可提供的任务列表
 *
 * @param eEmote 表情动画信息（延迟和类型）
 * @param Title 对话框标题（当没有 QuestGreeting 时使用）
 * @param guid 任务给予者的 GUID
 *
 * 数据包结构：
 * - GUID: 任务给予者 GUID
 * - Greeting/Title: 问候文本或标题（支持本地化）
 * - EmoteDelay: 表情延迟时间
 * - EmoteType: 表情类型
 * - QuestCount: 任务数量
 * - QuestItems: 任务列表
 *   - QuestID: 任务 ID
 *   - QuestIcon: 任务图标
 *   - QuestLevel: 任务等级
 *   - QuestFlags: 任务标志
 *   - IsAutoComplete: 是否自动完成
 *   - Title: 任务标题
 *
 * @note 优先使用 QuestGreeting 表中的问候语，支持本地化
 */
void PlayerMenu::SendQuestGiverQuestList(QEmote const& eEmote, const std::string& Title, ObjectGuid guid)
{
    WorldPacket data(SMSG_QUESTGIVER_QUEST_LIST, 100);    // 预估数据包大小
    data << uint64(guid);

    // 尝试从数据库获取任务问候语
    if (QuestGreeting const* questGreeting = sObjectMgr->GetQuestGreeting(guid))
    {
        std::string strGreeting = questGreeting->greeting;

        // 问候语文本本地化处理
        LocaleConstant localeConstant = _session->GetSessionDbLocaleIndex();
        if (localeConstant != LOCALE_enUS)
            if (QuestGreetingLocale const* questGreetingLocale = sObjectMgr->GetQuestGreetingLocale(MAKE_PAIR32(guid.GetEntry(), guid.GetTypeId())))
                ObjectMgr::GetLocaleString(questGreetingLocale->greeting, localeConstant, strGreeting);

        data << strGreeting;
        data << uint32(questGreeting->greetEmoteDelay);
        data << uint32(questGreeting->greetEmoteType);
    }
    else
    {
        // 使用传入的标题和表情
        data << Title;
        data << uint32(eEmote._Delay);                         // 玩家表情延迟
        data << uint32(eEmote._Emote);                         // NPC 表情类型
    }

    // 写入任务列表
    size_t count_pos = data.wpos();
    data << uint8(0);
    uint32 count = 0;
    for (uint32 i = 0; i < _questMenu.GetMenuItemCount(); ++i)
    {
        QuestMenuItem const& questMenuItem = _questMenu.GetItem(i);

        uint32 questID = questMenuItem.QuestId;

        if (Quest const* quest = sObjectMgr->GetQuestTemplate(questID))
        {
            ++count;
            std::string title = quest->GetTitle();

            // 任务标题本地化处理
            LocaleConstant localeConstant = _session->GetSessionDbLocaleIndex();
            if (localeConstant != LOCALE_enUS)
                if (QuestLocale const* questTemplateLocaleData = sObjectMgr->GetQuestLocale(questID))
                    ObjectMgr::GetLocaleString(questTemplateLocaleData->Title, localeConstant, title);

            data << uint32(questID);
            data << uint32(questMenuItem.QuestIcon);
            data << int32(quest->GetQuestLevel());
            data << uint32(quest->GetFlags()); // 3.3.3 版本任务标志
            // 3.3.3 版本图标变化：0=黄色感叹号，1=蓝色问号
            data << uint8(quest->IsAutoComplete() && quest->IsRepeatable() && !quest->IsDailyOrWeekly() && !quest->IsMonthly());
            data << title;
        }
    }

    // 回填实际的任务数量
    data.put<uint8>(count_pos, count);
    _session->SendPacket(&data);

    TC_LOG_DEBUG("network", "WORLD: Sent SMSG_QUESTGIVER_QUEST_LIST (QuestGiver: {})", guid.ToString());
}

/**
 * @brief 发送任务给予者状态数据包
 *
 * 发送 SMSG_QUESTGIVER_STATUS 数据包，告知客户端 NPC 的任务状态
 *
 * @param questStatus 任务状态（如：可接取、进行中、可完成等）
 * @param npcGUID NPC 的 GUID
 *
 * @note 任务状态用于在 NPC 头顶显示不同的图标（黄色感叹号、灰色问号等）
 */
void PlayerMenu::SendQuestGiverStatus(uint8 questStatus, ObjectGuid npcGUID) const
{
    WorldPacket data(SMSG_QUESTGIVER_STATUS, 9);
    data << uint64(npcGUID);
    data << uint8(questStatus);

    _session->SendPacket(&data);
    TC_LOG_DEBUG("network", "WORLD: Sent SMSG_QUESTGIVER_STATUS NPC={}, status={}", npcGUID.ToString(), questStatus);
}

/**
 * @brief 发送任务详情数据包
 *
 * 构建并发送任务详情数据包，显示任务的完整信息（标题、详情、目标等）
 *
 * @param quest 任务对象指针
 * @param npcGUID 任务给予者的 GUID
 * @param activateAccept 是否自动激活接受按钮
 *
 * 数据包内容：
 * - Title: 任务标题（支持本地化）
 * - Details: 任务详情描述（支持本地化）
 * - Objectives: 任务目标描述（支持本地化）
 * - QuestGiverGUID: 任务给予者 GUID
 * - InformUnit: 共享任务的玩家 GUID
 * - QuestID: 任务 ID
 * - AutoLaunched: 是否自动启动
 * - Flags: 任务标志
 * - SuggestedGroupNum: 建议组队人数
 * - Rewards: 任务奖励
 * - DescEmotes: 详情页的表情动画
 *
 * @note 如果服务器配置了 CONFIG_QUEST_IGNORE_AUTO_ACCEPT，则会移除 QUEST_FLAGS_AUTO_ACCEPT 标志
 */
void PlayerMenu::SendQuestGiverQuestDetails(Quest const* quest, ObjectGuid npcGUID, bool activateAccept) const
{
    WorldPackets::Quest::QuestGiverQuestDetails packet;

    packet.Title = quest->GetTitle();
    packet.Details = quest->GetDetails();
    packet.Objectives = quest->GetObjectives();

    // 任务文本本地化处理
    LocaleConstant localeConstant = _session->GetSessionDbLocaleIndex();
    if (localeConstant != LOCALE_enUS)
    {
        if (QuestLocale const* localeData = sObjectMgr->GetQuestLocale(quest->GetQuestId()))
        {
            ObjectMgr::GetLocaleString(localeData->Title,      localeConstant, packet.Title);
            ObjectMgr::GetLocaleString(localeData->Details,    localeConstant, packet.Details);
            ObjectMgr::GetLocaleString(localeData->Objectives, localeConstant, packet.Objectives);
        }
    }

    packet.QuestGiverGUID = npcGUID;
    packet.InformUnit = _session->GetPlayer()->GetPlayerSharingQuest();  // 共享任务的玩家
    packet.QuestID = quest->GetQuestId();
    packet.AutoLaunched = activateAccept;
    // 根据配置决定是否移除自动接受标志
    packet.Flags = quest->GetFlags() & (sWorld->getBoolConfig(CONFIG_QUEST_IGNORE_AUTO_ACCEPT) ? ~QUEST_FLAGS_AUTO_ACCEPT : ~0);
    packet.SuggestedGroupNum = quest->GetSuggestedPlayers();

    // 构建任务奖励数据
    quest->BuildQuestRewards(packet.Rewards, _session->GetPlayer());

    // 添加详情页的表情动画
    packet.DescEmotes.reserve(QUEST_EMOTE_COUNT);
    for (uint32 i = 0; i < QUEST_EMOTE_COUNT; ++i)
        packet.DescEmotes.emplace_back(quest->DetailsEmote[i], quest->DetailsEmoteDelay[i]);

    _session->SendPacket(packet.Write());

    TC_LOG_DEBUG("network", "WORLD: Sent SMSG_QUEST_GIVER_QUEST_DETAILS NPC={}, questid={}", npcGUID.ToString(), quest->GetQuestId());
}

/**
 * @brief 发送任务查询响应数据包
 *
 * 发送 SMSG_QUEST_QUERY_RESPONSE 数据包，包含任务的完整数据
 *
 * @param quest 任务对象指针
 *
 * @note 根据配置决定使用缓存数据还是动态构建：
 *       - 如果启用了 CONFIG_CACHE_DATA_QUERIES，直接发送预缓存的数据
 *       - 否则动态构建查询数据包
 *
 * @note 缓存模式性能更好，但会占用更多内存
 */
void PlayerMenu::SendQuestQueryResponse(Quest const* quest) const
{
    if (sWorld->getBoolConfig(CONFIG_CACHE_DATA_QUERIES))
        // 使用缓存的任务查询数据
        _session->SendPacket(&quest->QueryData[static_cast<uint32>(_session->GetSessionDbLocaleIndex())]);
    else
    {
        // 动态构建任务查询数据
        WorldPacket queryPacket = quest->BuildQueryData(_session->GetSessionDbLocaleIndex());
        _session->SendPacket(&queryPacket);
    }

    TC_LOG_DEBUG("network", "WORLD: Sent SMSG_QUEST_QUERY_RESPONSE questid={}", quest->GetQuestId());
}

/**
 * @brief 发送任务奖励展示数据包
 *
 * 构建并发送任务完成时的奖励展示界面
 *
 * @param quest 任务对象指针
 * @param npcGUID 任务给予者的 GUID
 * @param autoLaunched 是否自动启动
 *
 * 数据包内容：
 * - Title: 任务标题（支持本地化）
 * - RewardText: 奖励文本（支持本地化）
 * - QuestGiverGUID: 任务给予者 GUID
 * - QuestID: 任务 ID
 * - AutoLaunched: 是否自动启动
 * - Flags: 任务标志
 * - SuggestedGroupNum: 建议组队人数
 * - Emotes: NPC 的表情动画列表
 * - Rewards: 任务奖励列表
 *
 * @note 奖励文本从 QuestOfferReward 表获取，支持本地化
 */
void PlayerMenu::SendQuestGiverOfferReward(Quest const* quest, ObjectGuid npcGUID, bool autoLaunched) const
{
    WorldPackets::Quest::QuestGiverOfferRewardMessage packet;

    packet.Title = quest->GetTitle();
    packet.RewardText = quest->GetOfferRewardText();

    // 任务文本本地化处理
    LocaleConstant localeConstant = _session->GetSessionDbLocaleIndex();
    if (localeConstant != LOCALE_enUS)
    {
        if (QuestLocale const* localeData = sObjectMgr->GetQuestLocale(quest->GetQuestId()))
            ObjectMgr::GetLocaleString(localeData->Title, localeConstant, packet.Title);

        if (QuestOfferRewardLocale const* questOfferRewardLocale = sObjectMgr->GetQuestOfferRewardLocale(quest->GetQuestId()))
            ObjectMgr::GetLocaleString(questOfferRewardLocale->RewardText, localeConstant, packet.RewardText);
    }

    packet.QuestGiverGUID = npcGUID;
    packet.QuestID = quest->GetQuestId();
    packet.AutoLaunched = autoLaunched;
    packet.Flags = quest->GetFlags();
    packet.SuggestedGroupNum = quest->GetSuggestedPlayers();

    // 添加 NPC 的表情动画（最多 4 个）
    for (uint32 i = 0; i < QUEST_EMOTE_COUNT && quest->OfferRewardEmote[i]; ++i)
        packet.Emotes.emplace_back(quest->OfferRewardEmote[i], quest->OfferRewardEmoteDelay[i]);

    // 构建任务奖励数据（包括可选奖励）
    quest->BuildQuestRewards(packet.Rewards, _session->GetPlayer(), true);

    _session->SendPacket(packet.Write());

    TC_LOG_DEBUG("network", "WORLD: Sent SMSG_QUEST_GIVER_OFFER_REWARD NPC={}, questid={}", npcGUID.ToString(), quest->GetQuestId());
}

/**
 * @brief 发送任务物品需求请求数据包
 *
 * 构建并发送 SMSG_QUESTGIVER_REQUEST_ITEMS 数据包，显示任务所需的物品列表
 *
 * @param quest 任务对象指针
 * @param npcGUID 任务给予者的 GUID
 * @param canComplete 任务是否可以完成
 * @param closeOnCancel 取消时是否关闭窗口
 *
 * 数据包结构：
 * - NPCGUID: NPC 的 GUID
 * - QuestID: 任务 ID
 * - Title: 任务标题（支持本地化）
 * - CompletionText: 完成文本（支持本地化）
 * - Unknown: 未知字段
 * - Emote: 表情（完成或未完成）
 * - CloseOnCancel: 取消时关闭标志
 * - QuestFlags: 任务标志
 * - SuggestedGroupNum: 建议组队人数
 * - RequiredMoney: 所需金钱
 * - ItemCount: 所需物品数量
 * - Items: 物品列表
 *   - ItemID: 物品 ID
 *   - Count: 所需数量
 *   - DisplayID: 物品显示 ID
 * - Buttons: 按钮标志（根据是否可完成决定）
 *
 * 优化说明：
 * - 如果任务不需要物品且可以完成，直接跳转到奖励界面
 * - 减少不必要的网络通信
 *
 * @note 物品需求界面仅在有物品需求时才会显示
 */
void PlayerMenu::SendQuestGiverRequestItems(Quest const* quest, ObjectGuid npcGUID, bool canComplete, bool closeOnCancel) const
{
    // 如果任务不需要物品且可以完成，直接跳转到奖励界面
    // 这是一种优化，避免显示空的物品需求界面

    std::string questTitle = quest->GetTitle();
    std::string requestItemsText = quest->GetRequestItemsText();

    // 任务文本本地化处理
    LocaleConstant localeConstant = _session->GetSessionDbLocaleIndex();
    if (localeConstant != LOCALE_enUS)
    {
        if (QuestLocale const* localeData = sObjectMgr->GetQuestLocale(quest->GetQuestId()))
            ObjectMgr::GetLocaleString(localeData->Title,            localeConstant, questTitle);

        if (QuestRequestItemsLocale const* questRequestItemsLocale = sObjectMgr->GetQuestRequestItemsLocale(quest->GetQuestId()))
            ObjectMgr::GetLocaleString(questRequestItemsLocale->CompletionText, localeConstant, requestItemsText);
    }

    // 优化：如果没有物品需求且可以完成，直接发送奖励界面
    if (!quest->GetReqItemsCount() && canComplete)
    {
        SendQuestGiverOfferReward(quest, npcGUID, true);
        return;
    }

    WorldPacket data(SMSG_QUESTGIVER_REQUEST_ITEMS, 50);    // 预估数据包大小
    data << uint64(npcGUID);
    data << uint32(quest->GetQuestId());
    data << questTitle;
    data << requestItemsText;

    data << uint32(0);                                   // 未知字段

    // 根据是否可完成选择表情
    if (canComplete)
        data << quest->GetCompleteEmote();
    else
        data << quest->GetIncompleteEmote();

    // 取消时是否关闭窗口
    data << uint32(closeOnCancel);

    data << uint32(quest->GetFlags());                      // 3.3.3 版本任务标志
    data << uint32(quest->GetSuggestedPlayers());           // 建议组队人数

    // 所需金钱（如果任务需要金钱）
    data << uint32(quest->GetRewOrReqMoney() < 0 ? -quest->GetRewOrReqMoney() : 0);

    // 写入所需物品列表
    data << uint32(quest->GetReqItemsCount());
    for (int i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
    {
        if (!quest->RequiredItemId[i])
            continue;

        data << uint32(quest->RequiredItemId[i]);
        data << uint32(quest->RequiredItemCount[i]);

        // 获取物品的显示 ID
        if (ItemTemplate const* itemTemplate = sObjectMgr->GetItemTemplate(quest->RequiredItemId[i]))
            data << uint32(itemTemplate->DisplayInfoID);
        else
            data << uint32(0);
    }

    // 根据是否可完成设置按钮标志
    // 0x00: 只显示取消按钮
    // 0x03: 显示完成和取消按钮
    if (!canComplete)
        data << uint32(0x00);
    else
        data << uint32(0x03);

    // 额外的按钮标志
    data << uint32(0x04);
    data << uint32(0x08);
    data << uint32(0x10);

    _session->SendPacket(&data);
    TC_LOG_DEBUG("network", "WORLD: Sent SMSG_QUESTGIVER_REQUEST_ITEMS NPC={}, questid={}", npcGUID.ToString(), quest->GetQuestId());
}
