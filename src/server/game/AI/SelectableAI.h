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

#ifndef SelectableAI_h__
#define SelectableAI_h__

#include "FactoryHolder.h"

/**
 * @file SelectableAI.h
 * @brief 可选择的 AI 工厂系统
 *
 * 该文件提供了可选择的 AI 工厂基础设施，允许根据对象类型动态创建和管理 AI 实例。
 * 系统结合了工厂模式、权限检查机制和数据库许可验证，为 AI 选择提供了灵活的框架。
 *
 * 主要组件：
 * - DBPermit: 数据库许可接口，用于控制脚本名称是否允许在数据库配置中使用
 * - SelectableAI: 可选择的 AI 工厂模板类，整合工厂创建、权限检查和数据库许可功能
 *
 * 使用场景：
 * - 为生物(Creature)创建不同类型的 AI 实例
 * - 根据数据库配置动态选择 AI 脚本
 * - 控制 AI 脚本的使用权限范围
 */

/**
 * @class DBPermit
 * @brief 数据库许可接口
 *
 * 抽象基类，定义了检查脚本名称是否允许在数据库中使用的接口。
 * 该接口用于控制哪些 AI 脚本可以通过数据库配置进行分配。
 *
 * 设计目的：
 * - 防止某些内部或系统级 AI 脚本被错误地分配给数据库中的生物
 * - 提供权限控制机制，确保只有合适的脚本可以在数据库中配置
 * - 允许系统区分内部使用的 AI 和可在数据库中配置的 AI
 *
 * 使用方式：
 * - 派生类需要实现 IsScriptNameAllowedInDB() 方法
 * - 返回 true 表示该脚本可以在数据库中配置使用
 * - 返回 false 表示该脚本仅供内部使用，不应出现在数据库配置中
 */
class DBPermit
{
    public:
        /**
         * @brief 虚析构函数
         *
         * 确保派生类对象可以正确销毁，避免内存泄漏。
         */
        virtual ~DBPermit() { }

        /**
         * @brief 检查脚本名称是否允许在数据库中使用
         *
         * 纯虚函数，由派生类实现具体的权限检查逻辑。
         *
         * @return bool 如果脚本名称允许在数据库配置中使用则返回 true，
         *              否则返回 false（仅供内部使用）
         *
         * @note 该方法是权限系统的核心，用于在加载生物数据时验证
         *       数据库中配置的 AI 脚本名称是否合法
         *
         * @example
         * // 在数据库加载时的验证示例：
         * if (aiFactory->IsScriptNameAllowedInDB())
         * {
         *     // 允许在数据库配置中使用此 AI 脚本
         * }
         * else
         * {
         *     // 拒绝使用，记录警告或错误
         * }
         */
        virtual bool IsScriptNameAllowedInDB() const = 0;
};

/**
 * @struct SelectableAI
 * @brief 可选择的 AI 工厂模板结构
 *
 * 该模板结构整合了三个重要功能：
 * 1. 工厂模式（继承自 FactoryHolder）：提供 AI 实例的创建能力
 * 2. 权限检查（继承自 Permissible）：提供基于对象类型的权限验证
 * 3. 数据库许可（继承自 DBPermit）：控制脚本是否可在数据库中配置
 *
 * 该结构是 AI 系统的核心组件，允许根据不同的对象类型和配置动态创建和管理 AI 实例。
 * 通过模板参数，可以灵活地指定对象类型、AI 类型和数据库许可状态。
 *
 * @tparam O 对象类型，通常是 Creature 或其派生类型
 * @tparam AI AI 类型，具体的 AI 实现类，如 CreatureAI
 * @tparam is_db_allowed 布尔模板参数，控制该 AI 是否允许在数据库中配置
 *                        默认值为 true，表示允许在数据库中使用
 *                        设置为 false 可限制该 AI 仅用于内部系统
 *
 * @note 该结构使用 CRTP（Curiously Recurring Template Pattern）模式，
 *       通过多继承组合多个功能接口
 *
 * @example 示例：创建一个可在数据库中配置的 AI 工厂
 * @code
 * // 定义一个生物 AI 工厂
 * struct MyCreatureAIFactory : public SelectableAI<Creature, CreatureAI, true>
 * {
 *     MyCreatureAIFactory() : SelectableAI("my_creature_ai") { }
 *
 *     CreatureAI* Create(Creature* creature) override
 *     {
 *         return new MyCreatureAI(creature);
 *     }
 * };
 *
 * // 定义一个仅供内部使用的 AI 工厂
 * struct InternalAIFactory : public SelectableAI<Creature, CreatureAI, false>
 * {
 *     InternalAIFactory() : SelectableAI("internal_ai") { }
 *
 *     CreatureAI* Create(Creature* creature) override
 *     {
 *         return new InternalAI(creature);
 *     }
 * };
 * @endcode
 *
 * @see FactoryHolder
 * @see Permissible
 * @see DBPermit
 */
template <class O, class AI, bool is_db_allowed = true>
struct SelectableAI : public FactoryHolder<AI, O>, public Permissible<O>, public DBPermit
{
    /**
     * @brief 构造函数
     *
     * 初始化工厂持有者、权限接口和数据库许可接口。
     * 通过初始化列表调用各个基类的构造函数，确保对象完整初始化。
     *
     * @param name AI 工厂的名称标识，用于在工厂注册表中唯一标识该 AI 类型。
     *              该名称通常与脚本名称对应，用于从数据库或配置中查找对应的 AI。
     *
     * @note 该名称会被传递给 FactoryHolder 基类，用于工厂注册和查找。
     *       名称应该是唯一的，避免与其他 AI 工厂冲突。
     *
     * @example
     * // 创建一个名为 "smart_ai" 的 AI 工厂
     * SelectableAI("smart_ai");
     */
    SelectableAI(std::string const& name) : FactoryHolder<AI, O>(name), Permissible<O>(), DBPermit() { }

    /**
     * @brief 检查脚本名称是否允许在数据库中使用
     *
     * 重写 DBPermit 接口的纯虚函数，返回模板参数 is_db_allowed 的值。
     * 该方法在编译时确定，因此不需要运行时计算，效率极高。
     *
     * @return bool 返回模板参数 is_db_allowed 的值：
     *              - true: 该 AI 脚本可以在数据库配置中使用
     *              - false: 该 AI 脚本仅供内部系统使用，不应出现在数据库配置中
     *
     * @note 使用 final 关键字确保该方法不会被进一步重写，保证权限行为的一致性。
     *       该方法的实现利用了模板参数的编译时常量特性，实现了零运行时开销。
     *
     * @example 使用场景
     * @code
     * // 在加载生物模板时验证 AI 脚本
     * std::string scriptName = creatureTemplate->ScriptName;
     * auto aiFactory = sScriptMgr->GetCreatureAIFactory(scriptName);
     *
     * if (aiFactory && !aiFactory->IsScriptNameAllowedInDB())
     * {
     *     LOG_ERROR("sql.sql", "AI script '{}' is not allowed in database", scriptName);
     *     return nullptr;
     * }
     * @endcode
     */
    bool IsScriptNameAllowedInDB() const final override { return is_db_allowed; }
};

#endif // SelectableAI_h__
