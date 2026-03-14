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
 * @file cs_ahbot.cpp
 * @brief 拍卖行机器人(Auction House Bot)命令模块
 *
 * 本模块实现了拍卖行机器人相关的管理命令。拍卖行机器人是一个自动化系统，
 * 用于在拍卖行中自动上架物品，模拟真实的经济环境。
 *
 * 主要功能包括：
 * - 设置各品质物品的数量配置
 * - 设置各拍卖行（联盟/部落/中立）的物品比例
 * - 重建拍卖行物品
 * - 重新加载配置
 * - 查看拍卖行机器人状态
 *
 * 拍卖行系统说明：
 * - 联盟拍卖行(AUCTION_HOUSE_ALLIANCE): 主要服务于联盟玩家
 * - 部落拍卖行(AUCTION_HOUSE_HORDE): 主要服务于部落玩家
 * - 中立拍卖行(AUCTION_HOUSE_NEUTRAL): 跨阵营交易场所
 *
 * 物品品质分级：
 * - GRAY: 灰色（垃圾物品）
 * - WHITE: 白色（普通物品）
 * - GREEN: 绿色（优秀物品）
 * - BLUE: 蓝色（精良物品）
 * - PURPLE: 紫色（史诗物品）
 * - ORANGE: 橙色（传说物品）
 * - YELLOW: 黄色（神器物品）
 *
 * 命令层次结构：
 * - .ahbot items: 物品数量配置
 *   - .ahbot items gray/white/green/blue/purple/orange/yellow: 设置各品质物品数量
 * - .ahbot ratio: 物品比例配置
 *   - .ahbot ratio alliance/horde/neutral: 设置各拍卖行的物品比例
 * - .ahbot rebuild: 重建拍卖行物品
 * - .ahbot reload: 重新加载配置
 * - .ahbot status: 查看状态信息
 */

#include "ScriptMgr.h"
#include "AuctionHouseBot.h"
#include "Chat.h"
#include "Language.h"
#include "RBAC.h"

using namespace Trinity::ChatCommands;

/**
 * @brief 物品品质到语言ID的映射表
 *
 * 用于将物品品质枚举值映射到对应的多语言字符串ID，
 * 以便在命令输出中显示相应品质的本地化名称。
 */
static std::unordered_map<AuctionQuality, uint32> const ahbotQualityLangIds =
{
    { AUCTION_QUALITY_GRAY,   LANG_AHBOT_QUALITY_GRAY },
    { AUCTION_QUALITY_WHITE,  LANG_AHBOT_QUALITY_WHITE },
    { AUCTION_QUALITY_GREEN,  LANG_AHBOT_QUALITY_GREEN },
    { AUCTION_QUALITY_BLUE,   LANG_AHBOT_QUALITY_BLUE },
    { AUCTION_QUALITY_PURPLE, LANG_AHBOT_QUALITY_PURPLE },
    { AUCTION_QUALITY_ORANGE, LANG_AHBOT_QUALITY_ORANGE },
    { AUCTION_QUALITY_YELLOW, LANG_AHBOT_QUALITY_YELLOW }
};

/**
 * @class ahbot_commandscript
 * @brief 拍卖行机器人命令脚本类
 *
 * 继承自 CommandScript 基类，实现拍卖行机器人管理相关的所有命令。
 * 该类提供了配置、监控和管理拍卖行机器人行为的完整接口。
 */
class ahbot_commandscript : public CommandScript
{
public:
    /**
     * @brief 构造函数
     *
     * 初始化拍卖行机器人命令脚本，设置脚本名称为 "ahbot_commandscript"
     */
    ahbot_commandscript(): CommandScript("ahbot_commandscript") {}

    /**
     * @brief 获取命令表
     *
     * 注册所有拍卖行机器人相关的命令及其子命令，定义命令的权限要求。
     *
     * @return ChatCommandTable 返回命令表，包含所有注册的拍卖行机器人命令
     *
     * 命令结构：
     * - ahbot items: 物品数量配置命令组
     *   - gray/white/green/blue/purple/orange/yellow: 设置各品质物品数量
     *   - (空): 设置所有品质物品数量
     * - ahbot ratio: 物品比例配置命令组
     *   - alliance/horde/neutral: 设置各拍卖行物品比例
     *   - (空): 设置所有拍卖行物品比例
     * - ahbot rebuild: 重建拍卖行物品
     * - ahbot reload: 重新加载配置
     * - ahbot status: 查看状态信息
     */
    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable ahbotItemsAmountCommandTable =
        {
            { "gray",       HandleAHBotItemsAmountQualityCommand<AUCTION_QUALITY_GRAY>,     rbac::RBAC_PERM_COMMAND_AHBOT_ITEMS_GRAY,       Console::Yes },
            { "white",      HandleAHBotItemsAmountQualityCommand<AUCTION_QUALITY_WHITE>,    rbac::RBAC_PERM_COMMAND_AHBOT_ITEMS_WHITE,      Console::Yes },
            { "green",      HandleAHBotItemsAmountQualityCommand<AUCTION_QUALITY_GREEN>,    rbac::RBAC_PERM_COMMAND_AHBOT_ITEMS_GREEN,      Console::Yes },
            { "blue",       HandleAHBotItemsAmountQualityCommand<AUCTION_QUALITY_BLUE>,     rbac::RBAC_PERM_COMMAND_AHBOT_ITEMS_BLUE,       Console::Yes },
            { "purple",     HandleAHBotItemsAmountQualityCommand<AUCTION_QUALITY_PURPLE>,   rbac::RBAC_PERM_COMMAND_AHBOT_ITEMS_PURPLE,     Console::Yes },
            { "orange",     HandleAHBotItemsAmountQualityCommand<AUCTION_QUALITY_ORANGE>,   rbac::RBAC_PERM_COMMAND_AHBOT_ITEMS_ORANGE,     Console::Yes },
            { "yellow",     HandleAHBotItemsAmountQualityCommand<AUCTION_QUALITY_YELLOW>,   rbac::RBAC_PERM_COMMAND_AHBOT_ITEMS_YELLOW,     Console::Yes },
            { "",           HandleAHBotItemsAmountCommand,                                  rbac::RBAC_PERM_COMMAND_AHBOT_ITEMS,            Console::Yes },
        };

        static ChatCommandTable ahbotItemsRatioCommandTable =
        {
            { "alliance",   HandleAHBotItemsRatioHouseCommand<AUCTION_HOUSE_ALLIANCE>,      rbac::RBAC_PERM_COMMAND_AHBOT_RATIO_ALLIANCE,   Console::Yes },
            { "horde",      HandleAHBotItemsRatioHouseCommand<AUCTION_HOUSE_HORDE>,         rbac::RBAC_PERM_COMMAND_AHBOT_RATIO_HORDE,      Console::Yes },
            { "neutral",    HandleAHBotItemsRatioHouseCommand<AUCTION_HOUSE_NEUTRAL>,       rbac::RBAC_PERM_COMMAND_AHBOT_RATIO_NEUTRAL,    Console::Yes },
            { "",           HandleAHBotItemsRatioCommand,                                   rbac::RBAC_PERM_COMMAND_AHBOT_RATIO,            Console::Yes },
        };

        static ChatCommandTable ahbotCommandTable =
        {
            { "items",      ahbotItemsAmountCommandTable },
            { "ratio",      ahbotItemsRatioCommandTable },
            { "rebuild",    HandleAHBotRebuildCommand,  rbac::RBAC_PERM_COMMAND_AHBOT_REBUILD,  Console::Yes },
            { "reload",     HandleAHBotReloadCommand,   rbac::RBAC_PERM_COMMAND_AHBOT_RELOAD,   Console::Yes },
            { "status",     HandleAHBotStatusCommand,   rbac::RBAC_PERM_COMMAND_AHBOT_STATUS,   Console::Yes },
        };

        static ChatCommandTable commandTable =
        {
            { "ahbot", ahbotCommandTable },
        };

        return commandTable;
    }

    /**
     * @brief 设置所有品质物品的数量
     *
     * 批量设置拍卖行机器人上架的各品质物品数量。此命令会同时更新所有品质的配置。
     *
     * @param handler 聊天命令处理器
     * @param items 包含所有品质物品数量的数组，数组索引对应品质枚举值
     *
     * @return bool 总是返回 true
     *
     * @details 执行流程：
     * 1. 调用 sAuctionBot->SetItemsAmount 设置所有品质的物品数量
     * 2. 遍历所有品质并显示更新后的配置值
     *
     * 调用时机：管理员执行 .ahbot items 命令时
     * 性能注意：涉及多个配置项的更新，但操作轻量
     */
    static bool HandleAHBotItemsAmountCommand(ChatHandler* handler, std::array<uint32, MAX_AUCTION_QUALITY> items)
    {
        sAuctionBot->SetItemsAmount(items);

        for (AuctionQuality quality : EnumUtils::Iterate<AuctionQuality>())
            handler->PSendSysMessage(LANG_AHBOT_ITEMS_AMOUNT, handler->GetTrinityString(ahbotQualityLangIds.at(quality)), sAuctionBotConfig->GetConfigItemQualityAmount(quality));

        return true;
    }

    /**
     * @brief 设置指定品质物品的数量（模板函数）
     *
     * 设置拍卖行机器人上架的指定品质物品数量。这是一个模板函数，
     * 编译时会为每种品质生成对应的处理函数。
     *
     * @tparam Q 拍卖品质模板参数，如 AUCTION_QUALITY_GRAY、AUCTION_QUALITY_WHITE 等
     * @param handler 聊天命令处理器
     * @param amount 该品质物品的数量
     *
     * @return bool 总是返回 true
     *
     * @details 执行流程：
     * 1. 调用 sAuctionBot->SetItemsAmountForQuality 设置指定品质的物品数量
     * 2. 显示该品质更新后的配置值
     *
     * 模板实例化：
     * 编译器会为以下品质生成具体的函数实例：
     * - AUCTION_QUALITY_GRAY (灰色)
     * - AUCTION_QUALITY_WHITE (白色)
     * - AUCTION_QUALITY_GREEN (绿色)
     * - AUCTION_QUALITY_BLUE (蓝色)
     * - AUCTION_QUALITY_PURPLE (紫色)
     * - AUCTION_QUALITY_ORANGE (橙色)
     * - AUCTION_QUALITY_YELLOW (黄色)
     *
     * 调用时机：管理员执行 .ahbot items <quality> 命令时
     */
    template <AuctionQuality Q>
    static bool HandleAHBotItemsAmountQualityCommand(ChatHandler* handler, uint32 amount)
    {
        sAuctionBot->SetItemsAmountForQuality(Q, amount);
        handler->PSendSysMessage(LANG_AHBOT_ITEMS_AMOUNT, handler->GetTrinityString(ahbotQualityLangIds.at(Q)),
            sAuctionBotConfig->GetConfigItemQualityAmount(Q));

        return true;
    }

    /**
     * @brief 设置所有拍卖行的物品比例
     *
     * 批量设置联盟、部落和中立拍卖行的物品上架比例。
     * 比例值决定了机器人向各拍卖行投放物品的相对频率。
     *
     * @param handler 聊天命令处理器
     * @param alliance 联盟拍卖行的物品比例
     * @param horde 部落拍卖行的物品比例
     * @param neutral 中立拍卖行的物品比例
     *
     * @return bool 总是返回 true
     *
     * @details 执行流程：
     * 1. 调用 sAuctionBot->SetItemsRatio 设置三个拍卖行的物品比例
     * 2. 遍历所有拍卖行类型并显示更新后的比例值
     *
     * 比例说明：
     * - 比例值越高，该拍卖行上架的物品越多
     * - 总比例通常建议为100或1000以便于理解和计算
     * - 例如：联盟50、部落30、中立20，表示联盟拍卖行物品最多
     *
     * 调用时机：管理员执行 .ahbot ratio 命令时
     */
    static bool HandleAHBotItemsRatioCommand(ChatHandler* handler, uint32 alliance, uint32 horde, uint32 neutral)
    {
        sAuctionBot->SetItemsRatio(alliance, horde, neutral);

        for (AuctionHouseType type : EnumUtils::Iterate<AuctionHouseType>())
            handler->PSendSysMessage(LANG_AHBOT_ITEMS_RATIO, AuctionBotConfig::GetHouseTypeName(type), sAuctionBotConfig->GetConfigItemAmountRatio(type));
        return true;
    }

    /**
     * @brief 设置指定拍卖行的物品比例（模板函数）
     *
     * 设置指定拍卖行（联盟/部落/中立）的物品上架比例。
     * 这是一个模板函数，编译时会为每种拍卖行类型生成对应的处理函数。
     *
     * @tparam H 拍卖行类型模板参数，如 AUCTION_HOUSE_ALLIANCE、AUCTION_HOUSE_HORDE、AUCTION_HOUSE_NEUTRAL
     * @param handler 聊天命令处理器
     * @param ratio 该拍卖行的物品比例
     *
     * @return bool 总是返回 true
     *
     * @details 执行流程：
     * 1. 调用 sAuctionBot->SetItemsRatioForHouse 设置指定拍卖行的物品比例
     * 2. 显示该拍卖行更新后的比例值
     *
     * 模板实例化：
     * 编译器会为以下拍卖行类型生成具体的函数实例：
     * - AUCTION_HOUSE_ALLIANCE (联盟拍卖行)
     * - AUCTION_HOUSE_HORDE (部落拍卖行)
     * - AUCTION_HOUSE_NEUTRAL (中立拍卖行)
     *
     * 调用时机：管理员执行 .ahbot ratio <house> 命令时
     */
    template<AuctionHouseType H>
    static bool HandleAHBotItemsRatioHouseCommand(ChatHandler* handler, uint32 ratio)
    {
        sAuctionBot->SetItemsRatioForHouse(H, ratio);
        handler->PSendSysMessage(LANG_AHBOT_ITEMS_RATIO, AuctionBotConfig::GetHouseTypeName(H), sAuctionBotConfig->GetConfigItemAmountRatio(H));
        return true;
    }

    /**
     * @brief 重建拍卖行物品
     *
     * 清空并重新生成拍卖行中的物品。可以选择重建所有拍卖行或仅重建有问题的物品。
     *
     * @param handler 聊天命令处理器（未使用）
     * @param all 可选参数，如果指定 "all" 则重建所有拍卖行的所有物品
     *
     * @return bool 总是返回 true
     *
     * @details 执行流程：
     * 1. 检查是否指定了 "all" 参数
     * 2. 调用 sAuctionBot->Rebuild 执行重建操作
     *
     * 重建模式：
     * - 不带参数：仅重建有问题的物品（如过期、数量不足等）
     * - 带 "all" 参数：清空所有拍卖行并重新生成所有物品
     *
     * 使用场景：
     * - 拍卖行物品异常或损坏时
     * - 调整配置后需要重新生成物品
     * - 定期清理和维护拍卖行
     *
     * 调用时机：管理员执行 .ahbot rebuild [all] 命令时
     * 性能注意：重建操作可能耗时较长，特别是重建所有物品时
     */
    static bool HandleAHBotRebuildCommand(ChatHandler* /*handler*/, Optional<EXACT_SEQUENCE("all")> all)
    {
        sAuctionBot->Rebuild(all.has_value());
        return true;
    }

    /**
     * @brief 重新加载拍卖行机器人配置
     *
     * 从配置文件重新加载拍卖行机器人的所有配置项。
     * 在修改配置文件后执行此命令使配置生效。
     *
     * @param handler 聊天命令处理器
     *
     * @return bool 总是返回 true
     *
     * @details 执行流程：
     * 1. 调用 sAuctionBot->ReloadAllConfig 重新加载所有配置
     * 2. 发送配置重载成功的消息
     *
     * 配置内容包括：
     * - 物品品质数量配置
     * - 拍卖行比例配置
     * - 价格配置
     * - 其他拍卖行机器人行为参数
     *
     * 调用时机：管理员执行 .ahbot reload 命令时
     * 性能注意：配置重载是轻量操作，可以频繁执行
     */
    static bool HandleAHBotReloadCommand(ChatHandler* handler)
    {
        sAuctionBot->ReloadAllConfig();
        handler->SendSysMessage(LANG_AHBOT_RELOAD_OK);
        return true;
    }

    /**
     * @brief 显示拍卖行机器人状态
     *
     * 显示拍卖行机器人的当前运行状态和统计信息。
     * 可以选择显示简要信息或详细信息。
     *
     * @param handler 聊天命令处理器
     * @param all 可选参数，如果指定 "all" 则显示详细的状态信息
     *
     * @return bool 总是返回 true
     *
     * @details 执行流程：
     * 1. 收集各拍卖行的状态信息
     * 2. 根据调用来源（控制台或游戏内）选择不同的显示格式
     * 3. 显示各拍卖行的物品数量统计
     * 4. 如果指定了 "all" 参数：
     *    - 显示各拍卖行的物品比例配置
     *    - 显示各品质物品在三个拍卖行的分布情况
     *
     * 显示信息包括：
     * - 物品总数：联盟、部落、中立拍卖行的物品数量及总和
     * - 物品比例：三个拍卖行的配置比例
     * - 品质分布：各品质物品在三个拍卖行的分布情况（详细模式）
     *
     * 显示格式：
     * - 控制台：使用表格格式，带分隔线
     * - 游戏内：使用紧凑格式，适合聊天框显示
     *
     * 调用时机：管理员执行 .ahbot status [all] 命令时
     */
    static bool HandleAHBotStatusCommand(ChatHandler* handler, Optional<EXACT_SEQUENCE("all")> all)
    {
        // 收集各拍卖行的状态信息
        std::unordered_map<AuctionHouseType, AuctionHouseBotStatusInfoPerType> statusInfo;
        sAuctionBot->PrepareStatusInfos(statusInfo);

        WorldSession* session = handler->GetSession();

        // 根据调用来源（控制台或游戏内）显示不同的表头格式
        if (!session)
        {
            // 控制台格式：带分隔线的表头
            handler->SendSysMessage(LANG_AHBOT_STATUS_BAR_CONSOLE);
            handler->SendSysMessage(LANG_AHBOT_STATUS_TITLE1_CONSOLE);
            handler->SendSysMessage(LANG_AHBOT_STATUS_MIDBAR_CONSOLE);
        }
        else
            // 游戏内格式：紧凑的表头
            handler->SendSysMessage(LANG_AHBOT_STATUS_TITLE1_CHAT);

        // 选择显示格式ID（控制台或聊天框）
        uint32 fmtId = session ? LANG_AHBOT_STATUS_FORMAT_CHAT : LANG_AHBOT_STATUS_FORMAT_CONSOLE;

        // 显示物品数量统计：联盟、部落、中立、总计
        handler->PSendSysMessage(fmtId, handler->GetTrinityString(LANG_AHBOT_STATUS_ITEM_COUNT),
            statusInfo[AUCTION_HOUSE_ALLIANCE].ItemsCount,
            statusInfo[AUCTION_HOUSE_HORDE].ItemsCount,
            statusInfo[AUCTION_HOUSE_NEUTRAL].ItemsCount,
            statusInfo[AUCTION_HOUSE_ALLIANCE].ItemsCount +
            statusInfo[AUCTION_HOUSE_HORDE].ItemsCount +
            statusInfo[AUCTION_HOUSE_NEUTRAL].ItemsCount);

        // 如果指定了 "all" 参数，显示详细信息
        if (all)
        {
            // 显示物品比例配置：联盟、部落、中立、总计
            handler->PSendSysMessage(fmtId, handler->GetTrinityString(LANG_AHBOT_STATUS_ITEM_RATIO),
                sAuctionBotConfig->GetConfig(CONFIG_AHBOT_ALLIANCE_ITEM_AMOUNT_RATIO),
                sAuctionBotConfig->GetConfig(CONFIG_AHBOT_HORDE_ITEM_AMOUNT_RATIO),
                sAuctionBotConfig->GetConfig(CONFIG_AHBOT_NEUTRAL_ITEM_AMOUNT_RATIO),
                sAuctionBotConfig->GetConfig(CONFIG_AHBOT_ALLIANCE_ITEM_AMOUNT_RATIO) +
                sAuctionBotConfig->GetConfig(CONFIG_AHBOT_HORDE_ITEM_AMOUNT_RATIO) +
                sAuctionBotConfig->GetConfig(CONFIG_AHBOT_NEUTRAL_ITEM_AMOUNT_RATIO));

            // 显示品质分布的表头
            if (!session)
            {
                handler->SendSysMessage(LANG_AHBOT_STATUS_BAR_CONSOLE);
                handler->SendSysMessage(LANG_AHBOT_STATUS_TITLE2_CONSOLE);
                handler->SendSysMessage(LANG_AHBOT_STATUS_MIDBAR_CONSOLE);
            }
            else
                handler->SendSysMessage(LANG_AHBOT_STATUS_TITLE2_CHAT);

            // 遍历所有品质，显示各品质在三个拍卖行的分布情况
            for (AuctionQuality quality : EnumUtils::Iterate<AuctionQuality>())
                handler->PSendSysMessage(fmtId, handler->GetTrinityString(ahbotQualityLangIds.at(quality)),
                    statusInfo[AUCTION_HOUSE_ALLIANCE].QualityInfo.at(quality),
                    statusInfo[AUCTION_HOUSE_HORDE].QualityInfo.at(quality),
                    statusInfo[AUCTION_HOUSE_NEUTRAL].QualityInfo.at(quality),
                    sAuctionBotConfig->GetConfigItemQualityAmount(quality));
        }

        // 控制台格式：显示结束分隔线
        if (!session)
            handler->SendSysMessage(LANG_AHBOT_STATUS_BAR_CONSOLE);

        return true;
    }

};

// 模板函数显式实例化声明，确保链接时能找到对应的函数定义
template bool ahbot_commandscript::HandleAHBotItemsAmountQualityCommand<AUCTION_QUALITY_GRAY>(ChatHandler* handler, uint32 amount);
template bool ahbot_commandscript::HandleAHBotItemsAmountQualityCommand<AUCTION_QUALITY_WHITE>(ChatHandler* handler, uint32 amount);
template bool ahbot_commandscript::HandleAHBotItemsAmountQualityCommand<AUCTION_QUALITY_GREEN>(ChatHandler* handler, uint32 amount);
template bool ahbot_commandscript::HandleAHBotItemsAmountQualityCommand<AUCTION_QUALITY_BLUE>(ChatHandler* handler, uint32 amount);
template bool ahbot_commandscript::HandleAHBotItemsAmountQualityCommand<AUCTION_QUALITY_PURPLE>(ChatHandler* handler, uint32 amount);
template bool ahbot_commandscript::HandleAHBotItemsAmountQualityCommand<AUCTION_QUALITY_ORANGE>(ChatHandler* handler, uint32 amount);
template bool ahbot_commandscript::HandleAHBotItemsAmountQualityCommand<AUCTION_QUALITY_YELLOW>(ChatHandler* handler, uint32 amount);

template bool ahbot_commandscript::HandleAHBotItemsRatioHouseCommand<AUCTION_HOUSE_ALLIANCE>(ChatHandler* handler, uint32 ratio);
template bool ahbot_commandscript::HandleAHBotItemsRatioHouseCommand<AUCTION_HOUSE_HORDE>(ChatHandler* handler, uint32 ratio);
template bool ahbot_commandscript::HandleAHBotItemsRatioHouseCommand<AUCTION_HOUSE_NEUTRAL>(ChatHandler* handler, uint32 ratio);

/**
 * @brief 注册拍卖行机器人命令脚本
 *
 * 此函数在服务器启动时被调用，用于创建并注册拍卖行机器人命令脚本实例。
 * 该函数由脚本管理系统自动调用。
 *
 * 调用时机：服务器初始化时，由脚本加载系统调用
 */
void AddSC_ahbot_commandscript()
{
    new ahbot_commandscript();
}
