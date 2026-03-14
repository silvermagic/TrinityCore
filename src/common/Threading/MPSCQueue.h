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
 * @file MPSCQueue.h
 * @brief 无锁多生产者单消费者队列实现
 *
 * @details 本文件实现了 Dmitry Vyukov 提出的高性能无锁 MPSC 队列算法。
 * MPSC 表示 Multiple Producers Single Consumer（多生产者单消费者）。
 *
 * ## 模块职责
 * - 提供高性能的无锁队列实现
 * - 支持多生产者并发写入，单消费者读取的场景
 * - 提供侵入式和非侵入式两种实现方式
 *
 * ## 主要功能
 * 1. 无锁入队操作（Enqueue）- 支持多线程并发
 * 2. 无锁出队操作（Dequeue）- 只允许单线程操作
 * 3. 两种实现方式：
 *    - MPSCQueueNonIntrusive：非侵入式，使用独立的节点包装数据
 *    - MPSCQueueIntrusive：侵入式，数据结构内嵌链接指针
 *
 * ## 并发队列原理
 * ### MPSC 队列的特点
 * - **多生产者**：多个线程可以并发地向队列添加元素
 * - **单消费者**：只有一个线程从队列取出元素
 * - **无锁设计**：使用原子操作（atomic）代替互斥锁，避免锁竞争
 *
 * ### 实现原理
 * 队列基于单向链表实现，采用以下关键技术：
 * 1. **原子操作**：使用 std::atomic 保证操作的原子性
 * 2. **内存序（Memory Order）**：
 *    - memory_order_acquire：获取语义，确保后续读操作不会被重排到此之前
 *    - memory_order_release：释放语义，确保之前写操作不会被重排到此之后
 *    - memory_order_acq_rel：同时具有获取和释放语义
 * 3. **节点交换**：生产者通过原子交换将新节点插入队列头部
 * 4. **延迟链接**：消费者等待生产者完成节点的链接操作
 *
 * ### 性能特点
 * - **优点**：
 *   - 无锁设计，避免线程阻塞
 *   - 生产者操作快速，仅需一次原子交换
 *   - 适合高并发写入场景
 * - **缺点**：
 *   - 只支持单消费者
 *   - 消费者在某些情况下可能需要忙等待
 *   - 实现复杂，调试困难
 *
 * ### 适用场景
 * - 日志系统：多线程写入，单线程刷新到磁盘
 * - 任务队列：多线程提交任务，单线程处理
 * - 事件系统：多线程触发事件，单线程分发
 * - 网络消息队列：多线程发送消息，单线程网络IO
 *
 * ## 两种实现对比
 * ### MPSCQueueNonIntrusive（非侵入式）
 * - 每个元素包装在独立的节点中
 * - 内存分配开销较大
 * - 使用简单，不要求修改数据结构
 *
 * ### MPSCQueueIntrusive（侵入式）
 * - 数据结构内嵌链接指针
 * - 无需额外内存分配
 * - 性能更高，但需要修改数据结构
 *
 * ## 使用示例
 * @code
 * // 非侵入式用法
 * MPSCQueue<int> queue;
 * queue.Enqueue(new int(42));  // 生产者线程
 * int* value;
 * if (queue.Dequeue(value))    // 消费者线程
 *     delete value;
 *
 * // 侵入式用法
 * struct MyData {
 *     std::atomic<MyData*> Next;
 *     int value;
 * };
 * MPSCQueue<MyData, &MyData::Next> intrusiveQueue;
 * @endcode
 *
 * ## 参考文献
 * - Dmitry Vyukov's MPSC Queue:
 *   http://www.1024cores.net/home/lock-free-algorithms/queues/non-intrusive-mpsc-node-based-queue
 */

#ifndef MPSCQueue_h__
#define MPSCQueue_h__

#include <atomic>
#include <utility>

namespace Trinity
{
namespace Impl
{
/**
 * @class MPSCQueueNonIntrusive
 * @brief 非侵入式无锁 MPSC 队列实现
 *
 * @details 这是 Dmitry Vyukov 无锁 MPSC 队列的 C++ 实现。
 * 非侵入式意味着每个数据元素会被包装在一个独立的节点中。
 *
 * ## 模板参数
 * @tparam T 队列中存储的数据类型（指针类型）
 *
 * ## 数据结构
 * 队列内部维护一个单向链表：
 * - _head: 指向链表头部（最新插入的节点）
 * - _tail: 指向链表尾部（下一个要消费的节点）
 * - 链表方向：从 _tail 到 _head
 *
 * ## 线程安全保证
 * - Enqueue：多线程安全，支持多个生产者并发调用
 * - Dequeue：单线程安全，只能由一个消费者线程调用
 *
 * ## 内存管理
 * - 生产者：负责分配节点内存
 * - 消费者：负责释放节点和数据内存
 * - 析构时清理所有剩余元素
 *
 * ## 性能特点
 * - Enqueue：O(1)，快速原子交换操作
 * - Dequeue：O(1)，但可能需要等待生产者完成链接
 * - 无锁设计，避免线程阻塞和上下文切换
 */
// C++ implementation of Dmitry Vyukov's lock free MPSC queue
// http://www.1024cores.net/home/lock-free-algorithms/queues/non-intrusive-mpsc-node-based-queue
template<typename T>
class MPSCQueueNonIntrusive
{
public:
    /**
     * @brief 构造一个空的 MPSC 队列
     *
     * @details 初始化队列时创建一个哨兵节点（dummy node），
     * 避免在第一次 Enqueue 时处理特殊情况。
     * 初始状态：
     * - _head 和 _tail 都指向哨兵节点
     * - 哨兵节点的 Next 指针为 nullptr
     *
     * @note 使用 memory_order_relaxed 因为此时还没有并发访问
     */
    MPSCQueueNonIntrusive() : _head(new Node()), _tail(_head.load(std::memory_order_relaxed))
    {
        Node* front = _head.load(std::memory_order_relaxed);
        front->Next.store(nullptr, std::memory_order_relaxed);  // 哨兵节点的 Next 为空
    }

    /**
     * @brief 析构函数，清理队列中所有元素
     *
     * @details 析构时会：
     * 1. 从队列中取出所有剩余元素并删除（数据和节点）
     * 2. 删除哨兵节点
     *
     * @warning 假设消费者线程已经停止，否则存在竞态条件
     */
    ~MPSCQueueNonIntrusive()
    {
        T* output;
        // 循环取出并删除所有剩余数据
        while (Dequeue(output))
            delete output;

        // 删除哨兵节点
        Node* front = _head.load(std::memory_order_relaxed);
        delete front;
    }

    /**
     * @brief 向队列中添加一个元素（生产者操作）
     *
     * @details 线程安全的入队操作，可以被多个生产者线程并发调用。
     *
     * ## 算法流程
     * 1. 创建新节点包装数据
     * 2. 原子交换 _head，将新节点插入链表头部
     * 3. 将前一个头节点的 Next 指向新节点，完成链接
     *
     * ## 关键点
     * - 使用 exchange 原子操作，确保并发安全
     * - 链接操作可能延迟，消费者需要等待链接完成
     *
     * ## 内存序说明
     * - memory_order_acq_rel：确保节点创建和指针设置对其他线程可见
     * - memory_order_release：确保之前的写操作对消费者可见
     *
     * @param input 要添加的数据指针（调用者分配内存）
     *
     * @note 线程安全：支持多线程并发调用
     * @note 内存管理：调用者负责分配数据内存，队列负责释放
     */
    void Enqueue(T* input)
    {
        // 创建新节点，包装用户数据
        Node* node = new Node(input);

        // 原子交换：将 node 设置为新的 _head，返回旧的 _head
        // memory_order_acq_rel 确保操作的原子性和可见性
        Node* prevHead = _head.exchange(node, std::memory_order_acq_rel);

        // 将前一个头节点的 Next 指向新节点，完成链接
        // memory_order_release 确保消费者能看到完整的节点状态
        prevHead->Next.store(node, std::memory_order_release);
    }

    /**
     * @brief 从队列中取出一个元素（消费者操作）
     *
     * @details 单线程安全的出队操作，只能由一个消费者线程调用。
     *
     * ## 算法流程
     * 1. 读取 _tail 指向的节点
     * 2. 检查该节点的 Next 指针
     * 3. 如果 Next 为空，说明队列为空
     * 4. 如果 Next 非空，取出数据，更新 _tail，删除旧节点
     *
     * ## 为什么需要等待 Next？
     * 生产者在 Enqueue 时，先交换 _head，再设置 prevHead->Next。
     * 消费者可能在生产者设置 Next 之前读取到 prevHead，
     * 此时需要等待生产者完成链接操作。
     *
     * @param[out] result 输出参数，存储取出的数据指针
     * @return true 成功取出数据
     * @return false 队列为空
     *
     * @note 单线程安全：只能由一个消费者线程调用
     * @note 内存管理：调用者负责删除返回的数据
     */
    bool Dequeue(T*& result)
    {
        // 读取当前尾节点
        Node* tail = _tail.load(std::memory_order_relaxed);

        // 尝试读取尾节点的下一个节点
        // memory_order_acquire 确保能看到生产者的完整写入
        Node* next = tail->Next.load(std::memory_order_acquire);

        // 如果 next 为空，说明队列为空
        if (!next)
            return false;

        // 取出数据
        result = next->Data;

        // 更新 _tail 指向下一个节点
        _tail.store(next, std::memory_order_release);

        // 删除旧的尾节点（哨兵节点或已消费的节点）
        delete tail;

        return true;
    }

private:
    /**
     * @brief 队列的内部节点结构
     *
     * @details 每个节点包含：
     * - Data: 指向用户数据的指针
     * - Next: 原子指针，指向下一个节点
     */
    struct Node
    {
        Node() = default;

        /**
         * @brief 构造包含数据的节点
         * @param data 用户数据指针
         */
        explicit Node(T* data) : Data(data)
        {
            Next.store(nullptr, std::memory_order_relaxed);
        }

        T* Data;                     //!< 用户数据指针
        std::atomic<Node*> Next;     //!< 指向下一个节点的原子指针
    };

    std::atomic<Node*> _head;        //!< 队列头，生产者在此插入新节点
    std::atomic<Node*> _tail;        //!< 队列尾，消费者从此取出节点

    // 禁用拷贝和赋值，确保队列的唯一性
    MPSCQueueNonIntrusive(MPSCQueueNonIntrusive const&) = delete;
    MPSCQueueNonIntrusive& operator=(MPSCQueueNonIntrusive const&) = delete;
};

/**
 * @class MPSCQueueIntrusive
 * @brief 侵入式无锁 MPSC 队列实现
 *
 * @details 这是 Dmitry Vyukov 侵入式无锁 MPSC 队列的 C++ 实现。
 * 侵入式意味着数据类型 T 必须内嵌一个原子链接指针成员。
 *
 * ## 模板参数
 * @tparam T 队列中存储的数据类型
 * @tparam IntrusiveLink 指向 T 中原子链接指针的成员指针
 *                       格式为 &T::Next，其中 Next 是 std::atomic<T*> 类型
 *
 * ## 侵入式设计优势
 * - 无需额外的节点内存分配
 * - 减少内存碎片
 * - 更好的缓存局部性
 * - 更高的性能
 *
 * ## 数据结构要求
 * 类型 T 必须包含一个 std::atomic<T*> 类型的成员：
 * @code
 * struct MyData {
 *     std::atomic<MyData*> Next;  // 侵入式链接指针
 *     int value;
 *     // ... 其他数据
 * };
 * @endcode
 *
 * ## 与非侵入式的区别
 * - 不使用额外的 Node 包装数据
 * - 数据本身包含链接指针
 * - 使用哨兵对象而非哨兵节点
 * - Dequeue 逻辑更复杂（需要处理哨兵对象）
 *
 * ## 线程安全保证
 * - Enqueue：多线程安全，支持多个生产者并发调用
 * - Dequeue：单线程安全，只能由一个消费者线程调用
 *
 * ## 内存管理
 * - 生产者：负责分配数据内存，并初始化链接指针
 * - 消费者：负责释放数据内存
 * - 哨兵对象在队列析构时销毁
 */
// C++ implementation of Dmitry Vyukov's lock free MPSC queue
// http://www.1024cores.net/home/lock-free-algorithms/queues/intrusive-mpsc-node-based-queue
template<typename T, std::atomic<T*> T::* IntrusiveLink>
class MPSCQueueIntrusive
{
public:
    /**
     * @brief 构造一个空的侵入式 MPSC 队列
     *
     * @details 初始化队列时创建一个哨兵对象（dummy object）。
     * 使用 std::aligned_storage 创建未初始化的存储空间，
     * 避免要求类型 T 有默认构造函数。
     *
     * ## 初始化流程
     * 1. 在对齐存储中创建哨兵对象
     * 2. 只初始化哨兵对象的链接指针成员
     * 3. 将 _head 和 _tail 都指向哨兵对象
     *
     * ## 为什么使用 aligned_storage？
     * - T 可能没有默认构造函数
     * - 只需要哨兵对象的链接指针，不需要完整构造 T
     * - 节省构造开销
     */
    MPSCQueueIntrusive() : _dummyPtr(reinterpret_cast<T*>(std::addressof(_dummy))), _head(_dummyPtr), _tail(_dummyPtr)
    {
        // _dummy 从 aligned_storage 构造，故意不初始化（T 可能没有默认构造函数）
        // 只初始化其 IntrusiveLink 成员
        std::atomic<T*>* dummyNext = new (&(_dummyPtr->*IntrusiveLink)) std::atomic<T*>();
        dummyNext->store(nullptr, std::memory_order_relaxed);  // 哨兵对象的链接指针为空
    }

    /**
     * @brief 析构函数，清理队列中所有元素
     *
     * @details 析构时会从队列中取出所有剩余元素并删除。
     * 哨兵对象会随着队列对象自动销毁。
     *
     * @warning 假设消费者线程已经停止，否则存在竞态条件
     */
    ~MPSCQueueIntrusive()
    {
        T* output;
        // 循环取出并删除所有剩余数据
        while (Dequeue(output))
            delete output;
    }

    /**
     * @brief 向队列中添加一个元素（生产者操作）
     *
     * @details 线程安全的入队操作，可以被多个生产者线程并发调用。
     *
     * ## 算法流程
     * 1. 设置输入对象的链接指针为 nullptr（标记为链表尾部）
     * 2. 原子交换 _head，将输入对象插入链表头部
     * 3. 将前一个头对象的链接指针指向输入对象，完成链接
     *
     * ## 与非侵入式的区别
     * - 不需要创建额外的节点对象
     * - 直接操作数据对象的成员指针
     *
     * @param input 要添加的数据对象指针（调用者分配内存）
     *
     * @note 线程安全：支持多线程并发调用
     * @note 内存管理：调用者负责分配数据内存，队列负责释放
     * @note 输入对象的链接指针会被修改
     */
    void Enqueue(T* input)
    {
        // 设置输入对象的链接指针为空（标记为链表末端）
        (input->*IntrusiveLink).store(nullptr, std::memory_order_release);

        // 原子交换：将 input 设置为新的 _head，返回旧的 _head
        T* prevHead = _head.exchange(input, std::memory_order_acq_rel);

        // 将前一个头对象的链接指针指向输入对象，完成链接
        (prevHead->*IntrusiveLink).store(input, std::memory_order_release);
    }

    /**
     * @brief 从队列中取出一个元素（消费者操作）
     *
     * @details 单线程安全的出队操作，只能由一个消费者线程调用。
     *
     * ## 算法流程（相比非侵入式更复杂）
     * 1. 读取 _tail 指向的对象
     * 2. 检查该对象的链接指针
     * 3. 如果 _tail 是哨兵对象：
     *    - 如果链接为空，队列为空
     *    - 否则更新 _tail 到下一个对象
     * 4. 如果下一个对象存在：
     *    - 取出当前对象，更新 _tail
     * 5. 如果下一个对象不存在且 _tail != _head：
     *    - 说明生产者正在链接，稍后重试
     * 6. 如果 _tail == _head，队列为空：
     *    - 插入哨兵对象以检测后续入队
     *
     * ## 为什么需要哨兵对象？
     * - 避免在第一次 Enqueue 时处理特殊情况
     * - 提供一个稳定的起始点
     * - 允许检测队列是否为空
     *
     * @param[out] result 输出参数，存储取出的数据对象指针
     * @return true 成功取出数据
     * @return false 队列为空或生产者正在链接
     *
     * @note 单线程安全：只能由一个消费者线程调用
     * @note 内存管理：调用者负责删除返回的数据对象
     */
    bool Dequeue(T*& result)
    {
        // 读取当前尾对象
        T* tail = _tail.load(std::memory_order_relaxed);

        // 尝试读取尾对象的链接指针（下一个对象）
        T* next = (tail->*IntrusiveLink).load(std::memory_order_acquire);

        // 特殊处理：如果尾对象是哨兵对象
        if (tail == _dummyPtr)
        {
            // 哨兵对象后面没有其他对象，队列为空
            if (!next)
                return false;

            // 更新 _tail 指向下一个对象（跳过哨兵对象）
            _tail.store(next, std::memory_order_release);
            tail = next;  // tail 现在指向真正的数据对象
            next = (next->*IntrusiveLink).load(std::memory_order_acquire);  // 读取下一个对象
        }

        // 如果下一个对象存在，正常出队
        if (next)
        {
            _tail.store(next, std::memory_order_release);  // 更新 _tail
            result = tail;  // 返回当前对象
            return true;
        }

        // 下一个对象不存在，检查是否队列为空
        T* head = _head.load(std::memory_order_acquire);
        if (tail != head)
            return false;  // 生产者正在链接，返回 false

        // 队列为空，插入哨兵对象以便检测后续入队
        Enqueue(_dummyPtr);

        // 再次尝试读取下一个对象
        next = (tail->*IntrusiveLink).load(std::memory_order_acquire);
        if (next)
        {
            _tail.store(next, std::memory_order_release);
            result = tail;
            return true;
        }

        return false;  // 仍然为空，返回 false
    }

private:
    std::aligned_storage_t<sizeof(T), alignof(T)> _dummy;  //!< 哨兵对象的存储空间（未初始化）
    T* _dummyPtr;                                           //!< 指向哨兵对象的指针
    std::atomic<T*> _head;                                  //!< 队列头，生产者在此插入新对象
    std::atomic<T*> _tail;                                  //!< 队列尾，消费者从此取出对象

    // 禁用拷贝和赋值，确保队列的唯一性
    MPSCQueueIntrusive(MPSCQueueIntrusive const&) = delete;
    MPSCQueueIntrusive& operator=(MPSCQueueIntrusive const&) = delete;
};
} // namespace Impl
} // namespace Trinity

/**
 * @brief MPSC 队列的统一接口
 *
 * @details 根据模板参数自动选择侵入式或非侵入式实现。
 *
 * ## 模板参数
 * @tparam T 队列中存储的数据类型
 * @tparam IntrusiveLink 指向 T 中原子链接指针的成员指针，默认为 nullptr
 *
 * ## 使用方式
 *
 * ### 非侵入式用法（默认）
 * @code
 * // 不指定 IntrusiveLink 参数，使用非侵入式实现
 * MPSCQueue<int> queue;
 * queue.Enqueue(new int(42));
 * int* value;
 * if (queue.Dequeue(value))
 *     delete value;
 * @endcode
 *
 * ### 侵入式用法
 * @code
 * // 定义包含链接指针的数据结构
 * struct MyData {
 *     std::atomic<MyData*> Next;
 *     int value;
 * };
 *
 * // 指定 IntrusiveLink 参数，使用侵入式实现
 * MPSCQueue<MyData, &MyData::Next> queue;
 * queue.Enqueue(new MyData{nullptr, 42});
 * MyData* data;
 * if (queue.Dequeue(data))
 *     delete data;
 * @endcode
 *
 * ## 实现选择规则
 * - IntrusiveLink == nullptr：使用 MPSCQueueNonIntrusive（非侵入式）
 * - IntrusiveLink != nullptr：使用 MPSCQueueIntrusive（侵入式）
 *
 * ## 性能对比
 * - 非侵入式：使用简单，但有额外的节点内存分配开销
 * - 侵入式：性能更高，但需要修改数据结构定义
 */
template<typename T, std::atomic<T*> T::* IntrusiveLink = nullptr>
using MPSCQueue = std::conditional_t<IntrusiveLink != nullptr, Trinity::Impl::MPSCQueueIntrusive<T, IntrusiveLink>, Trinity::Impl::MPSCQueueNonIntrusive<T>>;

#endif // MPSCQueue_h__
