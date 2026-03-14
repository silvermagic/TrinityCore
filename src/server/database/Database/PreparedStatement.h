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
 * @file PreparedStatement.h
 * @brief 预处理语句头文件
 *
 * 本文件定义了 TrinityCore 数据库抽象层中的预处理语句相关类和数据结构。
 * 预处理语句是一种预先编译的 SQL 语句模板,可以通过参数绑定来重复执行,
 * 提供了更好的性能和安全性(防止 SQL 注入)。
 *
 * 主要组件:
 * - PreparedStatementData: 存储预处理语句参数的数据结构
 * - PreparedStatementBase: 预处理语句基类
 * - PreparedStatement: 预处理语句模板类
 * - PreparedStatementTask: 可执行的预处理语句任务
 *
 * @see DatabaseWorkerPool
 * @see SQLOperation
 */

#ifndef _PREPAREDSTATEMENT_H
#define _PREPAREDSTATEMENT_H

#include "Define.h"
#include "Duration.h"
#include "SQLOperation.h"
#include <future>
#include <vector>
#include <variant>

/**
 * @struct PreparedStatementData
 * @brief 预处理语句参数数据结构
 *
 * 该结构体用于存储预处理语句的单个参数值。使用 std::variant 来支持
 * 多种数据类型,包括基本类型、字符串、二进制数据和时间点等。
 *
 * 支持的数据类型:
 * - 布尔类型: bool
 * - 无符号整数: uint8, uint16, uint32, uint64
 * - 有符号整数: int8, int16, int32, int64
 * - 浮点数: float, double
 * - 字符串: std::string
 * - 二进制数据: std::vector<uint8>
 * - 时间点: SystemTimePoint
 * - 空值: std::nullptr_t
 *
 * @note 该结构体提供了 ToString 方法用于调试和日志记录
 */
struct PreparedStatementData
{
    /**
     * @brief 参数数据的变体类型
     *
     * 使用 std::variant 存储多种可能的参数类型,
     * 确保类型安全并避免不必要的类型转换。
     */
    std::variant<
        bool,                       ///< 布尔值
        uint8,                      ///< 8位无符号整数
        uint16,                     ///< 16位无符号整数
        uint32,                     ///< 32位无符号整数
        uint64,                     ///< 64位无符号整数
        int8,                       ///< 8位有符号整数
        int16,                      ///< 16位有符号整数
        int32,                      ///< 32位有符号整数
        int64,                      ///< 64位有符号整数
        float,                      ///< 单精度浮点数
        double,                     ///< 双精度浮点数
        std::string,                ///< 字符串
        std::vector<uint8>,         ///< 二进制数据向量
        SystemTimePoint,            ///< 系统时间点
        std::nullptr_t              ///< 空值类型
    > data;

    /**
     * @brief 通用模板转换为字符串方法
     * @tparam T 值的类型
     * @param value 要转换的值
     * @return 转换后的字符串表示
     */
    template<typename T>
    static std::string ToString(T value);

    /**
     * @brief 布尔值转换为字符串
     * @param value 布尔值
     * @return 字符串表示 ("true" 或 "false")
     */
    static std::string ToString(bool value);

    /**
     * @brief 8位无符号整数转换为字符串
     * @param value 8位无符号整数值
     * @return 字符串表示
     */
    static std::string ToString(uint8 value);

    /**
     * @brief 8位有符号整数转换为字符串
     * @param value 8位有符号整数值
     * @return 字符串表示
     */
    static std::string ToString(int8 value);

    /**
     * @brief 字符串转换为字符串(返回副本)
     * @param value 字符串值
     * @return 字符串副本
     */
    static std::string ToString(std::string const& value);

    /**
     * @brief 二进制数据向量转换为字符串
     * @param value 二进制数据向量
     * @return 十六进制格式的字符串表示
     */
    static std::string ToString(std::vector<uint8> const& value);

    /**
     * @brief 系统时间点转换为字符串
     * @param value 系统时间点值
     * @return 格式化的时间字符串
     */
    static std::string ToString(SystemTimePoint value);

    /**
     * @brief 空值转换为字符串
     * @return 字符串 "NULL"
     */
    static std::string ToString(std::nullptr_t);
};

/**
 * @class PreparedStatementBase
 * @brief 预处理语句基类
 *
 * 这是应用程序代码中使用的高级类,用于构建和管理预处理语句。
 * 预处理语句提供了一种安全、高效的方式来执行带参数的 SQL 查询。
 *
 * 主要功能:
 * - 存储预处理语句的索引(用于标识具体的 SQL 语句)
 * - 提供参数绑定接口(支持多种数据类型)
 * - 管理参数数据缓冲区
 *
 * 使用示例:
 * @code
 * auto stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER);
 * stmt->setUInt32(0, guid.GetCounter());
 * result = CharacterDatabase.Query(stmt);
 * @endcode
 *
 * @note 该类不可复制,以防止意外的参数数据共享
 * @see PreparedStatement
 * @see PreparedStatementTask
 */
class TC_DATABASE_API PreparedStatementBase
{
    friend class PreparedStatementTask;

    public:
        /**
         * @brief 构造函数
         * @param index 预处理语句的索引,用于标识具体的 SQL 语句
         * @param capacity 参数容量(参数占位符的数量)
         *
         * 初始化预处理语句对象,预留参数存储空间。
         */
        explicit PreparedStatementBase(uint32 index, uint8 capacity);

        /**
         * @brief 虚析构函数
         *
         * 允许派生类正确释放资源。
         */
        virtual ~PreparedStatementBase();

        /**
         * @brief 设置参数为 NULL 值
         * @param index 参数索引(从 0 开始)
         *
         * 将指定位置的参数设置为 SQL NULL 值。
         */
        void setNull(uint8 index);

        /**
         * @brief 设置布尔类型参数
         * @param index 参数索引(从 0 开始)
         * @param value 布尔值
         */
        void setBool(uint8 index, bool value);

        /**
         * @brief 设置 8 位无符号整数参数
         * @param index 参数索引(从 0 开始)
         * @param value 8 位无符号整数值
         */
        void setUInt8(uint8 index, uint8 value);

        /**
         * @brief 设置 16 位无符号整数参数
         * @param index 参数索引(从 0 开始)
         * @param value 16 位无符号整数值
         */
        void setUInt16(uint8 index, uint16 value);

        /**
         * @brief 设置 32 位无符号整数参数
         * @param index 参数索引(从 0 开始)
         * @param value 32 位无符号整数值
         */
        void setUInt32(uint8 index, uint32 value);

        /**
         * @brief 设置 64 位无符号整数参数
         * @param index 参数索引(从 0 开始)
         * @param value 64 位无符号整数值
         */
        void setUInt64(uint8 index, uint64 value);

        /**
         * @brief 设置 8 位有符号整数参数
         * @param index 参数索引(从 0 开始)
         * @param value 8 位有符号整数值
         */
        void setInt8(uint8 index, int8 value);

        /**
         * @brief 设置 16 位有符号整数参数
         * @param index 参数索引(从 0 开始)
         * @param value 16 位有符号整数值
         */
        void setInt16(uint8 index, int16 value);

        /**
         * @brief 设置 32 位有符号整数参数
         * @param index 参数索引(从 0 开始)
         * @param value 32 位有符号整数值
         */
        void setInt32(uint8 index, int32 value);

        /**
         * @brief 设置 64 位有符号整数参数
         * @param index 参数索引(从 0 开始)
         * @param value 64 位有符号整数值
         */
        void setInt64(uint8 index, int64 value);

        /**
         * @brief 设置单精度浮点数参数
         * @param index 参数索引(从 0 开始)
         * @param value 单精度浮点数值
         */
        void setFloat(uint8 index, float value);

        /**
         * @brief 设置双精度浮点数参数
         * @param index 参数索引(从 0 开始)
         * @param value 双精度浮点数值
         */
        void setDouble(uint8 index, double value);

        /**
         * @brief 设置日期/时间参数
         * @param index 参数索引(从 0 开始)
         * @param value 系统时间点值
         *
         * 将时间点转换为数据库可接受的日期/时间格式。
         */
        void setDate(uint8 index, SystemTimePoint value);

        /**
         * @brief 设置字符串参数
         * @param index 参数索引(从 0 开始)
         * @param value 字符串值(const 引用)
         *
         * 字符串会被复制存储,适用于较长的字符串。
         */
        void setString(uint8 index, std::string const& value);

        /**
         * @brief 设置字符串参数(字符串视图版本)
         * @param index 参数索引(从 0 开始)
         * @param value 字符串视图值
         *
         * 使用 std::string_view 避免不必要的字符串复制,
         * 适用于临时字符串或字符串片段。
         */
        void setStringView(uint8 index, std::string_view value);

        /**
         * @brief 设置二进制数据参数
         * @param index 参数索引(从 0 开始)
         * @param value 二进制数据向量
         *
         * 用于存储二进制大对象(BLOB)或字节数组数据。
         */
        void setBinary(uint8 index, std::vector<uint8> const& value);

        /**
         * @brief 设置二进制数据参数(数组模板版本)
         * @tparam Size 数组大小
         * @param index 参数索引(从 0 开始)
         * @param value 固定大小的字节数组
         *
         * 模板方法,允许直接传递固定大小的字节数组,
         * 内部会转换为 vector 进行存储。
         */
        template <size_t Size>
        void setBinary(const uint8 index, std::array<uint8, Size> const& value)
        {
            std::vector<uint8> vec(value.begin(), value.end());
            setBinary(index, vec);
        }

        /**
         * @brief 获取预处理语句索引
         * @return 预处理语句的索引值
         */
        uint32 GetIndex() const { return m_index; }

        /**
         * @brief 获取所有参数数据
         * @return 参数数据向量的常量引用
         */
        std::vector<PreparedStatementData> const& GetParameters() const { return statement_data; }

    protected:
        uint32 m_index;         ///< 预处理语句索引,标识具体的 SQL 语句

        /**
         * @brief 参数数据缓冲区
         *
         * 存储所有绑定参数的数据,独立于具体的数据库实现。
         * 参数按索引顺序存储,与 SQL 语句中的占位符对应。
         */
        std::vector<PreparedStatementData> statement_data;

        /// 禁用拷贝构造函数
        PreparedStatementBase(PreparedStatementBase const& right) = delete;
        /// 禁用拷贝赋值运算符
        PreparedStatementBase& operator=(PreparedStatementBase const& right) = delete;
};

/**
 * @class PreparedStatement
 * @brief 预处理语句模板类
 * @tparam T 数据库连接类型,用于类型安全
 *
 * 这是一个模板化的预处理语句类,继承自 PreparedStatementBase。
 * 模板参数 T 用于区分不同数据库连接类型,提供编译时类型检查。
 *
 * 实际使用中,数据库连接池会返回此类型的指针:
 * - LoginDatabase 返回 PreparedStatement<LoginDatabaseConnection>
 * - CharacterDatabase 返回 PreparedStatement<CharacterDatabaseConnection>
 * - WorldDatabase 返回 PreparedStatement<WorldDatabaseConnection>
 *
 * @note 该类不可复制,继承自基类的删除拷贝构造函数
 * @see PreparedStatementBase
 */
template<typename T>
class PreparedStatement : public PreparedStatementBase
{
public:
    /**
     * @brief 构造函数
     * @param index 预处理语句的索引
     * @param capacity 参数容量
     *
     * 直接调用基类构造函数进行初始化。
     */
    explicit PreparedStatement(uint32 index, uint8 capacity) : PreparedStatementBase(index, capacity)
    {
    }

private:
    /// 禁用拷贝构造函数
    PreparedStatement(PreparedStatement const& right) = delete;
    /// 禁用拷贝赋值运算符
    PreparedStatement& operator=(PreparedStatement const& right) = delete;
};

/**
 * @class PreparedStatementTask
 * @brief 预处理语句执行任务
 *
 * 这是一个底层类,表示一个可入队的数据库操作任务。
 * 继承自 SQLOperation,可以在数据库工作线程池中异步执行。
 *
 * 该类支持两种执行模式:
 * 1. 同步执行: 直接执行并等待结果
 * 2. 异步执行: 在后台线程执行,通过 future 获取结果
 *
 * 使用示例(异步):
 * @code
 * PreparedQueryResultFuture future = database.AsyncQuery(stmt);
 * // ... 其他操作 ...
 * PreparedQueryResult result = future.get();
 * @endcode
 *
 * @note 该类负责管理预处理语句对象的生命周期
 * @see SQLOperation
 * @see PreparedStatementBase
 */
class TC_DATABASE_API PreparedStatementTask : public SQLOperation
{
    public:
        /**
         * @brief 构造函数
         * @param stmt 预处理语句指针(任务会接管所有权)
         * @param async 是否异步执行标志
         *
         * 创建预处理语句任务对象。如果 async 为 true,会创建
         * promise 对象用于返回查询结果。
         */
        PreparedStatementTask(PreparedStatementBase* stmt, bool async = false);

        /**
         * @brief 析构函数
         *
         * 释放预处理语句对象和 promise 对象(如果存在)。
         */
        ~PreparedStatementTask();

        /**
         * @brief 执行预处理语句
         * @return 执行成功返回 true,失败返回 false
         *
         * 实现自 SQLOperation 基类,执行预处理语句并将结果
         * 存储到 promise 中(如果是异步查询)。
         */
        bool Execute() override;

        /**
         * @brief 获取异步查询结果的 future
         * @return PreparedQueryResultFuture 对象
         *
         * 用于异步查询场景,调用者可以通过返回的 future 对象
         * 在将来某个时刻获取查询结果。
         *
         * @warning 只能在异步查询任务上调用此方法
         */
        PreparedQueryResultFuture GetFuture() { return m_result->get_future(); }

    protected:
        PreparedStatementBase* m_stmt;      ///< 预处理语句指针
        bool m_has_result;                  ///< 是否有结果标志(查询语句)
        PreparedQueryResultPromise* m_result; ///< 异步查询结果的 promise 对象
};
#endif
