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
 * @file Hyperlinks.cpp
 * @brief 超链接解析和验证实现文件
 *
 * 本文件实现了超链接的解析和验证功能,包括:
 * - ParseSingleHyperlink: 解析单个超链接
 * - CheckAllLinks: 验证字符串中的所有超链接
 * - 各种链接验证器(用于验证不同类型链接的正确性)
 *
 * 超链接格式: |cAARRGGBB|Htag:data|h[text]|h|r
 * 其中:
 * - |cAARRGGBB: 颜色标记(ARGB格式)
 * - |H: 链接开始标记
 * - tag: 链接类型标签(如item, spell, achievement等)
 * - data: 链接数据(用冒号分隔的多个字段)
 * - |h: 链接数据结束标记
 * - [text]: 显示文本
 * - |h|r: 链接结束和颜色重置标记
 */

#include "Hyperlinks.h"
#include "advstd.h"
#include "Common.h"
#include "DBCStores.h"
#include "Errors.h"
#include "ObjectMgr.h"
#include "SharedDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "QuestDef.h"
#include "World.h"

using namespace Trinity::Hyperlinks;

/**
 * @brief 将十六进制字符转换为数值
 *
 * @param c 十六进制字符('0'-'9'或'a'-'f')
 * @return uint8 转换后的数值(0x10-0x19或0x1a-0x1f),无效字符返回0x00
 */
inline uint8 toHex(char c) { return (c >= '0' && c <= '9') ? c - '0' + 0x10 : (c >= 'a' && c <= 'f') ? c - 'a' + 0x1a : 0x00; }

/**
 * @brief 解析单个超链接
 *
 * 从字符串中解析第一个超链接,提取所有相关信息。
 * 超链接格式: |cAARRGGBB|Htag:data|h[text]|h|r
 *
 * @param str 包含超链接的字符串
 * @return HyperlinkInfo 解析结果,失败时ok为false
 *
 * @note 调用时机:需要解析字符串中的第一个超链接时
 * @note 性能注意事项:字符串视图操作,不进行内存分配
 */
HyperlinkInfo Trinity::Hyperlinks::ParseSingleHyperlink(std::string_view str)
{
    uint32 color = 0;
    std::string_view tag;
    std::string_view data;
    std::string_view text;

    // 解析颜色标记 |c
    if (str.substr(0, 2) != "|c")
        return {};
    str.remove_prefix(2);

    // 颜色值必须有8个字符(AARRGGBB)
    if (str.length() < 8)
        return {};

    // 解析颜色值(十六进制)
    for (uint8 i = 0; i < 8; ++i)
    {
        if (uint8 hex = toHex(str[i]))
            color = (color << 4) | (hex & 0xf);
        else
            return {};
    }
    str.remove_prefix(8);

    // 解析链接开始标记 |H
    if (str.substr(0, 2) != "|H")
        return {};
    str.remove_prefix(2);

    // 解析标签和数据部分
    if (size_t delimPos = str.find('|'); delimPos != std::string_view::npos)
    {
        tag = str.substr(0, delimPos);
        str.remove_prefix(delimPos+1);
    }
    else
        return {};

    // 如果存在冒号,分离标签和数据
    if (size_t dataStart = tag.find(':'); dataStart != std::string_view::npos)
    {
        data = tag.substr(dataStart+1);
        tag = tag.substr(0, dataStart);
    }

    // 检查链接数据结束标记 h
    if (str.substr(0, 1) != "h")
        return {};
    str.remove_prefix(1);

    // 跳到最后一个|并解析文本和结束标记
    if (size_t end = str.find('|'); end != std::string_view::npos)
    {
        // 检查结束标记 |h|r
        if (str.substr(end, 4) != "|h|r")
            return {};
        // 检查文本必须用[]包围
        if ((str[0] != '[') || (str[end - 1] != ']'))
            return {};
        // 提取文本(去掉[])
        text = str.substr(1, end - 2);
        // 提取尾部剩余文本
        str = str.substr(end + 4);
    }
    else
        return {};

    // 成功解析,返回超链接信息
    return { str, color, tag, data, text };
}

/**
 * @struct LinkValidator
 * @brief 链接验证器模板基类
 *
 * 为不同类型的链接提供文本和颜色验证功能。
 * 默认实现接受所有文本和颜色。
 *
 * @tparam T 链接标签类型
 */
template <typename T>
struct LinkValidator
{
    /**
     * @brief 验证链接文本
     *
     * @param data 链接数据
     * @param text 要验证的文本
     * @return true 默认接受所有文本
     */
    static bool IsTextValid(typename T::value_type, std::string_view) { return true; }

    /**
     * @brief 验证链接颜色
     *
     * @param data 链接数据
     * @param color 要验证的颜色
     * @return true 默认接受所有颜色
     */
    static bool IsColorValid(typename T::value_type, HyperlinkColor) { return true; }
};

/**
 * @struct LinkValidator<LinkTags::achievement>
 * @brief 成就链接验证器特化
 *
 * 验证成就链接的文本和颜色是否正确
 */
template <>
struct LinkValidator<LinkTags::achievement>
{
    /**
     * @brief 验证成就文本
     *
     * 检查显示文本是否与成就标题匹配(支持所有语言)
     *
     * @param data 成就链接数据
     * @param text 显示文本
     * @return true 如果文本匹配成就标题
     */
    static bool IsTextValid(AchievementLinkData const& data, std::string_view text)
    {
        if (text.empty())
            return false;
        // 检查所有语言版本的标题
        for (uint8 i = 0; i < TOTAL_LOCALES; ++i)
            if (text == data.Achievement->Title[i])
                return true;
        return false;
    }

    /**
     * @brief 验证成就颜色
     *
     * @param data 成就链接数据
     * @param c 链接颜色
     * @return true 如果颜色为成就链接的标准颜色
     */
    static bool IsColorValid(AchievementLinkData const&, HyperlinkColor c)
    {
        return c == CHAT_LINK_COLOR_ACHIEVEMENT;
    }
};

/**
 * @struct LinkValidator<LinkTags::item>
 * @brief 物品链接验证器特化
 *
 * 验证物品链接的文本和颜色是否正确,考虑随机属性后缀
 */
template <>
struct LinkValidator<LinkTags::item>
{
    /**
     * @brief 验证物品文本
     *
     * 检查显示文本是否与物品名称匹配,考虑:
     * - 多语言支持
     * - 随机属性后缀
     * - BUG观察链接(客户端可能丢失后缀)
     *
     * @param data 物品链接数据
     * @param text 显示文本
     * @return true 如果文本匹配物品名称
     */
    static bool IsTextValid(ItemLinkData const& data, std::string_view text)
    {
        ItemLocale const* locale = sObjectMgr->GetItemLocale(data.Item->ItemId);

        // 获取随机后缀名称
        std::array<char const*, 16> const* randomSuffixes = nullptr;
        if (data.RandomProperty)
            randomSuffixes = &data.RandomProperty->Name;
        else if (data.RandomSuffix)
            randomSuffixes = &data.RandomSuffix->Name;

        // 如果是有BUG的观察链接,DBC查找在客户端会失败,链接应该不带后缀
        if (data.IsBuggedInspectLink)
            randomSuffixes = nullptr;

        // 检查所有语言版本
        for (uint8 i = 0; i < TOTAL_LOCALES; ++i)
        {
            if (!locale && i != DEFAULT_LOCALE)
                continue;
            std::string_view name = (i == DEFAULT_LOCALE) ? data.Item->Name1 : ObjectMgr::GetLocaleString(locale->Name, i);
            if (name.empty())
                continue;

            // 如果有随机后缀,文本格式应为: "基础名称 后缀名称"
            if (randomSuffixes)
            {
                std::string_view randomSuffix((*randomSuffixes)[i]);
                if (
                  (!randomSuffix.empty()) &&
                  (text.length() == (name.length() + 1 + randomSuffix.length())) &&
                  (text.substr(0, name.length()) == name) &&
                  (text[name.length()] == ' ') &&
                  (text.substr(name.length() + 1) == randomSuffix)
                )
                    return true;
            }
            // 没有随机后缀,直接比较名称
            else if (text == name)
                return true;
        }
        return false;
    }

    /**
     * @brief 验证物品颜色
     *
     * 颜色应与物品品质对应
     *
     * @param data 物品链接数据
     * @param c 链接颜色
     * @return true 如果颜色与物品品质匹配
     */
    static bool IsColorValid(ItemLinkData const& data, HyperlinkColor c)
    {
        return c == ItemQualityColors[data.Item->Quality];
    }
};

/**
 * @struct LinkValidator<LinkTags::quest>
 * @brief 任务链接验证器特化
 *
 * 验证任务链接的文本和颜色是否正确
 */
template <>
struct LinkValidator<LinkTags::quest>
{
    /**
     * @brief 验证任务文本
     *
     * 检查显示文本是否与任务标题匹配(支持多语言)
     *
     * @param data 任务链接数据
     * @param text 显示文本
     * @return true 如果文本匹配任务标题
     */
    static bool IsTextValid(QuestLinkData const& data, std::string_view text)
    {
        if (text.empty())
            return false;

        // 首先检查默认语言
        if (text == data.Quest->GetTitle())
            return true;

        // 检查其他语言版本
        QuestLocale const* locale = sObjectMgr->GetQuestLocale(data.Quest->GetQuestId());
        if (!locale)
            return false;

        for (uint8 i = 0; i < TOTAL_LOCALES; ++i)
        {
            if (i == DEFAULT_LOCALE)
                continue;

            std::string_view name = ObjectMgr::GetLocaleString(locale->Title, i);
            if (!name.empty() && (text == name))
                return true;
        }

        return false;
    }

    /**
     * @brief 验证任务颜色
     *
     * 颜色应与任务难度等级对应(灰、绿、黄、橙、红)
     *
     * @param data 任务链接数据
     * @param c 链接颜色
     * @return true 如果颜色为任务难度颜色之一
     */
    static bool IsColorValid(QuestLinkData const&, HyperlinkColor c)
    {
        for (uint8 i = 0; i < MAX_QUEST_DIFFICULTY; ++i)
            if (c == QuestDifficultyColors[i])
                return true;
        return false;
    }
};

/**
 * @struct LinkValidator<LinkTags::spell>
 * @brief 法术链接验证器特化
 *
 * 验证法术链接的文本和颜色是否正确
 */
template <>
struct LinkValidator<LinkTags::spell>
{
    /**
     * @brief 验证法术文本
     *
     * 检查显示文本是否与法术名称匹配(支持多语言)
     *
     * @param info 法术信息
     * @param text 显示文本
     * @return true 如果文本匹配法术名称
     */
    static bool IsTextValid(SpellInfo const* info, std::string_view text)
    {
        for (uint8 i = 0; i < TOTAL_LOCALES; ++i)
            if (text == info->SpellName[i])
                return true;
        return false;
    }

    /**
     * @brief 验证法术颜色
     *
     * @param info 法术信息
     * @param c 链接颜色
     * @return true 如果颜色为法术链接的标准颜色
     */
    static bool IsColorValid(SpellInfo const*, HyperlinkColor c)
    {
        return c == CHAT_LINK_COLOR_SPELL;
    }
};

/**
 * @struct LinkValidator<LinkTags::enchant>
 * @brief 附魔链接验证器特化
 *
 * 验证附魔链接的文本和颜色,支持两种格式:
 * 1. 直接使用法术名称
 * 2. 使用"技能名称: 法术名称"格式
 */
template <>
struct LinkValidator<LinkTags::enchant>
{
    /**
     * @brief 验证附魔文本
     *
     * 支持两种文本格式:
     * - 直接法术名称
     * - "技能名称: 法术名称"格式
     *
     * @param info 法术信息
     * @param text 显示文本
     * @return true 如果文本匹配任一格式
     */
    static bool IsTextValid(SpellInfo const* info, std::string_view text)
    {
        // 首先尝试直接匹配法术名称
        if (LinkValidator<LinkTags::spell>::IsTextValid(info, text))
            return true;

        // 尝试匹配"技能名称: 法术名称"格式
        SkillLineAbilityMapBounds bounds = sSpellMgr->GetSkillLineAbilityMapBounds(info->Id);
        if (bounds.first == bounds.second)
            return false;

        for (auto pair = bounds.first; pair != bounds.second; ++pair)
        {
            SkillLineEntry const* skill = sSkillLineStore.LookupEntry(pair->second->SkillLine);
            if (!skill)
                return false;

            for (uint8 i = 0; i < TOTAL_LOCALES; ++i)
            {
                std::string_view skillName = skill->DisplayName[i];
                std::string_view spellName = info->SpellName[i];
                // 检查格式: [技能名称: 法术名称]
                if ((text.length() == (skillName.length() + 2 + spellName.length())) &&
                    (text.substr(0, skillName.length()) == skillName) &&
                    (text.substr(skillName.length(), 2) == ": ") &&
                    (text.substr(skillName.length() + 2) == spellName))
                    return true;
            }
        }
        return false;
    }

    /**
     * @brief 验证附魔颜色
     *
     * @param info 法术信息
     * @param c 链接颜色
     * @return true 如果颜色为附魔链接的标准颜色
     */
    static bool IsColorValid(SpellInfo const*, HyperlinkColor c)
    {
        return c == CHAT_LINK_COLOR_ENCHANT;
    }
};

/**
 * @struct LinkValidator<LinkTags::glyph>
 * @brief 雕文链接验证器特化
 *
 * 验证雕文链接的文本和颜色
 */
template <>
struct LinkValidator<LinkTags::glyph>
{
    /**
     * @brief 验证雕文文本
     *
     * 使用雕文对应的法术名称进行验证
     *
     * @param data 雕文链接数据
     * @param text 显示文本
     * @return true 如果文本匹配法术名称
     */
    static bool IsTextValid(GlyphLinkData const& data, std::string_view text)
    {
        if (SpellInfo const* info = sSpellMgr->GetSpellInfo(data.Glyph->SpellID))
            return LinkValidator<LinkTags::spell>::IsTextValid(info, text);
        return false;
    }

    /**
     * @brief 验证雕文颜色
     *
     * @param data 雕文链接数据
     * @param c 链接颜色
     * @return true 如果颜色为雕文链接的标准颜色
     */
    static bool IsColorValid(GlyphLinkData const&, HyperlinkColor c)
    {
        return c == CHAT_LINK_COLOR_GLYPH;
    }
};

/**
 * @struct LinkValidator<LinkTags::talent>
 * @brief 天赋链接验证器特化
 *
 * 验证天赋链接的文本和颜色
 */
template <>
struct LinkValidator<LinkTags::talent>
{
    /**
     * @brief 验证天赋文本
     *
     * 使用天赋对应的法术名称进行验证
     *
     * @param data 天赋链接数据
     * @param text 显示文本
     * @return true 如果文本匹配法术名称
     */
    static bool IsTextValid(TalentLinkData const& data, std::string_view text)
    {
        SpellInfo const* info = data.Spell;
        if (!info)
            info = sSpellMgr->GetSpellInfo(data.Talent->SpellRank[0]);
        if (!info)
            return false;
        return LinkValidator<LinkTags::spell>::IsTextValid(info, text);
    }

    /**
     * @brief 验证天赋颜色
     *
     * @param data 天赋链接数据
     * @param c 链接颜色
     * @return true 如果颜色为天赋链接的标准颜色
     */
    static bool IsColorValid(TalentLinkData const&, HyperlinkColor c)
    {
        return c == CHAT_LINK_COLOR_TALENT;
    }
};

/**
 * @struct LinkValidator<LinkTags::trade>
 * @brief 商业技能链接验证器特化
 *
 * 验证商业技能链接的文本和颜色
 */
template <>
struct LinkValidator<LinkTags::trade>
{
    /**
     * @brief 验证商业技能文本
     *
     * 使用商业技能对应的法术名称进行验证
     *
     * @param data 商业技能链接数据
     * @param text 显示文本
     * @return true 如果文本匹配法术名称
     */
    static bool IsTextValid(TradeskillLinkData const& data, std::string_view text)
    {
        return LinkValidator<LinkTags::spell>::IsTextValid(data.Spell, text);
    }

    /**
     * @brief 验证商业技能颜色
     *
     * @param data 商业技能链接数据
     * @param c 链接颜色
     * @return true 如果颜色为商业技能链接的标准颜色
     */
    static bool IsColorValid(TradeskillLinkData const&, HyperlinkColor c)
    {
        return c == CHAT_LINK_COLOR_TRADE;
    }
};

/**
 * @brief 验证链接为指定类型
 *
 * 尝试将链接数据存储为指定类型并验证其有效性
 *
 * @tparam TAG 链接标签类型
 * @param info 链接信息
 * @return true 如果验证通过
 * @return false 如果验证失败
 *
 * @note 验证级别由CONFIG_CHAT_STRICT_LINK_CHECKING_SEVERITY配置控制:
 *       - severity >= 0: 验证颜色
 *       - severity >= 1: 同时验证文本
 */
template <typename TAG>
static bool ValidateAs(HyperlinkInfo const& info)
{
    std::decay_t<typename TAG::value_type> t;
    // 尝试存储链接数据
    if (!TAG::StoreTo(t, info.data))
        return false;

    // 根据配置的验证严格程度进行验证
    int32 const severity = static_cast<int32>(sWorld->getIntConfig(CONFIG_CHAT_STRICT_LINK_CHECKING_SEVERITY));
    if (severity >= 0)
    {
        // 验证颜色
        if (!LinkValidator<TAG>::IsColorValid(t, info.color))
            return false;
        // 如果严格程度>=1,验证文本
        if (severity >= 1)
        {
            if (!LinkValidator<TAG>::IsTextValid(t, info.text))
                return false;
        }
    }
    return true;
}

/// 尝试验证为指定类型的宏
#define TryValidateAs(T) do { if (info.tag == T::tag()) return ValidateAs<T>(info); } while (0);

/**
 * @brief 验证链接信息
 *
 * 根据链接标签类型分派到相应的验证器
 *
 * @param info 链接信息
 * @return true 如果验证通过
 * @return false 如果验证失败或未知链接类型
 */
static bool ValidateLinkInfo(HyperlinkInfo const& info)
{
    using namespace LinkTags;
    // 尝试所有已知的链接类型
    TryValidateAs(achievement);
    TryValidateAs(area);
    TryValidateAs(areatrigger);
    TryValidateAs(creature);
    TryValidateAs(creature_entry);
    TryValidateAs(enchant);
    TryValidateAs(gameevent);
    TryValidateAs(gameobject);
    TryValidateAs(gameobject_entry);
    TryValidateAs(glyph);
    TryValidateAs(item);
    TryValidateAs(itemset);
    TryValidateAs(player);
    TryValidateAs(quest);
    TryValidateAs(skill);
    TryValidateAs(spell);
    TryValidateAs(talent);
    TryValidateAs(taxinode);
    TryValidateAs(tele);
    TryValidateAs(title);
    TryValidateAs(trade);
    return false;
}

/**
 * @brief 检查所有超链接和控制序列
 *
 * 验证字符串中的所有超链接和控制序列是否有效。
 * 分两步进行:
 * 1. 检查所有控制序列,只允许 ||, |H, |h, |c, |r
 * 2. 解析并验证所有链接序列
 *
 * @param str 要检查的字符串
 * @return true 如果所有链接和控制序列都有效
 * @return false 如果发现无效的链接或控制序列
 *
 * @note 调用时机:验证玩家输入的聊天消息时,防止恶意链接
 * @note 性能注意事项:会遍历整个字符串两次,对长字符串可能有性能影响
 */
bool Trinity::Hyperlinks::CheckAllLinks(std::string_view str)
{
    // 步骤1: 禁止除 ||, |H, |h, |c 和 |r 之外的所有控制序列
    {
        std::string_view::size_type pos = 0;
        while ((pos = str.find('|', pos)) != std::string::npos)
        {
            ++pos;
            if (pos == str.length())
                return false;
            char next = str[pos];
            // 只允许特定的控制序列
            if (next == 'H' || next == 'h' || next == 'c' || next == 'r' || next == '|')
                ++pos;
            else
                return false;
        }
    }

    // 步骤2: 解析所有链接序列
    // 链接格式: |c<color>|H<linktag>:<linkdata>|h[<linktext>]|h|r
    // - <color> 是8个十六进制字符 AARRGGBB
    // - <linktag> 是任意长度的[a-z_]
    // - <linkdata> 是任意长度,不包含|
    // - <linktext> 是可打印字符
    {
        std::string::size_type pos;
        while ((pos = str.find('|')) != std::string::npos)
        {
            // 检查是否为转义的管道字符(||)
            if (str[pos + 1] == '|')
            {
                str = str.substr(pos + 2);
                continue;
            }

            // 解析并验证单个超链接
            HyperlinkInfo info = ParseSingleHyperlink(str.substr(pos));
            if (!info || !ValidateLinkInfo(info))
                return false;

            // 链接有效,继续处理剩余部分
            str = info.tail;
        }
    }

    // 所有标签都有效
    return true;
}
