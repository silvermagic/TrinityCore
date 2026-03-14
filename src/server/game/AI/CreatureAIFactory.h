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

#ifndef TRINITY_CREATUREAIFACTORY_H
#define TRINITY_CREATUREAIFACTORY_H

#include "ObjectRegistry.h"
#include "SelectableAI.h"

class Creature;
class CreatureAI;

/**
 * @file CreatureAIFactory.h
 * @brief 生物 AI 工厂类定义文件
 *
 * 本文件提供了用于创建和管理生物 AI 实例的工厂模板类。
 * 采用工厂模式，允许动态注册和创建不同类型的 AI 实例。
 */

/**
 * @brief 生物 AI 工厂模板类
 *
 * 该模板类用于创建特定类型的 CreatureAI 实例。它继承自 SelectableAI 基类，
 * 提供了创建 AI 实例和检查 AI 适用性的功能。
 *
 * 工厂类通过模板参数 REAL_AI 指定具体要创建的 AI 类型，
 * 并通过统一的接口 Create() 和 Permit() 来管理 AI 的创建和选择。
 *
 * @tparam REAL_AI 实际的 AI 类型，必须继承自 CreatureAI
 * @tparam is_db_allowed 是否允许从数据库配置中使用此 AI，默认为 true
 *                       设置为 false 可以防止某些特殊 AI 被数据库配置误用
 *
 * @note 使用示例：
 * @code
 * // 注册一个自定义 AI 工厂
 * new CreatureAIFactory<MyCustomAI>("MyCustomAI");
 *
 * // 注册一个不允许从数据库配置的 AI（如核心内部使用的 AI）
 * new CreatureAIFactory<InternalAI, false>("InternalAI");
 * @endcode
 *
 * @see SelectableAI 基类，提供 AI 选择机制的接口
 * @see CreatureAI 生物 AI 基类
 * @see Creature 生物实体类
 */
template <class REAL_AI, bool is_db_allowed = true>
struct CreatureAIFactory : public SelectableAI<Creature, CreatureAI, is_db_allowed>
{
    /**
     * @brief 构造函数
     *
     * 创建一个 AI 工厂实例，并将其注册到全局 AI 注册表中。
     *
     * @param name AI 的名称标识符，用于在脚本和数据库中引用此 AI 类型
     *              名称应该是唯一的，通常与 AI 类名相同
     *
     * @note 工厂通常在脚本加载时创建，并通过全局注册表自动注册
     */
    CreatureAIFactory(std::string const& name) : SelectableAI<Creature, CreatureAI, is_db_allowed>(name) { }

    /**
     * @brief 创建 AI 实例
     *
     * 根据模板参数 REAL_AI 创建具体的 AI 实例。
     * 这是工厂模式的核心方法，通过基类接口调用以创建派生类实例。
     *
     * @param c 指向需要绑定 AI 的生物对象的指针
     * @return CreatureAI* 新创建的 AI 实例指针，调用者负责管理其生命周期
     *
     * @note 返回的 AI 实例会被生物对象管理，当生物销毁时自动删除
     * @note 该方法是内联函数以提高性能，因为会在每个生物创建时调用
     */
    inline CreatureAI* Create(Creature* c) const override
    {
        return new REAL_AI(c);
    }

    /**
     * @brief 检查 AI 适用性
     *
     * 判断此 AI 类型是否适用于指定的生物。AI 选择系统会调用此方法
     * 来确定最适合某个生物的 AI 类型。
     *
     * 返回值表示适用程度：
     * - 正值：表示适用，数值越大优先级越高
     * - 零：表示不适用，不应使用此 AI
     * - 负值：表示禁止使用
     *
     * @param c 指向要检查的生物对象的常量指针
     * @return int32 适用性分数，数值越大表示越适合
     *
     * @note 该方法通过调用 REAL_AI::Permissible(c) 静态方法实现，
     *       因此每个 AI 类必须实现 Permissible 静态方法
     *
     * @see CreatureAI::Permissible AI 适用性检查的标准实现
     */
    int32 Permit(Creature const* c) const override
    {
        return REAL_AI::Permissible(c);
    }
};

/**
 * @brief 生物 AI 注册表类型定义
 *
 * 定义了生物 AI 的全局注册表类型。该注册表管理所有已注册的 AI 工厂，
 * 并提供根据名称或优先级查找和创建 AI 的功能。
 *
 * 注册表是一个单例对象，在服务器启动时通过脚本系统填充所有可用的 AI 类型。
 *
 * @see ObjectRegistry 对象注册表基础实现
 * @see SelectableAI::FactoryHolderRegistry 工厂持有者注册表基类
 */
typedef SelectableAI<Creature, CreatureAI>::FactoryHolderRegistry CreatureAIRegistry;

/**
 * @brief 生物 AI 注册表全局访问宏
 *
 * 提供对生物 AI 注册表单例实例的全局访问点。
 * 使用此宏可以方便地访问注册表以查询、创建或管理 AI 工厂。
 *
 * @note 使用示例：
 * @code
 * // 查找特定名称的 AI 工厂
 * auto factory = sCreatureAIRegistry->GetRegistryItem("AggressorAI");
 *
 * // 为生物选择最合适的 AI
 * CreatureAI* ai = sCreatureAIRegistry->GetInterface(creature);
 * @endcode
 */
#define sCreatureAIRegistry CreatureAIRegistry::instance()

#endif
