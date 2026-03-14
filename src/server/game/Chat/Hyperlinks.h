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
 * @file Hyperlinks.h
 * @brief 超链接系统头文件
 *
 * 本文件定义了游戏内超链接的数据结构和处理接口,包括:
 * - 各种链接数据结构(成就、物品、任务、法术等)
 * - 链接标签系统(用于解析和验证不同类型的链接)
 * - 超链接解析函数
 *
 * 超链接格式: |c<颜色>|H<标签>:<数据>|h[<文本>]|h|r
 *
 * 主要功能:
 * - 解析超链接字符串
 * - 验证超链接数据的正确性
 * - 提取超链接中的游戏对象信息
 */

#ifndef TRINITY_HYPERLINKS_H
#define TRINITY_HYPERLINKS_H

#include "ObjectGuid.h"
#include "StringConvert.h"
#include <array>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

struct AchievementEntry;
struct GlyphPropertiesEntry;
struct GlyphSlotEntry;
struct ItemRandomPropertiesEntry;
struct ItemRandomSuffixEntry;
struct ItemTemplate;
class SpellInfo;
class Quest;
struct TalentEntry;

namespace Trinity::Hyperlinks
{

    /**
     * @struct AchievementLinkData
     * @brief 成就链接数据结构
     *
     * 存储从成就超链接中提取的所有信息
     */
    struct AchievementLinkData
    {
        AchievementEntry const* Achievement;  ///< 成就定义条目
        ObjectGuid CharacterId;               ///< 完成成就的角色ID
        bool IsFinished;                      ///< 是否已完成
        uint8 Year;                           ///< 完成年份
        uint8 Month;                          ///< 完成月份
        uint8 Day;                            ///< 完成日期
        std::array<uint32, 4> Criteria;       ///< 成就条件进度
    };

    /**
     * @struct GlyphLinkData
     * @brief 雕文链接数据结构
     *
     * 存储从雕文超链接中提取的信息
     */
    struct GlyphLinkData
    {
        GlyphPropertiesEntry const* Glyph;    ///< 雕文属性
        GlyphSlotEntry const* Slot;           ///< 雕文槽位
    };

    /**
     * @struct ItemLinkData
     * @brief 物品链接数据结构
     *
     * 存储从物品超链接中提取的所有信息
     * 包括物品本身、附魔、宝石、随机属性等
     */
    struct ItemLinkData
    {
        ItemTemplate const* Item;                         ///< 物品模板
        uint32 EnchantId;                                 ///< 附魔ID
        std::array<uint32, 3> GemEnchantId;               ///< 宝石附魔ID(最多3个)
        ItemRandomPropertiesEntry const* RandomProperty;  ///< 随机属性
        ItemRandomSuffixEntry const* RandomSuffix;        ///< 随机后缀
        uint32 RandomSuffixBaseAmount;                    ///< 随机后缀基础值(ITEM_FIELD_PROPERTY_SEED)
                                                          ///< 仅对RandomSuffix物品非零,DBC中的AllocationPct与此值相乘并取整得到属性值
        uint8 RenderLevel;                                ///< 渲染等级
        bool IsBuggedInspectLink;                         ///< 是否为有BUG的观察链接
    };

    /**
     * @struct QuestLinkData
     * @brief 任务链接数据结构
     *
     * 存储从任务超链接中提取的信息
     */
    struct QuestLinkData
    {
        ::Quest const* Quest;    ///< 任务定义
        int16 QuestLevel;        ///< 任务等级
    };

    /**
     * @struct TalentLinkData
     * @brief 天赋链接数据结构
     *
     * 存储从天赋超链接中提取的信息
     */
    struct TalentLinkData
    {
        TalentEntry const* Talent;  ///< 天赋条目
        uint8 Rank;                 ///< 天赋等级
        SpellInfo const* Spell;     ///< 天赋对应的法术
    };

    /**
     * @struct TradeskillLinkData
     * @brief 商业技能链接数据结构
     *
     * 存储从商业技能超链接中提取的信息
     */
    struct TradeskillLinkData
    {
        SpellInfo const* Spell;      ///< 商业技能法术
        uint16 CurValue;             ///< 当前技能值
        uint16 MaxValue;             ///< 最大技能值
        ObjectGuid Owner;            ///< 拥有者GUID
        std::string KnownRecipes;    ///< 已知配方列表
    };

    namespace LinkTags {

        /************************** LINK TAGS ***************************************************\
        |* 链接标签系统                                                                          *|
        |* 链接标签必须遵守以下规则:                                                             *|
        |* - 必须公开 ::value_type typedef                                                      *|
        |*   - 存储类型为 remove_cvref_t<value_type>                                            *|
        |* - 必须公开静态 ::tag 方法, void -> std::string_view                                  *|
        |*   - 此方法应该是 constexpr                                                           *|
        |*   - 返回链接的标识字符串(如"creature", "creature_entry", "item")                     *|
        |* - 必须公开静态 ::StoreTo 方法, (storage&, std::string_view)                          *|
        |*   - 根据 std::string_view 的内容赋值给 storage&                                       *|
        |*   - 返回值表示成功/失败                                                              *|
        |*   - 对于整型/字符串类型,可以通过继承 base_tag 实现                                    *|
        \****************************************************************************************/

        /**
         * @struct base_tag
         * @brief 链接标签基类
         *
         * 提供基本类型的存储功能,包括字符串、整型和ObjectGuid
         */
        struct base_tag
        {
            /**
             * @brief 存储字符串视图
             * @param val 输出值
             * @param data 输入数据
             * @return true 始终成功
             */
            static bool StoreTo(std::string_view& val, std::string_view data)
            {
                val = data;
                return true;
            }

            /**
             * @brief 存储字符串
             * @param val 输出值
             * @param data 输入数据
             * @return true 始终成功
             */
            static bool StoreTo(std::string& val, std::string_view data)
            {
                val.assign(data);
                return true;
            }

            /**
             * @brief 存储整型值
             *
             * @tparam T 整型类型
             * @param val 输出值
             * @param data 输入数据(字符串形式)
             * @return true 如果转换成功
             * @return false 如果转换失败
             */
            template <typename T>
            static std::enable_if_t<std::is_integral_v<T>, bool> StoreTo(T& val, std::string_view data)
            {
                if (Optional<T> res = Trinity::StringTo<T>(data))
                {
                    val = *res;
                    return true;
                }
                else
                    return false;
            }

            /**
             * @brief 存储ObjectGuid
             *
             * @param val 输出值
             * @param data 输入数据(十六进制字符串)
             * @return true 如果转换成功
             * @return false 如果转换失败
             */
            static bool StoreTo(ObjectGuid& val, std::string_view data)
            {
                if (Optional<uint64> res = Trinity::StringTo<uint64>(data, 16))
                {
                    val.Set(*res);
                    return true;
                }
                else
                    return false;
            }
        };

        /**
         * @brief 创建基础链接标签的宏
         *
         * 用于快速创建简单类型的链接标签
         *
         * @param ltag 标签名
         * @param type 值类型
         */
    #define make_base_tag(ltag, type) struct ltag : public base_tag { using value_type = type; static constexpr std::string_view tag() { return #ltag; } }
        make_base_tag(area, uint32);                          ///< 区域标签
        make_base_tag(areatrigger, uint32);                   ///< 区域触发器标签
        make_base_tag(creature, ObjectGuid::LowType);         ///< 生物标签(使用GUID)
        make_base_tag(creature_entry, uint32);                ///< 生物标签(使用条目ID)
        make_base_tag(gameevent, uint16);                     ///< 游戏事件标签
        make_base_tag(gameobject, ObjectGuid::LowType);       ///< 游戏对象标签(使用GUID)
        make_base_tag(gameobject_entry, uint32);              ///< 游戏对象标签(使用条目ID)
        make_base_tag(itemset, uint32);                       ///< 物品套装标签
        make_base_tag(player, std::string_view);              ///< 玩家标签
        make_base_tag(skill, uint32);                         ///< 技能标签
        make_base_tag(taxinode, uint32);                      ///< 出租车节点标签
        make_base_tag(tele, uint32);                          ///< 传送点标签
        make_base_tag(title, uint32);                         ///< 称号标签
    #undef make_base_tag

        /**
         * @struct achievement
         * @brief 成就链接标签
         */
        struct TC_GAME_API achievement
        {
            using value_type = AchievementLinkData const&;
            static constexpr std::string_view tag() { return "achievement"; }
            static bool StoreTo(AchievementLinkData& val, std::string_view data);
        };

        /**
         * @struct enchant
         * @brief 附魔链接标签
         */
        struct TC_GAME_API enchant
        {
            using value_type = SpellInfo const*;
            static constexpr std::string_view tag() { return "enchant"; }
            static bool StoreTo(SpellInfo const*& val, std::string_view data);
        };

        /**
         * @struct glyph
         * @brief 雕文链接标签
         */
        struct TC_GAME_API glyph
        {
            using value_type = GlyphLinkData const&;
            static constexpr std::string_view tag() { return "glyph"; };
            static bool StoreTo(GlyphLinkData& val, std::string_view data);
        };

        /**
         * @struct item
         * @brief 物品链接标签
         */
        struct TC_GAME_API item
        {
            using value_type = ItemLinkData const&;
            static constexpr std::string_view tag() { return "item"; }
            static bool StoreTo(ItemLinkData& val, std::string_view data);
        };

        /**
         * @struct quest
         * @brief 任务链接标签
         */
        struct TC_GAME_API quest
        {
            using value_type = QuestLinkData const&;
            static constexpr std::string_view tag() { return "quest"; }
            static bool StoreTo(QuestLinkData& val, std::string_view data);
        };

        /**
         * @struct spell
         * @brief 法术链接标签
         */
        struct TC_GAME_API spell
        {
            using value_type = SpellInfo const*;
            static constexpr std::string_view tag() { return "spell"; }
            static bool StoreTo(SpellInfo const*& val, std::string_view data);
        };

        /**
         * @struct talent
         * @brief 天赋链接标签
         */
        struct TC_GAME_API talent
        {
            using value_type = TalentLinkData const&;
            static constexpr std::string_view tag() { return "talent"; }
            static bool StoreTo(TalentLinkData& val, std::string_view data);
        };

        /**
         * @struct trade
         * @brief 商业技能链接标签
         */
        struct TC_GAME_API trade
        {
            using value_type = TradeskillLinkData const&;
            static constexpr std::string_view tag() { return "trade"; }
            static bool StoreTo(TradeskillLinkData& val, std::string_view data);
        };
    }

    /**
     * @struct HyperlinkColor
     * @brief 超链接颜色结构
     *
     * 用于存储和比较超链接颜色值(ARGB格式)
     */
    struct HyperlinkColor
    {
        /**
         * @brief 构造函数
         * @param c 颜色值(ARGB格式)
         */
        HyperlinkColor(uint32 c) : r(c >> 16), g(c >> 8), b(c), a(c >> 24) {}
        uint8 const r, g, b, a;  ///< 红、绿、蓝、透明度分量

        /**
         * @brief 颜色比较操作符
         * @param c 要比较的颜色值
         * @return true 如果颜色相同
         */
        bool operator==(uint32 c) const
        {
            if ((c & 0xff) ^ b)
                return false;
            if (((c >>= 8) & 0xff) ^ g)
                return false;
            if (((c >>= 8) & 0xff) ^ r)
                return false;
            if ((c >>= 8) ^ a)
                return false;
            return true;
        }
    };

    /**
     * @struct HyperlinkInfo
     * @brief 超链接解析信息结构
     *
     * 存储解析超链接后得到的所有信息
     */
    struct HyperlinkInfo
    {
        HyperlinkInfo() : ok(false), color(0) {}
        HyperlinkInfo(std::string_view t, uint32 c, std::string_view ta, std::string_view d, std::string_view te) :
            ok(true), tail(t), color(c), tag(ta), data(d), text(te) {}

        explicit operator bool() { return ok; }
        bool const ok;                       ///< 解析是否成功
        std::string_view const tail;         ///< 链接后的剩余文本
        HyperlinkColor const color;          ///< 链接颜色
        std::string_view const tag;          ///< 链接标签
        std::string_view const data;         ///< 链接数据
        std::string_view const text;         ///< 链接显示文本
    };

    /**
     * @brief 解析单个超链接
     *
     * 从字符串中解析第一个超链接并返回解析结果
     *
     * @param str 包含超链接的字符串
     * @return HyperlinkInfo 解析结果,失败时ok为false
     *
     * @note 调用时机:需要解析单个超链接时
     */
    HyperlinkInfo TC_GAME_API ParseSingleHyperlink(std::string_view str);

    /**
     * @brief 检查所有超链接
     *
     * 验证字符串中的所有超链接和控制序列是否有效
     *
     * @param str 要检查的字符串
     * @return true 如果所有链接都有效
     *
     * @note 调用时机:验证聊天消息或命令参数中的链接时
     * @note 性能注意事项:会扫描整个字符串,对长字符串可能有性能影响
     */
    bool TC_GAME_API CheckAllLinks(std::string_view str);

}

#endif
