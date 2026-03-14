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
 * @file GossipDef.h
 * @brief NPC 对话系统定义文件
 *
 * 本文件定义了 NPC 对话菜单系统的核心数据结构和类，包括：
 * - 对话选项类型枚举（Gossip_Option）
 * - 对话选项图标枚举（GossipOptionIcon）
 * - POI 图标枚举（Poi_Icon）
 * - 对话菜单项数据结构
 * - 对话菜单管理类（GossipMenu）
 * - 任务菜单管理类（QuestMenu）
 * - 玩家菜单综合管理类（PlayerMenu）
 *
 * 这些组件共同实现了玩家与 NPC 交互时的对话菜单系统，
 * 支持普通对话、任务接取、商品购买、飞行点选择等多种交互类型。
 */

#ifndef TRINITYCORE_GOSSIP_H
#define TRINITYCORE_GOSSIP_H

#include "Common.h"
#include "ObjectGuid.h"
#include "NPCHandler.h"
#include <map>

class Quest;
class WorldSession;

/**
 * @brief 对话菜单最大选项数量限制
 * 客户端限制每个对话菜单最多显示 32 个选项
 */
#define GOSSIP_MAX_MENU_ITEMS               32

/**
 * @brief 默认对话消息文本 ID
 * 当没有指定特定的 NPC 文本 ID 时使用此默认值
 */
#define DEFAULT_GOSSIP_MESSAGE              0xffffff

/**
 * @enum Gossip_Option
 * @brief NPC 对话选项类型枚举
 *
 * 定义了玩家与 NPC 交互时可用的所有对话选项类型。
 * 每种类型对应 NPC 的一种功能标志位（UNIT_NPC_FLAG_*）。
 * 当玩家点击对话选项时，系统根据此类型决定后续处理逻辑。
 */
enum Gossip_Option
{
    GOSSIP_OPTION_NONE              = 0,                    ///< 无选项（UNIT_NPC_FLAG_NONE）
    GOSSIP_OPTION_GOSSIP            = 1,                    ///< 普通对话（UNIT_NPC_FLAG_GOSSIP）
    GOSSIP_OPTION_QUESTGIVER        = 2,                    ///< 任务给予者（UNIT_NPC_FLAG_QUESTGIVER）
    GOSSIP_OPTION_VENDOR            = 3,                    ///< 商商（UNIT_NPC_FLAG_VENDOR）
    GOSSIP_OPTION_TAXIVENDOR        = 4,                    ///< 飞行管理员（UNIT_NPC_FLAG_TAXIVENDOR）
    GOSSIP_OPTION_TRAINER           = 5,                    ///< 训练师（UNIT_NPC_FLAG_TRAINER）
    GOSSIP_OPTION_SPIRITHEALER      = 6,                    ///< 灵魂治疗者（UNIT_NPC_FLAG_SPIRITHEALER）
    GOSSIP_OPTION_SPIRITGUIDE       = 7,                    ///< 灵魂向导（UNIT_NPC_FLAG_SPIRITGUIDE）
    GOSSIP_OPTION_INNKEEPER         = 8,                    ///< 旅店老板（UNIT_NPC_FLAG_INNKEEPER）
    GOSSIP_OPTION_BANKER            = 9,                    ///< 银行家（UNIT_NPC_FLAG_BANKER）
    GOSSIP_OPTION_PETITIONER        = 10,                   ///< 公会注册员（UNIT_NPC_FLAG_PETITIONER）
    GOSSIP_OPTION_TABARDDESIGNER    = 11,                   ///< 战袍设计师（UNIT_NPC_FLAG_TABARDDESIGNER）
    GOSSIP_OPTION_BATTLEFIELD       = 12,                   ///< 战场军官（UNIT_NPC_FLAG_BATTLEFIELDPERSON）
    GOSSIP_OPTION_AUCTIONEER        = 13,                   ///< 拍卖师（UNIT_NPC_FLAG_AUCTIONEER）
    GOSSIP_OPTION_STABLEPET         = 14,                   ///< 宠物管理员（UNIT_NPC_FLAG_STABLE）
    GOSSIP_OPTION_ARMORER           = 15,                   ///< 护甲修理师（UNIT_NPC_FLAG_ARMORER）
    GOSSIP_OPTION_UNLEARNTALENTS    = 16,                   ///< 重置天赋（训练师额外选项）
    GOSSIP_OPTION_UNLEARNPETTALENTS = 17,                   ///< 重置宠物天赋（训练师额外选项）
    GOSSIP_OPTION_LEARNDUALSPEC     = 18,                   ///< 学习双天赋（训练师额外选项）
    GOSSIP_OPTION_OUTDOORPVP        = 19,                   ///< 户外 PVP（代码动态添加的选项）
    GOSSIP_OPTION_DUALSPEC_INFO     = 20,                   ///< 双天赋信息（训练师额外选项）
    GOSSIP_OPTION_MAX                                       ///< 枚举边界值
};

/**
 * @enum GossipOptionIcon
 * @brief 对话选项图标枚举
 *
 * 定义对话选项在客户端界面中显示的图标类型。
 * 图标可以帮助玩家快速识别选项的功能类型。
 *
 * @note 值为 14 和 15 的图标无效，不应使用
 */
enum GossipOptionIcon : uint8
{
    GOSSIP_ICON_CHAT                = 0,                    ///< 白色对话气泡
    GOSSIP_ICON_VENDOR              = 1,                    ///< 棕色背包图标
    GOSSIP_ICON_TAXI                = 2,                    ///< 飞行标记（纸飞机形状）
    GOSSIP_ICON_TRAINER             = 3,                    ///< 棕色书籍（训练师）
    GOSSIP_ICON_INTERACT_1          = 4,                    ///< 金色交互齿轮
    GOSSIP_ICON_INTERACT_2          = 5,                    ///< 金色交互齿轮（变体）
    GOSSIP_ICON_MONEY_BAG           = 6,                    ///< 棕色背包（右下角有金币）
    GOSSIP_ICON_TALK                = 7,                    ///< 白色对话气泡（内含"..."）
    GOSSIP_ICON_TABARD              = 8,                    ///< 白色战袍图标
    GOSSIP_ICON_BATTLE              = 9,                    ///< 两把交叉的剑
    GOSSIP_ICON_DOT                 = 10,                   ///< 黄色圆点/标记
    GOSSIP_ICON_CHAT_11             = 11,                   ///< 白色对话气泡（变体）
    GOSSIP_ICON_CHAT_12             = 12,                   ///< 白色对话气泡（变体）
    GOSSIP_ICON_CHAT_13             = 13,                   ///< 白色对话气泡（变体）
    GOSSIP_ICON_UNK_14              = 14,                   ///< 无效图标 - 禁止使用
    GOSSIP_ICON_UNK_15              = 15,                   ///< 无效图标 - 禁止使用
    GOSSIP_ICON_CHAT_16             = 16,                   ///< 白色对话气泡（变体）
    GOSSIP_ICON_CHAT_17             = 17,                   ///< 白色对话气泡（变体）
    GOSSIP_ICON_CHAT_18             = 18,                   ///< 白色对话气泡（变体）
    GOSSIP_ICON_CHAT_19             = 19,                   ///< 白色对话气泡（变体）
    GOSSIP_ICON_CHAT_20             = 20,                   ///< 白色对话气泡（变体）
    GOSSIP_ICON_MAX                                         ///< 枚举边界值
};

/**
 * @enum Poi_Icon
 * @brief POI（Point of Interest，兴趣点）图标枚举
 *
 * 定义小地图和世界地图上显示的兴趣点标记图标。
 * 常用于战场、任务目标、重要地点等场景的地图标记。
 *
 * @note 此列表不完整，游戏中存在更多 POI 图标类型
 * @note 颜色前缀说明：Grey=灰色, Blue=蓝色, Red=红色, RW=红白, BW=蓝白
 */
enum Poi_Icon
{
    ICON_POI_BLANK              =   0,                      ///< 空白（不可见）
    ICON_POI_GREY_AV_MINE       =   1,                      ///< 灰色矿车（阿拉希盆地矿山）
    ICON_POI_RED_AV_MINE        =   2,                      ///< 红色矿车
    ICON_POI_BLUE_AV_MINE       =   3,                      ///< 蓝色矿车
    ICON_POI_BWTOMB             =   4,                      ///< 蓝白墓碑
    ICON_POI_SMALL_HOUSE        =   5,                      ///< 小房子
    ICON_POI_GREYTOWER          =   6,                      ///< 灰色塔楼
    ICON_POI_REDFLAG            =   7,                      ///< 红色旗帜（带黄色感叹号）
    ICON_POI_TOMBSTONE          =   8,                      ///< 普通墓碑（棕色）
    ICON_POI_BWTOWER            =   9,                      ///< 蓝白塔楼
    ICON_POI_REDTOWER           =   10,                     ///< 红色塔楼
    ICON_POI_BLUETOWER          =   11,                     ///< 蓝色塔楼
    ICON_POI_RWTOWER            =   12,                     ///< 红白塔楼
    ICON_POI_REDTOMB            =   13,                     ///< 红色墓碑
    ICON_POI_RWTOMB             =   14,                     ///< 红白墓碑
    ICON_POI_BLUETOMB           =   15,                     ///< 蓝色墓碑
    ICON_POI_16                 =   16,                     ///< 灰色图标（具体类型未知）
    ICON_POI_17                 =   17,                     ///< 蓝白图标（具体类型未知）
    ICON_POI_18                 =   18,                     ///< 蓝色图标（具体类型未知）
    ICON_POI_19                 =   19,                     ///< 红白图标（具体类型未知）
    ICON_POI_20                 =   20,                     ///< 红色图标（具体类型未知）
    ICON_POI_GREYLOGS           =   21,                     ///< 灰色木头
    ICON_POI_BWLOGS             =   22,                     ///< 蓝白木头
    ICON_POI_BLUELOGS           =   23,                     ///< 蓝色木头
    ICON_POI_RWLOGS             =   24,                     ///< 红白木头
    ICON_POI_REDLOGS            =   25,                     ///< 红色木头
    ICON_POI_26                 =   26,                     ///< 灰色图标（具体类型未知）
    ICON_POI_27                 =   27,                     ///< 蓝白图标（具体类型未知）
    ICON_POI_28                 =   28,                     ///< 蓝色图标（具体类型未知）
    ICON_POI_29                 =   29,                     ///< 红白图标（具体类型未知）
    ICON_POI_30                 =   30,                     ///< 红色图标（具体类型未知）
    ICON_POI_GREYHOUSE          =   31,                     ///< 灰色房屋
    ICON_POI_BWHOUSE            =   32,                     ///< 蓝白房屋
    ICON_POI_BLUEHOUSE          =   33,                     ///< 蓝色房屋
    ICON_POI_RWHOUSE            =   34,                     ///< 红白房屋
    ICON_POI_REDHOUSE           =   35,                     ///< 红色房屋
    ICON_POI_GREYHORSE          =   36,                     ///< 灰色马匹
    ICON_POI_BWHORSE            =   37,                     ///< 蓝白马匹
    ICON_POI_BLUEHORSE          =   38,                     ///< 蓝色马匹
    ICON_POI_RWHORSE            =   39,                     ///< 红白马匹
    ICON_POI_REDHORSE           =   40                      ///< 红色马匹
};

/**
 * @struct GossipMenuItem
 * @brief 对话菜单项数据结构
 *
 * 存储单个对话选项的所有显示和交互信息，
 * 包括图标、文本、发送者标识、选项类型等。
 */
struct GossipMenuItem
{
    uint8       MenuItemIcon;   ///< 菜单项图标（GossipOptionIcon 枚举值）
    bool        IsCoded;        ///< 是否为编码选项（用于特殊脚本处理）
    std::string Message;        ///< 菜单项显示文本
    uint32      Sender;         ///< 发送者标识（用于脚本识别来源）
    uint32      OptionType;     ///< 选项类型（Gossip_Option 枚举值）
    std::string BoxMessage;     ///< 确认对话框显示的文本
    uint32      BoxMoney;       ///< 确认对话框需要的金币数量
};

/**
 * @typedef GossipMenuItemContainer
 * @brief 对话菜单项容器类型
 *
 * 使用有序映射存储菜单项，确保菜单项按 ID 顺序显示。
 * 键为菜单项 ID，值为 GossipMenuItem 结构体。
 */
typedef std::map<uint32, GossipMenuItem> GossipMenuItemContainer;

/**
 * @struct GossipMenuItemData
 * @brief 对话菜单项关联数据
 *
 * 存储点击菜单项后触发的动作数据，
 * 包括跳转的目标菜单 ID 和 POI 标记。
 */
struct GossipMenuItemData
{
    uint32 GossipActionMenuId;  ///< 点击此选项后跳转到的对话菜单 ID
    uint32 GossipActionPoi;     ///< 点击此选项后显示的 POI（兴趣点）ID
};

/**
 * @typedef GossipMenuItemDataContainer
 * @brief 对话菜单项数据容器类型
 *
 * 使用有序映射存储菜单项关联数据。
 * 键为菜单项 ID，值为 GossipMenuItemData 结构体。
 */
typedef std::map<uint32, GossipMenuItemData> GossipMenuItemDataContainer;

/**
 * @struct QuestMenuItem
 * @brief 任务菜单项数据结构
 *
 * 存储单个任务选项的信息，
 * 用于 NPC 任务列表显示。
 */
struct QuestMenuItem
{
    uint32  QuestId;    ///< 任务 ID
    uint8   QuestIcon;  ///< 任务图标（任务状态图标）
};

/**
 * @typedef QuestMenuItemList
 * @brief 任务菜单项列表类型
 *
 * 使用向量存储任务菜单项，保持任务在列表中的顺序。
 */
typedef std::vector<QuestMenuItem> QuestMenuItemList;

/**
 * @class GossipMenu
 * @brief NPC 对话菜单管理类
 *
 * 负责管理 NPC 对话菜单的所有菜单项及其关联数据。
 * 提供菜单项的添加、查询、删除等功能。
 *
 * 对话菜单用于玩家与 NPC 交互时显示可选项列表，
 * 每个菜单项可以触发特定的动作或打开下一级菜单。
 */
class TC_GAME_API GossipMenu
{
    public:
        /**
         * @brief 构造函数
         * 初始化空的对话菜单
         */
        GossipMenu();

        /**
         * @brief 析构函数
         */
        ~GossipMenu();

        /**
         * @brief 添加菜单项（带完整参数）
         *
         * 添加一个新的对话菜单项，包含所有显示和交互参数。
         * 如果菜单项数量已达到上限（GOSSIP_MAX_MENU_ITEMS），则不再添加。
         *
         * @param menuItemId 菜单项 ID，如果为 -1 则自动分配下一个可用 ID
         * @param icon 菜单项图标类型
         * @param message 菜单项显示文本
         * @param sender 发送者标识（用于脚本识别来源）
         * @param action 动作类型/选项类型
         * @param boxMessage 确认对话框文本
         * @param boxMoney 确认对话框所需金币
         * @param coded 是否为编码选项，默认为 false
         * @return 返回实际分配的菜单项 ID；如果添加失败返回 0
         *
         * @note 当菜单项数量达到上限时，会在错误日志中记录
         */
        uint32 AddMenuItem(int32 menuItemId, GossipOptionIcon icon, std::string const& message, uint32 sender, uint32 action, std::string const& boxMessage, uint32 boxMoney, bool coded = false);

        /**
         * @brief 添加菜单项（从数据库加载）
         *
         * 从数据库数据添加菜单项，通常用于加载预定义的对话菜单。
         *
         * @param menuId 对话菜单 ID
         * @param menuItemId 菜单项 ID
         * @param sender 发送者标识
         * @param action 动作类型
         */
        void AddMenuItem(uint32 menuId, uint32 menuItemId, uint32 sender, uint32 action);

        /**
         * @brief 设置对话菜单 ID
         * @param menu_id 菜单 ID
         */
        void SetMenuId(uint32 menu_id) { _menuId = menu_id; }

        /**
         * @brief 获取对话菜单 ID
         * @return 当前菜单 ID
         */
        uint32 GetMenuId() const { return _menuId; }

        /**
         * @brief 设置发送者 GUID
         * @param guid 发送对话的 NPC 或游戏对象的 GUID
         */
        void SetSenderGUID(ObjectGuid guid) { _senderGUID = guid; }

        /**
         * @brief 获取发送者 GUID
         * @return 发送对话的实体 GUID
         */
        ObjectGuid GetSenderGUID() const { return _senderGUID; }

        /**
         * @brief 设置语言环境
         * @param locale 语言常量
         */
        void SetLocale(LocaleConstant locale) { _locale = locale; }

        /**
         * @brief 获取语言环境
         * @return 当前语言设置
         */
        LocaleConstant GetLocale() const { return _locale; }

        /**
         * @brief 添加菜单项关联数据
         *
         * 为指定的菜单项添加点击后触发的动作数据。
         *
         * @param menuItemId 菜单项 ID
         * @param gossipActionMenuId 触发的目标对话菜单 ID
         * @param gossipActionPoi 触发的 POI ID
         */
        void AddGossipMenuItemData(uint32 menuItemId, uint32 gossipActionMenuId, uint32 gossipActionPoi);

        /**
         * @brief 获取菜单项数量
         * @return 当前菜单中的选项数量
         */
        uint32 GetMenuItemCount() const
        {
            return _menuItems.size();
        }

        /**
         * @brief 检查菜单是否为空
         * @return 如果菜单没有任何选项，返回 true
         */
        bool Empty() const
        {
            return _menuItems.empty();
        }

        /**
         * @brief 获取指定 ID 的菜单项
         *
         * @param id 菜单项 ID
         * @return 如果找到，返回菜单项指针；否则返回 nullptr
         *
         * @note 返回的指针在菜单结构修改后可能失效
         */
        GossipMenuItem const* GetItem(uint32 id) const
        {
            GossipMenuItemContainer::const_iterator itr = _menuItems.find(id);
            if (itr != _menuItems.end())
                return &itr->second;

            return nullptr;
        }

        /**
         * @brief 获取菜单项关联数据
         *
         * @param indexId 菜单项索引 ID
         * @return 如果找到，返回菜单项数据指针；否则返回 nullptr
         *
         * @note 返回的指针在容器修改后可能失效
         */
        GossipMenuItemData const* GetItemData(uint32 indexId) const
        {
            GossipMenuItemDataContainer::const_iterator itr = _menuItemData.find(indexId);
            if (itr != _menuItemData.end())
                return &itr->second;

            return nullptr;
        }

        /**
         * @brief 获取菜单项的发送者标识
         * @param menuItemId 菜单项 ID
         * @return 发送者标识值；如果菜单项不存在返回 0
         */
        uint32 GetMenuItemSender(uint32 menuItemId) const;

        /**
         * @brief 获取菜单项的动作类型
         * @param menuItemId 菜单项 ID
         * @return 动作类型值；如果菜单项不存在返回 0
         */
        uint32 GetMenuItemAction(uint32 menuItemId) const;

        /**
         * @brief 检查菜单项是否为编码选项
         * @param menuItemId 菜单项 ID
         * @return 如果是编码选项返回 true；菜单项不存在返回 false
         */
        bool IsMenuItemCoded(uint32 menuItemId) const;

        /**
         * @brief 清空菜单
         *
         * 移除所有菜单项和关联数据。
         * 调用后菜单将变为空菜单。
         */
        void ClearMenu();

        /**
         * @brief 获取所有菜单项的只读引用
         * @return 菜单项容器的常量引用
         *
         * @note 用于遍历所有菜单项
         */
        GossipMenuItemContainer const& GetMenuItems() const
        {
            return _menuItems;
        }

    private:
        GossipMenuItemContainer _menuItems;       ///< 菜单项容器
        GossipMenuItemDataContainer _menuItemData;///< 菜单项关联数据容器
        uint32 _menuId;                           ///< 对话菜单 ID
        ObjectGuid _senderGUID;                   ///< 发送者 GUID
        LocaleConstant _locale;                   ///< 语言环境设置
};

/**
 * @class QuestMenu
 * @brief NPC 任务菜单管理类
 *
 * 负责管理 NPC 提供的任务列表菜单。
 * 玩家与任务 NPC 交互时，显示可接取或可提交的任务列表。
 *
 * 任务菜单通常与对话菜单配合使用，
 * 当 NPC 同时具有对话和任务功能时，会在同一界面中显示。
 */
class TC_GAME_API QuestMenu
{
    public:
        /**
         * @brief 构造函数
         * 初始化空的任务菜单
         */
        QuestMenu();

        /**
         * @brief 析构函数
         */
        ~QuestMenu();

        /**
         * @brief 添加任务菜单项
         *
         * 将一个任务添加到任务列表中显示。
         *
         * @param QuestId 任务 ID
         * @param Icon 任务状态图标
         */
        void AddMenuItem(uint32 QuestId, uint8 Icon);

        /**
         * @brief 清空任务菜单
         *
         * 移除所有任务菜单项。
         * 调用后菜单将变为空菜单。
         */
        void ClearMenu();

        /**
         * @brief 获取任务菜单项数量
         * @return 当前任务列表中的任务数量
         */
        uint8 GetMenuItemCount() const
        {
            return _questMenuItems.size();
        }

        /**
         * @brief 检查任务菜单是否为空
         * @return 如果没有任何任务，返回 true
         */
        bool Empty() const
        {
            return _questMenuItems.empty();
        }

        /**
         * @brief 检查是否包含指定任务
         * @param questId 要检查的任务 ID
         * @return 如果菜单中包含该任务，返回 true
         */
        bool HasItem(uint32 questId) const;

        /**
         * @brief 获取指定索引的任务菜单项
         *
         * @param index 任务在列表中的索引位置（从 0 开始）
         * @return 任务菜单项的常量引用
         *
         * @warning 调用前必须确保索引有效，否则会导致未定义行为
         */
        QuestMenuItem const& GetItem(uint16 index) const
        {
            return _questMenuItems[index];
        }

    private:
        QuestMenuItemList _questMenuItems;  ///< 任务菜单项列表
};

/**
 * @class PlayerMenu
 * @brief 玩家菜单综合管理类
 *
 * 为玩家会话提供统一的对话和任务菜单管理接口。
 * 该类封装了 GossipMenu 和 QuestMenu，并负责向客户端发送菜单数据包。
 *
 * 主要职责：
 * - 管理 NPC 对话菜单和任务菜单
 * - 向客户端发送菜单显示、关闭等数据包
 * - 处理任务相关的状态查询和详情展示
 */
class TC_GAME_API PlayerMenu
{
    public:
        /**
         * @brief 构造函数
         * @param session 玩家会话指针
         */
        explicit PlayerMenu(WorldSession* session);

        /**
         * @brief 析构函数
         */
        ~PlayerMenu();

        /**
         * @brief 获取对话菜单的引用
         * @return 对话菜单对象的可修改引用
         */
        GossipMenu& GetGossipMenu() { return _gossipMenu; }

        /**
         * @brief 获取任务菜单的引用
         * @return 任务菜单对象的可修改引用
         */
        QuestMenu& GetQuestMenu() { return _questMenu; }

        /**
         * @brief 检查两个菜单是否都为空
         * @return 如果对话菜单和任务菜单都为空，返回 true
         */
        bool Empty() const { return _gossipMenu.Empty() && _questMenu.Empty(); }

        /**
         * @brief 清空所有菜单
         *
         * 同时清空对话菜单和任务菜单中的所有内容。
         */
        void ClearMenus();

        /**
         * @brief 获取对话选项的发送者标识
         * @param selection 选择的菜单项 ID
         * @return 发送者标识值
         */
        uint32 GetGossipOptionSender(uint32 selection) const { return _gossipMenu.GetMenuItemSender(selection); }

        /**
         * @brief 获取对话选项的动作类型
         * @param selection 选择的菜单项 ID
         * @return 动作类型值
         */
        uint32 GetGossipOptionAction(uint32 selection) const { return _gossipMenu.GetMenuItemAction(selection); }

        /**
         * @brief 检查对话选项是否为编码选项
         * @param selection 选择的菜单项 ID
         * @return 如果是编码选项返回 true
         */
        bool IsGossipOptionCoded(uint32 selection) const { return _gossipMenu.IsMenuItemCoded(selection); }

        /**
         * @brief 发送对话菜单到客户端
         *
         * 构建并发送对话菜单数据包，显示 NPC 对话界面。
         *
         * @param titleTextId NPC 标题文本 ID（来自 npc_text 表）
         * @param objectGUID 对话对象的 GUID
         */
        void SendGossipMenu(uint32 titleTextId, ObjectGuid objectGUID);

        /**
         * @brief 发送关闭对话菜单数据包
         *
         * 通知客户端关闭当前显示的对话菜单。
         */
        void SendCloseGossip();

        /**
         * @brief 发送兴趣点（POI）数据
         *
         * 在客户端小地图/世界地图上显示指定的兴趣点标记。
         *
         * @param poiId 兴趣点 ID（来自 points_of_interest 表）
         */
        void SendPointOfInterest(uint32 poiId) const;

        /*********************************************************/
        /***                    任务系统                       ***/
        /*********************************************************/

        /**
         * @brief 发送任务给予者的状态
         *
         * 通知客户端该 NPC 相对于玩家的任务状态
         * （如：无可接任务、有可接任务、有可交任务等）。
         *
         * @param questStatus 任务状态标志
         * @param npcGUID NPC 的 GUID
         */
        void SendQuestGiverStatus(uint8 questStatus, ObjectGuid npcGUID) const;

        /**
         * @brief 发送任务给予者任务列表
         *
         * 显示 NPC 可提供的所有任务列表，
         * 包含 NPC 的表情动画和问候语。
         *
         * @param eEmote NPC 表情动作数据
         * @param Title 对话窗口标题文本
         * @param npcGUID NPC 的 GUID
         */
        void SendQuestGiverQuestList(QEmote const& eEmote, const std::string& Title, ObjectGuid npcGUID);

        /**
         * @brief 发送任务详细信息查询响应
         *
         * 发送任务的完整详细信息数据到客户端，
         * 包括任务目标、奖励、描述等。
         *
         * @param quest 任务对象指针
         */
        void SendQuestQueryResponse(Quest const* quest) const;

        /**
         * @brief 发送任务详情界面
         *
         * 显示任务的详细信息界面，
         * 包括任务描述、目标、奖励等，
         * 并可选择是否激活"接受"按钮。
         *
         * @param quest 任务对象指针
         * @param npcGUID NPC 的 GUID
         * @param activateAccept 是否激活任务接受按钮
         */
        void SendQuestGiverQuestDetails(Quest const* quest, ObjectGuid npcGUID, bool activateAccept) const;

        /**
         * @brief 发送任务奖励选择界面
         *
         * 当任务完成且有奖励可选时，
         * 显示任务奖励选择界面。
         *
         * @param quest 任务对象指针
         * @param npcGUID NPC 的 GUID
         * @param autoLaunched 是否为自动启动的任务完成流程
         */
        void SendQuestGiverOfferReward(Quest const* quest, ObjectGuid npcGUID, bool autoLaunched) const;

        /**
         * @brief 发送任务物品需求界面
         *
         * 显示任务完成所需的物品收集进度，
         * 以及是否可以完成任务。
         *
         * @param quest 任务对象指针
         * @param npcGUID NPC 的 GUID
         * @param canComplete 当前是否满足任务完成条件
         * @param closeOnCancel 取消时是否关闭对话框
         */
        void SendQuestGiverRequestItems(Quest const* quest, ObjectGuid npcGUID, bool canComplete, bool closeOnCancel) const;

    private:
        GossipMenu _gossipMenu;    ///< 对话菜单对象
        QuestMenu  _questMenu;     ///< 任务菜单对象
        WorldSession* _session;    ///< 玩家会话指针
};
#endif
