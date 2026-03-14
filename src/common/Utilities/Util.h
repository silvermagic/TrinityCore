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
 * @file Util.h
 * @brief 通用工具模块 - 提供字符串处理、时间转换、字符编码等实用工具函数
 *
 * 模块职责：
 *   - 提供字符串分割和标记化功能
 *   - 提供时间格式化和解析功能
 *   - 提供UTF-8和宽字符编码转换功能
 *   - 提供字符分类和大小写转换功能
 *   - 提供字节数组与十六进制字符串转换功能
 *   - 提供IP地址验证功能
 *   - 提供百分比计算和数值比较功能
 *
 * 主要功能分类：
 *   1. 字符串操作：
 *      - Tokenize: 字符串分割
 *      - StringEqualI: 忽略大小写比较字符串
 *      - StringContainsStringI: 忽略大小写查找子串
 *
 *   2. 时间处理：
 *      - secsToTimeString: 秒数转时间字符串
 *      - TimeStringToSecs: 时间字符串转秒数
 *      - TimeToTimestampStr: 时间戳转字符串
 *
 *   3. 编码转换：
 *      - Utf8toWStr: UTF-8转宽字符
 *      - WStrToUtf8: 宽字符转UTF-8
 *      - utf8toConsole: UTF-8转控制台编码
 *
 *   4. 数值计算：
 *      - CalculatePct: 计算百分比
 *      - AddPct: 增加百分比
 *      - ApplyPct: 应用百分比
 *
 *   5. 字符处理：
 *      - isBasicLatinCharacter: 判断是否为基本拉丁字符
 *      - wcharToUpper: 宽字符转大写
 *      - wcharToLower: 宽字符转小写
 */

#ifndef _UTIL_H
#define _UTIL_H

#include "Define.h"
#include "Errors.h"
#include "Optional.h"

#include <array>
#include <string>
#include <string_view>
#include <sstream>
#include <typeinfo>
#include <utility>
#include <vector>

/**
 * @enum TimeFormat
 * @brief 时间格式枚举 - 定义时间字符串的显示格式
 */
enum class TimeFormat : uint8
{
    FullText,       ///< 完整文本格式：1 Days 2 Hours 3 Minutes 4 Seconds
    ShortText,      ///< 简短文本格式：1d 2h 3m 4s
    Numeric         ///< 数字格式：1:2:3:4
};

namespace Trinity
{
    /**
     * @brief 验证操作系统版本是否符合要求
     *
     * 职责：
     *   检查当前操作系统版本是否满足 TrinityCore 运行的最低要求
     *   仅在 Windows 平台上执行版本检查
     *
     * 参数：
     *   无
     *
     * 返回值：
     *   无
     *
     * 主要流程：
     *   1. 在 Windows 平台上检查系统构建号是否满足最低要求
     *   2. 如果版本过低，则终止程序并输出错误信息
     *   3. 非 Windows 平台不做任何操作
     */
    TC_COMMON_API void VerifyOsVersion();

    /**
     * @brief 将字符串按指定分隔符分割成多个标记
     *
     * 职责：
     *   将输入字符串按照指定的分隔符拆分成多个子字符串（标记）
     *   返回所有标记的视图数组，避免不必要的内存拷贝
     *
     * 参数：
     *   str: 要分割的字符串视图
     *   sep: 分隔符字符
     *   keepEmpty: 是否保留空标记
     *
     * 返回值：
     *   包含所有标记的字符串视图向量
     *
     * 主要流程：
     *   1. 遍历字符串，查找所有分隔符位置
     *   2. 提取两个分隔符之间的子字符串作为标记
     *   3. 根据 keepEmpty 参数决定是否跳过空标记
     *   4. 处理最后一个标记（字符串末尾到上一个分隔符之间的内容）
     */
    TC_COMMON_API std::vector<std::string_view> Tokenize(std::string_view str, char sep, bool keepEmpty);

    /* 禁止右值字符串的Tokenize重载，避免返回临时字符串的视图 */
    std::vector<std::string_view> Tokenize(std::string&&, char, bool) = delete;
    std::vector<std::string_view> Tokenize(std::string const&&, char, bool) = delete;

    /* 由于删除了右值重载，需要显式提供C字符串版本的Tokenize */
    inline std::vector<std::string_view> Tokenize(char const* str, char sep, bool keepEmpty) { return Tokenize(std::string_view(str ? str : ""), sep, keepEmpty); }
}

/**
 * @brief 将货币字符串转换为货币数值
 *
 * 职责：
 *   解析游戏货币字符串格式（如 "10g 50s 25c"），转换为铜币总数
 *   格式：数字 + 单位（g=金币、s=银币、c=铜币）
 *
 * 参数：
 *   moneyString: 货币字符串，如 "10g 50s 25c"
 *
 * 返回值：
 *   转换成功返回货币总数（铜币），失败返回 nullopt
 *
 * 主要流程：
 *   1. 使用空格分割字符串
 *   2. 解析每个标记的数字和单位
 *   3. 验证每种单位只出现一次
 *   4. 汇总计算总铜币数（1金=100银=10000铜）
 */
TC_COMMON_API Optional<int32> MoneyStringToMoney(std::string const& moneyString);

#if (defined(WIN32) || defined(_WIN32) || defined(__WIN32__))
/**
 * @brief Windows 平台的线程安全本地时间转换函数
 *
 * 职责：
 *   为 Windows 平台提供 POSIX 标准的 localtime_r 函数实现
 *   将时间戳转换为本地时间的 tm 结构，线程安全版本
 *
 * 参数：
 *   time: 指向时间戳的指针
 *   result: 指向 tm 结构的指针，用于存储转换结果
 *
 * 返回值：
 *   成功返回 result 指针，失败返回 nullptr
 */
TC_COMMON_API struct tm* localtime_r(time_t const* time, struct tm *result);

/**
 * @brief Windows 平台的线程安全 UTC 时间转换函数
 *
 * 职责：
 *   为 Windows 平台提供 POSIX 标准的 gmtime_r 函数实现
 *   将时间戳转换为 UTC 时间的 tm 结构，线程安全版本
 *
 * 参数：
 *   time: 指向时间戳的指针
 *   result: 指向 tm 结构的指针，用于存储转换结果
 *
 * 返回值：
 *   成功返回 result 指针，失败返回 nullptr
 */
TC_COMMON_API struct tm* gmtime_r(time_t const* time, struct tm *result);

/**
 * @brief Windows 平台的时间转换为时间戳函数（UTC）
 *
 * 职责：
 *   为 Windows 平台提供 POSIX 标准的 timegm 函数实现
 *   将 tm 结构（UTC 时间）转换为时间戳
 *
 * 参数：
 *   tm: 指向 tm 结构的指针，包含 UTC 时间信息
 *
 * 返回值：
 *   对应的时间戳
 */
TC_COMMON_API time_t timegm(struct tm* tm);
#endif

/**
 * @brief 获取指定日期中特定小时的时间戳
 *
 * 职责：
 *   计算给定时间所在日期中指定小时（本地时间）的时间戳
 *   可选择只返回在给定时间之后的时间戳
 *
 * 参数：
 *   time: 参考时间戳
 *   hour: 目标小时（0-23）
 *   onlyAfterTime: 是否只返回在 time 之后的时间戳
 *
 * 返回值：
 *   计算出的时间戳
 */
TC_COMMON_API time_t GetLocalHourTimestamp(time_t time, uint8 hour, bool onlyAfterTime = true);

/**
 * @brief 将时间戳分解为本地时间结构
 *
 * 职责：
 *   将 Unix 时间戳转换为本地时间的 tm 结构
 *   包含年、月、日、时、分、秒等信息
 *
 * 参数：
 *   t: Unix 时间戳
 *
 * 返回值：
 *   tm 结构，包含分解后的时间信息
 */
TC_COMMON_API tm TimeBreakdown(time_t t);

/**
 * @brief 将秒数转换为时间字符串
 *
 * 职责：
 *   将给定的秒数格式化为可读的时间字符串
 *   支持多种格式：纯数字、简短文本、完整文本
 *
 * 参数：
 *   timeInSecs: 时间长度（秒）
 *   timeFormat: 输出格式（Numeric/ShortText/FullText）
 *   hoursOnly: 是否只显示小时，忽略分钟和秒
 *
 * 返回值：
 *   格式化后的时间字符串
 */
TC_COMMON_API std::string secsToTimeString(uint64 timeInSecs, TimeFormat timeFormat = TimeFormat::FullText, bool hoursOnly = false);

/**
 * @brief 将时间字符串转换为秒数
 *
 * 职责：
 *   解析时间字符串（如 "1d2h3m4s"），转换为总秒数
 *   支持的单位：d(天)、h(小时)、m(分钟)、s(秒)
 *
 * 参数：
 *   timestring: 时间字符串，如 "1d2h3m4s"
 *
 * 返回值：
 *   总秒数，格式错误返回 0
 */
TC_COMMON_API uint32 TimeStringToSecs(std::string const& timestring);

/**
 * @brief 将时间戳转换为格式化的时间字符串
 *
 * 职责：
 *   将 Unix 时间戳转换为可读的日期时间字符串
 *   格式：YYYY-MM-DD_HH-MM-SS
 *
 * 参数：
 *   t: Unix 时间戳
 *
 * 返回值：
 *   格式化后的时间字符串
 */
TC_COMMON_API std::string TimeToTimestampStr(time_t t);

/**
 * @brief 将时间戳转换为人类可读的字符串
 *
 * 职责：
 *   将 Unix 时间戳转换为本地化的日期时间字符串
 *   使用系统本地化格式显示
 *
 * 参数：
 *   t: Unix 时间戳
 *
 * 返回值：
 *   人类可读的时间字符串
 */
TC_COMMON_API std::string TimeToHumanReadable(time_t t);

// 百分比计算

/**
 * @brief 计算百分比值
 * @tparam T 基础值类型
 * @tparam U 百分比类型
 * @param base 基础值
 * @param pct 百分比
 * @return 返回base的pct百分比
 *
 * 职责：
 *   计算指定值的百分比
 */
template <class T, class U>
inline T CalculatePct(T base, U pct)
{
    return T(base * static_cast<float>(pct) / 100.0f);
}

/**
 * @brief 增加百分比值
 * @tparam T 基础值类型
 * @tparam U 百分比类型
 * @param base 基础值（会被修改）
 * @param pct 百分比
 * @return 返回增加后的值
 *
 * 职责：
 *   将基础值增加指定的百分比
 */
template <class T, class U>
inline T AddPct(T &base, U pct)
{
    return base += CalculatePct(base, pct);
}

/**
 * @brief 应用百分比值
 * @tparam T 基础值类型
 * @tparam U 百分比类型
 * @param base 基础值（会被修改）
 * @param pct 百分比
 * @return 返回应用后的值
 *
 * 职责：
 *   将基础值设置为指定的百分比
 */
template <class T, class U>
inline T ApplyPct(T &base, U pct)
{
    return base = CalculatePct(base, pct);
}

/**
 * @brief 将数值舍入到指定区间
 * @tparam T 数值类型
 * @param num 要舍入的数值（会被修改）
 * @param floor 区间下限
 * @param ceil 区间上限
 * @return 返回舍入后的值
 *
 * 职责：
 *   将数值限制在[floor, ceil]区间内
 */
template <class T>
inline T RoundToInterval(T& num, T floor, T ceil)
{
    return num = std::min(std::max(num, floor), ceil);
}

/**
 * @brief 计算平方值
 * @tparam T 数值类型
 * @param x 要计算平方的值
 * @return 返回x的平方
 */
template <class T>
inline T square(T x) { return x*x; }

// UTF8 处理

/**
 * @brief 将UTF-8字符串转换为宽字符串
 *
 * 职责：
 *   将UTF-8编码的字符串转换为宽字符（wchar_t）编码
 *
 * 参数：
 *   utf8str: UTF-8源字符串视图
 *   wstr: 输出宽字符串
 *
 * 返回值：
 *   成功返回true，失败返回false
 */
TC_COMMON_API bool Utf8toWStr(std::string_view utf8str, std::wstring& wstr);

/**
 * @brief 将UTF-8字符串转换为宽字符串（C风格）
 *
 * 职责：
 *   将UTF-8编码的字符串转换为宽字符（wchar_t）编码
 *   使用预分配的缓冲区
 *
 * 参数：
 *   utf8str: UTF-8源字符串指针
 *   csize: UTF-8字符串的字节长度
 *   wstr: 宽字符输出缓冲区
 *   wsize: 输入时为缓冲区大小，输出时为实际使用的字符数
 *
 * 返回值：
 *   成功返回true，失败返回false
 */
TC_COMMON_API bool Utf8toWStr(char const* utf8str, size_t csize, wchar_t* wstr, size_t& wsize);

/**
 * @brief UTF-8转宽字符串的便捷包装
 */
inline bool Utf8toWStr(std::string_view utf8str, wchar_t* wstr, size_t& wsize)
{
    return Utf8toWStr(utf8str.data(), utf8str.size(), wstr, wsize);
}

/**
 * @brief 将宽字符串转换为UTF-8字符串
 *
 * 职责：
 *   将宽字符（wchar_t）编码的字符串转换为UTF-8编码
 *
 * 参数：
 *   wstr: 宽字符源字符串视图
 *   utf8str: 输出UTF-8字符串
 *
 * 返回值：
 *   成功返回true，失败返回false
 */
TC_COMMON_API bool WStrToUtf8(std::wstring_view wstr, std::string& utf8str);

/**
 * @brief 将宽字符串转换为UTF-8字符串（C风格）
 *
 * 职责：
 *   将宽字符（wchar_t）编码的字符串转换为UTF-8编码
 *
 * 参数：
 *   wstr: 宽字符源字符串指针
 *   size: 宽字符串的实际字符数
 *   utf8str: 输出UTF-8字符串
 *
 * 返回值：
 *   成功返回true，失败返回false
 */
TC_COMMON_API bool WStrToUtf8(wchar_t const* wstr, size_t size, std::string& utf8str);

/**
 * @brief 计算UTF-8字符串的字符长度
 *
 * 职责：
 *   计算UTF-8编码字符串中的字符数量（而非字节数）
 *   正确处理多字节字符
 *
 * 参数：
 *   utf8str: UTF-8编码的字符串（会被修改）
 *
 * 返回值：
 *   字符数量，失败返回0并清空字符串
 */
TC_COMMON_API size_t utf8length(std::string& utf8str);

/**
 * @brief 截断UTF-8字符串到指定字符长度
 *
 * 职责：
 *   将UTF-8字符串截断到指定的字符数量（不是字节数）
 *   确保不会在多字节字符中间截断
 *
 * 参数：
 *   utf8str: 要截断的UTF-8字符串（会被修改）
 *   len: 目标字符数量
 */
TC_COMMON_API void utf8truncate(std::string& utf8str, size_t len);

/**
 * @brief 判断是否为基本拉丁字符（a-z, A-Z）
 * @param wchar 要判断的宽字符
 * @return 如果是基本拉丁字符返回true，否则返回false
 */
inline bool isBasicLatinCharacter(wchar_t wchar)
{
    if (wchar >= L'a' && wchar <= L'z')                      // LATIN SMALL LETTER A - LATIN SMALL LETTER Z
        return true;
    if (wchar >= L'A' && wchar <= L'Z')                      // LATIN CAPITAL LETTER A - LATIN CAPITAL LETTER Z
        return true;
    return false;
}

/**
 * @brief 判断是否为扩展拉丁字符
 * @param wchar 要判断的宽字符
 * @return 如果是扩展拉丁字符返回true，否则返回false
 *
 * 包括基本拉丁字符以及带重音符号的拉丁字符
 */
inline bool isExtendedLatinCharacter(wchar_t wchar)
{
    if (isBasicLatinCharacter(wchar))
        return true;
    if (wchar >= 0x00C0 && wchar <= 0x00D6)                  // LATIN CAPITAL LETTER A WITH GRAVE - LATIN CAPITAL LETTER O WITH DIAERESIS
        return true;
    if (wchar >= 0x00D8 && wchar <= 0x00DE)                  // LATIN CAPITAL LETTER O WITH STROKE - LATIN CAPITAL LETTER THORN
        return true;
    if (wchar == 0x00DF)                                     // LATIN SMALL LETTER SHARP S
        return true;
    if (wchar >= 0x00E0 && wchar <= 0x00F6)                  // LATIN SMALL LETTER A WITH GRAVE - LATIN SMALL LETTER O WITH DIAERESIS
        return true;
    if (wchar >= 0x00F8 && wchar <= 0x00FE)                  // LATIN SMALL LETTER O WITH STROKE - LATIN SMALL LETTER THORN
        return true;
    if (wchar >= 0x0100 && wchar <= 0x012F)                  // LATIN CAPITAL LETTER A WITH MACRON - LATIN SMALL LETTER I WITH OGONEK
        return true;
    if (wchar == 0x1E9E)                                     // LATIN CAPITAL LETTER SHARP S
        return true;
    return false;
}

/**
 * @brief 判断是否为西里尔字符
 * @param wchar 要判断的宽字符
 * @return 如果是西里尔字符返回true，否则返回false
 */
inline bool isCyrillicCharacter(wchar_t wchar)
{
    if (wchar >= 0x0410 && wchar <= 0x044F)                  // CYRILLIC CAPITAL LETTER A - CYRILLIC SMALL LETTER YA
        return true;
    if (wchar == 0x0401 || wchar == 0x0451)                  // CYRILLIC CAPITAL LETTER IO, CYRILLIC SMALL LETTER IO
        return true;
    return false;
}

/**
 * @brief 判断是否为东亚字符
 * @param wchar 要判断的宽字符
 * @return 如果是东亚字符返回true，否则返回false
 *
 * 包括韩文、日文假名、汉字等
 */
inline bool isEastAsianCharacter(wchar_t wchar)
{
    if (wchar >= 0x1100 && wchar <= 0x11F9)                  // Hangul Jamo
        return true;
    if (wchar >= 0x3041 && wchar <= 0x30FF)                  // Hiragana + Katakana
        return true;
    if (wchar >= 0x3131 && wchar <= 0x318E)                  // Hangul Compatibility Jamo
        return true;
    if (wchar >= 0x31F0 && wchar <= 0x31FF)                  // Katakana Phonetic Ext.
        return true;
    if (wchar >= 0x3400 && wchar <= 0x4DB5)                  // CJK Ideographs Ext. A
        return true;
    if (wchar >= 0x4E00 && wchar <= 0x9FC3)                  // Unified CJK Ideographs
        return true;
    if (wchar >= 0xAC00 && wchar <= 0xD7A3)                  // Hangul Syllables
        return true;
    if (wchar >= 0xFF01 && wchar <= 0xFFEE)                  // Halfwidth forms
        return true;
    return false;
}

/**
 * @brief 判断是否为数字字符（宽字符版本）
 * @param wchar 要判断的宽字符
 * @return 如果是数字字符返回true，否则返回false
 */
inline bool isNumeric(wchar_t wchar)
{
    return (wchar >= L'0' && wchar <=L'9');
}

/**
 * @brief 判断是否为数字字符（char版本）
 * @param c 要判断的字符
 * @return 如果是数字字符返回true，否则返回false
 */
inline bool isNumeric(char c)
{
    return (c >= '0' && c <='9');
}

/**
 * @brief 判断字符串是否全为数字
 * @param str 要判断的字符串
 * @return 如果字符串全为数字返回true，否则返回false
 */
inline bool isNumeric(char const* str)
{
    for (char const* c = str; *c; ++c)
        if (!isNumeric(*c))
            return false;

    return true;
}

/**
 * @brief 判断是否为数字或空格字符
 * @param wchar 要判断的宽字符
 * @return 如果是数字或空格返回true，否则返回false
 */
inline bool isNumericOrSpace(wchar_t wchar)
{
    return isNumeric(wchar) || wchar == L' ';
}

/**
 * @brief 判断字符串是否全为基本拉丁字符
 * @param wstr 要判断的宽字符串
 * @param numericOrSpace 是否允许数字和空格
 * @return 如果字符串符合条件返回true，否则返回false
 */
inline bool isBasicLatinString(std::wstring_view wstr, bool numericOrSpace)
{
    for (wchar_t c : wstr)
        if (!isBasicLatinCharacter(c) && (!numericOrSpace || !isNumericOrSpace(c)))
            return false;
    return true;
}

/**
 * @brief 判断字符串是否全为扩展拉丁字符
 * @param wstr 要判断的宽字符串
 * @param numericOrSpace 是否允许数字和空格
 * @return 如果字符串符合条件返回true，否则返回false
 */
inline bool isExtendedLatinString(std::wstring_view wstr, bool numericOrSpace)
{
    for (wchar_t c : wstr)
        if (!isExtendedLatinCharacter(c) && (!numericOrSpace || !isNumericOrSpace(c)))
            return false;
    return true;
}

/**
 * @brief 判断字符串是否全为西里尔字符
 * @param wstr 要判断的宽字符串
 * @param numericOrSpace 是否允许数字和空格
 * @return 如果字符串符合条件返回true，否则返回false
 */
inline bool isCyrillicString(std::wstring_view wstr, bool numericOrSpace)
{
    for (wchar_t c : wstr)
        if (!isCyrillicCharacter(c) && (!numericOrSpace || !isNumericOrSpace(c)))
            return false;
    return true;
}

/**
 * @brief 判断字符串是否全为东亚字符
 * @param wstr 要判断的宽字符串
 * @param numericOrSpace 是否允许数字和空格
 * @return 如果字符串符合条件返回true，否则返回false
 */
inline bool isEastAsianString(std::wstring_view wstr, bool numericOrSpace)
{
    for (wchar_t c : wstr)
        if (!isEastAsianCharacter(c) && (!numericOrSpace || !isNumericOrSpace(c)))
            return false;
    return true;
}

/**
 * @brief 宽字符转大写
 * @param wchar 要转换的宽字符
 * @return 返回转换后的大写字符
 *
 * 支持拉丁字符、西里尔字符的大小写转换
 */
inline wchar_t wcharToUpper(wchar_t wchar)
{
    if (wchar >= L'a' && wchar <= L'z')                      // LATIN SMALL LETTER A - LATIN SMALL LETTER Z
        return wchar_t(uint16(wchar)-0x0020);
    if (wchar == 0x00DF)                                     // LATIN SMALL LETTER SHARP S
        return wchar_t(0x1E9E);
    if (wchar >= 0x00E0 && wchar <= 0x00F6)                  // LATIN SMALL LETTER A WITH GRAVE - LATIN SMALL LETTER O WITH DIAERESIS
        return wchar_t(uint16(wchar)-0x0020);
    if (wchar >= 0x00F8 && wchar <= 0x00FE)                  // LATIN SMALL LETTER O WITH STROKE - LATIN SMALL LETTER THORN
        return wchar_t(uint16(wchar)-0x0020);
    if (wchar >= 0x0101 && wchar <= 0x012F)                  // LATIN SMALL LETTER A WITH MACRON - LATIN SMALL LETTER I WITH OGONEK (only %2=1)
    {
        if (wchar % 2 == 1)
            return wchar_t(uint16(wchar)-0x0001);
    }
    if (wchar >= 0x0430 && wchar <= 0x044F)                  // CYRILLIC SMALL LETTER A - CYRILLIC SMALL LETTER YA
        return wchar_t(uint16(wchar)-0x0020);
    if (wchar == 0x0451)                                     // CYRILLIC SMALL LETTER IO
        return wchar_t(0x0401);

    return wchar;
}

/**
 * @brief 宽字符转大写（仅拉丁字符）
 * @param wchar 要转换的宽字符
 * @return 返回转换后的大写字符，非拉丁字符保持不变
 */
inline wchar_t wcharToUpperOnlyLatin(wchar_t wchar)
{
    return isBasicLatinCharacter(wchar) ? wcharToUpper(wchar) : wchar;
}

/**
 * @brief 宽字符转小写
 * @param wchar 要转换的宽字符
 * @return 返回转换后的小写字符
 *
 * 支持拉丁字符、西里尔字符的大小写转换
 */
inline wchar_t wcharToLower(wchar_t wchar)
{
    if (wchar >= L'A' && wchar <= L'Z')                      // LATIN CAPITAL LETTER A - LATIN CAPITAL LETTER Z
        return wchar_t(uint16(wchar)+0x0020);
    if (wchar >= 0x00C0 && wchar <= 0x00D6)                  // LATIN CAPITAL LETTER A WITH GRAVE - LATIN CAPITAL LETTER O WITH DIAERESIS
        return wchar_t(uint16(wchar)+0x0020);
    if (wchar >= 0x00D8 && wchar <= 0x00DE)                  // LATIN CAPITAL LETTER O WITH STROKE - LATIN CAPITAL LETTER THORN
        return wchar_t(uint16(wchar)+0x0020);
    if (wchar >= 0x0100 && wchar <= 0x012E)                  // LATIN CAPITAL LETTER A WITH MACRON - LATIN CAPITAL LETTER I WITH OGONEK (only %2=0)
    {
        if (wchar % 2 == 0)
            return wchar_t(uint16(wchar)+0x0001);
    }
    if (wchar == 0x1E9E)                                     // LATIN CAPITAL LETTER SHARP S
        return wchar_t(0x00DF);
    if (wchar == 0x0401)                                     // CYRILLIC CAPITAL LETTER IO
        return wchar_t(0x0451);
    if (wchar >= 0x0410 && wchar <= 0x042F)                  // CYRILLIC CAPITAL LETTER A - CYRILLIC CAPITAL LETTER YA
        return wchar_t(uint16(wchar)+0x0020);

    return wchar;
}

/**
 * @brief 字符转大写
 * @param c 要转换的字符
 * @return 返回转换后的大写字符
 */
inline char charToUpper(char c) { return std::toupper(c); }

/**
 * @brief 字符转小写
 * @param c 要转换的字符
 * @return 返回转换后的小写字符
 */
inline char charToLower(char c) { return std::tolower(c); }

/**
 * @brief 宽字符串转大写
 * @param str 要转换的宽字符串（会被修改）
 */
TC_COMMON_API void wstrToUpper(std::wstring& str);

/**
 * @brief 宽字符串转小写
 * @param str 要转换的宽字符串（会被修改）
 */
TC_COMMON_API void wstrToLower(std::wstring& str);

/**
 * @brief 字符串转大写
 * @param str 要转换的字符串（会被修改）
 */
TC_COMMON_API void strToUpper(std::string& str);

/**
 * @brief 字符串转小写
 * @param str 要转换的字符串（会被修改）
 */
TC_COMMON_API void strToLower(std::string& str);

/**
 * @brief 获取名字的主干部分（用于俄语变格）
 * @param wname 名字的宽字符串
 * @param declension 变格类型
 * @return 返回名字的主干部分
 *
 * 仅支持西里尔字符，用于俄语名字的变格处理
 */
TC_COMMON_API std::wstring GetMainPartOfName(std::wstring const& wname, uint32 declension);

/**
 * @brief 将UTF-8字符串转换为控制台编码
 * @param utf8str UTF-8源字符串
 * @param conStr 输出控制台编码字符串
 * @return 成功返回true，失败返回false
 */
TC_COMMON_API bool utf8ToConsole(std::string_view utf8str, std::string& conStr);

/**
 * @brief 将控制台编码字符串转换为UTF-8
 * @param conStr 控制台编码源字符串
 * @param utf8str 输出UTF-8字符串
 * @return 成功返回true，失败返回false
 */
TC_COMMON_API bool consoleToUtf8(std::string_view conStr, std::string& utf8str);

/**
 * @brief 检查UTF-8字符串是否匹配搜索字符串
 * @param str 要搜索的UTF-8字符串
 * @param search 要查找的宽字符搜索字符串
 * @return 如果匹配返回true，否则返回false
 *
 * 忽略大小写进行匹配
 */
TC_COMMON_API bool Utf8FitTo(std::string_view str, std::wstring_view search);

/**
 * @brief UTF-8格式的printf函数
 * @param out 输出文件指针
 * @param str 格式字符串
 * @param ... 可变参数
 *
 * 在Windows平台上正确处理UTF-8编码输出
 */
TC_COMMON_API void utf8printf(FILE* out, const char *str, ...);

/**
 * @brief UTF-8格式的vprintf函数
 * @param out 输出文件指针
 * @param str 格式字符串
 * @param ap 可变参数列表
 *
 * 在Windows平台上正确处理UTF-8编码输出
 */
TC_COMMON_API void vutf8printf(FILE* out, const char *str, va_list* ap);

/**
 * @brief 将UTF-8字符串转换为大写（仅拉丁字符）
 * @param utf8String 要转换的UTF-8字符串（会被修改）
 * @return 成功返回true，失败返回false
 *
 * 仅转换拉丁字符，其他字符保持不变
 */
TC_COMMON_API bool Utf8ToUpperOnlyLatin(std::string& utf8String);

#if TRINITY_PLATFORM == TRINITY_PLATFORM_WINDOWS
/**
 * @brief 从Windows控制台读取字符串
 * @param str 输出字符串
 * @param size 读取的最大字符数
 * @return 成功返回true，失败返回false
 */
TC_COMMON_API bool ReadWinConsole(std::string& str, size_t size = 256);

/**
 * @brief 向Windows控制台写入字符串
 * @param str 要写入的字符串
 * @param error 是否写入到标准错误输出
 * @return 成功返回true，失败返回false
 */
TC_COMMON_API bool WriteWinConsole(std::string_view str, bool error = false);
#endif

/**
 * @brief 移除字符串中的回车换行符
 * @param str 要处理的字符串（会被修改）
 * @return 返回移除的字符位置，如果没有CRLF返回nullopt
 */
TC_COMMON_API Optional<std::size_t> RemoveCRLF(std::string& str);

/**
 * @brief 检查字符串是否为有效的IP地址
 * @param ipaddress IP地址字符串
 * @return 如果是有效的IP地址返回true，否则返回false
 */
TC_COMMON_API bool IsIPAddress(char const* ipaddress);

/**
 * @brief 创建PID文件
 * @param filename PID文件路径
 * @return 成功返回进程ID，失败返回0
 *
 * 将当前进程ID写入指定文件，用于防止多实例运行
 */
TC_COMMON_API uint32 CreatePIDFile(std::string const& filename);

/**
 * @brief 获取当前进程ID
 * @return 返回当前进程ID
 */
TC_COMMON_API uint32 GetPID();

namespace Trinity::Impl
{
    /**
     * @brief 字节数组转十六进制字符串的实现函数
     * @param bytes 字节数组指针
     * @param length 数组长度
     * @param reverse 是否反向输出
     * @return 返回十六进制字符串
     */
    TC_COMMON_API std::string ByteArrayToHexStr(uint8 const* bytes, size_t length, bool reverse = false);

    /**
     * @brief 十六进制字符串转字节数组的实现函数
     * @param str 十六进制字符串
     * @param out 输出字节数组
     * @param outlen 输出数组长度
     * @param reverse 是否反向解析
     */
    TC_COMMON_API void HexStrToByteArray(std::string_view str, uint8* out, size_t outlen, bool reverse = false);
}

/**
 * @brief 字节数组转十六进制字符串
 * @tparam Container 容器类型
 * @param c 字节数组容器
 * @param reverse 是否反向输出
 * @return 返回十六进制字符串
 */
template <typename Container>
std::string ByteArrayToHexStr(Container const& c, bool reverse = false)
{
    return Trinity::Impl::ByteArrayToHexStr(std::data(c), std::size(c), reverse);
}

/**
 * @brief 十六进制字符串转字节数组
 * @tparam Size 数组大小
 * @param str 十六进制字符串
 * @param buf 输出字节数组
 * @param reverse 是否反向解析
 */
template <size_t Size>
void HexStrToByteArray(std::string_view str, std::array<uint8, Size>& buf, bool reverse = false)
{
    Trinity::Impl::HexStrToByteArray(str, buf.data(), Size, reverse);
}

/**
 * @brief 十六进制字符串转字节数组（返回值版本）
 * @tparam Size 数组大小
 * @param str 十六进制字符串
 * @param reverse 是否反向解析
 * @return 返回字节数组
 */
template <size_t Size>
std::array<uint8, Size> HexStrToByteArray(std::string_view str, bool reverse = false)
{
    std::array<uint8, Size> arr;
    HexStrToByteArray(str, arr, reverse);
    return arr;
}

/**
 * @brief 十六进制字符串转字节向量
 * @param str 十六进制字符串
 * @param reverse 是否反向解析
 * @return 返回字节向量
 */
inline std::vector<uint8> HexStrToByteVector(std::string_view str, bool reverse = false)
{
    std::vector<uint8> buf;
    size_t const sz = (str.size() / 2);
    buf.resize(sz);
    Trinity::Impl::HexStrToByteArray(str, buf.data(), sz, reverse);
    return buf;
}

/**
 * @brief 忽略大小写比较两个字符串是否相等
 * @param str1 第一个字符串
 * @param str2 第二个字符串
 * @return 如果相等返回true，否则返回false
 */
TC_COMMON_API bool StringEqualI(std::string_view str1, std::string_view str2);

/**
 * @brief 检查字符串是否以指定前缀开头
 * @param haystack 要检查的字符串
 * @param needle 前缀字符串
 * @return 如果以指定前缀开头返回true，否则返回false
 */
inline bool StringStartsWith(std::string_view haystack, std::string_view needle) { return (haystack.substr(0, needle.length()) == needle); }

/**
 * @brief 检查字符串是否以指定前缀开头（忽略大小写）
 * @param haystack 要检查的字符串
 * @param needle 前缀字符串
 * @return 如果以指定前缀开头返回true，否则返回false
 */
inline bool StringStartsWithI(std::string_view haystack, std::string_view needle) { return StringEqualI(haystack.substr(0, needle.length()), needle); }

/**
 * @brief 检查字符串是否包含指定子串（忽略大小写）
 * @param haystack 要搜索的字符串
 * @param needle 要查找的子串
 * @return 如果包含返回true，否则返回false
 */
TC_COMMON_API bool StringContainsStringI(std::string_view haystack, std::string_view needle);

/**
 * @brief 检查值中是否包含指定字符串（忽略大小写）
 * @tparam T 值类型
 * @param haystack 键值对，值为要搜索的字符串
 * @param needle 要查找的子串
 * @return 如果包含返回true，否则返回false
 */
template <typename T>
inline bool ValueContainsStringI(std::pair<T, std::string_view> const& haystack, std::string_view needle)
{
    return StringContainsStringI(haystack.second, needle);
}

/**
 * @brief 忽略大小写比较两个字符串（小于比较）
 * @param a 第一个字符串
 * @param b 第二个字符串
 * @return 如果a小于b返回true，否则返回false
 */
TC_COMMON_API bool StringCompareLessI(std::string_view a, std::string_view b);

/**
 * @struct StringCompareLessI_T
 * @brief 忽略大小写的字符串比较仿函数
 *
 * 用于标准容器（如std::map、std::set）的比较器
 */
struct StringCompareLessI_T {
    bool operator()(std::string_view a, std::string_view b) const { return StringCompareLessI(a, b); }
};

/**
 * @class HookList
 * @brief 钩子列表 - 不可修改的回调函数列表
 *
 * 职责：
 *   - 存储回调函数列表
 *   - 提供添加和遍历功能
 *   - 不提供删除功能
 *
 * @tparam T 回调函数类型
 */
template <typename T>
class HookList final
{
    private:
        typedef std::vector<T> ContainerType;   ///< 容器类型

        ContainerType _container;               ///< 回调函数容器

    public:
        typedef typename ContainerType::iterator iterator;  ///< 迭代器类型

        /**
         * @brief 添加回调函数
         * @param t 回调函数（右值引用）
         * @return 返回当前列表引用，支持链式调用
         */
        HookList<T>& operator+=(T&& t)
        {
            _container.push_back(std::move(t));
            return *this;
        }

        /**
         * @brief 获取回调函数数量
         * @return 返回回调函数数量
         */
        size_t size() const
        {
            return _container.size();
        }

        /**
         * @brief 获取开始迭代器
         * @return 返回开始迭代器
         */
        iterator begin()
        {
            return _container.begin();
        }

        /**
         * @brief 获取结束迭代器
         * @return 返回结束迭代器
         */
        iterator end()
        {
            return _container.end();
        }
};

/**
 * @class flag96
 * @brief 96位标志类 - 使用三个32位整数存储96位标志
 *
 * 职责：
 *   - 提供96位标志存储和操作
 *   - 支持位运算操作（与、或、异或、取反）
 *   - 支持标志检查和设置
 *
 * 使用场景：
 *   - 需要超过64位标志位的情况
 *   - 游戏中的大容量标志集合
 */
class TC_COMMON_API flag96
{
private:
    uint32 part[3];     ///< 三个32位整数存储96位标志

public:
    /**
     * @brief 构造函数
     * @param p1 第一部分（位0-31）
     * @param p2 第二部分（位32-63）
     * @param p3 第三部分（位64-95）
     */
    flag96(uint32 p1 = 0, uint32 p2 = 0, uint32 p3 = 0)
    {
        part[0] = p1;
        part[1] = p2;
        part[2] = p3;
    }

    /**
     * @brief 检查是否等于指定值
     * @param p1 第一部分值
     * @param p2 第二部分值
     * @param p3 第三部分值
     * @return 如果相等返回true，否则返回false
     */
    inline bool IsEqual(uint32 p1 = 0, uint32 p2 = 0, uint32 p3 = 0) const
    {
        return (part[0] == p1 && part[1] == p2 && part[2] == p3);
    }

    /**
     * @brief 检查是否包含指定标志
     * @param p1 第一部分标志
     * @param p2 第二部分标志
     * @param p3 第三部分标志
     * @return 如果包含任一标志返回true，否则返回false
     */
    inline bool HasFlag(uint32 p1 = 0, uint32 p2 = 0, uint32 p3 = 0) const
    {
        return (part[0] & p1 || part[1] & p2 || part[2] & p3);
    }

    /**
     * @brief 设置标志值
     * @param p1 第一部分值
     * @param p2 第二部分值
     * @param p3 第三部分值
     */
    inline void Set(uint32 p1 = 0, uint32 p2 = 0, uint32 p3 = 0)
    {
        part[0] = p1;
        part[1] = p2;
        part[2] = p3;
    }

    /// 相等比较运算符
    inline bool operator==(flag96 const& right) const
    {
        return
        (
            part[0] == right.part[0] &&
            part[1] == right.part[1] &&
            part[2] == right.part[2]
        );
    }

    /// 不等比较运算符
    inline bool operator!=(flag96 const& right) const
    {
        return !(*this == right);
    }

    /// 按位与运算符
    inline flag96 operator&(flag96 const& right) const
    {
        return flag96(part[0] & right.part[0], part[1] & right.part[1], part[2] & right.part[2]);
    }

    /// 按位与赋值运算符
    inline flag96& operator&=(flag96 const& right)
    {
        part[0] &= right.part[0];
        part[1] &= right.part[1];
        part[2] &= right.part[2];
        return *this;
    }

    /// 按位或运算符
    inline flag96 operator|(flag96 const& right) const
    {
        return flag96(part[0] | right.part[0], part[1] | right.part[1], part[2] | right.part[2]);
    }

    /// 按位或赋值运算符
    inline flag96& operator |=(flag96 const& right)
    {
        part[0] |= right.part[0];
        part[1] |= right.part[1];
        part[2] |= right.part[2];
        return *this;
    }

    /// 按位取反运算符
    inline flag96 operator~() const
    {
        return flag96(~part[0], ~part[1], ~part[2]);
    }

    /// 按位异或运算符
    inline flag96 operator^(flag96 const& right) const
    {
        return flag96(part[0] ^ right.part[0], part[1] ^ right.part[1], part[2] ^ right.part[2]);
    }

    /// 按位异或赋值运算符
    inline flag96& operator^=(flag96 const& right)
    {
        part[0] ^= right.part[0];
        part[1] ^= right.part[1];
        part[2] ^= right.part[2];
        return *this;
    }

    /// 布尔转换运算符
    inline operator bool() const
    {
        return (part[0] != 0 || part[1] != 0 || part[2] != 0);
    }

    /// 逻辑非运算符
    inline bool operator !() const
    {
        return !(bool(*this));
    }

    /// 下标运算符（可修改）
    inline uint32& operator[](uint8 el)
    {
        return part[el];
    }

    /// 下标运算符（只读）
    inline uint32 const& operator [](uint8 el) const
    {
        return part[el];
    }
};

/**
 * @enum ComparisionType
 * @brief 比较类型枚举 - 定义数值比较的类型
 */
enum ComparisionType
{
    COMP_TYPE_EQ = 0,       ///< 等于
    COMP_TYPE_HIGH,         ///< 大于
    COMP_TYPE_LOW,          ///< 小于
    COMP_TYPE_HIGH_EQ,      ///< 大于等于
    COMP_TYPE_LOW_EQ,       ///< 小于等于
    COMP_TYPE_MAX           ///< 枚举最大值
};

/**
 * @brief 根据比较类型比较两个值
 * @tparam T 值类型
 * @param type 比较类型
 * @param val1 第一个值
 * @param val2 第二个值
 * @return 根据比较类型返回比较结果
 */
template <class T>
bool CompareValues(ComparisionType type, T val1, T val2)
{
    switch (type)
    {
        case COMP_TYPE_EQ:
            return val1 == val2;
        case COMP_TYPE_HIGH:
            return val1 > val2;
        case COMP_TYPE_LOW:
            return val1 < val2;
        case COMP_TYPE_HIGH_EQ:
            return val1 >= val2;
        case COMP_TYPE_LOW_EQ:
            return val1 <= val2;
        default:
            // 参数不正确
            ABORT();
            return false;
    }
}

/**
 * @brief 将枚举值转换为其底层类型
 * @tparam E 枚举类型
 * @param enumValue 枚举值
 * @return 返回枚举的底层类型值
 */
template<typename E>
constexpr typename std::underlying_type<E>::type AsUnderlyingType(E enumValue)
{
    static_assert(std::is_enum<E>::value, "AsUnderlyingType can only be used with enums");
    return static_cast<typename std::underlying_type<E>::type>(enumValue);
}

/**
 * @brief 合并多个指针，返回第一个非空指针
 * @tparam Ret 返回类型
 * @tparam T1 第一个指针类型
 * @tparam T 剩余指针类型
 * @param first 第一个指针
 * @param rest 剩余指针
 * @return 返回第一个非空指针，转换为目标类型
 */
template<typename Ret, typename T1, typename... T>
Ret* Coalesce(T1* first, T*... rest)
{
    if constexpr (sizeof...(T) > 0)
        return (first ? static_cast<Ret*>(first) : Coalesce<Ret>(rest...));
    else
        return static_cast<Ret*>(first);
}

namespace Trinity
{
namespace Impl
{
    /**
     * @brief 获取类型名称的实现函数
     * @param info 类型信息对象
     * @return 返回类型名称字符串
     */
    TC_COMMON_API std::string GetTypeName(std::type_info const&);
}

/**
 * @brief 获取类型的名称
 * @tparam T 类型
 * @return 返回类型名称字符串
 */
template <typename T>
std::string GetTypeName() { return Impl::GetTypeName(typeid(T)); }

/**
 * @brief 获取值的类型名称
 * @tparam T 值类型
 * @param v 值或type_info对象
 * @return 返回类型名称字符串
 */
template <typename T>
std::string GetTypeName(T&& v)
{
    if constexpr (std::is_same_v<std::remove_cv_t<T>, std::type_info>)
        return Impl::GetTypeName(v);
    else
        return Impl::GetTypeName(typeid(v));
}
}

#endif
