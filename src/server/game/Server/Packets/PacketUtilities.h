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
 * @file PacketUtilities.h
 * @brief 网络包工具类头文件
 *
 * 本文件提供用于处理客户端网络包的安全工具类,主要功能包括:
 * 1. 字符串验证和过滤 - 防止恶意或无效字符串输入
 * 2. 数组容量限制 - 防止循环计数器欺骗攻击
 * 3. 异常定义 - 为无效数据提供详细的错误信息
 *
 * 这些工具类在网络包解析过程中自动执行验证,确保服务器不会处理
 * 恶意构造的数据包,提高服务器的安全性和稳定性。
 */

#ifndef PacketUtilities_h__
#define PacketUtilities_h__

#include "ByteBuffer.h"
#include "Tuples.h"
#include <short_alloc/short_alloc.h>
#include <string_view>

namespace WorldPackets
{
    /**
     * @class InvalidStringValueException
     * @brief 无效字符串值异常
     *
     * 继承自 ByteBufferInvalidValueException,用于表示从网络包中读取的
     * 字符串值无效。这是所有字符串验证相关异常的基类。
     *
     * 当字符串未通过验证时抛出,携带无效字符串的值以便调试。
     */
    class InvalidStringValueException : public ByteBufferInvalidValueException
    {
    public:
        /**
         * @brief 构造函数
         * @param value 无效的字符串值
         */
        InvalidStringValueException(std::string const& value);

        /**
         * @brief 获取无效的字符串值
         * @return 无效字符串的常量引用
         */
        std::string const& GetInvalidValue() const { return _value; }

    private:
        std::string _value;  ///< 存储无效的字符串值,用于错误报告
    };

    /**
     * @class InvalidUtf8ValueException
     * @brief 无效UTF-8编码异常
     *
     * 继承自 InvalidStringValueException,表示字符串包含无效的UTF-8编码。
     * 当客户端发送非UTF-8编码的字符串时抛出此异常。
     */
    class InvalidUtf8ValueException : public InvalidStringValueException
    {
    public:
        /**
         * @brief 构造函数
         * @param value 包含无效UTF-8编码的字符串
         */
        InvalidUtf8ValueException(std::string const& value);
    };

    /**
     * @class InvalidHyperlinkException
     * @brief 无效超链接异常
     *
     * 继承自 InvalidStringValueException,表示字符串包含格式错误或无效的超链接。
     * 当客户端发送格式不正确的超链接时抛出此异常。
     */
    class InvalidHyperlinkException : public InvalidStringValueException
    {
    public:
        /**
         * @brief 构造函数
         * @param value 包含无效超链接的字符串
         */
        InvalidHyperlinkException(std::string const& value);
    };

    /**
     * @class IllegalHyperlinkException
     * @brief 非法超链接异常
     *
     * 继承自 InvalidStringValueException,表示字符串包含不允许的超链接。
     * 当客户端在不应该包含超链接的地方发送了超链接时抛出此异常。
     */
    class IllegalHyperlinkException : public InvalidStringValueException
    {
    public:
        /**
         * @brief 构造函数
         * @param value 包含非法超链接的字符串
         */
        IllegalHyperlinkException(std::string const& value);
    };

    /**
     * @namespace Strings
     * @brief 字符串验证器集合
     *
     * 包含多种字符串验证策略,用于在解析网络包时对字符串内容进行验证。
     * 每个验证器提供一个静态 Validate 方法,返回 true 表示验证通过,
     * 验证失败时抛出相应的异常。
     */
    namespace Strings
    {
        /**
         * @struct RawBytes
         * @brief 原始字节验证器
         *
         * 允许任何字节序列通过验证,不执行任何检查。
         * 用于需要接受原始字节数据的场景。
         */
        struct RawBytes { static bool Validate(std::string const& /*value*/) { return true; } };

        /**
         * @struct ByteSize
         * @brief 字节大小验证器
         * @tparam MaxBytesWithoutNullTerminator 允许的最大字节数(不包括空终止符)
         *
         * 验证字符串的字节大小是否超过指定限制。
         * 用于防止客户端发送过长的字符串导致缓冲区溢出。
         */
        template<std::size_t MaxBytesWithoutNullTerminator>
        struct ByteSize
        {
            /**
             * @brief 验证字符串大小
             * @param value 待验证的字符串
             * @return 如果字符串大小不超过限制则返回 true
             */
            static bool Validate(std::string const& value) { return value.size() <= MaxBytesWithoutNullTerminator; }
        };

        /**
         * @struct Utf8
         * @brief UTF-8编码验证器
         *
         * 验证字符串是否为有效的UTF-8编码。
         * 确保客户端发送的字符串符合UTF-8标准,避免编码问题。
         */
        struct Utf8 { static bool Validate(std::string const& value); };

        /**
         * @struct Hyperlinks
         * @brief 超链接验证器
         *
         * 验证字符串中的所有超链接是否格式正确。
         * 允许存在超链接,但要求所有超链接格式有效。
         */
        struct Hyperlinks { static bool Validate(std::string const& value); };

        /**
         * @struct NoHyperlinks
         * @brief 禁止超链接验证器
         *
         * 验证字符串中是否包含任何超链接(以 '|' 字符标记)。
         * 在不允许用户输入超链接的场景使用,防止注入攻击。
         */
        struct NoHyperlinks { static bool Validate(std::string const& value); };
    }

    /**
     * @class String
     * @brief 安全字符串包装类,自动防止客户端数据包中的无效字符串
     *
     * @tparam MaxBytesWithoutNullTerminator 最大字节数(不包括空终止符)
     * @tparam Validators 额外的验证器类型列表
     *
     * 该模板类用于在解析网络包时自动执行字符串验证,防止恶意或格式错误的字符串
     * 进入服务器逻辑。通过模板参数组合不同的验证策略。
     *
     * 默认验证流程:
     * 1. 字节大小检查(强制)
     * 2. UTF-8编码验证(除非指定了 RawBytes)
     * 3. 用户指定的其他验证器
     *
     * 使用示例:
     * @code
     * String<256> simpleString;  // 最大256字节,UTF-8编码
     * String<64, Strings::NoHyperlinks> noLinkString;  // 最大64字节,不允许超链接
     * String<1024, Strings::RawBytes> rawString;  // 最大1024字节,不验证编码
     * @endcode
     *
     * @note 当验证失败时,会抛出相应的异常,阻止继续处理该数据包
     */
    template<std::size_t MaxBytesWithoutNullTerminator, typename... Validators>
    class String
    {
        /**
         * @brief 验证器列表类型
         *
         * 根据 Validators 参数自动构建验证器列表:
         * - 如果包含 RawBytes,则只执行大小验证
         * - 否则执行大小验证 + UTF-8验证 + 用户指定的验证器
         */
        using ValidatorList = std::conditional_t<!Trinity::has_type<Strings::RawBytes, std::tuple<Validators...>>::value,
            std::tuple<Strings::ByteSize<MaxBytesWithoutNullTerminator>, Strings::Utf8, Validators...>,
            std::tuple<Strings::ByteSize<MaxBytesWithoutNullTerminator>, Validators...>>;

    public:
        /**
         * @brief 检查字符串是否为空
         * @return 如果字符串为空返回 true
         */
        bool empty() const { return _storage.empty(); }

        /**
         * @brief 获取C风格字符串指针
         * @return 指向内部字符串数据的指针
         */
        char const* c_str() const { return _storage.c_str(); }

        /**
         * @brief 转换为字符串视图
         * @return 包含字符串内容的字符串视图
         */
        operator std::string_view() const { return _storage; }

        /**
         * @brief 转换为字符串引用(可修改)
         * @return 内部字符串的可修改引用
         */
        operator std::string&() { return _storage; }

        /**
         * @brief 转换为字符串常量引用
         * @return 内部字符串的常量引用
         */
        operator std::string const&() const { return _storage; }

        /**
         * @brief 移动内部字符串的所有权
         * @return 移动后的字符串对象
         *
         * 允许将内部字符串移动出来,避免不必要的拷贝。
         * 移动后当前对象将包含空字符串。
         */
        std::string&& Move() { return std::move(_storage); }

        /**
         * @brief 从字节缓冲区读取字符串并验证
         * @param data 字节缓冲区引用
         * @param value 目标字符串对象引用
         * @return 字节缓冲区引用,支持链式调用
         *
         * 从缓冲区读取以空字符结尾的字符串,并执行所有验证。
         * 如果验证失败,抛出相应的异常。
         *
         * @note 此操作在数据包解析时自动调用,开发者无需手动验证
         */
        friend ByteBuffer& operator>>(ByteBuffer& data, String& value)
        {
            value._storage = data.ReadCString(false);  // 读取C风格字符串,不进行验证
            value.Validate();  // 执行所有验证器
            return data;
        }

    private:
        /**
         * @brief 执行所有验证器
         * @return 所有验证都通过则返回 true
         *
         * 使用模板元编程技术依次调用验证器列表中的所有验证器。
         * 如果任何验证器失败(抛出异常),验证过程终止。
         */
        bool Validate() const
        {
            return ValidateNth(std::make_index_sequence<std::tuple_size_v<ValidatorList>>{});
        }

        /**
         * @brief 验证第N个验证器
         * @tparam indexes 验证器索引序列
         * @return 所有验证都通过则返回 true
         *
         * 使用折叠表达式依次调用每个验证器的 Validate 方法。
         * 实现编译期的验证器链式调用,具有零运行时开销。
         */
        template<std::size_t... indexes>
        bool ValidateNth(std::index_sequence<indexes...>) const
        {
            return (std::tuple_element_t<indexes, ValidatorList>::Validate(_storage) && ...);
        }

        std::string _storage;  ///< 内部字符串存储
    };

    /**
     * @class PacketArrayMaxCapacityException
     * @brief 数据包数组最大容量异常
     *
     * 继承自 ByteBufferException,表示尝试读取的数组元素数量超过了安全限制。
     * 这是防止"循环计数器欺骗"攻击的关键机制。
     *
     * 恶意客户端可能在数据包中声明一个巨大的数组大小,导致服务器分配过多内存
     * 或执行过多的循环迭代。此异常强制限制数组的最大容量。
     */
    class PacketArrayMaxCapacityException : public ByteBufferException
    {
    public:
        /**
         * @brief 构造函数
         * @param requestedSize 客户端请求的数组大小
         * @param sizeLimit 允许的最大容量限制
         */
        PacketArrayMaxCapacityException(std::size_t requestedSize, std::size_t sizeLimit);
    };

    /**
     * @class Array
     * @brief 安全数组容器,自动防止客户端数据包中的循环计数器欺骗
     *
     * @tparam T 数组元素类型
     * @tparam N 数组的最大容量
     *
     * 该模板类是一个类似 std::vector 的容器,但具有以下安全特性:
     * 1. 编译期确定的最大容量限制
     * 2. 超过容量限制时抛出异常,而非分配更多内存
     * 3. 使用短字符串优化技术(short_alloc),小数组在栈上分配,提高性能
     *
     * 使用示例:
     * @code
     * Array<int, 100> intArray;  // 最多100个int元素
     * Array<std::string, 50> stringArray;  // 最多50个字符串元素
     *
     * // 从数据包读取时自动检查容量
     * uint32 count;
     * data >> count;
     * for (uint32 i = 0; i < count; ++i)
     * {
     *     MyStruct element;
     *     data >> element;
     *     array.push_back(element);  // 如果超过100个元素会抛出异常
     * }
     * @endcode
     *
     * @note 容量限制在编译期确定,运行时无法绕过
     * @note 使用栈内存优化小数组的性能,避免频繁的堆内存分配
     */
    template<typename T, std::size_t N>
    class Array
    {
    public:
        /**
         * @brief 分配器类型
         *
         * 使用 short_alloc 分配器,小数组在栈上预分配的 arena 中分配内存。
         * arena 大小 = N * sizeof(T),并对齐到 max_align_t 边界。
         */
        using allocator_type = short_alloc::short_alloc<T, (N * sizeof(T) + (alignof(std::max_align_t) - 1)) & ~(alignof(std::max_align_t) - 1)>;

        /**
         * @brief Arena类型,用于管理栈上的预分配内存
         */
        using arena_type = typename allocator_type::arena_type;

        /**
         * @brief 底层存储类型
         *
         * 使用 std::vector 作为底层容器,配合自定义分配器实现内存管理。
         */
        using storage_type = std::vector<T, allocator_type>;

        /**
         * @brief 最大容量常量
         *
         * 编译期常量,表示数组的最大容量限制。
         * 任何尝试超过此限制的操作都会抛出异常。
         */
        using max_capacity = std::integral_constant<std::size_t, N>;

        // 标准容器类型定义,支持STL算法和迭代器
        using value_type = typename storage_type::value_type;
        using size_type = typename storage_type::size_type;
        using pointer = typename storage_type::pointer;
        using const_pointer = typename storage_type::const_pointer;
        using reference = typename storage_type::reference;
        using const_reference = typename storage_type::const_reference;
        using iterator = typename storage_type::iterator;
        using const_iterator = typename storage_type::const_iterator;

        /**
         * @brief 默认构造函数
         *
         * 初始化存储并关联到栈上的 arena。
         * 小于N个元素的数组将在栈内存中分配,无需堆分配。
         */
        Array() : _storage(_data) { }

        /**
         * @brief 拷贝构造函数
         * @param other 源数组对象
         *
         * 执行深拷贝,将所有元素从源数组复制到新数组。
         * 新数组使用自己的 arena,不共享内存。
         */
        Array(Array const& other) : Array()
        {
            for (T const& element : other)
                _storage.push_back(element);
        }

        /**
         * @brief 移动构造函数(已删除)
         *
         * 禁用移动构造,因为 arena 不能移动。
         */
        Array(Array&& other) noexcept = delete;

        /**
         * @brief 拷贝赋值运算符
         * @param other 源数组对象
         * @return 当前对象的引用
         *
         * 清空当前数组并复制源数组的所有元素。
         */
        Array& operator=(Array const& other)
        {
            if (this == &other)
                return *this;

            _storage.clear();
            for (T const& element : other)
                _storage.push_back(element);

            return *this;
        }

        /**
         * @brief 移动赋值运算符(已删除)
         *
         * 禁用移动赋值,因为 arena 不能移动。
         */
        Array& operator=(Array&& other) noexcept = delete;

        /**
         * @brief 获取指向数组起始位置的迭代器
         * @return 可修改的起始迭代器
         */
        iterator begin() { return _storage.begin(); }

        /**
         * @brief 获取指向数组起始位置的常量迭代器
         * @return 只读的起始迭代器
         */
        const_iterator begin() const { return _storage.begin(); }

        /**
         * @brief 获取指向数组末尾的迭代器
         * @return 可修改的末尾迭代器
         */
        iterator end() { return _storage.end(); }

        /**
         * @brief 获取指向数组末尾的常量迭代器
         * @return 只读的末尾迭代器
         */
        const_iterator end() const { return _storage.end(); }

        /**
         * @brief 获取指向底层数组的指针
         * @return 指向元素数组的可修改指针
         */
        pointer data() { return _storage.data(); }

        /**
         * @brief 获取指向底层数组的常量指针
         * @return 指向元素数组的只读指针
         */
        const_pointer data() const { return _storage.data(); }

        /**
         * @brief 获取数组中的元素数量
         * @return 当前元素数量
         */
        size_type size() const { return _storage.size(); }

        /**
         * @brief 检查数组是否为空
         * @return 如果数组为空返回 true
         */
        bool empty() const { return _storage.empty(); }

        /**
         * @brief 下标访问运算符
         * @param i 元素索引
         * @return 指定位置元素的可修改引用
         */
        reference operator[](size_type i) { return _storage[i]; }

        /**
         * @brief 下标访问运算符(只读版本)
         * @param i 元素索引
         * @return 指定位置元素的常量引用
         */
        const_reference operator[](size_type i) const { return _storage[i]; }

        /**
         * @brief 调整数组大小
         * @param newSize 新的大小
         *
         * 如果新大小超过最大容量,抛出 PacketArrayMaxCapacityException 异常。
         * 否则调整数组大小,新增元素使用默认构造。
         *
         * @warning 此方法会检查容量限制,防止恶意数据包导致内存过度分配
         */
        void resize(size_type newSize)
        {
            if (newSize > max_capacity::value)
                throw PacketArrayMaxCapacityException(newSize, max_capacity::value);

            _storage.resize(newSize);
        }

        /**
         * @brief 在数组末尾添加元素(拷贝版本)
         * @param value 要添加的元素值
         *
         * 如果数组已达到最大容量,抛出 PacketArrayMaxCapacityException 异常。
         * 否则将元素的拷贝添加到数组末尾。
         *
         * @warning 此方法会检查容量限制,防止恶意数据包导致无限增长
         */
        void push_back(value_type const& value)
        {
            if (_storage.size() >= max_capacity::value)
                throw PacketArrayMaxCapacityException(_storage.size() + 1, max_capacity::value);

            _storage.push_back(value);
        }

        /**
         * @brief 在数组末尾添加元素(移动版本)
         * @param value 要添加的元素值(右值引用)
         *
         * 如果数组已达到最大容量,抛出 PacketArrayMaxCapacityException 异常。
         * 否则将元素移动到数组末尾,避免不必要的拷贝。
         *
         * @warning 此方法会检查容量限制,防止恶意数据包导致无限增长
         */
        void push_back(value_type&& value)
        {
            if (_storage.size() >= max_capacity::value)
                throw PacketArrayMaxCapacityException(_storage.size() + 1, max_capacity::value);

            _storage.push_back(std::forward<value_type>(value));
        }

        /**
         * @brief 在数组末尾原位构造元素
         * @tparam Args 构造函数参数类型
         * @param args 传递给元素构造函数的参数
         * @return 新构造元素的引用
         *
         * 直接在数组末尾构造元素,避免临时对象的创建和移动。
         * 性能优于 push_back,特别是对于复杂对象。
         *
         * @note emplace_back 不检查容量限制,由底层 vector 处理
         *       (会在超过 arena 大小时使用堆内存,但不会超过 N)
         */
        template<typename... Args>
        T& emplace_back(Args&&... args)
        {
            _storage.emplace_back(std::forward<Args>(args)...);
            return _storage.back();
        }

        /**
         * @brief 删除指定范围的元素
         * @param first 指向范围起始的迭代器
         * @param last 指向范围末尾的迭代器
         * @return 指向被删除元素之后元素的迭代器
         *
         * 删除 [first, last) 范围内的所有元素。
         * 被删除元素之后的元素会向前移动。
         */
        iterator erase(const_iterator first, const_iterator last)
        {
            return _storage.erase(first, last);
        }

        /**
         * @brief 清空数组
         *
         * 删除所有元素,数组大小变为0。
         * 但不释放 arena 内存,保留栈上的预分配空间供后续使用。
         */
        void clear()
        {
            _storage.clear();
        }

    private:
        arena_type _data;      ///< 栈上预分配的内存区域,大小为 N * sizeof(T)
        storage_type _storage;  ///< 向量容器,使用自定义分配器管理内存
    };
}

#endif // PacketUtilities_h__
