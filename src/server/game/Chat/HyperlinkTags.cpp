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
 * @file HyperlinkTags.cpp
 * @brief 超链接标签实现文件
 *
 * 本文件实现了各种游戏内超链接标签的数据存储和解析功能,包括:
 * - 成就链接(achievement)
 * - 附魔链接(enchant)
 * - 雕文链接(glyph)
 * - 物品链接(item)
 * - 任务链接(quest)
 * - 法术链接(spell)
 * - 天赋链接(talent)
 * - 商业技能链接(trade)
 *
 * 超链接数据格式通常为: tag:data1:data2:...
 * 例如: item:12345:0:0:0:0:0:0:0
 */

#include "Hyperlinks.h"
#include "AchievementMgr.h"
#include "ObjectMgr.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include <limits>

/// 超链接数据分隔符
static constexpr char HYPERLINK_DATA_DELIMITER = ':';

/**
 * @class HyperlinkDataTokenizer
 * @brief 超链接数据分词器
 *
 * 用于解析超链接数据字符串,按分隔符分割并提取各个字段。
 * 类似于字符串的tokenize操作,但专门用于超链接数据格式。
 */
class HyperlinkDataTokenizer
{
    public:
        /**
         * @brief 构造函数
         * @param str 要解析的字符串视图
         */
        HyperlinkDataTokenizer(std::string_view str) : _str(str) {}

        /**
         * @brief 尝试消费并转换下一个数据字段
         *
         * 从字符串中提取下一个字段(直到分隔符或字符串结尾),
         * 并将其转换为指定类型
         *
         * @tparam T 目标类型
         * @param val 输出参数,存储转换后的值
         * @return true 如果成功提取并转换
         * @return false 如果失败
         */
        template <typename T>
        bool TryConsumeTo(T& val)
        {
            if (IsEmpty())
                return false;

            // 查找分隔符位置
            if (size_t off = _str.find(HYPERLINK_DATA_DELIMITER); off != std::string_view::npos)
            {
                // 找到分隔符,提取分隔符前的数据
                if (!Trinity::Hyperlinks::LinkTags::base_tag::StoreTo(val, _str.substr(0, off)))
                    return false;
                // 跳过分隔符,继续处理剩余部分
                _str = _str.substr(off+1);
            }
            else
            {
                // 没有找到分隔符,处理整个字符串
                if (!Trinity::Hyperlinks::LinkTags::base_tag::StoreTo(val, _str))
                    return false;
                _str = std::string_view();
            }
            return true;
        }

        /**
         * @brief 检查是否已处理完所有数据
         * @return true 如果字符串为空
         */
        bool IsEmpty() { return _str.empty(); }

    private:
        std::string_view _str;  ///< 待处理的字符串视图
};

/**
 * @brief 解析成就链接数据
 *
 * 成就链接格式: achievement:achievementId:characterId:isFinished:month:day:year:criteria[0]:criteria[1]:criteria[2]:criteria[3]
 *
 * @param val 输出参数,存储解析结果
 * @param text 输入的链接数据字符串
 * @return true 如果解析成功
 * @return false 如果解析失败
 *
 * @note 数据验证:
 *       - 成就必须存在
 *       - 月份必须在1-12之间
 *       - 日期必须在1-31之间
 *       - 如果已完成,年份必须>=0
 */
bool Trinity::Hyperlinks::LinkTags::achievement::StoreTo(AchievementLinkData& val, std::string_view text)
{
    HyperlinkDataTokenizer t(text);

    // 提取成就ID并查找成就定义
    uint32 achievementId;
    if (!t.TryConsumeTo(achievementId))
        return false;
    val.Achievement = sAchievementMgr->GetAchievement(achievementId);

    // 提取角色ID、完成状态和日期
    if (!(val.Achievement && t.TryConsumeTo(val.CharacterId) && t.TryConsumeTo(val.IsFinished) && t.TryConsumeTo(val.Month) && t.TryConsumeTo(val.Day)))
        return false;

    // 验证月份和日期的合理性
    if ((12 < val.Month) || (31 < val.Day))
        return false;

    // 提取年份(有符号数,因为未完成的成就是-1)
    int8 year;
    if (!t.TryConsumeTo(year))
        return false;

    if (val.IsFinished) // 如果已完成,年份必须>=0
    {
        if (year < 0)
            return false;
        val.Year = static_cast<uint8>(year);
    }
    else
        val.Year = 0;

    // 提取四个成就条件进度
    return (t.TryConsumeTo(val.Criteria[0]) && t.TryConsumeTo(val.Criteria[1]) && t.TryConsumeTo(val.Criteria[2]) && t.TryConsumeTo(val.Criteria[3]) && t.IsEmpty());
}

/**
 * @brief 解析附魔链接数据
 *
 * 附魔链接格式: enchant:spellId
 *
 * @param val 输出参数,存储法术信息指针
 * @param text 输入的链接数据字符串
 * @return true 如果解析成功且法术具有TRADESPELL属性
 * @return false 如果解析失败或不是商业技能法术
 *
 * @note 只接受具有SPELL_ATTR0_TRADESPELL属性的法术
 */
bool Trinity::Hyperlinks::LinkTags::enchant::StoreTo(SpellInfo const*& val, std::string_view text)
{
    HyperlinkDataTokenizer t(text);
    uint32 spellId;
    // 必须只有一个字段(spellId)且没有额外数据
    if (!(t.TryConsumeTo(spellId) && t.IsEmpty()))
        return false;

    // 获取法术信息并验证是否为商业技能法术
    return (val = sSpellMgr->GetSpellInfo(spellId)) && val->HasAttribute(SPELL_ATTR0_TRADESPELL);
}

/**
 * @brief 解析雕文链接数据
 *
 * 雕文链接格式: glyph:slotId:propertyId
 *
 * @param val 输出参数,存储雕文槽位和属性
 * @param text 输入的链接数据字符串
 * @return true 如果解析成功且槽位和属性都有效
 * @return false 如果解析失败或数据无效
 */
bool Trinity::Hyperlinks::LinkTags::glyph::StoreTo(GlyphLinkData& val, std::string_view text)
{
    HyperlinkDataTokenizer t(text);
    uint32 slot, prop;
    // 必须恰好有两个字段
    if (!(t.TryConsumeTo(slot) && t.TryConsumeTo(prop) && t.IsEmpty()))
        return false;

    // 查找雕文槽位和属性定义
    if (!(val.Slot = sGlyphSlotStore.LookupEntry(slot)))
        return false;
    if (!(val.Glyph = sGlyphPropertiesStore.LookupEntry(prop)))
        return false;
    return true;
}

bool Trinity::Hyperlinks::LinkTags::item::StoreTo(ItemLinkData& val, std::string_view text)
{
    HyperlinkDataTokenizer t(text);
    uint32 itemId, dummy;
    if (!t.TryConsumeTo(itemId))
        return false;
    val.Item = sObjectMgr->GetItemTemplate(itemId);
    val.IsBuggedInspectLink = false;

    // randomPropertyId is actually a int16 in the client
    // positive values index ItemRandomSuffix.dbc, while negative values index ItemRandomProperties.dbc
    // however, there is also a client bug in inspect packet handling that causes a int16 to be cast to uint16, then int32 (dropping sign extension along the way)
    // this results in the wrong value being sent in the link; DBC lookup clientside fails, so it sends the link without suffix
    // to detect and allow these invalid links, we first read randomPropertyId as a full int32
    int32 randomPropertyId;
    if (!(val.Item && t.TryConsumeTo(val.EnchantId) && t.TryConsumeTo(val.GemEnchantId[0]) && t.TryConsumeTo(val.GemEnchantId[1]) &&
        t.TryConsumeTo(val.GemEnchantId[2]) && t.TryConsumeTo(dummy) && t.TryConsumeTo(randomPropertyId) && t.TryConsumeTo(val.RandomSuffixBaseAmount) &&
        t.TryConsumeTo(val.RenderLevel) && t.IsEmpty() && !dummy))
        return false;

    if ((static_cast<int32>(std::numeric_limits<int16>::max()) < randomPropertyId) && (randomPropertyId <= std::numeric_limits<uint16>::max()))
    { // this is the bug case, the id we received is actually static_cast<uint16>(i16RandomPropertyId)
        randomPropertyId = static_cast<int16>(randomPropertyId);
        val.IsBuggedInspectLink = true;
    }

    if (randomPropertyId < 0)
    {
        if (!val.Item->RandomSuffix)
            return false;
        if (randomPropertyId < -static_cast<int32>(sItemRandomSuffixStore.GetNumRows()))
            return false;
        if (ItemRandomSuffixEntry const* suffixEntry = sItemRandomSuffixStore.LookupEntry(-randomPropertyId))
        {
            val.RandomSuffix = suffixEntry;
            val.RandomProperty = nullptr;
        }
        else
            return false;
    }
    else if (randomPropertyId > 0)
    {
        if (!val.Item->RandomProperty)
            return false;
        if (ItemRandomPropertiesEntry const* propEntry = sItemRandomPropertiesStore.LookupEntry(randomPropertyId))
        {
            val.RandomSuffix = nullptr;
            val.RandomProperty = propEntry;
        }
        else
            return false;
    }
    else
    {
        val.RandomSuffix = nullptr;
        val.RandomProperty = nullptr;
    }

    if ((val.RandomSuffix && !val.RandomSuffixBaseAmount) || (val.RandomSuffixBaseAmount && !val.RandomSuffix))
        return false;

    return true;
}

bool Trinity::Hyperlinks::LinkTags::quest::StoreTo(QuestLinkData& val, std::string_view text)
{
    HyperlinkDataTokenizer t(text);
    uint32 questId;
    if (!t.TryConsumeTo(questId))
        return false;
    return (val.Quest = sObjectMgr->GetQuestTemplate(questId)) && t.TryConsumeTo(val.QuestLevel) && (val.QuestLevel >= -1) && t.IsEmpty();
}

bool Trinity::Hyperlinks::LinkTags::spell::StoreTo(SpellInfo const*& val, std::string_view text)
{
    HyperlinkDataTokenizer t(text);
    uint32 spellId;
    if (!(t.TryConsumeTo(spellId) && t.IsEmpty()))
        return false;
    return !!(val = sSpellMgr->GetSpellInfo(spellId));
}

bool Trinity::Hyperlinks::LinkTags::talent::StoreTo(TalentLinkData& val, std::string_view text)
{
    HyperlinkDataTokenizer t(text);
    uint32 talentId;
    int8 rank; // talent links contain <learned rank>-1, we store <learned rank>
    if (!(t.TryConsumeTo(talentId) && t.TryConsumeTo(rank) && t.IsEmpty()))
        return false;
    if (rank < -1 || rank >= MAX_TALENT_RANK)
        return false;
    val.Talent = sTalentStore.LookupEntry(talentId);
    val.Rank = rank + 1;
    if (!val.Talent)
        return false;
    val.Spell = sSpellMgr->GetSpellInfo(val.Talent->SpellRank[std::max<int32>(rank, 0)]);
    if (!val.Spell)
        return false;
    return true;
}

bool Trinity::Hyperlinks::LinkTags::trade::StoreTo(TradeskillLinkData& val, std::string_view text)
{
    HyperlinkDataTokenizer t(text);
    uint32 spellId;
    if (!t.TryConsumeTo(spellId))
        return false;
    val.Spell = sSpellMgr->GetSpellInfo(spellId);
    return (val.Spell && val.Spell->GetEffect(EFFECT_0).Effect == SPELL_EFFECT_TRADE_SKILL && t.TryConsumeTo(val.CurValue) &&
        t.TryConsumeTo(val.MaxValue) && t.TryConsumeTo(val.Owner) && t.TryConsumeTo(val.KnownRecipes) && t.IsEmpty());
}
