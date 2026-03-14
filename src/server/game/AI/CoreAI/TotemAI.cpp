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
 * @file TotemAI.cpp
 * @brief 图腾AI模块实现文件
 *
 * 本文件实现了图腾AI类，用于控制萨满祭司图腾的行为。
 * 图腾是特殊的召唤单位，具有自动攻击或施法功能。
 */

#include "TotemAI.h"
#include "CellImpl.h"
#include "Creature.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "ObjectAccessor.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Totem.h"

/**
 * @brief 判断指定生物是否可以使用图腾AI
 *
 * 静态函数，用于判断是否应该为此生物使用图腾AI。
 *
 * @param creature 要检查的生物指针
 * @return int32 返回权限值：
 *         - PERMIT_BASE_PROACTIVE: 如果是图腾，允许使用此AI
 *         - PERMIT_BASE_NO: 如果不是图腾，不允许使用此AI
 *
 * 判断逻辑：
 * - IsTotem() 检查生物是否为图腾类型
 * - 图腾通过 creature_template 中的 unit_flags 和 type 标识
 * - 只有真正的图腾才使用此AI
 */
int32 TotemAI::Permissible(Creature const* creature)
{
    // 只有图腾类型生物才使用图腾AI
    if (creature->IsTotem())
        return PERMIT_BASE_PROACTIVE;

    return PERMIT_BASE_NO;
}

/**
 * @brief 图腾AI构造函数
 *
 * 初始化图腾AI实例，设置初始受害者GUID为空
 *
 * @param creature 图腾生物指针
 *
 * @note 使用断言确保传入的生物确实是图腾类型
 *       这是一种防御性编程，防止错误地将图腾AI应用到非图腾生物
 *       断言失败会输出详细的错误信息，便于调试
 */
TotemAI::TotemAI(Creature* creature) : NullCreatureAI(creature), _victimGUID()
{
    // 断言检查：确保AI只应用于图腾类型
    ASSERT(creature->IsTotem(), "TotemAI: AI assigned to a non-totem creature (%s)!", creature->GetGUID().ToString().c_str());
}

/**
 * @brief 图腾AI主更新函数
 *
 * 每个游戏时钟周期调用一次，负责图腾的行为逻辑更新
 *
 * @param diff 距离上次更新的时间间隔（毫秒），当前未使用
 *
 * 主要流程：
 * 1. 检查图腾类型：只有主动攻击型图腾（TOTEM_ACTIVE）才执行攻击逻辑
 * 2. 检查图腾状态：死亡或正在施法则跳过本次更新
 * 3. 获取图腾法术信息：从图腾对象获取其施放的法术
 * 4. 获取法术射程：确定攻击范围
 * 5. 查找目标：
 *    - 首先尝试使用缓存的受害者GUID
 *    - 如果缓存目标无效（死亡、超出范围、友方、不可见等），则搜索新目标
 *    - 使用网格访问器在射程范围内查找最近的攻击目标
 * 6. 施放法术：找到有效目标后，对目标施放图腾法术
 * 7. 清除目标：如果未找到有效目标，清除缓存的受害者GUID
 *
 * 性能优化：
 * - 使用 _victimGUID 缓存目标，避免每次更新都执行完整的网格搜索
 * - 只在目标失效时才执行耗时的目标搜索
 * - 使用网格访问器而不是遍历所有单位，提高效率
 *
 * @note 被动型图腾（如治疗图腾、法力图腾）直接返回，不执行攻击逻辑
 */
void TotemAI::UpdateAI(uint32 /*diff*/)
{
    // ========================================
    // 第一步：检查图腾类型
    // ========================================
    // 只有主动攻击型图腾才执行攻击逻辑，被动图腾（如治疗图腾）直接返回
    if (me->ToTotem()->GetTotemType() != TOTEM_ACTIVE)
        return;

    // ========================================
    // 第二步：检查图腾状态
    // ========================================
    // 图腾死亡或正在施法中，不执行后续逻辑
    // IsNonMeleeSpellCast(false) 检查是否正在施放非近战法术
    if (!me->IsAlive() || me->IsNonMeleeSpellCast(false))
        return;

    // ========================================
    // 第三步：获取图腾的法术信息
    // ========================================
    // 从图腾对象获取其施放的法术ID，然后获取法术信息
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(me->ToTotem()->GetSpell());
    if (!spellInfo)
        return;

    // ========================================
    // 第四步：获取法术的最大射程
    // ========================================
    // GetMaxRange(false) 获取法术的最大射程
    // 参数 false 表示不使用正面效果射程（使用负面效果射程）
    float max_range = spellInfo->GetMaxRange(false);

    // SPELLMOD_RANGE（法术射程修正）未在此处应用
    // 原因：攻击图腾不存在射程修正效果

    // ========================================
    // 第五步：查找攻击目标
    // ========================================
    // 首先尝试获取缓存的目标单位
    Unit* victim = _victimGUID ? ObjectAccessor::GetUnit(*me, _victimGUID) : nullptr;

    // 重新搜索目标的条件：
    // 1. 无缓存目标
    // 2. 目标不可攻击（死亡、无敌等）
    // 3. 目标超出射程
    // 4. 目标变为友方（可能发生在决斗结束时）
    // 5. 图腾无法看到或探测到目标（隐身、潜行等）
    if (!victim || !victim->isTargetableForAttack() || !me->IsWithinDistInMap(victim, max_range) || me->IsFriendlyTo(victim) || !me->CanSeeOrDetect(victim))
    {
        victim = nullptr;

        // 计算额外的搜索半径，确保能搜索到边缘的目标
        // EXTRA_CELL_SEARCH_RADIUS 用于补偿网格边界问题
        float extraSearchRadius = max_range > 0.0f ? EXTRA_CELL_SEARCH_RADIUS : 0.0f;

        // 创建攻击目标检查器，查找最近的可攻击单位
        // 参数说明：
        // - me: 图腾自身，作为搜索的参考点
        // - me->GetCharmerOrOwnerOrSelf(): 获取图腾的主人（施法者）
        // - max_range: 最大搜索距离
        Trinity::NearestAttackableUnitInObjectRangeCheck u_check(me, me->GetCharmerOrOwnerOrSelf(), max_range);

        // 使用单位搜索器遍历网格中的所有对象
        // 这个搜索器会找到满足条件的最近单位
        Trinity::UnitLastSearcher<Trinity::NearestAttackableUnitInObjectRangeCheck> checker(me, victim, u_check);

        // 访问范围内的所有对象，查找攻击目标
        // Cell::VisitAllObjects 会遍历指定半径内的所有网格
        Cell::VisitAllObjects(me, checker, max_range + extraSearchRadius);
    }

    // ========================================
    // 第六步：处理找到的目标
    // ========================================
    if (victim)
    {
        // 缓存目标GUID，下次更新优先使用
        // 这避免了每次更新都重新搜索目标，提高性能
        _victimGUID = victim->GetGUID();

        // 对目标施放图腾法术
        // 这会触发法术施放过程，包括施法时间、公共冷却等
        me->CastSpell(victim, me->ToTotem()->GetSpell());
    }
    else
    {
        // 未找到目标，清除缓存的受害者GUID
        // 这样下次更新会重新搜索
        _victimGUID.Clear();
    }
}

/**
 * @brief 开始攻击处理函数
 *
 * 当图腾被触发攻击时调用。当前仅处理岗哨图腾的特殊行为。
 *
 * @param victim 攻击目标（当前未使用）
 *
 * 主要流程：
 * 1. 检查是否为岗哨图腾（SENTRY_TOTEM_ENTRY）
 * 2. 如果是岗哨图腾，获取图腾的主人
 * 3. 如果主人是玩家，发送小地图雷达ping消息
 *    - 包含图腾的GUID和位置坐标
 *    - 玩家客户端会在小地图上显示ping标记
 *
 * 岗哨图腾机制说明：
 * - 岗哨图腾是萨满祭司的一个技能
 * - 它可以被放置在远处，提供该区域的视野
 * - 当有敌人接近图腾时，图腾会向主人发送警报
 * - 小地图ping让玩家知道敌人的大概位置
 *
 * 数据包格式（MSG_MINIMAP_PING）：
 * - GUID: 发送ping的单位GUID（这里是图腾）
 * - X坐标: ping的位置X坐标
 * - Y坐标: ping的位置Y坐标
 *
 * @note 普通攻击图腾不使用此函数，
 *       它们通过 UpdateAI 自动攻击目标
 */
void TotemAI::AttackStart(Unit* /*victim*/)
{
    // 岗哨图腾在攻击时发送小地图ping
    if (me->GetEntry() == SENTRY_TOTEM_ENTRY)
    {
        // 获取图腾的主人
        if (Unit* owner = me->GetOwner())
            if (Player* player = owner->ToPlayer())
            {
                // 构建小地图ping数据包
                // MSG_MINIMAP_PING 是客户端识别的消息ID
                WorldPacket data(MSG_MINIMAP_PING, (8 + 4 + 4));
                data << me->GetGUID();           // 图腾GUID
                data << me->GetPositionX();      // 图腾X坐标
                data << me->GetPositionY();      // 图腾Y坐标

                // 直接发送给玩家客户端
                // 客户端收到后会在小地图上显示ping标记
                player->SendDirectMessage(&data);
            }
    }
}
