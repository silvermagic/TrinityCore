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
 * @file enuminfo_AuctionHouseBot.cpp
 * @brief 拍卖行机器人枚举工具自动生成文件
 *
 * 本文件由TrinityCore自动生成系统创建,提供了拍卖行机器人相关枚举类型的序列化、
 * 反序列化和字符串转换功能。主要支持以下枚举类型:
 * - AuctionQuality: 拍卖物品品质等级
 * - AuctionHouseType: 拍卖行类型(联盟/部落/中立)
 *
 * 这些工具函数用于配置文件解析、命令行参数处理和日志输出等场景。
 */

#include "AuctionHouseBot.h"
#include "Define.h"
#include "SmartEnum.h"
#include <stdexcept>

namespace Trinity::Impl::EnumUtilsImpl
{

/************************************************************************\
|* data for enum 'AuctionQuality' in 'AuctionHouseBot.h' auto-generated *|
\************************************************************************/

/**
 * @brief 将拍卖品质枚举值转换为文本表示
 * @param value 拍卖品质枚举值
 * @return EnumText结构体,包含枚举的名称、显示文本和描述
 * @throws std::out_of_range 当传入的枚举值无效时抛出异常
 *
 * 此函数用于将AuctionQuality枚举值转换为可读的字符串形式,
 * 主要用于日志输出、配置文件序列化和调试信息显示。
 */
template <>
TC_API_EXPORT EnumText EnumUtils<AuctionQuality>::ToString(AuctionQuality value)
{
    switch (value)
    {
        case AUCTION_QUALITY_GRAY: return { "AUCTION_QUALITY_GRAY", "AUCTION_QUALITY_GRAY", "" };
        case AUCTION_QUALITY_WHITE: return { "AUCTION_QUALITY_WHITE", "AUCTION_QUALITY_WHITE", "" };
        case AUCTION_QUALITY_GREEN: return { "AUCTION_QUALITY_GREEN", "AUCTION_QUALITY_GREEN", "" };
        case AUCTION_QUALITY_BLUE: return { "AUCTION_QUALITY_BLUE", "AUCTION_QUALITY_BLUE", "" };
        case AUCTION_QUALITY_PURPLE: return { "AUCTION_QUALITY_PURPLE", "AUCTION_QUALITY_PURPLE", "" };
        case AUCTION_QUALITY_ORANGE: return { "AUCTION_QUALITY_ORANGE", "AUCTION_QUALITY_ORANGE", "" };
        case AUCTION_QUALITY_YELLOW: return { "AUCTION_QUALITY_YELLOW", "AUCTION_QUALITY_YELLOW", "" };
        default: throw std::out_of_range("value");
    }
}

/**
 * @brief 获取拍卖品质枚举的元素总数
 * @return 返回AuctionQuality枚举类型的元素数量(7)
 *
 * 用于枚举遍历和边界检查,表示品质从灰色(垃圾)到黄色(神器)共7个等级。
 */
template <>
TC_API_EXPORT size_t EnumUtils<AuctionQuality>::Count() { return 7; }

/**
 * @brief 将索引转换为拍卖品质枚举值
 * @param index 索引值(0-6)
 * @return 对应的AuctionQuality枚举值
 * @throws std::out_of_range 当索引超出有效范围时抛出异常
 *
 * 用于枚举值的序列化和数组索引访问,将数值索引映射回枚举类型。
 */
template <>
TC_API_EXPORT AuctionQuality EnumUtils<AuctionQuality>::FromIndex(size_t index)
{
    switch (index)
    {
        case 0: return AUCTION_QUALITY_GRAY;
        case 1: return AUCTION_QUALITY_WHITE;
        case 2: return AUCTION_QUALITY_GREEN;
        case 3: return AUCTION_QUALITY_BLUE;
        case 4: return AUCTION_QUALITY_PURPLE;
        case 5: return AUCTION_QUALITY_ORANGE;
        case 6: return AUCTION_QUALITY_YELLOW;
        default: throw std::out_of_range("index");
    }
}

/**
 * @brief 将拍卖品质枚举值转换为索引
 * @param value 拍卖品质枚举值
 * @return 对应的索引值(0-6)
 * @throws std::out_of_range 当枚举值无效时抛出异常
 *
 * 用于枚举值的数组索引映射,便于在数组中存储和查找枚举相关数据。
 */
template <>
TC_API_EXPORT size_t EnumUtils<AuctionQuality>::ToIndex(AuctionQuality value)
{
    switch (value)
    {
        case AUCTION_QUALITY_GRAY: return 0;
        case AUCTION_QUALITY_WHITE: return 1;
        case AUCTION_QUALITY_GREEN: return 2;
        case AUCTION_QUALITY_BLUE: return 3;
        case AUCTION_QUALITY_PURPLE: return 4;
        case AUCTION_QUALITY_ORANGE: return 5;
        case AUCTION_QUALITY_YELLOW: return 6;
        default: throw std::out_of_range("value");
    }
}

/**************************************************************************\
|* data for enum 'AuctionHouseType' in 'AuctionHouseBot.h' auto-generated *|
\**************************************************************************/

/**
 * @brief 将拍卖行类型枚举值转换为文本表示
 * @param value 拍卖行类型枚举值
 * @return EnumText结构体,包含枚举的名称、显示文本和描述
 * @throws std::out_of_range 当传入的枚举值无效时抛出异常
 *
 * 此函数用于将AuctionHouseType枚举值转换为可读的字符串形式,
 * 用于区分中立、联盟和部落三种不同阵营的拍卖行。
 */
template <>
TC_API_EXPORT EnumText EnumUtils<AuctionHouseType>::ToString(AuctionHouseType value)
{
    switch (value)
    {
        case AUCTION_HOUSE_NEUTRAL: return { "AUCTION_HOUSE_NEUTRAL", "AUCTION_HOUSE_NEUTRAL", "" };
        case AUCTION_HOUSE_ALLIANCE: return { "AUCTION_HOUSE_ALLIANCE", "AUCTION_HOUSE_ALLIANCE", "" };
        case AUCTION_HOUSE_HORDE: return { "AUCTION_HOUSE_HORDE", "AUCTION_HOUSE_HORDE", "" };
        default: throw std::out_of_range("value");
    }
}

/**
 * @brief 获取拍卖行类型枚举的元素总数
 * @return 返回AuctionHouseType枚举类型的元素数量(3)
 *
 * 表示三种拍卖行类型:中立拍卖行、联盟拍卖行和部落拍卖行。
 */
template <>
TC_API_EXPORT size_t EnumUtils<AuctionHouseType>::Count() { return 3; }

/**
 * @brief 将索引转换为拍卖行类型枚举值
 * @param index 索引值(0-2)
 * @return 对应的AuctionHouseType枚举值
 * @throws std::out_of_range 当索引超出有效范围时抛出异常
 *
 * 用于将数值索引映射回拍卖行类型枚举,便于数组和枚举之间的转换。
 */
template <>
TC_API_EXPORT AuctionHouseType EnumUtils<AuctionHouseType>::FromIndex(size_t index)
{
    switch (index)
    {
        case 0: return AUCTION_HOUSE_NEUTRAL;
        case 1: return AUCTION_HOUSE_ALLIANCE;
        case 2: return AUCTION_HOUSE_HORDE;
        default: throw std::out_of_range("index");
    }
}

/**
 * @brief 将拍卖行类型枚举值转换为索引
 * @param value 拍卖行类型枚举值
 * @return 对应的索引值(0-2)
 * @throws std::out_of_range 当枚举值无效时抛出异常
 *
 * 用于将拍卖行类型映射为数组索引,便于在数组中存储拍卖行相关数据。
 */
template <>
TC_API_EXPORT size_t EnumUtils<AuctionHouseType>::ToIndex(AuctionHouseType value)
{
    switch (value)
    {
        case AUCTION_HOUSE_NEUTRAL: return 0;
        case AUCTION_HOUSE_ALLIANCE: return 1;
        case AUCTION_HOUSE_HORDE: return 2;
        default: throw std::out_of_range("value");
    }
}
}
