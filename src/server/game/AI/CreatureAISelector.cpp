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
 * @file CreatureAISelector.cpp
 * @brief AI选择器实现文件
 *
 * 本文件实现了AI和移动生成器的选择逻辑。选择器根据多种策略（配置、脚本、权限值）
 * 为生物和游戏对象选择最合适的AI实例。选择过程采用优先级机制，确保正确的AI被分配。
 */

#include "AIException.h"
#include "Creature.h"
#include "CreatureAISelector.h"
#include "CreatureAIFactory.h"

#include "MovementGenerator.h"

#include "GameObject.h"
#include "GameObjectAIFactory.h"

#include "Log.h"
#include "ScriptMgr.h"

namespace FactorySelector
{
    /**
     * @brief 获取指定对象对某个工厂的权限值
     *
     * 辅助模板函数，从工厂持有者中提取权限对象，并计算对象对该AI的适用性分数。
     * 权限值越高表示该AI越适合该对象。
     *
     * @tparam T 对象类型（Creature或GameObject）
     * @tparam Value 注册表值类型（包含工厂智能指针的键值对）
     * @param obj 需要评估的对象实例
     * @param value 注册表中的键值对，包含AI工厂
     * @return 返回权限值，正数表示适用，负数表示不适用，数值越大适用性越高
     *
     * @note 使用ASSERT_NOTNULL确保动态转换成功
     */
    template <class T, class Value>
    inline int32 GetPermitFor(T const* obj, Value const& value)
    {
        // 从工厂持有者中提取权限对象（Permissible）
        Permissible<T> const* const p = ASSERT_NOTNULL(dynamic_cast<Permissible<T> const*>(value.second.get()));
        // 计算并返回权限值
        return p->Permit(obj);
    }

    /**
     * @struct PermissibleOrderPred
     * @brief 权限值排序谓词
     *
     * 用于在注册表中查找具有最高权限值的AI工厂。作为std::max_element的比较函数。
     *
     * @tparam T 对象类型（Creature或GameObject）
     */
    template <class T>
    struct PermissibleOrderPred
    {
        public:
            /**
             * @brief 构造函数
             * @param obj 需要选择AI的对象实例
             */
            PermissibleOrderPred(T const* obj) : _obj(obj) { }

            /**
             * @brief 比较运算符
             *
             * 比较两个AI工厂的权限值，用于确定哪个AI更适合当前对象。
             *
             * @tparam Value 注册表值类型
             * @param left 左侧工厂项
             * @param right 右侧工厂项
             * @return 如果left的权限值小于right，返回true，否则返回false
             */
            template <class Value>
            bool operator()(Value const& left, Value const& right) const
            {
                return GetPermitFor(_obj, left) < GetPermitFor(_obj, right);
            }

        private:
            T const* const _obj;  ///< 需要选择AI的对象实例
    };

    /**
     * @brief 选择AI工厂的通用实现
     *
     * 根据AI名称配置或权限值选择最合适的AI工厂。
     * 这是一个模板函数，同时支持生物和游戏对象的AI选择。
     *
     * 选择策略：
     * 1. 如果对象配置了AI名称，直接查找对应的AI工厂
     * 2. 否则遍历所有已注册的AI，计算权限值，选择权限值最高且非负的AI
     * 3. 如果找不到合适的AI，系统将终止（理论上不应该发生，NullAI总是可用）
     *
     * @tparam AI AI类型（CreatureAI或GameObjectAI）
     * @tparam T 对象类型（Creature或GameObject）
     * @param obj 需要AI的对象实例
     * @return 返回选中的AI工厂指针，从不返回nullptr
     *
     * @note 使用static_assert确保模板参数的合法性
     * @warning 如果没有任何AI可用，将调用ABORT()终止程序
     */
    template <class AI, class T>
    inline FactoryHolder<AI, T> const* SelectFactory(T* obj)
    {
        // 编译时检查：确保AI和对象类型匹配
        static_assert(std::is_same<AI, CreatureAI>::value || std::is_same<AI, GameObjectAI>::value, "Invalid template parameter");
        static_assert(std::is_same<AI, CreatureAI>::value == std::is_same<T, Creature>::value, "Incompatible AI for type");
        static_assert(std::is_same<AI, GameObjectAI>::value == std::is_same<T, GameObject>::value, "Incompatible AI for type");

        // 获取对应AI类型的注册表
        using AIRegistry = typename FactoryHolder<AI, T>::FactoryHolderRegistry;

        // 策略1：优先使用数据库配置的AIName
        std::string const& aiName = obj->GetAIName();
        if (!aiName.empty())
            return AIRegistry::instance()->GetRegistryItem(aiName);

        // 策略2：根据权限值选择最合适的AI
        typename AIRegistry::RegistryMapType const& items = AIRegistry::instance()->GetRegisteredItems();
        // 查找权限值最大的AI工厂
        auto itr = std::max_element(items.begin(), items.end(), PermissibleOrderPred<T>(obj));
        if (itr != items.end() && GetPermitFor(obj, *itr) >= 0)
            return itr->second.get();

        // 永远不应该到达这里：Null AI定义为PERMIT_BASE_IDLE，必定会被找到
        ABORT();
        return nullptr;
    }

    /**
     * @brief 为生物选择AI实例
     *
     * 这是生物AI选择的入口函数，实现了多级优先级的AI选择策略。
     *
     * 选择优先级：
     * 1. 宠物优先：如果生物是宠物，强制使用PetAI
     * 2. 脚本AI：如果有脚本名称，尝试从脚本系统获取AI
     * 3. 配置AI：根据AI名称或权限值选择AI（通过SelectFactory）
     *
     * @param creature 需要AI的生物实例
     * @return 返回选中的AI实例指针，由生物对象管理内存
     *
     * @调用时机 生物创建时调用（Creature::CreateFromDB或相关函数）
     * @性能注意事项 包含脚本查询和字符串查找，但调用频率适中
     * @note 宠物AI强制选择确保宠物行为正确，避免被其他AI覆盖
     */
    CreatureAI* SelectAI(Creature* creature)
    {
        // 特殊情况：宠物强制使用PetAI
        // 即使数据库配置了其他AIName（如SmartAI），也需要覆盖为PetAI
        // 这确保了宠物的特殊行为（跟随、听从命令等）正常工作
        if (creature->IsPet())
            return ASSERT_NOTNULL(sCreatureAIRegistry->GetRegistryItem("PetAI"))->Create(creature);

        // 尝试从脚本系统获取AI
        // 脚本名称存储在数据库的ScriptName字段中
        try
        {
            if (CreatureAI* scriptedAI = sScriptMgr->GetCreatureAI(creature))
                return scriptedAI;
        }
        catch (InvalidAIException const& e)
        {
            // 脚本AI创建失败，记录错误并回退到默认AI
            TC_LOG_ERROR("entities.unit", "Exception trying to assign script '{}' to Creature (Entry: {}), this Creature will have a default AI. Exception message: {}",
                creature->GetScriptName(), creature->GetEntry(), e.what());
        }

        // 使用通用工厂选择逻辑（AI名称配置或权限值）
        return SelectFactory<CreatureAI>(creature)->Create(creature);
    }

    /**
     * @brief 为单位选择移动生成器实例
     *
     * 根据单位的默认移动类型从注册表中选择对应的移动生成器。
     * 对于生物，会检查是否被玩家控制，以确定正确的移动类型。
     *
     * @param unit 需要移动生成器的单位实例
     * @return 返回选中的移动生成器实例指针，由单位对象管理内存
     *
     * @调用时机 单位初始化或重置移动系统时调用
     * @性能注意事项 简单的类型查找，性能开销极小
     * @note 玩家控制的生物使用不同的移动类型
     */
    MovementGenerator* SelectMovementGenerator(Unit* unit)
    {
        // 获取默认移动类型
        MovementGeneratorType type = unit->GetDefaultMovementType();

        // 对于生物，检查是否有控制者
        if (Creature* creature = unit->ToCreature())
            if (!creature->GetCharmerOrSelfPlayer())  // 如果没有被玩家控制
                type = creature->GetDefaultMovementType();

        // 从注册表中查找对应类型的移动生成器工厂
        MovementGeneratorCreator const* mv_factory = sMovementGeneratorRegistry->GetRegistryItem(type);
        // 创建并返回移动生成器实例
        return ASSERT_NOTNULL(mv_factory)->Create(unit);
    }

    /**
     * @brief 为游戏对象选择AI实例
     *
     * 为游戏对象选择AI，优先考虑脚本AI，其次使用配置或权限值选择。
     *
     * 选择优先级：
     * 1. 脚本AI：如果有脚本名称，优先从脚本系统获取AI
     * 2. 配置AI：根据AI名称或权限值选择AI（通过SelectFactory）
     *
     * @param go 需要AI的游戏对象实例
     * @return 返回选中的游戏对象AI实例指针，由游戏对象管理内存
     *
     * @调用时机 游戏对象创建或初始化时调用
     * @性能注意事项 包含脚本查询和字符串查找，但调用频率低
     * @note 游戏对象AI相对简单，主要用于交互和脚本触发
     */
    GameObjectAI* SelectGameObjectAI(GameObject* go)
    {
        // 优先从脚本系统获取AI
        if (GameObjectAI* scriptedAI = sScriptMgr->GetGameObjectAI(go))
            return scriptedAI;

        // 使用通用工厂选择逻辑（AI名称配置或权限值）
        return SelectFactory<GameObjectAI>(go)->Create(go);
    }
}
