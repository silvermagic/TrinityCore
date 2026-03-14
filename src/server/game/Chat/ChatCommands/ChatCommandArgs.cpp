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
 * @file ChatCommandArgs.cpp
 * @brief 聊天命令参数解析器实现
 *
 * 本文件实现了游戏特定类型的参数提取器，包括：
 * - AchievementEntry：成就条目，支持 ID 和链接
 * - GameTele：传送点，支持名称和链接
 * - ItemTemplate：物品模板，支持 ID 和链接
 * - Quest：任务，支持 ID 和链接
 * - SpellInfo：法术信息，支持 ID 和多种链接类型
 *
 * 这些提取器使用访问者模式（Visitor Pattern）处理不同输入类型（链接或数值）。
 */

#include "ChatCommandArgs.h"
#include "AchievementMgr.h"
#include "ChatCommand.h"
#include "ObjectMgr.h"
#include "SpellMgr.h"
#include "Util.h"

using namespace Trinity::ChatCommands;
using ChatCommandResult = Trinity::Impl::ChatCommands::ChatCommandResult;

/**
 * @brief 成就访问者结构体
 *
 * 用于处理成就参数的两种可能输入类型：
 * - Hyperlink<achievement>：游戏内成就链接
 * - uint32：成就 ID
 */
struct AchievementVisitor
{
    using value_type = AchievementEntry const*;  ///< 返回类型

    /**
     * @brief 从成就链接提取成就条目
     * @param achData 成就链接数据
     * @return 成就条目指针
     */
    value_type operator()(Hyperlink<achievement> achData) const { return achData->Achievement; }

    /**
     * @brief 从数值 ID 查找成就条目
     * @param achId 成就 ID
     * @return 成就条目指针，不存在则返回 nullptr
     */
    value_type operator()(uint32 achId) const { return sAchievementMgr->GetAchievement(achId); }
};

/**
 * @brief 尝试从字符串中提取成就条目
 *
 * 支持两种输入方式：
 * 1. 游戏内成就链接（Shift+点击成就）
 * 2. 成就 ID（数字）
 *
 * @param data 输出参数，存储提取的成就条目指针
 * @param handler 聊天处理器
 * @param args 输入参数字符串
 * @return ChatCommandResult 解析结果，成功时返回剩余字符串
 */
ChatCommandResult Trinity::Impl::ChatCommands::ArgInfo<AchievementEntry const*>::TryConsume(AchievementEntry const*& data, ChatHandler const* handler, std::string_view args)
{
    // 使用 variant 支持链接或 ID 两种输入方式
    Variant<Hyperlink<achievement>, uint32> val;
    ChatCommandResult result = ArgInfo<decltype(val)>::TryConsume(val, handler, args);
    // 解析成功或成功获取到成就数据
    if (!result || (data = val.visit(AchievementVisitor())))
        return result;
    // 如果是 ID 方式但成就不存在
    if (uint32* id = std::get_if<uint32>(&val))
        return FormatTrinityString(handler, LANG_CMDPARSER_ACHIEVEMENT_NO_EXIST, *id);
    return std::nullopt;
}

/**
 * @brief 传送点访问者结构体
 *
 * 用于处理传送点参数的两种可能输入类型：
 * - Hyperlink<tele>：游戏内传送点链接
 * - std::string_view：传送点名称
 */
struct GameTeleVisitor
{
    using value_type = GameTele const*;  ///< 返回类型

    /**
     * @brief 从传送点链接提取传送点数据
     * @param tele 传送点链接数据
     * @return 传送点指针
     */
    value_type operator()(Hyperlink<tele> tele) const { return sObjectMgr->GetGameTele(tele); }

    /**
     * @brief 从名称字符串查找传送点
     * @param tele 传送点名称
     * @return 传送点指针，不存在则返回 nullptr
     */
    value_type operator()(std::string_view tele) const { return sObjectMgr->GetGameTele(tele); }
};

/**
 * @brief 尝试从字符串中提取传送点
 *
 * 支持两种输入方式：
 * 1. 游戏内传送点链接
 * 2. 传送点名称（字符串）
 *
 * @param data 输出参数，存储提取的传送点指针
 * @param handler 聊天处理器
 * @param args 输入参数字符串
 * @return ChatCommandResult 解析结果，成功时返回剩余字符串
 */
ChatCommandResult Trinity::Impl::ChatCommands::ArgInfo<GameTele const*>::TryConsume(GameTele const*& data, ChatHandler const* handler, std::string_view args)
{
    // 使用 variant 支持链接或名称两种输入方式
    Variant<Hyperlink<tele>, std::string_view> val;
    ChatCommandResult result = ArgInfo<decltype(val)>::TryConsume(val, handler, args);
    // 解析成功或成功获取到传送点数据
    if (!result || (data = val.visit(GameTeleVisitor())))
        return result;
    // 根据输入类型生成不同的错误消息
    if (val.holds_alternative<Hyperlink<tele>>())
        return FormatTrinityString(handler, LANG_CMDPARSER_GAME_TELE_ID_NO_EXIST, static_cast<uint32>(std::get<Hyperlink<tele>>(val)));
    else
        return FormatTrinityString(handler, LANG_CMDPARSER_GAME_TELE_NO_EXIST, STRING_VIEW_FMT_ARG(std::get<std::string_view>(val)));
}

/**
 * @brief 物品模板访问者结构体
 *
 * 用于处理物品参数的两种可能输入类型：
 * - Hyperlink<item>：游戏内物品链接
 * - uint32：物品 ID
 */
struct ItemTemplateVisitor
{
    using value_type = ItemTemplate const*;  ///< 返回类型

    /**
     * @brief 从物品链接提取物品模板
     * @param item 物品链接数据
     * @return 物品模板指针
     */
    value_type operator()(Hyperlink<item> item) const { return item->Item; }

    /**
     * @brief 从数值 ID 查找物品模板
     * @param item 物品 ID
     * @return 物品模板指针，不存在则返回 nullptr
     */
    value_type operator()(uint32 item) { return sObjectMgr->GetItemTemplate(item); }
};

/**
 * @brief 尝试从字符串中提取物品模板
 *
 * 支持两种输入方式：
 * 1. 游戏内物品链接（Shift+点击物品）
 * 2. 物品 ID（数字）
 *
 * @param data 输出参数，存储提取的物品模板指针
 * @param handler 聊天处理器
 * @param args 输入参数字符串
 * @return ChatCommandResult 解析结果，成功时返回剩余字符串
 */
ChatCommandResult Trinity::Impl::ChatCommands::ArgInfo<ItemTemplate const*>::TryConsume(ItemTemplate const*& data, ChatHandler const* handler, std::string_view args)
{
    // 使用 variant 支持链接或 ID 两种输入方式
    Variant<Hyperlink<item>, uint32> val;
    ChatCommandResult result = ArgInfo<decltype(val)>::TryConsume(val, handler, args);
    // 解析成功或成功获取到物品数据
    if (!result || (data = val.visit(ItemTemplateVisitor())))
        return result;
    // 如果是 ID 方式但物品不存在
    if (uint32* id = std::get_if<uint32>(&val))
        return FormatTrinityString(handler, LANG_CMDPARSER_ITEM_NO_EXIST, *id);
    return std::nullopt;
}

/**
 * @brief 任务访问者结构体
 *
 * 用于处理任务参数的两种可能输入类型：
 * - Hyperlink<quest>：游戏内任务链接
 * - uint32：任务 ID
 */
struct QuestVisitor
{
    using value_type = Quest const*;  ///< 返回类型

    /**
     * @brief 从任务链接提取任务数据
     * @param quest 任务链接数据
     * @return 任务指针
     */
    value_type operator()(Hyperlink<quest> quest) const { return quest->Quest; }

    /**
     * @brief 从数值 ID 查找任务模板
     * @param questId 任务 ID
     * @return 任务指针，不存在则返回 nullptr
     */
    value_type operator()(uint32 questId) const { return sObjectMgr->GetQuestTemplate(questId); }
};

/**
 * @brief 尝试从字符串中提取任务
 *
 * 支持两种输入方式：
 * 1. 游戏内任务链接（Shift+点击任务）
 * 2. 任务 ID（数字）
 *
 * @param data 输出参数，存储提取的任务指针
 * @param handler 聊天处理器
 * @param args 输入参数字符串
 * @return ChatCommandResult 解析结果，成功时返回剩余字符串
 */
ChatCommandResult Trinity::Impl::ChatCommands::ArgInfo<Quest const*, void>::TryConsume(Quest const*& data, ChatHandler const* handler, std::string_view args)
{
    // 使用 variant 支持链接或 ID 两种输入方式
    Variant<Hyperlink<quest>, uint32> val;
    ChatCommandResult result = ArgInfo<decltype(val)>::TryConsume(val, handler, args);
    // 解析成功或成功获取到任务数据
    if (!result || (data = val.visit(QuestVisitor())))
        return result;
    // 如果是 ID 方式但任务不存在
    if (uint32* id = std::get_if<uint32>(&val))
        return FormatTrinityString(handler, LANG_CMDPARSER_QUEST_NO_EXIST, *id);
    return std::nullopt;
}

/**
 * @brief 法术信息访问者结构体
 *
 * 用于处理法术参数的多种可能输入类型：
 * - Hyperlink<enchant>：附魔链接
 * - Hyperlink<glyph>：雕文链接
 * - Hyperlink<spell>：法术链接
 * - Hyperlink<talent>：天赋链接
 * - Hyperlink<trade>：商业技能链接
 * - uint32：法术 ID
 *
 * 所有链接类型最终都会转换为 SpellInfo 指针。
 */
struct SpellInfoVisitor
{
    using value_type = SpellInfo const*;  ///< 返回类型

    /**
     * @brief 从附魔链接获取法术信息
     * @param enchant 附魔链接数据
     * @return 附魔对应的法术信息
     */
    value_type operator()(Hyperlink<enchant> enchant) const { return enchant; };

    /**
     * @brief 从雕文链接获取法术信息
     * @param glyph 雕文链接数据
     * @return 雕文对应的法术信息
     */
    value_type operator()(Hyperlink<glyph> glyph) const { return operator()(glyph->Glyph->SpellID); };

    /**
     * @brief 从法术链接获取法术信息
     * @param spell 法术链接数据
     * @return 法术信息
     */
    value_type operator()(Hyperlink<spell> spell) const { return *spell; }

    /**
     * @brief 从天赋链接获取法术信息
     * @param talent 天赋链接数据
     * @return 天赋对应的法术信息
     */
    value_type operator()(Hyperlink<talent> talent) const { return talent->Spell; };

    /**
     * @brief 从商业技能链接获取法术信息
     * @param trade 商业技能链接数据
     * @return 商业技能对应的法术信息
     */
    value_type operator()(Hyperlink<trade> trade) const { return trade->Spell; };

    /**
     * @brief 从数值 ID 查找法术信息
     * @param spellId 法术 ID
     * @return 法术信息指针，不存在则返回 nullptr
     */
    value_type operator()(uint32 spellId) const { return sSpellMgr->GetSpellInfo(spellId); }
};

/**
 * @brief 尝试从字符串中提取法术信息
 *
 * 支持多种输入方式：
 * 1. 游戏内附魔链接
 * 2. 游戏内雕文链接
 * 3. 游戏内法术链接（Shift+点击法术）
 * 4. 游戏内天赋链接
 * 5. 游戏内商业技能链接
 * 6. 法术 ID（数字）
 *
 * @param data 输出参数，存储提取的法术信息指针
 * @param handler 聊天处理器
 * @param args 输入参数字符串
 * @return ChatCommandResult 解析结果，成功时返回剩余字符串
 */
ChatCommandResult Trinity::Impl::ChatCommands::ArgInfo<SpellInfo const*>::TryConsume(SpellInfo const*& data, ChatHandler const* handler, std::string_view args)
{
    // 使用 variant 支持多种链接类型和 ID 输入方式
    Variant<Hyperlink<enchant>, Hyperlink<glyph>, Hyperlink<spell>, Hyperlink<talent>, Hyperlink<trade>, uint32> val;
    ChatCommandResult result = ArgInfo<decltype(val)>::TryConsume(val, handler, args);
    // 解析成功或成功获取到法术数据
    if (!result || (data = val.visit(SpellInfoVisitor())))
        return result;
    // 如果是 ID 方式但法术不存在
    if (uint32* id = std::get_if<uint32>(&val))
        return FormatTrinityString(handler, LANG_CMDPARSER_SPELL_NO_EXIST, *id);
    return std::nullopt;
}
