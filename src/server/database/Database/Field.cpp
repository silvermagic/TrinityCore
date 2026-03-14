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
 * @file Field.cpp
 * @brief 数据库查询结果字段访问实现
 *
 * 本文件实现了Field类，提供对数据库查询结果字段的类型安全访问。
 * 所有getter方法通过FieldValueConverter进行类型转换，确保数据正确性。
 */

#include "Field.h"
#include "Errors.h"
#include "FieldValueConverter.h"
#include <cstring>

/**
 * @brief 构造函数 - 初始化字段对象
 *
 * 将所有成员初始化为空状态：
 * - _value 设为 nullptr
 * - _length 设为 0
 * - _meta 设为 nullptr
 */
Field::Field() : _value(nullptr), _length(0), _meta(nullptr)
{
}

/**
 * @brief 析构函数 - 默认实现
 *
 * 字段数据由ResultSet/PreparedResultSet管理，此处无需释放
 */
Field::~Field() = default;

/**
 * @brief 获取无符号8位整数
 * @return 转换后的uint8值，NULL字段返回0
 *
 * 通过元数据中的转换器进行类型转换
 */
uint8 Field::GetUInt8() const
{
    // NULL值检查，返回默认值0
    if (!_value)
        return 0;

    // 使用转换器进行类型转换
    return _meta->Converter->GetUInt8(_value, _length, _meta);
}

/**
 * @brief 获取有符号8位整数
 * @return 转换后的int8值，NULL字段返回0
 */
int8 Field::GetInt8() const
{
    if (!_value)
        return 0;

    return _meta->Converter->GetInt8(_value, _length, _meta);
}

/**
 * @brief 获取无符号16位整数
 * @return 转换后的uint16值，NULL字段返回0
 */
uint16 Field::GetUInt16() const
{
    if (!_value)
        return 0;

    return _meta->Converter->GetUInt16(_value, _length, _meta);
}

/**
 * @brief 获取有符号16位整数
 * @return 转换后的int16值，NULL字段返回0
 */
int16 Field::GetInt16() const
{
    if (!_value)
        return 0;

    return _meta->Converter->GetInt16(_value, _length, _meta);
}

/**
 * @brief 获取无符号32位整数
 * @return 转换后的uint32值，NULL字段返回0
 */
uint32 Field::GetUInt32() const
{
    if (!_value)
        return 0;

    return _meta->Converter->GetUInt32(_value, _length, _meta);
}

/**
 * @brief 获取有符号32位整数
 * @return 转换后的int32值，NULL字段返回0
 */
int32 Field::GetInt32() const
{
    if (!_value)
        return 0;

    return _meta->Converter->GetInt32(_value, _length, _meta);
}

/**
 * @brief 获取无符号64位整数
 * @return 转换后的uint64值，NULL字段返回0
 */
uint64 Field::GetUInt64() const
{
    if (!_value)
        return 0;

    return _meta->Converter->GetUInt64(_value, _length, _meta);
}

/**
 * @brief 获取有符号64位整数
 * @return 转换后的int64值，NULL字段返回0
 */
int64 Field::GetInt64() const
{
    if (!_value)
        return 0;

    return _meta->Converter->GetInt64(_value, _length, _meta);
}

/**
 * @brief 获取单精度浮点数
 * @return 转换后的float值，NULL字段返回0.0f
 */
float Field::GetFloat() const
{
    if (!_value)
        return 0.0f;

    return _meta->Converter->GetFloat(_value, _length, _meta);
}

/**
 * @brief 获取双精度浮点数
 * @return 转换后的double值，NULL字段返回0.0
 */
double Field::GetDouble() const
{
    if (!_value)
        return 0.0;

    return _meta->Converter->GetDouble(_value, _length, _meta);
}

/**
 * @brief 获取日期时间值
 * @return 转换后的SystemTimePoint，NULL字段返回最小时间点
 */
SystemTimePoint Field::GetDate() const
{
    if (!_value)
        return SystemTimePoint::min();

    return _meta->Converter->GetDate(_value, _length, _meta);
}

/**
 * @brief 获取C风格字符串指针
 * @return 原始字符串指针，NULL字段返回nullptr
 *
 * 返回的是结果集内部的原始数据指针，不进行数据拷贝
 */
char const* Field::GetCString() const
{
    if (!_value)
        return nullptr;

    return _meta->Converter->GetCString(_value, _length, _meta);
}

/**
 * @brief 获取字符串对象
 * @return 包含字段数据的std::string，NULL字段返回空字符串
 *
 * 创建字符串副本，适合需要长期持有数据的场景
 */
std::string Field::GetString() const
{
    if (!_value)
        return "";

    // 获取原始字符串指针
    char const* string = GetCString();
    if (!string)
        return "";

    // 使用长度构造字符串，确保正确处理二进制数据
    return std::string(string, _length);
}

/**
 * @brief 获取字符串视图
 * @return 指向字段数据的string_view，NULL字段返回空视图
 *
 * 性能优化：避免数据拷贝，适合临时访问场景
 * 注意：视图的生命周期受限于结果集的生命周期
 */
std::string_view Field::GetStringView() const
{
    if (!_value)
        return {};

    char const* const string = GetCString();
    if (!string)
        return {};

    return { string, _length };
}

/**
 * @brief 获取二进制数据
 * @return 包含二进制数据的vector，NULL或空数据返回空vector
 *
 * 将原始字节数据拷贝到vector中
 */
std::vector<uint8> Field::GetBinary() const
{
    std::vector<uint8> result;

    // NULL值或长度为0时返回空vector
    if (!_value || !_length)
        return result;

    // 分配空间并拷贝数据
    result.resize(_length);
    memcpy(result.data(), _value, _length);
    return result;
}

/**
 * @brief 获取固定大小的二进制数据（带大小检查）
 * @param buf 输出缓冲区指针
 * @param length 期望的数据长度
 *
 * @note 如果实际数据长度与期望不匹配，会触发断言失败
 * 用于模板GetBinary<S>方法的底层实现
 */
void Field::GetBinarySizeChecked(uint8* buf, size_t length) const
{
    // 断言检查：数据必须存在且长度必须匹配
    ASSERT(_value && (_length == length), "Expected %zu-byte binary blob, got %sdata (%u bytes) instead", length, _value ? "" : "no ", _length);

    // 拷贝数据到缓冲区
    memcpy(buf, _value, length);
}

/**
 * @brief 设置字段值
 * @param newValue 新值的原始字节指针
 * @param length 数据长度
 *
 * 存储原始字节指针，实际数据由结果集对象管理
 */
void Field::SetValue(char const* newValue, uint32 length)
{
    // 存储原始字节指针，稍后需要显式转换类型
    _value = newValue;
    _length = length;
}

/**
 * @brief 设置字段元数据
 * @param meta 元数据指针
 *
 * 元数据包含字段类型、转换器等信息，用于类型转换
 */
void Field::SetMetadata(QueryResultFieldMetadata const* meta)
{
    _meta = meta;
}
