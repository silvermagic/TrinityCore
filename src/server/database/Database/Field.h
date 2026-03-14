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
 * @file Field.h
 * @brief 数据库查询结果字段访问模块
 *
 * 本模块提供了对数据库查询结果集中单个字段的访问接口。
 * 主要功能包括：
 * - 定义数据库字段类型枚举，支持各种MySQL数据类型
 * - 提供字段元数据结构，包含表名、字段名、类型等信息
 * - 封装Field类，提供类型安全的数据获取方法
 *
 * 使用场景：
 * - 从ResultSet或PreparedResultSet中读取字段值
 * - 支持多种数据类型的转换和获取
 *
 * 性能考虑：
 * - 字段数据存储为原始字节，按需进行类型转换
 * - 避免不必要的数据拷贝，使用string_view提供轻量级字符串访问
 */

#ifndef TRINITY_DATABASE_FIELD_H
#define TRINITY_DATABASE_FIELD_H

#include "Define.h"
#include "Duration.h"
#include <array>
#include <string>
#include <string_view>
#include <vector>

class BaseDatabaseResultValueConverter;

/**
 * @enum DatabaseFieldTypes
 * @brief 数据库字段类型枚举
 *
 * 定义了数据库字段可能的数据类型，用于类型识别和转换。
 * 这些类型映射了MySQL数据库中的基本数据类型。
 */
enum class DatabaseFieldTypes : uint8
{
    Null,       // 空值类型
    UInt8,      // 无符号8位整数 (TINYINT UNSIGNED)
    Int8,       // 有符号8位整数 (TINYINT)
    UInt16,     // 无符号16位整数 (SMALLINT UNSIGNED)
    Int16,      // 有符号16位整数 (SMALLINT)
    UInt32,     // 无符号32位整数 (INT UNSIGNED)
    Int32,      // 有符号32位整数 (INT)
    UInt64,     // 无符号64位整数 (BIGINT UNSIGNED)
    Int64,      // 有符号64位整数 (BIGINT)
    Float,      // 单精度浮点数 (FLOAT)
    Double,     // 双精度浮点数 (DOUBLE)
    Decimal,    // 十进制数 (DECIMAL)
    Date,       // 日期类型 (DATE)
    Time,       // 时间类型 (TIME)
    Binary      // 二进制数据 (BLOB, BINARY)
};

/**
 * @struct QueryResultFieldMetadata
 * @brief 查询结果字段元数据结构
 *
 * 存储字段的元信息，包括表名、字段名、类型等，
 * 用于字段值的类型转换和数据访问。
 */
struct QueryResultFieldMetadata
{
    char const* TableName = nullptr;            // 字段所属表名
    char const* TableAlias = nullptr;           // 表别名
    char const* Name = nullptr;                 // 字段名
    char const* Alias = nullptr;                // 字段别名
    char const* TypeName = nullptr;             // 类型名称字符串
    uint32 Index = 0;                           // 字段在结果集中的索引位置
    DatabaseFieldTypes Type = DatabaseFieldTypes::Null;  // 字段类型枚举值
    BaseDatabaseResultValueConverter const* Converter = nullptr;  // 值转换器指针
};

/**
    @class Field
    @brief 数据库查询结果字段访问类

    用于访问数据库查询结果集中单个字段的值。
    提供类型安全的getter方法，支持各种MySQL数据类型的转换。

    使用指导 - 字段类型与方法匹配表：

    |   MySQL类型            |  推荐使用的方法                          |
    |------------------------|----------------------------------------|
    | TINYINT                | GetBool, GetInt8, GetUInt8             |
    | SMALLINT               | GetInt16, GetUInt16                    |
    | MEDIUMINT, INT         | GetInt32, GetUInt32                    |
    | BIGINT                 | GetInt64, GetUInt64                    |
    | FLOAT                  | GetFloat                               |
    | DOUBLE, DECIMAL        | GetDouble                              |
    | CHAR, VARCHAR,         | GetCString, GetString                  |
    | TINYTEXT, MEDIUMTEXT,  | GetCString, GetString                  |
    | TEXT, LONGTEXT         | GetCString, GetString                  |
    | TINYBLOB, MEDIUMBLOB,  | GetBinary, GetString                   |
    | BLOB, LONGBLOB         | GetBinary, GetString                   |
    | BINARY, VARBINARY      | GetBinary                              |

    聚合函数返回类型：

    | 函数     | 返回类型          |
    |----------|-------------------|
    | MIN, MAX | 与原字段相同      |
    | SUM, AVG | DECIMAL           |
    | COUNT    | BIGINT            |
*/
class TC_DATABASE_API Field
{
    friend class ResultSet;             // 结果集类需要访问私有成员
    friend class PreparedResultSet;     // 预处理结果集类需要访问私有成员

    public:
        /**
         * @brief 构造函数
         * 初始化字段对象，所有成员置为空状态
         */
        Field();

        /**
         * @brief 析构函数
         * 默认析构，字段数据由结果集管理
         */
        ~Field();

        /**
         * @brief 获取布尔值
         * @return 布尔值，UInt8值为1时返回true，否则返回false
         * @note 实际获取的是整数值，1为true，0为false
         */
        bool GetBool() const
        {
            return GetUInt8() == 1 ? true : false;
        }

        /**
         * @brief 获取无符号8位整数
         * @return 字段值转换为uint8
         */
        uint8 GetUInt8() const;

        /**
         * @brief 获取有符号8位整数
         * @return 字段值转换为int8
         */
        int8 GetInt8() const;

        /**
         * @brief 获取无符号16位整数
         * @return 字段值转换为uint16
         */
        uint16 GetUInt16() const;

        /**
         * @brief 获取有符号16位整数
         * @return 字段值转换为int16
         */
        int16 GetInt16() const;

        /**
         * @brief 获取无符号32位整数
         * @return 字段值转换为uint32
         */
        uint32 GetUInt32() const;

        /**
         * @brief 获取有符号32位整数
         * @return 字段值转换为int32
         */
        int32 GetInt32() const;

        /**
         * @brief 获取无符号64位整数
         * @return 字段值转换为uint64
         */
        uint64 GetUInt64() const;

        /**
         * @brief 获取有符号64位整数
         * @return 字段值转换为int64
         */
        int64 GetInt64() const;

        /**
         * @brief 获取单精度浮点数
         * @return 字段值转换为float
         */
        float GetFloat() const;

        /**
         * @brief 获取双精度浮点数
         * @return 字段值转换为double
         */
        double GetDouble() const;

        /**
         * @brief 获取日期时间值
         * @return 字段值转换为SystemTimePoint
         */
        SystemTimePoint GetDate() const;

        /**
         * @brief 获取C风格字符串指针
         * @return 字段值的原始字符串指针，可能为nullptr
         * @note 返回的指针指向结果集内部数据，生命周期由结果集管理
         */
        char const* GetCString() const;

        /**
         * @brief 获取字符串对象
         * @return 字段值复制的std::string对象
         */
        std::string GetString() const;

        /**
         * @brief 获取字符串视图
         * @return 字段值的string_view，避免数据拷贝
         * @note 性能优化：适合临时访问，不持有数据所有权
         */
        std::string_view GetStringView() const;

        /**
         * @brief 获取二进制数据
         * @return 字段值转换为二进制数据vector
         */
        std::vector<uint8> GetBinary() const;

        /**
         * @brief 获取固定大小的二进制数据
         * @tparam S 期望的二进制数据大小
         * @return 包含二进制数据的std::array
         * @note 会进行大小检查，数据长度必须匹配模板参数S
         */
        template <size_t S>
        std::array<uint8, S> GetBinary() const
        {
            std::array<uint8, S> buf;
            GetBinarySizeChecked(buf.data(), S);
            return buf;
        }

        /**
         * @brief 检查字段值是否为NULL
         * @return 如果字段值为NULL返回true，否则返回false
         */
        bool IsNull() const
        {
            return _value == nullptr;
        }

    private:
        char const* _value;                         // 字段数据在内存中的原始字节指针
        uint32 _length;                             // 字段数据的长度（字节数）

        /**
         * @brief 设置字段值
         * @param newValue 新值的原始字节指针
         * @param length 数据长度
         */
        void SetValue(char const* newValue, uint32 length);

        QueryResultFieldMetadata const* _meta;      // 字段元数据指针

        /**
         * @brief 设置字段元数据
         * @param meta 元数据指针
         */
        void SetMetadata(QueryResultFieldMetadata const* meta);

        /**
         * @brief 获取二进制数据并进行大小检查
         * @param buf 输出缓冲区
         * @param size 期望的数据大小
         * @note 如果实际数据大小与期望不匹配会触发断言失败
         */
        void GetBinarySizeChecked(uint8* buf, size_t size) const;
};

#endif
