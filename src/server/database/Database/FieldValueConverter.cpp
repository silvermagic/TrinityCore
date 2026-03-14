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
 * @file FieldValueConverter.cpp
 * @brief 数据库字段值转换器实现文件
 *
 * 本文件实现了 BaseDatabaseResultValueConverter 类，提供数据库查询结果字段值的
 * 类型转换基础设施。主要功能包括：
 *
 * - 提供转换器基类的构造和析构
 * - 处理类型转换过程中的截断错误日志记录
 * - 为各种数据库字段类型提供正确的访问器建议
 *
 * ## 设计说明
 *
 * 数据库字段值转换器采用策略模式设计，允许不同数据库后端（MySQL、PostgreSQL等）
 * 实现各自的转换逻辑。BaseDatabaseResultValueConverter 作为基类定义通用接口，
 * 子类可覆盖具体转换方法以适配特定数据库驱动。
 *
 * ## 类型转换流程
 *
 * 1. 查询执行后，数据库驱动返回原始二进制数据
 * 2. Field 类持有数据指针和元数据
 * 3. 转换器根据目标类型执行适当的转换
 * 4. 如果目标类型无法容纳源数据，记录截断警告
 *
 * ## 错误处理机制
 *
 * 当类型转换导致数据截断时（例如将 64 位整数转换为 8 位无符号整数），
 * LogTruncation 方法会生成详细的错误信息，包括：
 * - 当前使用的错误访问器
 * - 字段的实际数据库类型
 * - 字段所属的表名和列名
 * - 推荐使用的正确访问器方法
 *
 * 这些信息帮助开发者快速定位和修复数据访问错误。
 *
 * @see FieldValueConverter.h
 * @see Field
 * @see QueryResultFieldMetadata
 */

#include "FieldValueConverter.h"
#include "Errors.h"
#include "Field.h"

/**
 * @brief 默认构造函数
 *
 * 构造 BaseDatabaseResultValueConverter 实例。
 * 作为基类，此构造函数被派生类（如 MySQL fieldValueConverter）调用。
 */
BaseDatabaseResultValueConverter::BaseDatabaseResultValueConverter() = default;

/**
 * @brief 虚析构函数
 *
 * 销毁 BaseDatabaseResultValueConverter 实例。
 * 声明为虚函数以确保派生类对象能被正确析构，避免内存泄漏。
 */
BaseDatabaseResultValueConverter::~BaseDatabaseResultValueConverter() = default;

/**
 * @brief 记录字段值截断错误日志
 *
 * 当从数据库字段获取值时，如果目标类型无法完整表示源数据，
 * 此方法被调用以记录详细的错误信息。错误信息包含字段元数据
 * 和推荐的正确访问器方法，帮助开发者诊断问题。
 *
 * @param getter 当前使用的获取方法名称（如 "GetUInt8"、"GetFloat" 等）
 *               用于指出导致截断的具体调用
 * @param meta   字段元数据指针，包含以下信息：
 *               - Type: 字段的实际数据库类型（DatabaseFieldTypes 枚举）
 *               - TypeName: 类型名称字符串，用于日志输出
 *               - TableAlias: 表别名
 *               - Alias: 列别名
 *               - TableName: 实际表名
 *               - Name: 实际列名
 *               - Index: 字段在结果集中的索引位置
 *
 * @details
 *
 * ## 类型到访问器映射
 *
 * 方法根据字段的数据库类型（meta->Type）确定推荐的访问器方法：
 *
 * | 数据库类型        | 推荐访问器                              |
 * |------------------|----------------------------------------|
 * | UInt8            | Field::GetUInt8                        |
 * | Int8             | Field::GetInt8                         |
 * | UInt16           | Field::GetUInt16                       |
 * | Int16            | Field::GetInt16                        |
 * | UInt32           | Field::GetUIn32                        |
 * | Int32            | Field::GetInt32                        |
 * | UInt64           | Field::GetUIn64                        |
 * | Int64            | Field::GetInt64                        |
 * | Float            | Field::GetFloat                        |
 * | Double           | Field::GetDouble                       |
 * | Decimal          | Field::GetDouble 或 Field::GetString   |
 * | Date             | Field::GetDate                         |
 * | Time             | Field::GetTime                         |
 * | Binary           | Field::GetString 或 Field::GetBinary   |
 *
 * ## 错误信息格式
 *
 * 断言失败时输出的完整错误信息格式为：
 * @code
 * <getter> on <TypeName> field <TableAlias>.<Alias> (<TableName>.<Name>) at index <Index>
 * caused value to be truncated. Use <ExpectedAccessor> instead.
 * @endcode
 *
 * ## 使用示例
 *
 * 假设某字段定义为 BIGINT (UInt64)，但代码尝试使用 GetUInt8() 获取值：
 * @code
 * // 错误示例：类型不匹配导致截断
 * uint8 value = field->GetUInt8();  // 如果字段实际是 UInt64
 * // 将触发断言，错误信息：
 * // "GetUInt8 on BIGINT field alias.col (table.column) at index 0
 * //  caused value to be truncated. Use Field::GetUIn64 instead."
 * @endcode
 *
 * ## 错误处理行为
 *
 * 此方法使用 ASSERT 宏，在 Debug 构建中会触发断言失败并终止程序。
 * 这是故意设计的行为，以便在开发阶段尽早发现数据类型不匹配问题。
 *
 * @warning 此方法仅在类型转换发生截断时调用。如果转换成功但数据精度
 *          降低（如 Double 转 Float），可能不会触发此检查。
 *
 * @see DatabaseFieldTypes
 * @see QueryResultFieldMetadata
 * @see ASSERT
 */
void BaseDatabaseResultValueConverter::LogTruncation(char const* getter, QueryResultFieldMetadata const* meta)
{
    // 根据字段类型确定推荐的访问器方法
    // 此映射表覆盖了所有支持的数据库字段类型
    char const* expectedAccessor = "";
    switch (meta->Type)
    {
        case DatabaseFieldTypes::UInt8:   expectedAccessor = "Field::GetUInt8"; break;   ///< 8位无符号整数
        case DatabaseFieldTypes::Int8:    expectedAccessor = "Field::GetInt8"; break;    ///< 8位有符号整数
        case DatabaseFieldTypes::UInt16:  expectedAccessor = "Field::GetUInt16"; break;  ///< 16位无符号整数
        case DatabaseFieldTypes::Int16:   expectedAccessor = "Field::GetInt16"; break;   ///< 16位有符号整数
        case DatabaseFieldTypes::UInt32:  expectedAccessor = "Field::GetUIn32"; break;   ///< 32位无符号整数（注意：历史命名拼写）
        case DatabaseFieldTypes::Int32:   expectedAccessor = "Field::GetInt32"; break;   ///< 32位有符号整数
        case DatabaseFieldTypes::UInt64:  expectedAccessor = "Field::GetUIn64"; break;   ///< 64位无符号整数（注意：历史命名拼写）
        case DatabaseFieldTypes::Int64:   expectedAccessor = "Field::GetInt64"; break;   ///< 64位有符号整数
        case DatabaseFieldTypes::Float:   expectedAccessor = "Field::GetFloat"; break;   ///< 单精度浮点数
        case DatabaseFieldTypes::Double:  expectedAccessor = "Field::GetDouble"; break;  ///< 双精度浮点数
        case DatabaseFieldTypes::Decimal: expectedAccessor = "Field::GetDouble or Field::GetString"; break; ///< 十进制数，可用数值或字符串获取
        case DatabaseFieldTypes::Date:    expectedAccessor = "Field::GetDate"; break;    ///< 日期类型
        case DatabaseFieldTypes::Time:    expectedAccessor = "Field::GetTime"; break;    ///< 时间类型
        case DatabaseFieldTypes::Binary:  expectedAccessor = "Field::GetString or Field::GetBinary"; break; ///< 二进制数据，可用字符串或二进制方式获取
        default:
            break;
    }

    // 使用断言报告截断错误
    // ASSERT 在 Debug 构建中会终止程序，Release 构建中行为可能不同
    // 错误信息包含完整的字段上下文，便于定位问题
    ASSERT(false, "%s on %s field %s.%s (%s.%s) at index %u caused value to be truncated. Use %s instead.",
        getter,                  ///< 当前使用的错误获取方法
        meta->TypeName,          ///< 字段类型名称（如 "INT", "BIGINT"）
        meta->TableAlias,        ///< 表别名（SQL 中 AS 指定的名称）
        meta->Alias,             ///< 列别名（SQL 中 AS 指定的名称）
        meta->TableName,         ///< 实际表名
        meta->Name,              ///< 实际列名
        meta->Index,             ///< 字段在结果集中的索引
        expectedAccessor);       ///< 推荐使用的正确访问器方法
}
