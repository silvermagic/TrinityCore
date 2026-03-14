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
 * @file GridNotifiers.cpp
 * @brief 网格通知器实现文件
 *
 * 本文件实现了网格系统的各种通知器类,用于处理游戏世界中的对象可见性更新、
 * 位置重定位通知、消息分发以及对象更新等核心功能。
 *
 * 主要功能模块:
 * - 可见性通知 (VisibleNotifier): 处理玩家视野范围内对象的创建和销毁
 * - 变更通知 (VisibleChangesNotifier): 通知周围对象关于世界对象的变更
 * - 重定位通知 (PlayerRelocationNotifier/CreatureRelocationNotifier):
 *   处理玩家和生物移动时的视野和AI更新
 * - 延迟重定位 (DelayedUnitRelocation): 批量处理单位重定位通知
 * - AI重定位通知 (AIRelocationNotifier): 触发AI的视野检测
 * - 消息分发器 (MessageDistDeliverer/MessageDistDelivererToHostile):
 *   基于距离和阵营的消息投递
 * - 对象更新器 (ObjectUpdater): 批量更新网格中的对象状态
 *
 * 这些通知器通过访问者模式(Visitor Pattern)遍历网格中的对象集合,
 * 实现高效的区域查询和批量操作。
 */

#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "UpdateData.h"
#include "Transport.h"
#include "ObjectAccessor.h"
#include "CellImpl.h"

using namespace Trinity;

/**
 * @brief 向玩家自身发送可见性更新数据包
 *
 * 此方法完成可见性通知的最后阶段,处理以下任务:
 * 1. 特殊处理载具上的乘客对象(它们可能不在网格迭代范围内)
 * 2. 清理超出视野范围的对象GUID
 * 3. 构建并发送更新数据包
 * 4. 对新进入视野的对象发送初始可见性数据包
 *
 * @note 调用时机: 在网格迭代完成后,由玩家自己调用
 * @note 性能考虑: 此方法会遍历载具乘客列表和超出范围的GUID集合,
 *       对于载具战斗等场景需要关注性能影响
 *
 * 处理流程:
 * - 首先检查玩家是否在载具上,如果是则更新同载具乘客的可见性
 * - 然后处理所有超出范围的对象,从客户端GUID列表中移除
 * - 最后构建并发送SMSG_UPDATE_OBJECT数据包
 */
void VisibleNotifier::SendToSelf()
{
    // 此时 vis_guids 中包含的是在网格层级检查中未迭代到的GUID
    // 但存在一种特殊情况: 这些对象并未超出范围,而是玩家所在的载具上的乘客
    // 载具乘客可能不在同一网格,但仍应保持可见
    if (Transport* transport = i_player.GetTransport())
    {
        // 遍历载具上的所有乘客
        for (Transport::PassengerSet::const_iterator itr = transport->GetPassengers().begin(); itr != transport->GetPassengers().end(); ++itr)
        {
            // 检查该乘客是否在未迭代GUID列表中
            if (vis_guids.find((*itr)->GetGUID()) != vis_guids.end())
            {
                // 从列表中移除,因为该乘客应保持可见
                vis_guids.erase((*itr)->GetGUID());

                // 根据对象类型更新可见性
                switch ((*itr)->GetTypeId())
                {
                    case TYPEID_GAMEOBJECT:
                        // 游戏对象可见性更新
                        i_player.UpdateVisibilityOf((*itr)->ToGameObject(), i_data, i_visibleNow);
                        break;
                    case TYPEID_PLAYER:
                        // 玩家可见性更新 - 双向更新
                        i_player.UpdateVisibilityOf((*itr)->ToPlayer(), i_data, i_visibleNow);
                        // 如果对方玩家未收到可见性变更通知,则更新其对当前玩家的可见性
                        if (!(*itr)->isNeedNotify(NOTIFY_VISIBILITY_CHANGED))
                            (*itr)->ToPlayer()->UpdateVisibilityOf(&i_player);
                        break;
                    case TYPEID_UNIT:
                        // 生物可见性更新
                        i_player.UpdateVisibilityOf((*itr)->ToCreature(), i_data, i_visibleNow);
                        break;
                    case TYPEID_DYNAMICOBJECT:
                        // 动态对象可见性更新
                        i_player.UpdateVisibilityOf((*itr)->ToDynObject(), i_data, i_visibleNow);
                        break;
                    default:
                        break;
                }
            }
        }
    }

    // 处理所有真正超出范围的对象
    // 此时 vis_guids 中的GUID都是需要从客户端视野中移除的对象
    for (auto it = vis_guids.begin(); it != vis_guids.end(); ++it)
    {
        // 从客户端已知的GUID列表中移除
        i_player.m_clientGUIDs.erase(*it);
        // 添加到超出范围GUID列表,客户端将销毁这些对象
        i_data.AddOutOfRangeGUID(*it);

        // 如果是玩家对象,需要通知该玩家更新其对当前玩家的可见性
        if (it->IsPlayer())
        {
            Player* player = ObjectAccessor::FindPlayer(*it);
            // 只有当对方玩家未处于待处理的可见性变更状态时才更新
            if (player && !player->isNeedNotify(NOTIFY_VISIBILITY_CHANGED))
                player->UpdateVisibilityOf(&i_player);
        }
    }

    // 如果没有任何数据需要更新,直接返回
    if (!i_data.HasData())
        return;

    // 构建并发送更新数据包(SMSG_UPDATE_OBJECT)
    WorldPacket packet;
    i_data.BuildPacket(&packet);
    i_player.SendDirectMessage(&packet);

    // 对新进入视野的对象发送初始可见性数据包
    // 这些数据包包含对象的完整创建信息
    for (std::set<Unit*>::const_iterator it = i_visibleNow.begin(); it != i_visibleNow.end(); ++it)
        i_player.SendInitialVisiblePackets(*it);
}

/**
 * @brief 访问玩家集合,通知所有玩家关于世界对象的变更
 *
 * 当某个世界对象(WorldObject)发生可见性相关的变更时(如外貌改变、装备更换等),
 * 需要通知周围所有玩家更新该对象的显示状态。
 *
 * @param m 当前网格单元中的玩家集合
 *
 * 处理逻辑:
 * - 遍历网格中所有玩家
 * - 跳过变更对象自身(避免自通知)
 * - 更新玩家对该对象的可见性
 * - 处理共享视野:如果玩家具有共享视野能力,则通知共享视野的观察者
 *
 * @note 共享视野机制用于某些特殊技能(如猎人的"野兽之眼"),
 *       允许玩家通过其他单位观察世界
 */
void VisibleChangesNotifier::Visit(PlayerMapType &m)
{
    for (PlayerMapType::iterator iter = m.begin(); iter != m.end(); ++iter)
    {
        // 跳过变更对象自身,避免不必要的自通知
        if (iter->GetSource() == &i_object)
            continue;

        // 更新玩家对变更对象的可见性
        iter->GetSource()->UpdateVisibilityOf(&i_object);

        // 处理共享视野:通知所有通过该玩家观察世界的观察者
        if (iter->GetSource()->HasSharedVision())
        {
            for (SharedVisionList::const_iterator i = iter->GetSource()->GetSharedVisionList().begin();
                i != iter->GetSource()->GetSharedVisionList().end(); ++i)
            {
                // 只有当观察者的视野来源(seer)是该玩家时才需要更新
                if ((*i)->m_seer == iter->GetSource())
                    (*i)->UpdateVisibilityOf(&i_object);
            }
        }
    }
}

/**
 * @brief 访问生物集合,通知共享视野的观察者关于世界对象的变更
 *
 * 当世界对象发生变更时,需要通知所有通过生物观察世界的玩家
 * (例如使用"野兽之眼"技能的猎人通过宠物观察世界)。
 *
 * @param m 当前网格单元中的生物集合
 *
 * 处理逻辑:
 * - 遍历网格中所有生物
 * - 检查生物是否具有共享视野能力
 * - 通知所有通过该生物观察世界的玩家更新可见性
 *
 * @note 生物本身不会"看到"对象,只有通过共享视野机制观察该生物的玩家才需要更新
 */
void VisibleChangesNotifier::Visit(CreatureMapType &m)
{
    for (CreatureMapType::iterator iter = m.begin(); iter != m.end(); ++iter)
        // 检查生物是否具有共享视野能力
        if (iter->GetSource()->HasSharedVision())
            // 通知所有通过该生物观察世界的玩家
            for (SharedVisionList::const_iterator i = iter->GetSource()->GetSharedVisionList().begin();
                i != iter->GetSource()->GetSharedVisionList().end(); ++i)
                // 只有当观察者的视野来源(seer)是该生物时才需要更新
                if ((*i)->m_seer == iter->GetSource())
                    (*i)->UpdateVisibilityOf(&i_object);
}

/**
 * @brief 访问动态对象集合,通知施法者关于世界对象的变更
 *
 * 动态对象(如暴风雪、奉献等区域法术效果)可能作为玩家的视野来源。
 * 当世界对象发生变更时,需要通知通过动态对象观察世界的施法者。
 *
 * @param m 当前网格单元中的动态对象集合
 *
 * 处理逻辑:
 * - 遍历网格中所有动态对象
 * - 获取动态对象的施法者
 * - 如果施法者是玩家且视野来源是该动态对象,则更新其可见性
 *
 * @note 此机制用于支持某些特殊法术效果,允许玩家通过动态对象观察周围环境
 */
void VisibleChangesNotifier::Visit(DynamicObjectMapType &m)
{
    for (DynamicObjectMapType::iterator iter = m.begin(); iter != m.end(); ++iter)
        // 获取动态对象的施法者
        if (Unit* caster = iter->GetSource()->GetCaster())
            // 如果施法者是玩家
            if (Player* player = caster->ToPlayer())
                // 检查该玩家是否通过此动态对象观察世界
                if (player->m_seer == iter->GetSource())
                    player->UpdateVisibilityOf(&i_object);
}

/**
 * @brief 生物单位重定位工作函数 - 处理生物与单位之间的视野检测和AI反应
 *
 * 当单位移动时,需要检查周围生物的AI反应。此函数是重定位通知的核心逻辑,
 * 处理生物对移动单位的视野检测和相应的AI行为触发。
 *
 * @param c 生物指针 - 进行视野检测的生物
 * @param u 单位指针 - 移动中的单位(可能是玩家或其他生物)
 *
 * 处理流程:
 * 1. 前置检查:
 *    - 双方必须存活
 *    - 不能是同一单位
 *    - 目标单位不能在飞行中
 * 2. 视野检测:
 *    - 如果生物不具有"失明"状态(UNIT_STATE_SIGHTLESS)
 *    - 检查生物是否能看见或探测到目标单位
 * 3. AI反应:
 *    - 能看见目标: 触发 MoveInLineOfSight_Safe(进入视线事件)
 *    - 不能看见但目标是潜行玩家: 触发 TriggerAlert(警报事件)
 *
 * @note inline函数,性能关键路径
 * @note MoveInLineOfSight_Safe 可能触发战斗、追击等AI行为
 * @note TriggerAlert 用于生物检测到潜行玩家时的反应(如转头、发声等)
 */
inline void CreatureUnitRelocationWorker(Creature* c, Unit* u)
{
    // 前置检查:双方必须存活,且不能是同一单位,目标不能在飞行中
    if (!u->IsAlive() || !c->IsAlive() || c == u || u->IsInFlight())
        return;

    // 检查生物是否处于"失明"状态(某些法术效果会导致失明)
    if (!c->HasUnitState(UNIT_STATE_SIGHTLESS))
    {
        // 正常视野检测:生物能否看到目标
        if (c->IsAIEnabled() && c->CanSeeOrDetect(u, false, true))
        {
            // 目标进入视线,触发AI的视线进入事件
            // 注意:这是_Safe版本,会自动处理AI状态检查
            c->AI()->MoveInLineOfSight_Safe(u);
        }
        else
        {
            // 特殊情况:目标是潜行中的玩家
            // 使用更严格的检测条件(第四个参数true表示忽略潜行修饰符)
            if (u->GetTypeId() == TYPEID_PLAYER && u->HasStealthAura() && c->IsAIEnabled() && c->CanSeeOrDetect(u, false, true, true))
                // 生物检测到了潜行玩家,触发警报反应(如转头看向玩家方向)
                c->AI()->TriggerAlert(u);
        }
    }
}

void PlayerRelocationNotifier::Visit(PlayerMapType &m)
{
    for (PlayerMapType::iterator iter = m.begin(); iter != m.end(); ++iter)
    {
        Player* player = iter->GetSource();

        vis_guids.erase(player->GetGUID());

        i_player.UpdateVisibilityOf(player, i_data, i_visibleNow);

        if (player->m_seer->isNeedNotify(NOTIFY_VISIBILITY_CHANGED))
            continue;

        player->UpdateVisibilityOf(&i_player);
    }
}

void PlayerRelocationNotifier::Visit(CreatureMapType &m)
{
    bool relocated_for_ai = (&i_player == i_player.m_seer);

    for (CreatureMapType::iterator iter=m.begin(); iter != m.end(); ++iter)
    {
        Creature* c = iter->GetSource();

        vis_guids.erase(c->GetGUID());

        i_player.UpdateVisibilityOf(c, i_data, i_visibleNow);

        if (relocated_for_ai && !c->isNeedNotify(NOTIFY_VISIBILITY_CHANGED))
            CreatureUnitRelocationWorker(c, &i_player);
    }
}

void CreatureRelocationNotifier::Visit(PlayerMapType &m)
{
    for (PlayerMapType::iterator iter = m.begin(); iter != m.end(); ++iter)
    {
        Player* player = iter->GetSource();

        if (!player->m_seer->isNeedNotify(NOTIFY_VISIBILITY_CHANGED))
            player->UpdateVisibilityOf(&i_creature);

        CreatureUnitRelocationWorker(&i_creature, player);
    }
}

void CreatureRelocationNotifier::Visit(CreatureMapType &m)
{
    if (!i_creature.IsAlive())
        return;

    for (CreatureMapType::iterator iter = m.begin(); iter != m.end(); ++iter)
    {
        Creature* c = iter->GetSource();
        CreatureUnitRelocationWorker(&i_creature, c);

        if (!c->isNeedNotify(NOTIFY_VISIBILITY_CHANGED))
            CreatureUnitRelocationWorker(c, &i_creature);
    }
}

void DelayedUnitRelocation::Visit(CreatureMapType &m)
{
    for (CreatureMapType::iterator iter = m.begin(); iter != m.end(); ++iter)
    {
        Creature* unit = iter->GetSource();
        if (!unit->isNeedNotify(NOTIFY_VISIBILITY_CHANGED))
            continue;

        CreatureRelocationNotifier relocate(*unit);

        TypeContainerVisitor<CreatureRelocationNotifier, WorldTypeMapContainer > c2world_relocation(relocate);
        TypeContainerVisitor<CreatureRelocationNotifier, GridTypeMapContainer >  c2grid_relocation(relocate);

        cell.Visit(p, c2world_relocation, i_map, *unit, i_radius);
        cell.Visit(p, c2grid_relocation, i_map, *unit, i_radius);
    }
}

void DelayedUnitRelocation::Visit(PlayerMapType &m)
{
    for (PlayerMapType::iterator iter = m.begin(); iter != m.end(); ++iter)
    {
        Player* player = iter->GetSource();
        WorldObject const* viewPoint = player->m_seer;

        if (!viewPoint->isNeedNotify(NOTIFY_VISIBILITY_CHANGED))
            continue;

        if (player != viewPoint && !viewPoint->IsPositionValid())
            continue;

        PlayerRelocationNotifier relocate(*player);
        Cell::VisitAllObjects(viewPoint, relocate, i_radius, false);
        relocate.SendToSelf();
    }
}

void AIRelocationNotifier::Visit(CreatureMapType &m)
{
    for (CreatureMapType::iterator iter = m.begin(); iter != m.end(); ++iter)
    {
        Creature* c = iter->GetSource();
        CreatureUnitRelocationWorker(c, &i_unit);
        if (isCreature)
            CreatureUnitRelocationWorker((Creature*)&i_unit, c);
    }
}

void MessageDistDeliverer::Visit(PlayerMapType &m)
{
    for (PlayerMapType::iterator iter = m.begin(); iter != m.end(); ++iter)
    {
        Player* target = iter->GetSource();
        if (!target->InSamePhase(i_phaseMask))
            continue;

        if (required3dDist)
        {
            if (target->GetExactDistSq(i_source) > i_distSq)
                continue;
        }
        else
        {
            if (target->GetExactDist2dSq(i_source) > i_distSq)
                continue;
        }

        // Send packet to all who are sharing the player's vision
        if (target->HasSharedVision())
        {
            SharedVisionList::const_iterator i = target->GetSharedVisionList().begin();
            for (; i != target->GetSharedVisionList().end(); ++i)
                if ((*i)->m_seer == target)
                    SendPacket(*i);
        }

        if (target->m_seer == target || target->GetVehicle())
            SendPacket(target);
    }
}

void MessageDistDeliverer::Visit(CreatureMapType &m)
{
    for (CreatureMapType::iterator iter = m.begin(); iter != m.end(); ++iter)
    {
        Creature* target = iter->GetSource();
        if (!target->InSamePhase(i_phaseMask))
            continue;

        if (required3dDist)
        {
            if (target->GetExactDistSq(i_source) > i_distSq)
                continue;
        }
        else
        {
            if (target->GetExactDist2dSq(i_source) > i_distSq)
                continue;
        }

        // Send packet to all who are sharing the creature's vision
        if (target->HasSharedVision())
        {
            SharedVisionList::const_iterator i = target->GetSharedVisionList().begin();
            for (; i != target->GetSharedVisionList().end(); ++i)
                if ((*i)->m_seer == target)
                    SendPacket(*i);
        }
    }
}

void MessageDistDeliverer::Visit(DynamicObjectMapType &m)
{
    for (DynamicObjectMapType::iterator iter = m.begin(); iter != m.end(); ++iter)
    {
        DynamicObject* target = iter->GetSource();
        if (!target->InSamePhase(i_phaseMask))
            continue;

        if (required3dDist)
        {
            if (target->GetExactDistSq(i_source) > i_distSq)
                continue;
        }
        else
        {
            if (target->GetExactDist2dSq(i_source) > i_distSq)
                continue;
        }

        if (Unit* caster = target->GetCaster())
        {
            // Send packet back to the caster if the caster has vision of dynamic object
            Player* player = caster->ToPlayer();
            if (player && player->m_seer == target)
                SendPacket(player);
        }
    }
}

void MessageDistDelivererToHostile::Visit(PlayerMapType &m)
{
    for (PlayerMapType::iterator iter = m.begin(); iter != m.end(); ++iter)
    {
        Player* target = iter->GetSource();
        if (!target->InSamePhase(i_phaseMask))
            continue;

        if (target->GetExactDist2dSq(i_source) > i_distSq)
            continue;

        // Send packet to all who are sharing the player's vision
        if (target->HasSharedVision())
        {
            SharedVisionList::const_iterator i = target->GetSharedVisionList().begin();
            for (; i != target->GetSharedVisionList().end(); ++i)
                if ((*i)->m_seer == target)
                    SendPacket(*i);
        }

        if (target->m_seer == target || target->GetVehicle())
            SendPacket(target);
    }
}

void MessageDistDelivererToHostile::Visit(CreatureMapType &m)
{
    for (CreatureMapType::iterator iter = m.begin(); iter != m.end(); ++iter)
    {
        Creature* target = iter->GetSource();
        if (!target->InSamePhase(i_phaseMask))
            continue;

        if (target->GetExactDist2dSq(i_source) > i_distSq)
            continue;

        // Send packet to all who are sharing the creature's vision
        if (target->HasSharedVision())
        {
            SharedVisionList::const_iterator i = target->GetSharedVisionList().begin();
            for (; i != target->GetSharedVisionList().end(); ++i)
                if ((*i)->m_seer == target)
                    SendPacket(*i);
        }
    }
}

void MessageDistDelivererToHostile::Visit(DynamicObjectMapType &m)
{
    for (DynamicObjectMapType::iterator iter = m.begin(); iter != m.end(); ++iter)
    {
        DynamicObject* target = iter->GetSource();
        if (!target->InSamePhase(i_phaseMask))
            continue;

        if (target->GetExactDist2dSq(i_source) > i_distSq)
            continue;

        if (Unit* caster = target->GetCaster())
        {
            // Send packet back to the caster if the caster has vision of dynamic object
            Player* player = caster->ToPlayer();
            if (player && player->m_seer == target)
                SendPacket(player);
        }
    }
}

/*
void
MessageDistDeliverer::VisitObject(Player* player)
{
    if (!i_ownTeamOnly || (i_source.GetTypeId() == TYPEID_PLAYER && player->GetTeam() == ((Player&)i_source).GetTeam()))
    {
        SendPacket(player);
    }
}
*/

template<class T>
void ObjectUpdater::Visit(GridRefManager<T> &m)
{
    for (typename GridRefManager<T>::iterator iter = m.begin(); iter != m.end(); ++iter)
        if (iter->GetSource()->IsInWorld())
            iter->GetSource()->Update(i_timeDiff);
}

bool AnyDeadUnitObjectInRangeCheck::operator()(Player* u)
{
    return !u->IsAlive() && !u->HasAuraType(SPELL_AURA_GHOST) && i_searchObj->IsWithinDistInMap(u, i_range);
}

bool AnyDeadUnitObjectInRangeCheck::operator()(Corpse* u)
{
    return u->GetType() != CORPSE_BONES && i_searchObj->IsWithinDistInMap(u, i_range);
}

bool AnyDeadUnitObjectInRangeCheck::operator()(Creature* u)
{
    return !u->IsAlive() && i_searchObj->IsWithinDistInMap(u, i_range);
}

bool AnyDeadUnitSpellTargetInRangeCheck::operator()(Player* u)
{
    return AnyDeadUnitObjectInRangeCheck::operator()(u) && WorldObjectSpellTargetCheck::operator()(u);
}

bool AnyDeadUnitSpellTargetInRangeCheck::operator()(Corpse* u)
{
    return AnyDeadUnitObjectInRangeCheck::operator()(u) && WorldObjectSpellTargetCheck::operator()(u);
}

bool AnyDeadUnitSpellTargetInRangeCheck::operator()(Creature* u)
{
    return AnyDeadUnitObjectInRangeCheck::operator()(u) && WorldObjectSpellTargetCheck::operator()(u);
}

template void ObjectUpdater::Visit<Creature>(CreatureMapType&);
template void ObjectUpdater::Visit<GameObject>(GameObjectMapType&);
template void ObjectUpdater::Visit<DynamicObject>(DynamicObjectMapType&);
