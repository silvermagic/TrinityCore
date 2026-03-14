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

// ============================================================================
// Containers.h - 容器工具库
// ============================================================================
// 模块职责：
//   提供一系列容器操作的辅助工具函数，包括：
//   - 随机元素选择
//   - 容器随机重排
//   - 容器大小调整
//   - 集合交集检测
//   - 条件删除
//
// 使用场景：
//   - AI 目标选择（随机选择敌人）
//   - 战利品随机分配
//   - 技能目标筛选
//   - 数据容器清理
//
// 性能考虑：
//   - RandomResize: O(n) 时间复杂度
//   - SelectRandomContainerElement: O(n) 用于advance
//   - EraseIf: 根据元素是否可移动赋值优化性能
// ============================================================================

#ifndef TRINITY_CONTAINERS_H
#define TRINITY_CONTAINERS_H

#include "Define.h"
#include "MapUtils.h"
#include "Random.h"
#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace Trinity
{
    // ============================================================================
    // CheckedBufferOutputIterator - 带边界检查的缓冲区输出迭代器
    // ============================================================================
    // 类职责：
    //   提供一个安全的输出迭代器，用于向固定大小的缓冲区写入数据。
    //   当写入超出缓冲区范围时，会抛出 std::out_of_range 异常。
    //
    // 使用场景：
    //   - 网络数据包序列化
    //   - 固定大小缓冲区的数据写入
    //   - 需要边界检查的安全数据拷贝
    //
    // 性能注意事项：
    //   - 每次解引用和自增操作都会进行边界检查
    //   - 相比裸指针迭代器，有少量性能开销
    // ============================================================================
    template <class T>
    class CheckedBufferOutputIterator
    {
        public:
            // STL 迭代器所需的类型定义
            using iterator_category = std::output_iterator_tag;  // 迭代器类型：输出迭代器
            using value_type = void;                             // 值类型：输出迭代器不提供值访问
            using pointer = T*;                                  // 指针类型
            using reference = T&;                                // 引用类型
            using difference_type = std::ptrdiff_t;              // 差值类型

            /**
             * @brief 构造函数
             * @param buf 缓冲区起始指针
             * @param n 缓冲区元素数量
             *
             * 初始化迭代器，设置缓冲区范围 [buf, buf+n)
             */
            CheckedBufferOutputIterator(T* buf, size_t n) : _buf(buf), _end(buf+n) {}

            /**
             * @brief 解引用操作符
             * @return 当前位置的元素引用
             *
             * 返回当前位置的元素引用，用于写入数据。
             * 如果越界则抛出 std::out_of_range 异常。
             */
            T& operator*() const { check(); return *_buf; }

            /**
             * @brief 前置自增操作符
             * @return 自增后的迭代器引用
             *
             * 将迭代器移动到下一个位置。
             * 如果越界则抛出 std::out_of_range 异常。
             */
            CheckedBufferOutputIterator& operator++() { check(); ++_buf; return *this; }

            /**
             * @brief 后置自增操作符
             * @return 自增前的迭代器副本
             *
             * 将迭代器移动到下一个位置，返回移动前的副本。
             * 如果越界则抛出 std::out_of_range 异常。
             */
            CheckedBufferOutputIterator operator++(int) { CheckedBufferOutputIterator v = *this; operator++(); return v; }

            /**
             * @brief 获取剩余可用空间
             * @return 剩余元素数量
             *
             * 返回从当前位置到缓冲区末尾的元素数量。
             */
            size_t remaining() const { return (_end - _buf); }

        private:
            T* _buf;  // 当前位置指针，指向下一个要写入的位置
            T* _end;  // 缓冲区结束指针，指向缓冲区末尾的下一个位置

            /**
             * @brief 边界检查
             *
             * 检查当前位置是否在有效范围内。
             * 如果越界则抛出 std::out_of_range 异常。
             */
            void check() const
            {
                if (!(_buf < _end))
                    throw std::out_of_range("index");
            }
    };

    // ============================================================================
    // Containers 命名空间 - 容器工具函数集合
    // ============================================================================
    // 提供各种容器操作的辅助函数，包括随机选择、调整大小、删除等。
    // ============================================================================
    namespace Containers
    {
        /**
         * @brief 随机调整容器大小
         *
         * @brief 将容器调整为最多包含 requestedSize 个元素
         * @param container 要调整的容器
         * @param requestedSize 请求的目标大小
         *
         * 如果容器元素数量超过 requestedSize，则随机选择要保留的元素。
         * 使用蓄水池采样算法（Reservoir Sampling）确保每个元素被保留的概率相等。
         *
         * @tparam C 容器类型，必须支持前向迭代器
         *
         * @param container 要调整大小的容器
         * @param requestedSize 目标大小
         *
         * 调用时机：
         *   - AI 目标列表筛选
         *   - 随机选择一定数量的战利品
         *
         * 性能注意事项：
         *   - 时间复杂度：O(n)，其中 n 是容器大小
         *   - 空间复杂度：O(1)，原地操作
         *   - 使用 move 语义优化元素移动
         */
        template<class C>
        void RandomResize(C& container, std::size_t requestedSize)
        {
            // 编译时检查：容器必须支持前向迭代器
            static_assert(std::is_base_of<std::forward_iterator_tag, typename std::iterator_traits<typename C::iterator>::iterator_category>::value, "Invalid container passed to Trinity::Containers::RandomResize");

            // 如果容器已经小于等于目标大小，直接返回
            if (std::size(container) <= requestedSize)
                return;

            auto keepIt = std::begin(container), curIt = std::begin(container);
            uint32 elementsToKeep = requestedSize, elementsToProcess = std::size(container);

            // 蓄水池采样算法：遍历所有元素，以概率决定是否保留
            while (elementsToProcess)
            {
                // 当前元素被保留的概率 = elementsToKeep / elementsToProcess
                if (urand(1, elementsToProcess) <= elementsToKeep)
                {
                    // 如果需要保留且位置不同，则移动元素
                    if (keepIt != curIt)
                        *keepIt = std::move(*curIt);
                    ++keepIt;
                    --elementsToKeep;  // 减少需要保留的元素数量
                }
                ++curIt;
                --elementsToProcess;  // 减少待处理的元素数量
            }

            // 删除末尾不需要的元素
            container.erase(keepIt, std::end(container));
        }

        /**
         * @brief 随机调整容器大小（带谓词过滤）
         *
         * @tparam C 容器类型
         * @tparam Predicate 谓词类型，用于筛选元素
         *
         * @param container 要调整大小的容器
         * @param predicate 筛选谓词，返回 true 的元素才会被考虑
         * @param requestedSize 目标大小
         *
         * 首先使用谓词筛选容器中的元素，然后随机调整到指定大小。
         *
         * 调用时机：
         *   - 需要先筛选再随机选择的场景
         *   - 例如：从可见的敌人中随机选择 N 个
         *
         * 性能注意事项：
         *   - 会创建临时容器，内存开销较大
         *   - 时间复杂度：O(n)
         */
        template<class C, class Predicate>
        void RandomResize(C& container, Predicate&& predicate, std::size_t requestedSize)
        {
            //! 第一步：使用谓词筛选元素到临时容器
            C containerCopy;
            std::copy_if(std::begin(container), std::end(container), std::inserter(containerCopy, std::end(containerCopy)), predicate);

            // 第二步：如果需要随机调整大小
            if (requestedSize)
                RandomResize(containerCopy, requestedSize);

            // 第三步：将结果移回原容器
            container = std::move(containerCopy);
        }

        /**
         * @brief 从容器中随机选择一个元素
         *
         * @tparam C 容器类型
         *
         * @param container 要选择的容器
         * @return 随机选择的元素的常量引用
         *
         * 从容器中随机选择一个元素并返回其引用。
         * 每个元素被选中的概率相等。
         *
         * 调用时机：
         *   - 随机选择目标
         *   - 随机选择对话
         *   - 随机选择技能
         *
         * 注意：
         *   - 容器不能为空，否则行为未定义
         *   - 返回的是常量引用，不能修改元素
         *
         * 性能注意事项：
         *   - 时间复杂度：O(n)，需要 advance 到随机位置
         */
        template<class C>
        inline auto SelectRandomContainerElement(C const& container) -> typename std::add_const<decltype(*std::begin(container))>::type&
        {
            auto it = std::begin(container);
            // 随机前进到某个位置
            std::advance(it, urand(0, uint32(std::size(container)) - 1));
            return *it;
        }

        /**
         * @brief 从容器中加权随机选择一个元素
         *
         * @tparam C 容器类型
         *
         * @param container 要选择的容器
         * @param weights 每个元素的权重数组，顺序必须与容器中的元素顺序一致
         * @return 随机选择的元素的迭代器
         *
         * 根据权重随机选择一个元素，权重越大的元素被选中的概率越高。
         * 调用者需要确保权重总和大于 0。
         *
         * 调用时机：
         *   - 战利品随机掉落（稀有物品权重低）
         *   - 随机事件触发（不同事件有不同概率）
         *
         * 注意：
         *   - 容器不能为空
         *   - 权重数组大小必须与容器大小一致
         *   - 权重总和必须大于 0
         *
         * 性能注意事项：
         *   - 时间复杂度：O(n) 用于 urandweighted
         */
        template<class C>
        inline auto SelectRandomWeightedContainerElement(C const& container, std::vector<double> weights) -> decltype(std::begin(container))
        {
            auto it = std::begin(container);
            // 使用加权随机算法选择位置
            std::advance(it, urandweighted(weights.size(), weights.data()));
            return it;
        }

        /**
         * @brief 从容器中加权随机选择一个元素（使用权重提取器）
         *
         * @tparam C 容器类型
         * @tparam Fn 权重提取函数类型
         *
         * @param container 要选择的容器
         * @param weightExtractor 权重提取函数，接受容器元素并返回其权重（double）
         * @return 随机选择的元素的迭代器
         *
         * 使用提供的函数动态计算每个元素的权重，然后进行加权随机选择。
         * 如果所有权重总和 <= 0，则将所有元素的权重设为 1.0（等概率选择）。
         *
         * 调用时机：
         *   - 权重需要动态计算的场景
         *   - 例如：根据距离、等级等因素计算权重
         *
         * 注意：
         *   - 容器不能为空
         *   - 如果权重总和 <= 0，则退化为等概率选择
         *
         * 性能注意事项：
         *   - 时间复杂度：O(n) 用于计算权重和选择
         *   - 需要额外的 vector 存储权重
         */
        template<class C, class Fn>
        auto SelectRandomWeightedContainerElement(C const& container, Fn weightExtractor) -> decltype(std::begin(container))
        {
            // 收集所有元素的权重
            std::vector<double> weights;
            weights.reserve(std::size(container));
            double weightSum = 0.0;

            for (auto& val : container)
            {
                double weight = weightExtractor(val);
                weights.push_back(weight);
                weightSum += weight;
            }

            // 如果权重总和 <= 0，则所有元素设为等权重
            if (weightSum <= 0.0)
                weights.assign(std::size(container), 1.0);

            return SelectRandomWeightedContainerElement(container, weights);
        }

        /**
         * @brief 随机打乱容器中元素的顺序
         *
         * @tparam C 容器类型
         *
         * @param container 要打乱的容器
         *
         * 使用随机数生成器重新排列容器中的元素，使每个排列出现的概率相等。
         *
         * 调用时机：
         *   - 洗牌算法
         *   - 随机排列战斗顺序
         *   - 随机排列问题选项
         *
         * 性能注意事项：
         *   - 时间复杂度：O(n)
         *   - 使用 Fisher-Yates 洗牌算法
         */
        template<class C>
        inline void RandomShuffle(C& container)
        {
            // 使用 std::shuffle 和随机引擎打乱容器
            std::shuffle(std::begin(container), std::end(container), RandomEngine::Instance());
        }

        /**
         * @brief 检查两个已排序容器是否有交集
         *
         * @tparam Iterator1 第一个容器的迭代器类型
         * @tparam Iterator2 第二个容器的迭代器类型
         *
         * @param first1 第一个容器的起始迭代器
         * @param last1 第一个容器的结束迭代器
         * @param first2 第二个容器的起始迭代器
         * @param last2 第二个容器的结束迭代器
         * @return 如果两个容器有共同元素返回 true，否则返回 false
         *
         * 检查两个已排序的容器是否包含相同的元素。
         * 要求两个容器都已经排序（升序）。
         *
         * 调用时机：
         *   - 检查两个技能列表是否有相同技能
         *   - 检查两个任务列表是否有相同任务
         *
         * 性能注意事项：
         *   - 时间复杂度：O(n + m)，其中 n 和 m 是两个容器的大小
         *   - 空间复杂度：O(1)
         *   - 比暴力搜索更高效，但要求容器已排序
         */
        template<class Iterator1, class Iterator2>
        bool Intersects(Iterator1 first1, Iterator1 last1, Iterator2 first2, Iterator2 last2)
        {
            // 使用双指针法检查交集
            while (first1 != last1 && first2 != last2)
            {
                if (*first1 < *first2)
                    ++first1;  // 第一个元素更小，前进第一个迭代器
                else if (*first2 < *first1)
                    ++first2;  // 第二个元素更小，前进第二个迭代器
                else
                    return true;  // 找到相同元素，返回 true
            }

            return false;  // 没有找到相同元素
        }

        // ============================================================================
        // Impl 命名空间 - 内部实现细节
        // ============================================================================
        // 包含 EraseIf 的具体实现，根据元素类型选择不同的策略。
        // ============================================================================
        namespace Impl
        {
            /**
             * @brief 删除满足条件的元素（元素可移动赋值）
             *
             * @tparam Container 容器类型
             * @tparam Predicate 谓词类型
             *
             * @param c 容器
             * @param p 判断条件，返回 true 的元素会被删除
             *
             * 使用交换-删除技巧，适用于可移动赋值的元素类型。
             * 通过将不需要删除的元素交换到前面，然后一次性删除末尾元素。
             *
             * 性能注意事项：
             *   - 时间复杂度：O(n)
             *   - 每个元素最多移动一次
             *   - 元素的析构函数调用次数等于被删除元素数量
             */
            template <typename Container, typename Predicate>
            void EraseIfMoveAssignable(Container& c, Predicate p)
            {
                auto wpos = c.begin();  // 写位置指针，指向下一个要保留的元素位置

                for (auto rpos = c.begin(), end = c.end(); rpos != end; ++rpos)
                {
                    if (!p(*rpos))  // 如果元素不应该被删除
                    {
                        if (rpos != wpos)
                            std::swap(*rpos, *wpos);  // 将保留的元素交换到前面
                        ++wpos;
                    }
                }
                // 删除末尾所有要删除的元素
                c.erase(wpos, c.end());
            }

            /**
             * @brief 删除满足条件的元素（元素不可移动赋值）
             *
             * @tparam Container 容器类型
             * @tparam Predicate 谓词类型
             *
             * @param c 容器
             * @param p 判断条件，返回 true 的元素会被删除
             *
             * 使用逐个删除的方法，适用于不可移动赋值的元素类型。
             * 这种方法会导致更多的元素移动和析构函数调用。
             *
             * 性能注意事项：
             *   - 时间复杂度：O(n^2) 对于 vector，O(n) 对于 list
             *   - 每次删除都会导致后续元素移动
             *   - 适用于链表或不可移动赋值的元素
             */
            template <typename Container, typename Predicate>
            void EraseIfNotMoveAssignable(Container& c, Predicate p)
            {
                for (auto it = c.begin(); it != c.end();)
                {
                    if (p(*it))
                        it = c.erase(it);  // 删除元素，返回下一个迭代器
                    else
                        ++it;  // 不删除，前进到下一个元素
                }
            }
        }

        /**
         * @brief 删除容器中满足条件的元素
         *
         * @tparam Container 容器类型
         * @tparam Predicate 谓词类型
         *
         * @param c 容器
         * @param p 判断条件，返回 true 的元素会被删除
         *
         * 根据元素类型自动选择最优的删除策略：
         * - 如果元素可移动赋值：使用交换-删除技巧（高效）
         * - 如果元素不可移动赋值：使用逐个删除方法
         *
         * 调用时机：
         *   - 清理无效的游戏对象
         *   - 删除已完成的任务
         *   - 移除无效的玩家引用
         *
         * 性能注意事项：
         *   - 对于可移动赋值元素：O(n)
         *   - 对于不可移动赋值元素：O(n^2) 对于 vector，O(n) 对于 list
         *   - 编译时自动选择最优实现
         */
        template <typename Container, typename Predicate>
        void EraseIf(Container& c, Predicate p)
        {
            // 编译时根据元素类型选择最优实现
            if constexpr (std::is_move_assignable_v<decltype(*c.begin())>)
                Impl::EraseIfMoveAssignable(c, std::ref(p));  // 高效版本
            else
                Impl::EraseIfNotMoveAssignable(c, std::ref(p));  // 兼容版本
        }
    }
    //! namespace Containers
}
//! namespace Trinity

#endif //! #ifdef TRINITY_CONTAINERS_H
