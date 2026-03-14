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
 * @file MySQLPreparedStatement.cpp
 * @brief MySQL 预处理语句实现文件
 *
 * 本文件实现了 MySQLPreparedStatement 类，提供了 MySQL 预处理语句的功能。
 * 预处理语句用于提高数据库查询性能和防止 SQL 注入攻击。
 *
 * 主要功能包括：
 * - 参数绑定管理
 * - 各种数据类型的参数设置（整数、浮点数、字符串、二进制数据、时间等）
 * - 参数清理和资源释放
 * - SQL 查询字符串的构建和调试
 *
 * @see MySQLPreparedStatement
 * @see PreparedStatementBase
 */

#include "MySQLPreparedStatement.h"
#include "Errors.h"
#include "Log.h"
#include "MySQLHacks.h"
#include "PreparedStatement.h"
#include <chrono>
#include <cstring>

/**
 * @brief MySQL 类型映射模板基类
 *
 * 该模板用于将 C++ 数据类型映射到 MySQL 对应的字段类型。
 * 每个特化版本定义特定 C++ 类型对应的 MySQL 类型常量。
 *
 * @tparam T C++ 数据类型
 */
template<typename T>
struct MySQLType { };

/**
 * @brief MySQL 类型映射特化 - uint8 类型
 * 映射到 MySQL 的 TINY 类型（1 字节无符号整数）
 */
template<> struct MySQLType<uint8> : std::integral_constant<enum_field_types, MYSQL_TYPE_TINY> { };

/**
 * @brief MySQL 类型映射特化 - uint16 类型
 * 映射到 MySQL 的 SHORT 类型（2 字节无符号整数）
 */
template<> struct MySQLType<uint16> : std::integral_constant<enum_field_types, MYSQL_TYPE_SHORT> { };

/**
 * @brief MySQL 类型映射特化 - uint32 类型
 * 映射到 MySQL 的 LONG 类型（4 字节无符号整数）
 */
template<> struct MySQLType<uint32> : std::integral_constant<enum_field_types, MYSQL_TYPE_LONG> { };

/**
 * @brief MySQL 类型映射特化 - uint64 类型
 * 映射到 MySQL 的 LONGLONG 类型（8 字节无符号整数）
 */
template<> struct MySQLType<uint64> : std::integral_constant<enum_field_types, MYSQL_TYPE_LONGLONG> { };

/**
 * @brief MySQL 类型映射特化 - int8 类型
 * 映射到 MySQL 的 TINY 类型（1 字节有符号整数）
 */
template<> struct MySQLType<int8> : std::integral_constant<enum_field_types, MYSQL_TYPE_TINY> { };

/**
 * @brief MySQL 类型映射特化 - int16 类型
 * 映射到 MySQL 的 SHORT 类型（2 字节有符号整数）
 */
template<> struct MySQLType<int16> : std::integral_constant<enum_field_types, MYSQL_TYPE_SHORT> { };

/**
 * @brief MySQL 类型映射特化 - int32 类型
 * 映射到 MySQL 的 LONG 类型（4 字节有符号整数）
 */
template<> struct MySQLType<int32> : std::integral_constant<enum_field_types, MYSQL_TYPE_LONG> { };

/**
 * @brief MySQL 类型映射特化 - int64 类型
 * 映射到 MySQL 的 LONGLONG 类型（8 字节有符号整数）
 */
template<> struct MySQLType<int64> : std::integral_constant<enum_field_types, MYSQL_TYPE_LONGLONG> { };

/**
 * @brief MySQL 类型映射特化 - float 类型
 * 映射到 MySQL 的 FLOAT 类型（单精度浮点数）
 */
template<> struct MySQLType<float> : std::integral_constant<enum_field_types, MYSQL_TYPE_FLOAT> { };

/**
 * @brief MySQL 类型映射特化 - double 类型
 * 映射到 MySQL 的 DOUBLE 类型（双精度浮点数）
 */
template<> struct MySQLType<double> : std::integral_constant<enum_field_types, MYSQL_TYPE_DOUBLE> { };

/**
 * @brief 构造函数 - 初始化 MySQL 预处理语句对象
 *
 * 创建一个 MySQLPreparedStatement 实例，初始化参数绑定结构并设置语句属性。
 *
 * @param stmt MySQL 语句句柄，由上层代码创建
 * @param queryString SQL 查询字符串，用于调试和日志输出
 *
 * @details
 * 构造函数执行以下操作：
 * 1. 初始化参数计数，从 MySQL 语句句柄获取参数数量
 * 2. 创建参数绑定数组并初始化为 0
 * 3. 设置 STMT_ATTR_UPDATE_MAX_LENGTH 属性，使得 mysql_stmt_store_result()
 *    会更新 MYSQL_FIELD->max_length 值，这对于获取结果集字段的实际最大长度很有用
 *
 * @note 参数绑定数组在析构函数中释放
 */
MySQLPreparedStatement::MySQLPreparedStatement(MySQLStmt* stmt, std::string queryString) :
    m_stmt(nullptr), m_Mstmt(stmt), m_bind(nullptr), m_queryString(std::move(queryString))
{
    /// 初始化可变参数
    m_paramCount = mysql_stmt_param_count(stmt);
    m_paramsSet.assign(m_paramCount, false);
    m_bind = new MySQLBind[m_paramCount];
    memset(m_bind, 0, sizeof(MySQLBind) * m_paramCount);

    /// 设置语句属性："如果设置为 1，mysql_stmt_store_result() 会更新元数据 MYSQL_FIELD->max_length 值"
    MySQLBool bool_tmp = MySQLBool(1);
    mysql_stmt_attr_set(stmt, STMT_ATTR_UPDATE_MAX_LENGTH, &bool_tmp);
}

/**
 * @brief 析构函数 - 清理 MySQL 预处理语句资源
 *
 * 释放预处理语句占用的所有资源，包括参数绑定、结果绑定和 MySQL 语句句柄。
 *
 * @details
 * 析构函数执行以下清理操作：
 * 1. 清理所有参数绑定（调用 ClearParameters()）
 * 2. 如果存在结果集绑定，释放相关的 length 和 is_null 数组
 * 3. 关闭 MySQL 语句句柄
 * 4. 释放绑定数组内存
 *
 * @note 必须在销毁对象前确保不再使用该预处理语句
 */
MySQLPreparedStatement::~MySQLPreparedStatement()
{
    ClearParameters();
    if (m_Mstmt->bind_result_done)
    {
        delete[] m_Mstmt->bind->length;
        delete[] m_Mstmt->bind->is_null;
    }
    mysql_stmt_close(m_Mstmt);
    delete[] m_bind;
}

/**
 * @brief 绑定预处理语句的所有参数
 *
 * 将 PreparedStatementBase 中的参数数据绑定到 MySQL 预处理语句。
 *
 * @param stmt 预处理语句基类指针，包含要绑定的参数数据
 *
 * @details
 * 该函数执行以下操作：
 * 1. 保存语句指针的交叉引用，用于调试输出
 * 2. 遍历所有参数数据，使用 std::visit 进行类型匹配
 * 3. 根据参数的实际类型调用对应的 SetParameter 重载函数
 * 4. 在 Debug 模式下检查是否所有参数都已绑定
 *
 * @note 使用访问者模式处理不同类型的参数
 * @warning 在 Debug 模式下会警告未绑定的参数
 */
void MySQLPreparedStatement::BindParameters(PreparedStatementBase* stmt)
{
    m_stmt = stmt;     // 交叉引用用于调试输出

    uint8 pos = 0;
    for (PreparedStatementData const& data : stmt->GetParameters())
    {
        std::visit([&](auto&& param)
        {
            SetParameter(pos, param);
        }, data.data);
        ++pos;
    }
#ifdef _DEBUG
    if (pos < m_paramCount)
        TC_LOG_WARN("sql.sql", "[WARNING]: BindParameters() for statement {} did not bind all allocated parameters", stmt->GetIndex());
#endif
}

/**
 * @brief 清理所有绑定的参数
 *
 * 释放所有参数绑定占用的内存并重置绑定状态。
 *
 * @details
 * 该函数遍历所有参数绑定，执行以下操作：
 * 1. 删除参数的 length 指针（如果存在）
 * 2. 删除参数的 buffer 缓冲区（如果存在）
 * 3. 将参数设置为未绑定状态
 *
 * @note 该函数在析构函数中自动调用
 * @warning 调用此函数后，需要重新绑定参数才能执行语句
 */
void MySQLPreparedStatement::ClearParameters()
{
    for (uint32 i=0; i < m_paramCount; ++i)
    {
        delete m_bind[i].length;
        m_bind[i].length = nullptr;
        delete[] (char*) m_bind[i].buffer;
        m_bind[i].buffer = nullptr;
        m_paramsSet[i] = false;
    }
}

/**
 * @brief 参数索引断言失败处理函数
 *
 * 当参数索引超出有效范围时，输出详细的错误信息。
 *
 * @param stmtIndex 预处理语句的索引 ID
 * @param index 尝试绑定的参数索引（从 0 开始）
 * @param paramCount 语句实际的参数数量
 * @return 始终返回 false，用于 ASSERT 宏
 *
 * @details
 * 该函数输出格式化的错误消息，包括：
 * - 尝试绑定的参数位置（1-based，带序数后缀）
 * - 语句索引 ID
 * - 语句实际的参数数量
 *
 * @note 这是一个静态辅助函数，用于 AssertValidIndex 中的断言检查
 */
static bool ParamenterIndexAssertFail(uint32 stmtIndex, uint8 index, uint32 paramCount)
{
    TC_LOG_ERROR("sql.driver", "Attempted to bind parameter {}{} on a PreparedStatement {} (statement has only {} parameters)", uint32(index) + 1, (index == 1 ? "st" : (index == 2 ? "nd" : (index == 3 ? "rd" : "nd"))), stmtIndex, paramCount);
    return false;
}

/**
 * @brief 验证参数索引的有效性
 *
 * 检查参数索引是否在有效范围内，并检查该位置是否已被绑定。
 *
 * @param index 要验证的参数索引（从 0 开始）
 *
 * @details
 * 该函数执行以下验证：
 * 1. 检查索引是否小于参数总数，如果越界则触发断言失败
 * 2. 检查该索引位置是否已经被绑定过，如果已绑定则输出错误日志
 *
 * @note 这是所有 SetParameter 函数的前置检查
 * @warning 在一个位置重复绑定参数是错误的，可能导致不可预期的结果
 */
void MySQLPreparedStatement::AssertValidIndex(uint8 index)
{
    ASSERT(index < m_paramCount || ParamenterIndexAssertFail(m_stmt->GetIndex(), index, m_paramCount));

    if (m_paramsSet[index])
        TC_LOG_ERROR("sql.sql", "[ERROR] Prepared Statement (id: {}) trying to bind value on already bound index ({}).", m_stmt->GetIndex(), index);
}

/**
 * @brief 设置 NULL 参数
 *
 * 将指定位置的参数绑定为 NULL 值。
 *
 * @param index 参数索引位置（从 0 开始）
 * @param value nullptr 值，用于区分重载
 *
 * @details
 * 该函数设置 MySQL 绑定结构为：
 * - buffer_type: MYSQL_TYPE_NULL
 * - buffer: nullptr
 * - is_null_value: 1（表示这是一个 NULL 值）
 * - length: nullptr
 *
 * @note 在 SQL 中对应 NULL 值
 */
void MySQLPreparedStatement::SetParameter(uint8 index, std::nullptr_t)
{
    AssertValidIndex(index);
    m_paramsSet[index] = true;
    MYSQL_BIND* param = &m_bind[index];
    param->buffer_type = MYSQL_TYPE_NULL;
    delete[] static_cast<char*>(param->buffer);
    param->buffer = nullptr;
    param->buffer_length = 0;
    param->is_null_value = 1;
    delete param->length;
    param->length = nullptr;
}

/**
 * @brief 设置布尔类型参数
 *
 * 将布尔值转换为 uint8 类型后绑定到指定位置。
 *
 * @param index 参数索引位置（从 0 开始）
 * @param value 布尔值（true 转换为 1，false 转换为 0）
 *
 * @details
 * 布尔值在 MySQL 中以 TINYINT 类型存储：
 * - true -> 1
 * - false -> 0
 *
 * @note 实际调用 uint8 类型的 SetParameter 模板函数
 */
void MySQLPreparedStatement::SetParameter(uint8 index, bool value)
{
    SetParameter(index, uint8(value ? 1 : 0));
}

/**
 * @brief 设置数值类型参数（模板函数）
 *
 * 将各种数值类型（整数、浮点数）绑定到指定位置。
 *
 * @tparam T 数值类型（int8, uint8, int16, uint16, int32, uint32, int64, uint64, float, double）
 * @param index 参数索引位置（从 0 开始）
 * @param value 要绑定的数值
 *
 * @details
 * 该模板函数执行以下操作：
 * 1. 验证参数索引的有效性
 * 2. 根据类型自动选择对应的 MySQL 类型（通过 MySQLType<T> 映射）
 * 3. 分配缓冲区并复制数据
 * 4. 设置 is_unsigned 标志（对于无符号类型）
 * 5. 设置 is_null_value 为 0（表示非 NULL）
 *
 * @note length 字段仅对字符串类型有效，数值类型设为 nullptr
 * @note 支持的类型包括：所有整数类型和浮点类型
 */
template<typename T>
void MySQLPreparedStatement::SetParameter(uint8 index, T value)
{
    AssertValidIndex(index);
    m_paramsSet[index] = true;
    MYSQL_BIND* param = &m_bind[index];
    uint32 len = uint32(sizeof(T));
    param->buffer_type = MySQLType<T>::value;
    delete[] static_cast<char*>(param->buffer);
    param->buffer = new char[len];
    param->buffer_length = 0;
    param->is_null_value = 0;
    param->length = nullptr;               // 仅对字符串类型需要非 NULL
    param->is_unsigned = std::is_unsigned_v<T>;

    memcpy(param->buffer, &value, len);
}

/**
 * @brief 设置时间点参数
 *
 * 将系统时间点（SystemTimePoint）绑定到指定位置。
 *
 * @param index 参数索引位置（从 0 开始）
 * @param value 时间点值（SystemTimePoint 类型）
 *
 * @details
 * 该函数执行以下操作：
 * 1. 验证参数索引的有效性
 * 2. 分配 MYSQL_TIME 结构缓冲区
 * 3. 将 SystemTimePoint 分解为年月日和时分秒微秒
 * 4. 填充 MYSQL_TIME 结构
 *
 * MYSQL_TIME 结构包含：
 * - year: 年份
 * - month: 月份
 * - day: 日期
 * - hour: 小时
 * - minute: 分钟
 * - second: 秒
 * - second_part: 微秒部分
 *
 * @note 使用 C++20 的 chrono 库进行时间分解
 * @note buffer_type 设置为 MYSQL_TYPE_DATETIME
 */
void MySQLPreparedStatement::SetParameter(uint8 index, SystemTimePoint value)
{
    AssertValidIndex(index);
    m_paramsSet[index] = true;
    MYSQL_BIND* param = &m_bind[index];
    uint32 len = sizeof(MYSQL_TIME);
    param->buffer_type = MYSQL_TYPE_DATETIME;
    delete[] static_cast<char*>(param->buffer);
    param->buffer = new char[len];
    param->buffer_length = len;
    param->is_null_value = 0;
    delete param->length;
    param->length = new unsigned long(len);

    std::chrono::year_month_day ymd(time_point_cast<std::chrono::days>(value));
    std::chrono::hh_mm_ss hms(duration_cast<std::chrono::microseconds>(value - std::chrono::sys_days(ymd)));

    MYSQL_TIME* time = reinterpret_cast<MYSQL_TIME*>(static_cast<char*>(param->buffer));
    time->year = static_cast<int32>(ymd.year());
    time->month = static_cast<uint32>(ymd.month());
    time->day = static_cast<uint32>(ymd.day());
    time->hour = hms.hours().count();
    time->minute = hms.minutes().count();
    time->second = hms.seconds().count();
    time->second_part = hms.subseconds().count();
}

/**
 * @brief 设置字符串参数
 *
 * 将字符串值绑定到指定位置。
 *
 * @param index 参数索引位置（从 0 开始）
 * @param value 要绑定的字符串（std::string 常量引用）
 *
 * @details
 * 该函数执行以下操作：
 * 1. 验证参数索引的有效性
 * 2. 获取字符串长度
 * 3. 分配缓冲区并复制字符串内容
 * 4. 设置 buffer_type 为 MYSQL_TYPE_VAR_STRING
 * 5. 设置 length 字段为字符串长度
 *
 * @note 字符串不需要包含终止符 '\0'，MySQL 根据长度字段确定实际长度
 * @note length 字段对于字符串类型是必需的
 */
void MySQLPreparedStatement::SetParameter(uint8 index, std::string const& value)
{
    AssertValidIndex(index);
    m_paramsSet[index] = true;
    MYSQL_BIND* param = &m_bind[index];
    uint32 len = uint32(value.size());
    param->buffer_type = MYSQL_TYPE_VAR_STRING;
    delete [] static_cast<char*>(param->buffer);
    param->buffer = new char[len];
    param->buffer_length = len;
    param->is_null_value = 0;
    delete param->length;
    param->length = new unsigned long(len);

    memcpy(param->buffer, value.c_str(), len);
}

/**
 * @brief 设置二进制数据参数
 *
 * 将二进制数据（字节数组）绑定到指定位置。
 *
 * @param index 参数索引位置（从 0 开始）
 * @param value 要绑定的二进制数据（uint8 向量）
 *
 * @details
 * 该函数执行以下操作：
 * 1. 验证参数索引的有效性
 * 2. 获取数据长度
 * 3. 分配缓冲区并复制二进制数据
 * 4. 设置 buffer_type 为 MYSQL_TYPE_BLOB
 * 5. 设置 length 字段为数据长度
 *
 * @note 适用于存储二进制大对象（BLOB），如图片、序列化数据等
 * @note buffer_type 设置为 MYSQL_TYPE_BLOB 以区分于普通字符串
 */
void MySQLPreparedStatement::SetParameter(uint8 index, std::vector<uint8> const& value)
{
    AssertValidIndex(index);
    m_paramsSet[index] = true;
    MYSQL_BIND* param = &m_bind[index];
    uint32 len = uint32(value.size());
    param->buffer_type = MYSQL_TYPE_BLOB;
    delete [] static_cast<char*>(param->buffer);
    param->buffer = new char[len];
    param->buffer_length = len;
    param->is_null_value = 0;
    delete param->length;
    param->length = new unsigned long(len);

    memcpy(param->buffer, value.data(), len);
}

/**
 * @brief 获取完整的查询字符串
 *
 * 构建包含实际参数值的完整 SQL 查询字符串，主要用于调试和日志输出。
 *
 * @return std::string 完整的 SQL 查询字符串，参数占位符 '?' 已被实际值替换
 *
 * @details
 * 该函数执行以下操作：
 * 1. 复制原始查询模板字符串
 * 2. 遍历所有参数数据
 * 3. 将参数转换为字符串表示
 * 4. 替换查询字符串中的 '?' 占位符
 *
 * @note 这个函数主要用于调试目的，实际执行时 MySQL 会使用预处理语句的参数绑定机制
 * @note 转换的字符串可能与实际发送到 MySQL 的格式略有不同
 *
 * @see PreparedStatementData::ToString
 */
std::string MySQLPreparedStatement::getQueryString() const
{
    std::string queryString(m_queryString);

    size_t pos = 0;
    for (PreparedStatementData const& data : m_stmt->GetParameters())
    {
        pos = queryString.find('?', pos);

        std::string replaceStr = std::visit([&](auto&& data)
        {
            return PreparedStatementData::ToString(data);
        }, data.data);

        queryString.replace(pos, 1, replaceStr);
        pos += replaceStr.length();
    }

    return queryString;
}
