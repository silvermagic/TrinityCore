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
 * @file boss_novos.cpp
 * @brief 达克萨隆要塞副本 - 诺沃斯（Novos）首领战脚本
 *
 * 本文件实现了达克萨隆要塞副本中第二个首领诺沃斯的战斗逻辑。
 * 诺沃斯是一名天灾法师，战斗分为两个阶段：
 * 1. 防护阶段：被奥术护盾保护，玩家需要击杀4个水晶守卫来破除护盾
 * 2. 战斗阶段：护盾消失后，诺沃斯主动攻击玩家
 *
 * 主要功能：
 * - 诺沃斯的战斗AI和两阶段机制
 * - 水晶激活和召唤系统
 * - 水晶守卫击杀进度追踪
 * - 成就"Oh Novos!"的检测逻辑
 */

#include "ScriptMgr.h"
#include "drak_tharon_keep.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ScriptedCreature.h"
#include "SpellScript.h"

/**
 * @brief 诺沃斯的台词和喊话枚举
 */
enum Yells
{
    SAY_AGGRO                       = 0,  ///< 开战台词
    SAY_KILL                        = 1,  ///< 击杀玩家台词
    SAY_DEATH                       = 2,  ///< 死亡台词
    SAY_SUMMONING_ADDS              = 3,  ///< 召唤小怪台词（未使用）
    SAY_ARCANE_FIELD                = 4,  ///< 奥术护盾消失台词
    EMOTE_SUMMONING_ADDS            = 5   ///< 召唤小怪表情（未使用）
};

/**
 * @brief 诺沃斯使用的法术ID枚举
 */
enum Spells
{
    SPELL_BEAM_CHANNEL              = 52106,  ///< 光束通道 - 水晶发射的奥术光束
    SPELL_ARCANE_FIELD              = 47346,  ///< 奥术护盾 - 保护诺沃斯的护盾

    // 召唤法术
    SPELL_SUMMON_RISEN_SHADOWCASTER = 49105,  ///< 召唤复活的暗影施法者
    SPELL_SUMMON_FETID_TROLL_CORPSE = 49103,  ///< 召唤腐烂的巨魔尸体
    SPELL_SUMMON_HULKING_CORPSE     = 49104,  ///< 召唤庞大的尸体
    SPELL_SUMMON_CRYSTAL_HANDLER    = 49179,  ///< 召唤水晶守卫
    SPELL_SUMMON_COPY_OF_MINIONS    = 59933,  ///< 召唤小怪复制品（英雄模式）

    // 攻击法术
    SPELL_ARCANE_BLAST              = 49198,  ///< 奥术冲击 - 对目标造成奥术伤害
    SPELL_BLIZZARD                  = 49034,  ///< 暴风雪 - 范围冰霜伤害
    SPELL_FROSTBOLT                 = 49037,  ///< 冰霜箭 - 单体冰霜伤害
    SPELL_WRATH_OF_MISERY           = 50089,  ///< 苦难之怒 - 暗影伤害
    SPELL_SUMMON_MINIONS            = 59910   ///< 召唤仆从（英雄模式）
};

/**
 * @brief 其他常量枚举
 */
enum Misc
{
    ACTION_RESET_CRYSTALS,          ///< 重置水晶
    ACTION_ACTIVATE_CRYSTAL,        ///< 激活水晶
    ACTION_DEACTIVATE,              ///< 停用
    EVENT_ATTACK,                   ///< 攻击事件
    EVENT_SUMMON_MINIONS,           ///< 召唤仆从事件
    DATA_NOVOS_ACHIEV               ///< 成就数据标识
};

/**
 * @brief 召唤者信息结构体
 * @note 存储每个召唤点的召唤信息
 */
struct SummonerInfo
{
    uint32 data;    ///< 数据标识（用于获取召唤者GUID）
    uint32 spell;   ///< 召唤法术ID
    uint32 timer;   ///< 召唤间隔时间（毫秒）
};

/**
 * @brief 召唤者配置数组
 * @note 定义了4个召唤点的召唤信息
 */
const SummonerInfo summoners[] =
{
    { DATA_NOVOS_SUMMONER_1, SPELL_SUMMON_RISEN_SHADOWCASTER, 15000 },  ///< 召唤点1：复活的暗影施法者，每15秒
    { DATA_NOVOS_SUMMONER_2, SPELL_SUMMON_FETID_TROLL_CORPSE, 5000 },   ///< 召唤点2：腐烂的巨魔尸体，每5秒
    { DATA_NOVOS_SUMMONER_3, SPELL_SUMMON_HULKING_CORPSE, 30000 },      ///< 召唤点3：庞大的尸体，每30秒
    { DATA_NOVOS_SUMMONER_4, SPELL_SUMMON_CRYSTAL_HANDLER, 30000 }      ///< 召唤点4：水晶守卫，每30秒
};

/**
 * @brief 成就"Oh Novos!"的Y坐标阈值
 * @note 如果召唤的小怪越过此Y坐标线，则成就失败
 */
#define MAX_Y_COORD_OH_NOVOS        -771.95f

/**
 * @brief 诺沃斯首领AI结构体
 *
 * 实现了诺沃斯的完整战斗逻辑，包括：
 * - 两阶段战斗：防护阶段和战斗阶段
 * - 水晶管理和激活
 * - 召唤者管理和召唤控制
 * - 成就检测
 */
struct boss_novos : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 首领生物对象指针
     */
    boss_novos(Creature* creature) : BossAI(creature, DATA_NOVOS)
    {
        Initialize();
        _bubbled = false;
    }

    /**
     * @brief 初始化成员变量
     * @note 在构造函数和重置时调用
     */
    void Initialize()
    {
        _ohNovos = true;              ///< 初始化成就标志为true
        _crystalHandlerCount = 0;     ///< 水晶守卫击杀计数归零
    }

    /**
     * @brief 重置首领状态
     *
     * @调用时机 战斗结束、首领脱战、重置副本时
     */
    void Reset() override
    {
        _Reset();

        Initialize();
        SetCrystalsStatus(false);     ///< 关闭所有水晶
        SetSummonerStatus(false);     ///< 停止所有召唤者
        SetBubbled(false);            ///< 移除奥术护盾
    }

    /**
     * @brief 进入战斗
     * @param who 进入战斗的目标单位
     *
     * @调用时机 当诺沃斯被玩家攻击或主动攻击玩家时
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        Talk(SAY_AGGRO);              ///< 播放开战台词

        SetCrystalsStatus(true);      ///< 激活所有水晶
        SetSummonerStatus(true);      ///< 启动召唤者
        SetBubbled(true);             ///< 激活奥术护盾
    }

    /**
     * @brief 开始攻击
     * @param target 攻击目标
     *
     * @note 诺沃斯不会主动移动，只进行远程攻击
     */
    void AttackStart(Unit* target) override
    {
        if (!target)
            return;

        if (me->Attack(target, true))
            DoStartNoMovement(target);  ///< 不移动，原地攻击
    }

    /**
     * @brief 击杀单位
     * @param who 被击杀的单位
     *
     * @调用时机 当诺沃斯击杀玩家时
     */
    void KilledUnit(Unit* who) override
    {
        if (who->GetTypeId() == TYPEID_PLAYER)
            Talk(SAY_KILL);
    }

    /**
     * @brief 首领死亡
     * @param killer 击杀首领的单位（可能为nullptr）
     *
     * @调用时机 当诺沃斯生命值降至0时
     */
    void JustDied(Unit* /*killer*/) override
    {
        _JustDied();
        Talk(SAY_DEATH);
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 距离上次更新的时间间隔（毫秒）
     *
     * 主循环函数，负责：
     * - 检查护盾状态（护盾存在时不攻击）
     * - 处理事件定时器
     * - 施放攻击法术
     *
     * @调用时机 每个游戏帧（约每50毫秒）
     * @性能注意事项 在护盾阶段跳过大部分AI逻辑
     */
    void UpdateAI(uint32 diff) override
    {
        // 没有有效目标或在护盾状态下不执行攻击逻辑
        if (!UpdateVictim() || _bubbled)
            return;

        events.Update(diff);

        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        if (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_SUMMON_MINIONS:
                    // 英雄模式：召唤仆从复制品
                    DoCast(SPELL_SUMMON_MINIONS);
                    events.ScheduleEvent(EVENT_SUMMON_MINIONS, 15s);
                    break;
                case EVENT_ATTACK:
                    // 随机选择一个攻击法术对随机目标施放
                    if (Unit* victim = SelectTarget(SelectTargetMethod::Random))
                        DoCast(victim, RAND(SPELL_ARCANE_BLAST, SPELL_BLIZZARD, SPELL_FROSTBOLT, SPELL_WRATH_OF_MISERY));
                    events.ScheduleEvent(EVENT_ATTACK, 3s);
                    break;
                default:
                    break;
            }

            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }
    }

    /**
     * @brief 执行动作
     * @param action 动作类型
     *
     * @调用时机 当水晶守卫被击杀时，由副本脚本调用
     */
    void DoAction(int32 action) override
    {
        if (action == ACTION_CRYSTAL_HANDLER_DIED)
            CrystalHandlerDied();
    }

    /**
     * @brief 视线内移动检测
     * @param who 进入视线范围的单位
     *
     * @调用时机 当有单位进入诺沃斯的视线范围时
     * @note 用于检测成就"Oh Novos!"的失败条件
     */
    void MoveInLineOfSight(Unit* who) override
    {
        BossAI::MoveInLineOfSight(who);

        // 检测召唤的小怪是否越过Y坐标阈值
        if (!_ohNovos || !who || who->GetTypeId() != TYPEID_UNIT || who->GetPositionY() > MAX_Y_COORD_OH_NOVOS)
            return;

        uint32 entry = who->GetEntry();
        if (entry == NPC_HULKING_CORPSE || entry == NPC_RISEN_SHADOWCASTER || entry == NPC_FETID_TROLL_CORPSE)
            _ohNovos = false;  ///< 成就失败
    }

    /**
     * @brief 获取自定义数据
     * @param type 数据类型标识
     * @return 返回请求的数据值
     *
     * 用于成就系统查询状态
     */
    uint32 GetData(uint32 type) const override
    {
        return type == DATA_NOVOS_ACHIEV && _ohNovos ? 1 : 0;
    }

    /**
     * @brief 召唤生物回调
     * @param summon 被召唤的生物对象
     *
     * @调用时机 当诺沃斯召唤小怪时
     */
    void JustSummoned(Creature* summon) override
    {
        summons.Summon(summon);
    }

private:
    /**
     * @brief 设置护盾状态
     * @param state true为激活护盾，false为移除护盾
     *
     * @note 护盾激活时诺沃斯处于无敌状态
     */
    void SetBubbled(bool state)
    {
        _bubbled = state;
        if (!state)
        {
            // 移除护盾：取消无敌标志，停止施法
            if (me->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE))
                me->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
            if (me->HasUnitState(UNIT_STATE_CASTING))
                me->CastStop();
        }
        else
        {
            // 激活护盾：添加无敌标志，施放奥术护盾
            if (!me->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE))
                me->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
            DoCast(SPELL_ARCANE_FIELD);
        }
    }

    /**
     * @brief 设置召唤者状态
     * @param active true为激活召唤，false为停止召唤
     *
     * @note 控制4个召唤点的召唤行为
     */
    void SetSummonerStatus(bool active)
    {
        for (uint8 i = 0; i < 4; i++)
            if (ObjectGuid guid = instance->GetGuidData(summoners[i].data))
                if (Creature* crystalChannelTarget = ObjectAccessor::GetCreature(*me, guid))
                {
                    if (active)
                        crystalChannelTarget->AI()->SetData(summoners[i].spell, summoners[i].timer);
                    else
                        crystalChannelTarget->AI()->Reset();
                }
    }

    /**
     * @brief 设置水晶状态
     * @param active true为激活，false为关闭
     *
     * @note 控制4个水晶的激活状态和光束效果
     */
    void SetCrystalsStatus(bool active)
    {
        for (uint8 i = 0; i < 4; i++)
            if (ObjectGuid guid = instance->GetGuidData(DATA_NOVOS_CRYSTAL_1 + i))
                if (GameObject* crystal = ObjectAccessor::GetGameObject(*me, guid))
                    SetCrystalStatus(crystal, active);
    }

    /**
     * @brief 设置单个水晶状态
     * @param crystal 水晶游戏对象
     * @param active true为激活，false为关闭
     *
     * @note 控制水晶的视觉状态和光束效果
     */
    void SetCrystalStatus(GameObject* crystal, bool active)
    {
        crystal->SetGoState(active ? GO_STATE_ACTIVE : GO_STATE_READY);
        if (Creature* crystalChannelTarget = crystal->FindNearestCreature(NPC_CRYSTAL_CHANNEL_TARGET, 5.0f))
        {
            if (active)
                crystalChannelTarget->CastSpell(nullptr, SPELL_BEAM_CHANNEL);
            else if (crystalChannelTarget->HasUnitState(UNIT_STATE_CASTING))
                crystalChannelTarget->CastStop();
        }
    }

    /**
     * @brief 处理水晶守卫死亡
     *
     * 当水晶守卫被击杀时：
     * 1. 关闭一个水晶
     * 2. 增加击杀计数
     * 3. 如果击杀了4个守卫，移除护盾并开始战斗阶段
     */
    void CrystalHandlerDied()
    {
        // 找到第一个激活的水晶并关闭它
        for (uint8 i = 0; i < 4; i++)
            if (ObjectGuid guid = instance->GetGuidData(DATA_NOVOS_CRYSTAL_1 + i))
                if (GameObject* crystal = ObjectAccessor::GetGameObject(*me, guid))
                    if (crystal->GetGoState() == GO_STATE_ACTIVE)
                    {
                        SetCrystalStatus(crystal, false);
                        break;
                    }

        // 增加击杀计数，检查是否达到4个
        if (++_crystalHandlerCount >= 4)
        {
            // 所有水晶守卫被击杀，护盾消失
            Talk(SAY_ARCANE_FIELD);
            SetSummonerStatus(false);  ///< 停止召唤
            SetBubbled(false);          ///< 移除护盾
            events.ScheduleEvent(EVENT_ATTACK, 3s);  ///< 开始攻击
            if (IsHeroic())
                events.ScheduleEvent(EVENT_SUMMON_MINIONS, 15s);  ///< 英雄模式额外召唤
        }
        else
        {
            // 重新安排召唤下一个水晶守卫
            if (ObjectGuid guid = instance->GetGuidData(DATA_NOVOS_SUMMONER_4))
                if (Creature* crystalChannelTarget = ObjectAccessor::GetCreature(*me, guid))
                    crystalChannelTarget->AI()->SetData(SPELL_SUMMON_CRYSTAL_HANDLER, 15000);
        }
    }

    uint8 _crystalHandlerCount;  ///< 水晶守卫击杀计数
    bool _ohNovos;               ///< 成就标志：true表示召唤的小怪未越过阈值线
    bool _bubbled;               ///< 护盾状态：true表示护盾激活中
};

/**
 * @brief 水晶通道目标AI结构体
 *
 * 用于管理召唤点的召唤行为
 */
struct npc_crystal_channel_target : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_crystal_channel_target(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
    }

    /**
     * @brief 初始化成员变量
     */
    void Initialize()
    {
        _spell = 0;    ///< 要施放的法术ID
        _timer = 0;    ///< 施法间隔时间
        _temp = 0;     ///< 当前计时器
    }

    /**
     * @brief 重置AI
     */
    void Reset() override
    {
        Initialize();
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 距离上次更新的时间间隔（毫秒）
     *
     * @note 按设定的间隔时间周期性施放召唤法术
     */
    void UpdateAI(uint32 diff) override
    {
        if (_spell)
        {
            if (_temp <= diff)
            {
                DoCast(_spell);  ///< 施放召唤法术
                _temp = _timer;
            }
            else
                _temp -= diff;
        }
    }

    /**
     * @brief 设置自定义数据
     * @param id 法术ID
     * @param value 施法间隔时间
     *
     * @调用时机 由诺沃斯AI调用，启动召唤
     */
    void SetData(uint32 id, uint32 value) override
    {
        _spell = id;
        _timer = value;
        _temp = value;
    }

    /**
     * @brief 召唤生物回调
     * @param summon 被召唤的生物对象
     *
     * @调用时机 当召唤法术成功召唤生物时
     * @note 让召唤物沿预设路径移动
     */
    void JustSummoned(Creature* summon) override
    {
        // 通知诺沃斯有新的召唤物
        if (InstanceScript* instance = me->GetInstanceScript())
            if (ObjectGuid guid = instance->GetGuidData(DATA_NOVOS))
                if (Creature* novos = ObjectAccessor::GetCreature(*me, guid))
                    novos->AI()->JustSummoned(summon);

        // 让召唤物沿路径移动
        if (summon)
            summon->GetMotionMaster()->MovePath(summon->GetEntry() * 100, false);

        // 如果是水晶守卫，重置召唤器（等待下一个）
        if (_spell == SPELL_SUMMON_CRYSTAL_HANDLER)
            Reset();
    }

private:
    uint32 _spell;   ///< 要施放的法术ID
    uint32 _timer;   ///< 施法间隔时间（毫秒）
    uint32 _temp;    ///< 当前计时器（毫秒）
};

/**
 * @brief "Oh Novos!"成就脚本
 *
 * 检测玩家是否在没有任何召唤的小怪越过指定线的情况下击败诺沃斯
 */
class achievement_oh_novos : public AchievementCriteriaScript
{
public:
    /**
     * @brief 构造函数
     */
    achievement_oh_novos() : AchievementCriteriaScript("achievement_oh_novos") { }

    /**
     * @brief 检查成就条件
     * @param player 玩家对象（未使用）
     * @param target 目标单位（应该是诺沃斯）
     * @return true表示成就条件满足
     */
    bool OnCheck(Player* /*player*/, Unit* target) override
    {
        return target && target->GetTypeId() == TYPEID_UNIT && target->ToCreature()->AI()->GetData(DATA_NOVOS_ACHIEV);
    }
};

/**
 * @brief 召唤仆从法术脚本（59910）
 *
 * 英雄模式专属：召唤2个仆从复制品
 */
class spell_novos_summon_minions : public SpellScript
{
    PrepareSpellScript(spell_novos_summon_minions);

    /**
     * @brief 验证法术依赖
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_SUMMON_COPY_OF_MINIONS });
    }

    /**
     * @brief 处理脚本效果
     * @param effIndex 效果索引
     *
     * @调用时机 法术施放时
     * @note 召唤2个仆从复制品
     */
    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        for (uint8 i = 0; i < 2; ++i)
            GetCaster()->CastSpell(nullptr, SPELL_SUMMON_COPY_OF_MINIONS, true);
    }

    /**
     * @brief 注册法术效果处理函数
     */
    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_novos_summon_minions::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/**
 * @brief 注册脚本
 *
 * 将所有脚本注册到脚本系统中
 */
void AddSC_boss_novos()
{
    RegisterDrakTharonKeepCreatureAI(boss_novos);
    RegisterDrakTharonKeepCreatureAI(npc_crystal_channel_target);
    RegisterSpellScript(spell_novos_summon_minions);
    new achievement_oh_novos();
}
