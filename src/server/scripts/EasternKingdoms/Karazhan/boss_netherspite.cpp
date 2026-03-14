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
 * @file    boss_netherspite.cpp
 * @brief   卡拉赞副本 - 奈瑟斯派德(虚空幽龙)首领战AI实现
 * @details 实现奈瑟斯派德的战斗逻辑,包括:
 *          - 传送门阶段与放逐阶段的循环切换
 *          - 三色传送门机制(红/绿/蓝),每种颜色提供不同的增益效果
 *          - 光束追踪系统,玩家需站在光束中获得增益并阻止Boss获得增益
 *          - 虚空区域和虚空吐息等技能
 *          - 狂暴机制
 */

/* ScriptData
SDName: Boss_Netherspite
SD%Complete: 90
SDComment: Not sure about timing and portals placing
SDCategory: Karazhan
EndScriptData */

#include "ScriptMgr.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "karazhan.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "TemporarySummon.h"

/**
 * @brief 奈瑟斯派德相关枚举定义
 */
enum Netherspite
{
    // 说话/表情文本
    EMOTE_PHASE_PORTAL          = 0,    ///< 进入传送门阶段的表情
    EMOTE_PHASE_BANISH          = 1,    ///< 进入放逐阶段的表情

    // 法术ID
    SPELL_NETHERBURN_AURA       = 30522, ///< 虚空燃烧光环 - 对Boss周围的玩家造成伤害
    SPELL_VOIDZONE              = 37063, ///< 虚空区域 - 在地面生成持续伤害区域
    SPELL_NETHER_INFUSION       = 38688, ///< 虚空灌注 - 狂暴增益法术
    SPELL_NETHERBREATH          = 38523, ///< 虚空吐息 - 放逐阶段使用的锥形范围伤害
    SPELL_BANISH_VISUAL         = 39833, ///< 放逐视觉效果
    SPELL_BANISH_ROOT           = 42716, ///< 放逐定身效果
    SPELL_EMPOWERMENT           = 38549, ///< 赋能 - Boss在传送门阶段获得的增益
    SPELL_NETHERSPITE_ROAR      = 38684, ///< 奈瑟斯派德咆哮 - 狂暴时使用
};

/**
 * @brief 传送门可能出现的位置坐标
 * @details 三个预设位置,分别位于房间的左侧、右侧和后方
 */
const float PortalCoord[3][3] =
{
    {-11195.353516f, -1613.237183f, 278.237258f}, ///< 左侧位置
    {-11137.846680f, -1685.607422f, 278.239258f}, ///< 右侧位置
    {-11094.493164f, -1591.969238f, 279.949188f}  ///< 后方位置
};

/**
 * @brief 传送门颜色类型枚举
 */
enum Netherspite_Portal{
    RED_PORTAL = 0,    ///< 红色传送门 - 坚毅(Perseverence) - 增加生命值和威胁值
    GREEN_PORTAL = 1,  ///< 绿色传送门 - 宁静(Serenity) - 恢复法力值并降低施法消耗
    BLUE_PORTAL = 2    ///< 蓝色传送门 - 支配(Dominance) - 增加法术伤害
};

const uint32 PortalID[3] = {17369, 17367, 17368};          ///< 传送门生物ID
const uint32 PortalVisual[3] = {30487, 30490, 30491};      ///< 传送门视觉效果法术ID
const uint32 PortalBeam[3] = {30465, 30464, 30463};        ///< 光束投射法术ID
const uint32 PlayerBuff[3] = {30421, 30422, 30423};        ///< 玩家站在光束中获得的增益ID
const uint32 NetherBuff[3] = {30466, 30467, 30468};        ///< Boss被光束击中时获得的增益ID
const uint32 PlayerDebuff[3] = {38637, 38638, 38639};      ///< 玩家离开光束后获得的疲劳debuff ID

/**
 * @class boss_netherspite
 * @brief 奈瑟斯派德首领脚本类
 * @details 注册和管理奈瑟斯派德的AI实例
 */
class boss_netherspite : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     */
    boss_netherspite() : CreatureScript("boss_netherspite") { }

    /**
     * @brief 获取AI实例
     * @param creature 生物对象指针
     * @return 返回奈瑟斯派德AI实例
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetKarazhanAI<boss_netherspiteAI>(creature);
    }

    /**
     * @struct boss_netherspiteAI
     * @brief 奈瑟斯派德AI结构体
     * @details 实现奈瑟斯派德的战斗逻辑,包括传送门阶段和放逐阶段的循环
     */
    struct boss_netherspiteAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         */
        boss_netherspiteAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
            instance = creature->GetInstanceScript();

            PortalPhase = false;
            PhaseTimer = 0;
            EmpowermentTimer = 0;
            PortalTimer = 0;
        }

        /**
         * @brief 初始化成员变量
         * @details 设置技能计时器和状态标志的初始值
         */
        void Initialize()
        {
            Berserk = false;
            NetherInfusionTimer = 540000;  ///< 9分钟后狂暴
            VoidZoneTimer = 15000;         ///< 15秒施放一次虚空区域
            NetherbreathTimer = 3000;      ///< 放逐阶段3秒后开始虚空吐息
        }

        InstanceScript* instance;          ///< 副本实例脚本指针

        bool PortalPhase;                  ///< 是否处于传送门阶段
        bool Berserk;                      ///< 是否已狂暴
        uint32 PhaseTimer;                 ///< 阶段切换计时器
        uint32 VoidZoneTimer;              ///< 虚空区域施放计时器
        uint32 NetherInfusionTimer;        ///< 狂暴计时器
        uint32 NetherbreathTimer;          ///< 虚空吐息计时器
        uint32 EmpowermentTimer;           ///< 赋能法术计时器
        uint32 PortalTimer;                ///< 光束检查计时器
        ObjectGuid PortalGUID[3];          ///< 三个传送门的GUID
        ObjectGuid BeamerGUID[3];          ///< 三个光束投射者的GUID(用于光束视觉效果)
        ObjectGuid BeamTarget[3];          ///< 三个光束当前目标的GUID

        /**
         * @brief 检查目标是否在两个对象之间的连线上
         * @param u1 第一个对象(Boss)
         * @param target 要检查的目标(玩家)
         * @param u2 第二个对象(传送门)
         * @return true 如果目标在连线上且距离连线足够近
         * @details 使用点到直线距离公式判断目标是否在Boss和传送门之间的光束路径上
         *          性能注意:每次调用都进行浮点计算,在UpdatePortals中频繁调用
         */
        bool IsBetween(WorldObject* u1, WorldObject* target, WorldObject* u2)
        {
            if (!u1 || !u2 || !target)
                return false;

            float xn, yn, xp, yp, xh, yh;
            xn = u1->GetPositionX();
            yn = u1->GetPositionY();
            xp = u2->GetPositionX();
            yp = u2->GetPositionY();
            xh = target->GetPositionX();
            yh = target->GetPositionY();

            // 检查目标是否在两点之间(不考虑到光束的距离)
            if (dist(xn, yn, xh, yh) >= dist(xn, yn, xp, yp) || dist(xp, yp, xh, yh) >= dist(xn, yn, xp, yp))
                return false;
            // 检查目标到光束的距离是否小于1.5码
            return (std::abs((xn-xp)*yh+(yp-yn)*xh-xn*yp+xp*yn)/dist(xn, yn, xp, yp) < 1.5f);
        }

        /**
         * @brief 计算两点之间的距离
         * @param xa 第一个点的X坐标
         * @param ya 第一个点的Y坐标
         * @param xb 第二个点的X坐标
         * @param yb 第二个点的Y坐标
         * @return 两点间的欧几里得距离
         * @details 辅助函数,用于IsBetween中的距离计算
         */
        float dist(float xa, float ya, float xb, float yb)
        {
            return std::sqrt((xa-xb)*(xa-xb) + (ya-yb)*(ya-yb));
        }

        /**
         * @brief 重置Boss状态
         * @details 在战斗结束或重置时调用,清理所有状态并打开门
         *          调用时机:战斗结束、首领重置、副本重置
         */
        void Reset() override
        {
            Initialize();

            HandleDoors(true);
            DestroyPortals();

            instance->SetBossState(DATA_NETHERSPITE, NOT_STARTED);
        }

        /**
         * @brief 召唤三个传送门
         * @details 随机将红、绿、蓝三个传送门放置在预设的三个位置之一
         *          使用随机算法确保每次传送门位置不同
         *          调用时机:进入传送门阶段时
         */
        void SummonPortals()
        {
            uint8 r = rand32() % 4;
            uint8 pos[3];
            pos[RED_PORTAL] = ((r % 2) ? (r > 1 ? 2 : 1) : 0);
            pos[GREEN_PORTAL] = ((r % 2) ? 0 : (r > 1 ? 2 : 1));
            pos[BLUE_PORTAL] = (r > 1 ? 1 : 2); // 蓝色传送门不会出现在左侧(0)

            for (int i = 0; i < 3; ++i)
                if (Creature* portal = me->SummonCreature(PortalID[i], PortalCoord[pos[i]][0], PortalCoord[pos[i]][1], PortalCoord[pos[i]][2], 0, TEMPSUMMON_TIMED_DESPAWN, 1min))
                {
                    PortalGUID[i] = portal->GetGUID();
                    portal->AddAura(PortalVisual[i], portal);
                }
        }

        /**
         * @brief 销毁所有传送门和光束投射者
         * @details 清理传送门阶段创建的所有临时生物
         *          调用时机:切换到放逐阶段、Boss重置或死亡时
         */
        void DestroyPortals()
        {
            for (int i=0; i<3; ++i)
            {
                if (Creature* portal = ObjectAccessor::GetCreature(*me, PortalGUID[i]))
                    portal->DisappearAndDie();
                if (Creature* portal = ObjectAccessor::GetCreature(*me, BeamerGUID[i]))
                    portal->DisappearAndDie();
                PortalGUID[i].Clear();
                BeamTarget[i].Clear();
            }
        }

        /**
         * @brief 更新传送门光束的行为
         * @details 每秒检查并更新光束的目标:
         *          - 寻找每个传送门光束的最佳目标(站在光束路径上的玩家)
         *          - 如果没有合适的玩家,则光束击中Boss
         *          - 对目标施加相应的增益/减益效果
         *          - 红色光束目标获得大量威胁值
         *          性能注意:每秒遍历所有在线玩家,玩家数量多时可能有性能影响
         */
        void UpdatePortals()
        {
            for (int j = 0; j < 3; ++j) // j = 颜色索引(红/绿/蓝)
                if (Creature* portal = ObjectAccessor::GetCreature(*me, PortalGUID[j]))
                {
                    // 当前光束目标的引用
                    Unit* current = ObjectAccessor::GetUnit(*portal, BeamTarget[j]);
                    // 临时存储最佳光束接收者,默认为Boss自己
                    Unit* target = me;

                    Map::PlayerList const& players = me->GetMap()->GetPlayers();

                    // 寻找最佳目标
                    for (Map::PlayerList::const_iterator i = players.begin(); i != players.end(); ++i)
                    {
                        Player* p = i->GetSource();
                        if (p && p->IsAlive() // 存活状态
                            && (!target || target->GetDistance2d(portal)>p->GetDistance2d(portal)) // 比当前目标更近
                            && !p->HasAura(PlayerDebuff[j]) // 没有疲劳debuff
                            && !p->HasAura(PlayerBuff[(j + 1) % 3]) // 不在其他光束中
                            && !p->HasAura(PlayerBuff[(j + 2) % 3])
                            && IsBetween(me, p, portal)) // 在光束路径上
                            target = p;
                    }

                    // 对目标施加增益效果
                    if (target->GetTypeId() == TYPEID_PLAYER)
                        target->AddAura(PlayerBuff[j], target);
                    else
                        target->AddAura(NetherBuff[j], target);

                    // 如果目标切换,重新投射光束视觉效果
                    // 使用BeamerGUID作为 workaround,因为直接切换目标不工作
                    if (!current || target != current)
                    {
                        BeamTarget[j] = target->GetGUID();
                        // 移除当前的光束投射者
                        if (Creature* beamer = ObjectAccessor::GetCreature(*portal, BeamerGUID[j]))
                        {
                            beamer->CastSpell(target, PortalBeam[j], false);
                            beamer->DisappearAndDie();
                            BeamerGUID[j].Clear();
                        }
                        // 创建新的光束投射者并开始向目标投射
                        if (Creature* beamer = portal->SummonCreature(PortalID[j], portal->GetPositionX(), portal->GetPositionY(), portal->GetPositionZ(), portal->GetOrientation(), TEMPSUMMON_TIMED_DESPAWN, 1min))
                        {
                            beamer->CastSpell(target, PortalBeam[j], false);
                            BeamerGUID[j] = beamer->GetGUID();
                        }
                    }
                    // 如果是红色光束且目标不是当前坦克,给予大量威胁值
                    if (j == RED_PORTAL && me->GetVictim() != target && target->GetTypeId() == TYPEID_PLAYER)
                        AddThreat(target, 100000.0f);
                }
        }

        /**
         * @brief 切换到传送门阶段
         * @details 移除放逐效果,召唤三个传送门,开始60秒的传送门阶段
         *          调用时机:战斗开始、放逐阶段结束
         */
        void SwitchToPortalPhase()
        {
            me->RemoveAurasDueToSpell(SPELL_BANISH_ROOT);
            me->RemoveAurasDueToSpell(SPELL_BANISH_VISUAL);
            SummonPortals();
            PhaseTimer = 60000;      ///< 传送门阶段持续60秒
            PortalPhase = true;
            PortalTimer = 10000;     ///< 10秒后开始检查光束
            EmpowermentTimer = 10000; ///< 10秒后施放赋能
            Talk(EMOTE_PHASE_PORTAL);
        }

        /**
         * @brief 切换到放逐阶段
         * @details 移除增益效果,进入放逐状态,销毁传送门,开始30秒的放逐阶段
         *          调用时机:传送门阶段结束
         */
        void SwitchToBanishPhase()
        {
            me->RemoveAurasDueToSpell(SPELL_EMPOWERMENT);
            me->RemoveAurasDueToSpell(SPELL_NETHERBURN_AURA);
            DoCast(me, SPELL_BANISH_VISUAL, true);
            DoCast(me, SPELL_BANISH_ROOT, true);
            DestroyPortals();
            PhaseTimer = 30000;      ///< 放逐阶段持续30秒
            PortalPhase = false;
            Talk(EMOTE_PHASE_BANISH);

            // 移除Boss身上的所有光束增益
            for (uint8 i = 0; i < 3; ++i)
                me->RemoveAurasDueToSpell(NetherBuff[i]);
        }

        /**
         * @brief 处理大门的开关状态
         * @param open true为打开,false为关闭
         * @details 控制战斗区域大门的状态,防止战斗中逃跑
         */
        void HandleDoors(bool open)
        {
            if (GameObject* Door = ObjectAccessor::GetGameObject(*me, instance->GetGuidData(DATA_GO_MASSIVE_DOOR) ))
                Door->SetGoState(open ? GO_STATE_ACTIVE : GO_STATE_READY);
        }

        /**
         * @brief 进入战斗时调用
         * @param who 仇恨目标
         * @details 关闭大门,切换到传送门阶段,设置副本状态
         */
        void JustEngagedWith(Unit* /*who*/) override
        {
            HandleDoors(false);
            SwitchToPortalPhase();

            instance->SetBossState(DATA_NETHERSPITE, IN_PROGRESS);
        }

        /**
         * @brief Boss死亡时调用
         * @param killer 击杀者
         * @details 打开大门,清理传送门,设置副本状态为完成
         */
        void JustDied(Unit* /*killer*/) override
        {
            HandleDoors(true);
            DestroyPortals();

            instance->SetBossState(DATA_NETHERSPITE, DONE);
        }

        /**
         * @brief AI更新函数,每帧调用
         * @param diff 距离上次调用的时间间隔(毫秒)
         * @details 处理Boss的所有战斗逻辑:
         *          - 虚空区域技能
         *          - 狂暴检查
         *          - 传送门阶段的光束和赋能
         *          - 放逐阶段的虚空吐息
         *          - 阶段切换
         */
        void UpdateAI(uint32 diff) override
        {
            if (!UpdateVictim())
                return;

            // 虚空区域 - 每15秒施放一次
            if (VoidZoneTimer <= diff)
            {
                DoCast(SelectTarget(SelectTargetMethod::Random, 1, 45, true), SPELL_VOIDZONE, true);
                VoidZoneTimer = 15000;
            } else VoidZoneTimer -= diff;

            // 狂暴检查 - 9分钟后触发
            if (!Berserk && NetherInfusionTimer <= diff)
            {
                me->AddAura(SPELL_NETHER_INFUSION, me);
                DoCast(me, SPELL_NETHERSPITE_ROAR);
                Berserk = true;
            } else NetherInfusionTimer -= diff;

            if (PortalPhase) // 传送门阶段
            {
                // 每秒更新光束和增益
                if (PortalTimer <= diff)
                {
                    UpdatePortals();
                    PortalTimer = 1000;
                } else PortalTimer -= diff;

                // 赋能和虚空燃烧 - 10秒后施放,之后每90秒一次
                if (EmpowermentTimer <= diff)
                {
                    DoCast(me, SPELL_EMPOWERMENT);
                    me->AddAura(SPELL_NETHERBURN_AURA, me);
                    EmpowermentTimer = 90000;
                } else EmpowermentTimer -= diff;

                // 阶段切换检查
                if (PhaseTimer <= diff)
                {
                    if (!me->IsNonMeleeSpellCast(false))
                    {
                        SwitchToBanishPhase();
                        return;
                    }
                } else PhaseTimer -= diff;
            }
            else // 放逐阶段
            {
                // 虚空吐息 - 随机5-7秒间隔
                if (NetherbreathTimer <= diff)
                {
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 40, true))
                        DoCast(target, SPELL_NETHERBREATH);
                    NetherbreathTimer = urand(5000, 7000);
                } else NetherbreathTimer -= diff;

                // 阶段切换检查
                if (PhaseTimer <= diff)
                {
                    if (!me->IsNonMeleeSpellCast(false))
                    {
                        SwitchToPortalPhase();
                        return;
                    }
                } else PhaseTimer -= diff;
            }

            DoMeleeAttackIfReady();
        }
    };
};

/**
 * @brief 注册奈瑟斯派德首领脚本
 * @details 创建并注册奈瑟斯派德脚本实例到脚本系统
 *          调用时机:服务器启动时加载脚本模块
 */
void AddSC_boss_netherspite()
{
    new boss_netherspite();
}
