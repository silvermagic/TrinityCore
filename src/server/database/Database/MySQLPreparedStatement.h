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
 * @file MySQLPreparedStatement.h
 * @brief MySQL预处理语句封装类头文件
 *
 * 本文件定义了MySQLPreparedStatement类,用于封装MySQL预处理语句的功能。
 * 预处理语句可以提高数据库查询性能并防止SQL注入攻击。
 * 每个MySQLConnection实例都会拥有独立的MySQLPreparedStatement对象实例。
 */

#ifndef MySQLPreparedStatement_h__
#define MySQLPreparedStatement_h__

#include "DatabaseEnvFwd.h"
#include "Define.h"
#include "Duration.h"
#include "MySQLWorkaround.h"
#include <string>
#include <vector>

class MySQLConnection;
class PreparedStatementBase;

/**
 * @class MySQLPreparedStatement
 * @brief MySQL预处理语句封装类
 *
 * 该类封装了MySQL预处理语句(Prepared Statement)的功能,提供参数绑定和执行接口。
 * 实例在每个MySQLConnection中是唯一的,只有当预处理语句任务执行时才会访问这些对象。
 *
 * 主要功能:
 * - 封装MySQL_STMT结构体及其相关操作
 * - 提供参数绑定机制,支持多种数据类型
 * - 管理预处理语句的生命周期
 * - 提供查询字符串获取功能用于调试和日志
 *
 * @note 该类的实例由MySQLConnection管理,不应直接创建
 * @note 该类不可复制,以确保每个连接的预处理语句实例唯一性
 */
class TC_DATABASE_API MySQLPreparedStatement
{
    friend class MySQLConnection;           ///< MySQLConnection作为友元类,可以访问私有成员
    friend class PreparedStatementBase;     ///< PreparedStatementBase作为友元类,可以访问保护成员

    public:
        /**
         * @brief 构造函数
         *
         * 初始化MySQL预处理语句对象,设置参数绑定缓冲区
         *
         * @param stmt MySQL语句句柄(MySQLStmt*)
         * @param queryString 预处理语句的SQL查询字符串
         */
        MySQLPreparedStatement(MySQLStmt* stmt, std::string queryString);

        /**
         * @brief 析构函数
         *
         * 清理MySQL预处理语句资源,包括绑定缓冲区和语句句柄
         */
        ~MySQLPreparedStatement();

        /**
         * @brief 绑定参数到预处理语句
         *
         * 从PreparedStatementBase对象中提取参数并绑定到MySQL预处理语句
         *
         * @param stmt 包含待绑定参数的预处理语句基类指针
         */
        void BindParameters(PreparedStatementBase* stmt);

        /**
         * @brief 获取参数数量
         *
         * 返回预处理语句中参数占位符(?)的数量
         *
         * @return 参数数量
         */
        uint32 GetParameterCount() const { return m_paramCount; }

    protected:
        /**
         * @brief 设置NULL参数
         *
         * 将指定索引位置的参数设置为NULL值
         *
         * @param index 参数索引(从0开始)
         */
        void SetParameter(uint8 index, std::nullptr_t);

        /**
         * @brief 设置布尔类型参数
         *
         * 将指定索引位置的参数设置为布尔值
         *
         * @param index 参数索引(从0开始)
         * @param value 布尔值
         */
        void SetParameter(uint8 index, bool value);

        /**
         * @brief 设置数值类型参数(模板函数)
         *
         * 将指定索引位置的参数设置为数值类型值
         * 支持整数和浮点数类型(uint8, int8, uint16, int16, uint32, int32, uint64, int64, float, double)
         *
         * @tparam T 数值类型
         * @param index 参数索引(从0开始)
         * @param value 数值
         */
        template<typename T>
        void SetParameter(uint8 index, T value);

        /**
         * @brief 设置时间点参数
         *
         * 将指定索引位置的参数设置为时间点值(SystemTimePoint)
         * 通常用于DATETIME或TIMESTAMP类型的字段
         *
         * @param index 参数索引(从0开始)
         * @param value 时间点值
         */
        void SetParameter(uint8 index, SystemTimePoint value);

        /**
         * @brief 设置字符串参数
         *
         * 将指定索引位置的参数设置为字符串值
         * 用于VARCHAR、TEXT、CHAR等字符类型字段
         *
         * @param index 参数索引(从0开始)
         * @param value 字符串值
         */
        void SetParameter(uint8 index, std::string const& value);

        /**
         * @brief 设置二进制数据参数
         *
         * 将指定索引位置的参数设置为二进制数据
         * 用于BLOB、BINARY等二进制类型字段
         *
         * @param index 参数索引(从0开始)
         * @param value 二进制数据向量
         */
        void SetParameter(uint8 index, std::vector<uint8> const& value);

        /**
         * @brief 获取MySQL语句句柄
         *
         * 返回底层的MySQL_STMT指针
         *
         * @return MySQL语句句柄
         */
        MySQLStmt* GetSTMT() { return m_Mstmt; }

        /**
         * @brief 获取参数绑定结构
         *
         * 返回MySQL参数绑定结构数组指针
         *
         * @return 参数绑定结构指针
         */
        MySQLBind* GetBind() { return m_bind; }

        PreparedStatementBase* m_stmt;          ///< 关联的预处理语句基类指针

        /**
         * @brief 清除所有参数绑定
         *
         * 重置所有参数的绑定状态,为下一次执行做准备
         */
        void ClearParameters();

        /**
         * @brief 断言参数索引有效性
         *
         * 检查参数索引是否在有效范围内,无效时触发断言
         *
         * @param index 待检查的参数索引
         */
        void AssertValidIndex(uint8 index);

        /**
         * @brief 获取查询字符串
         *
         * 返回预处理语句的SQL查询字符串,用于调试和日志记录
         *
         * @return SQL查询字符串
         */
        std::string getQueryString() const;

    private:
        MySQLStmt* m_Mstmt;                    ///< MySQL预处理语句句柄(MySQL_STMT*)
        uint32 m_paramCount;                   ///< 参数数量
        std::vector<bool> m_paramsSet;         ///< 参数设置状态标记向量(标记哪些参数已被设置)
        MySQLBind* m_bind;                     ///< MySQL参数绑定结构数组(MySQL_BIND*)
        std::string const m_queryString;       ///< SQL查询字符串(用于调试和日志)

        /**
         * @brief 禁用拷贝构造函数
         */
        MySQLPreparedStatement(MySQLPreparedStatement const& right) = delete;

        /**
         * @brief 禁用赋值运算符
         */
        MySQLPreparedStatement& operator=(MySQLPreparedStatement const& right) = delete;
};

#endif // MySQLPreparedStatement_h__
