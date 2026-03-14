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
 * @file    pilgrims_bounty.cpp
 * @brief   感恩节事件脚本模块
 *
 * 本模块实现了感恩节（Pilgrim's Bounty）节日相关的游戏机制，包括：
 * - 节日食物增益效果：各种节日特制食物提供的增益效果
 * - 盛宴享用：玩家可以坐在餐桌旁享用各种节日美食
 * - 火鸡猎人：击杀火鸡获得成就
 * - 分享精神：品尝所有五种节日美食获得特殊奖励
 * - 餐桌互动：在餐桌上传递食物给其他玩家
 *
 * 感恩节是一个美国传统节日主题活动，玩家可以通过完成
 * 各种节日任务和活动获得成就和奖励。
 */

#include "ScriptMgr.h"
#include "CreatureAIImpl.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellAuraEffects.h"
#include "SpellScript.h"
#include "Vehicle.h"

/**
 * @brief 感恩节食物增益法术ID枚举
 *
 * 定义了感恩节食物提供的各种增益效果法术标识符
 */
enum PilgrimsBountyBuffFood
{
    // Pilgrims Bounty Buff Food
    SPELL_WELL_FED_AP_TRIGGER       = 65414, ///< 攻击强度增益触发
    SPELL_WELL_FED_ZM_TRIGGER       = 65412, ///< 法术伤害增益触发
    SPELL_WELL_FED_HIT_TRIGGER      = 65416, ///< 命中等级增益触发
    SPELL_WELL_FED_HASTE_TRIGGER    = 65410, ///< 急速等级增益触发
    SPELL_WELL_FED_SPIRIT_TRIGGER   = 65415  ///< 精神属性增益触发
};

/**
 * @class spell_pilgrims_bounty_buff_food
 * @brief 感恩节食物增益光环脚本 - 处理节日食物的周期性增益触发
 *
 * 当玩家食用感恩节特制食物时，这个光环会周期性地触发
 * 对应的增益效果。使用 _handled 标志确保每次进食只触发一次增益。
 */
class spell_pilgrims_bounty_buff_food : public AuraScript
{
    PrepareAuraScript(spell_pilgrims_bounty_buff_food);
private:
    uint32 const _triggeredSpellId; ///< 要触发的增益法术ID

public:
    /**
     * @brief 构造函数
     * @param triggeredSpellId 要触发的增益法术ID
     */
    spell_pilgrims_bounty_buff_food(uint32 triggeredSpellId) : AuraScript(), _triggeredSpellId(triggeredSpellId)
    {
        _handled = false;
    }

    /**
     * @brief 处理周期性触发法术效果
     * @param aurEff 光环效果（未使用）
     *
     * 阻止默认的周期性触发行为，改为只触发一次指定的增益法术。
     * 使用 _handled 标志防止重复触发。
     */
    void HandleTriggerSpell(AuraEffect const* /*aurEff*/)
    {
        PreventDefaultAction();
        if (_handled)
            return;

        _handled = true;
        GetTarget()->CastSpell(GetTarget(), _triggeredSpellId, true);
    }

    void Register() override
    {
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_pilgrims_bounty_buff_food::HandleTriggerSpell, EFFECT_2, SPELL_AURA_PERIODIC_TRIGGER_SPELL);
    }

    bool _handled; ///< 是否已触发过增益（防止重复触发）
};

/**
 * @brief 盛宴享用相关法术ID枚举
 *
 * 定义了各种感恩节美食的法术标识符
 */
enum FeastOnSpells
{
    FEAST_ON_TURKEY                     = 61784, ///< 享用火鸡
    FEAST_ON_CRANBERRIES                = 61785, ///< 享用蔓越莓
    FEAST_ON_SWEET_POTATOES             = 61786, ///< 享用甜土豆
    FEAST_ON_PIE                        = 61787, ///< 享用派
    FEAST_ON_STUFFING                   = 61788, ///< 享用填料
    SPELL_CRANBERRY_HELPINS             = 61841, ///< 蔓越莓帮助
    SPELL_TURKEY_HELPINS                = 61842, ///< 火鸡帮助
    SPELL_STUFFING_HELPINS              = 61843, ///< 填料帮助
    SPELL_SWEET_POTATO_HELPINS          = 61844, ///< 甜土豆帮助
    SPELL_PIE_HELPINS                   = 61845, ///< 派帮助
    SPELL_ON_PLATE_EAT_VISUAL           = 61826  ///< 盘中食物进食视觉效果
};

/**
 * @class spell_pilgrims_bounty_feast_on
 * @brief 盛宴享用法术脚本 - 处理玩家在餐桌上享用节日美食
 *
 * 当玩家在感恩节餐桌上享用各种美食时：
 * 1. 根据食物类型给予对应的增益光环
 * 2. 播放进食动画效果
 * 3. 减少餐桌上该食物的堆叠数量
 */
/* 61784 - Feast On Turkey
   61785 - Feast On Cranberries
   61786 - Feast On Sweet Potatoes
   61787 - Feast On Pie
   61788 - Feast On Stuffing */
class spell_pilgrims_bounty_feast_on : public SpellScript
{
    PrepareSpellScript(spell_pilgrims_bounty_feast_on);

    /**
     * @brief 处理虚拟效果 - 给予食物增益并消耗食物
     * @param effIndex 效果索引（未使用）
     *
     * 根据当前法术ID确定食物类型，然后：
     * 1. 找到坐在椅子上的玩家
     * 2. 施放进食视觉效果
     * 3. 给予对应的"帮助"增益
     * 4. 减少食物堆叠层数
     */
    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();

        // 根据法术ID确定要给予的增益法术
        uint32 _spellId = 0;
        switch (GetSpellInfo()->Id)
        {
            case FEAST_ON_TURKEY:
                _spellId = SPELL_TURKEY_HELPINS;
                break;
            case FEAST_ON_CRANBERRIES:
                _spellId = SPELL_CRANBERRY_HELPINS;
                break;
            case FEAST_ON_SWEET_POTATOES:
                _spellId = SPELL_SWEET_POTATO_HELPINS;
                break;
            case FEAST_ON_PIE:
                _spellId = SPELL_PIE_HELPINS;
                break;
            case FEAST_ON_STUFFING:
                _spellId = SPELL_STUFFING_HELPINS;
                break;
            default:
                return;
        }

        // 找到坐在餐桌椅子上的玩家（第一位乘客）
        if (Vehicle* vehicle = caster->GetVehicleKit())
            if (Unit* target = vehicle->GetPassenger(0))
                if (Player* player = target->ToPlayer())
                {
                    // 施放进食视觉效果和食物增益
                    player->CastSpell(player, SPELL_ON_PLATE_EAT_VISUAL, true);
                    caster->CastSpell(player, _spellId, player->GetGUID());
                }

        // 减少食物堆叠层数
        if (Aura* aura = caster->GetAura(GetEffectValue()))
        {
            // 当堆叠层数为1时，移除相关的光环
            if (aura->GetStackAmount() == 1)
                caster->RemoveAurasDueToSpell(aura->GetSpellInfo()->GetEffect(EFFECT_0).CalcValue());
            aura->ModStackAmount(-1);
        }
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_pilgrims_bounty_feast_on::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

/**
 * @brief 火鸡猎人相关数据枚举
 *
 * 定义了火鸡猎人成就所需的法术和表情标识符
 */
enum TheTurkinator
{
    SPELL_KILL_COUNTER_VISUAL       = 62015, ///< 击杀计数视觉效果
    SPELL_KILL_COUNTER_VISUAL_MAX   = 62021, ///< 击杀计数最大值视觉效果
    EMOTE_TURKEY_HUNTER             = 0,     ///< 火鸡猎人表情（10杀）
    EMOTE_TURKEY_DOMINATION         = 1,     ///< 火鸡统治表情（20杀）
    EMOTE_TURKEY_SLAUGHTER          = 2,     ///< 火鸡屠杀表情（30杀）
    EMOTE_TURKEY_TRIUMPH            = 3      ///< 火鸡胜利表情（40杀）
};

/**
 * @class spell_pilgrims_bounty_turkey_tracker
 * @brief 火鸡追踪器法术脚本 - 追踪玩家击杀火鸡的数量
 *
 * 用于"火鸡猎人"成就。当玩家击杀火鸡时，此法术会追踪击杀数量，
 * 并在达到特定里程碑（10/20/30/40杀）时播放相应的表情提示。
 * 达到40杀时完成成就。
 */
// 62014 - Turkey Tracker
class spell_pilgrims_bounty_turkey_tracker : public SpellScript
{
    PrepareSpellScript(spell_pilgrims_bounty_turkey_tracker);

    /**
     * @brief 验证所需法术是否存在
     * @param spell 法术信息（未使用）
     * @return 所有依赖法术都存在返回true
     */
    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo({ SPELL_KILL_COUNTER_VISUAL, SPELL_KILL_COUNTER_VISUAL_MAX });
    }

    /**
     * @brief 处理脚本效果 - 更新击杀计数并显示表情
     * @param effIndex 效果索引（未使用）
     *
     * 检查当前击杀堆叠层数，在达到里程碑时：
     * - 10杀：显示"火鸡猎人"表情
     * - 20杀：显示"火鸡统治"表情
     * - 30杀：显示"火鸡屠杀"表情
     * - 40杀：显示"火鸡胜利"表情，完成成就
     */
    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        Creature* caster = GetCaster()->ToCreature();
        Unit* target = GetHitUnit();

        if (!target || !caster)
            return;

        // 如果已经达到最大值，不再处理
        if (target->HasAura(SPELL_KILL_COUNTER_VISUAL_MAX))
            return;

        if (Aura const* aura = target->GetAura(GetSpellInfo()->Id))
        {
            switch (aura->GetStackAmount())
            {
                case 10:
                    // 10杀里程碑
                    caster->AI()->Talk(EMOTE_TURKEY_HUNTER, target);
                    break;
                case 20:
                    // 20杀里程碑
                    caster->AI()->Talk(EMOTE_TURKEY_DOMINATION, target);
                    break;
                case 30:
                    // 30杀里程碑
                    caster->AI()->Talk(EMOTE_TURKEY_SLAUGHTER, target);
                    break;
                case 40:
                    // 40杀里程碑 - 完成成就
                    caster->AI()->Talk(EMOTE_TURKEY_TRIUMPH, target);
                    target->CastSpell(target, SPELL_KILL_COUNTER_VISUAL_MAX, true);
                    target->RemoveAurasDueToSpell(GetSpellInfo()->Id);
                    break;
                default:
                    return;
            }
            // 显示击杀计数视觉效果
            target->CastSpell(target, SPELL_KILL_COUNTER_VISUAL, true);
        }
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_pilgrims_bounty_turkey_tracker::HandleScript, EFFECT_1, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/**
 * @brief 分享精神相关法术枚举
 *
 * 定义了"分享精神"成就相关的法术标识符
 */
enum SpiritOfSharing
{
    SPELL_THE_SPIRIT_OF_SHARING    = 61849 ///< 分享精神光环
};

/**
 * @class spell_pilgrims_bounty_well_fed
 * @brief 感恩节饱腹法术脚本 - 处理节日食物的"吃饱了"增益和分享精神成就
 *
 * 当玩家食用感恩节食物获得"吃饱了"状态时：
 * 1. 在堆叠达到5层时给予对应的属性增益
 * 2. 检查是否所有五种食物都吃到了5份
 * 3. 如果五种食物都达标，给予"分享精神"成就奖励
 */
class spell_pilgrims_bounty_well_fed : public SpellScript
{
    PrepareSpellScript(spell_pilgrims_bounty_well_fed);

    uint32 _triggeredSpellId; ///< 要触发的属性增益法术ID

public:
    /**
     * @brief 构造函数
     * @param triggeredSpellId 要触发的属性增益法术ID
     */
    spell_pilgrims_bounty_well_fed(uint32 triggeredSpellId) : SpellScript(), _triggeredSpellId(triggeredSpellId) { }

private:
    /**
     * @brief 验证所需法术是否存在
     * @param spell 法术信息（未使用）
     * @return 所有依赖法术都存在返回true
     */
    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo({ _triggeredSpellId });
    }

    /**
     * @brief 处理脚本效果 - 给予属性增益并检查分享精神成就
     * @param effIndex 效果索引
     *
     * 执行以下逻辑：
     * 1. 如果当前食物吃到5份，给予对应的属性增益
     * 2. 检查所有五种食物是否都吃到了5份
     * 3. 如果满足条件，施放"分享精神"光环并清理所有食物计数
     */
    void HandleScript(SpellEffIndex effIndex)
    {
        PreventHitDefaultEffect(effIndex);
        Player* target = GetHitPlayer();
        if (!target)
            return;

        // 当某种食物吃到5份时，给予对应的属性增益
        if (Aura const* aura = target->GetAura(GetSpellInfo()->Id))
        {
            if (aura->GetStackAmount() == 5)
                target->CastSpell(target, _triggeredSpellId, true);
        }

        // 检查所有五种食物的堆叠层数
        Aura const* turkey = target->GetAura(SPELL_TURKEY_HELPINS);
        Aura const* cranberies = target->GetAura(SPELL_CRANBERRY_HELPINS);
        Aura const* stuffing = target->GetAura(SPELL_STUFFING_HELPINS);
        Aura const* sweetPotatoes = target->GetAura(SPELL_SWEET_POTATO_HELPINS);
        Aura const* pie = target->GetAura(SPELL_PIE_HELPINS);

        // 如果五种食物都吃到了5份，给予"分享精神"奖励
        if ((turkey && turkey->GetStackAmount() == 5) && (cranberies && cranberies->GetStackAmount() == 5) && (stuffing && stuffing->GetStackAmount() == 5)
            && (sweetPotatoes && sweetPotatoes->GetStackAmount() == 5) && (pie && pie->GetStackAmount() == 5))
        {
            target->CastSpell(target, SPELL_THE_SPIRIT_OF_SHARING, true);
            // 清理所有食物计数光环
            target->RemoveAurasDueToSpell(SPELL_TURKEY_HELPINS);
            target->RemoveAurasDueToSpell(SPELL_CRANBERRY_HELPINS);
            target->RemoveAurasDueToSpell(SPELL_STUFFING_HELPINS);
            target->RemoveAurasDueToSpell(SPELL_SWEET_POTATO_HELPINS);
            target->RemoveAurasDueToSpell(SPELL_PIE_HELPINS);
        }
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_pilgrims_bounty_well_fed::HandleScript, EFFECT_1, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/**
 * @brief 丰盛宴席相关数据枚举
 *
 * 定义了感恩节餐桌（丰盛宴席）相关的座位、NPC和法术标识符
 */
enum BountifulTableMisc
{
    SEAT_PLAYER                             = 0,     ///< 玩家座位索引
    SEAT_PLATE_HOLDER                       = 6,     ///< 盘子支架座位索引
    NPC_BOUNTIFUL_TABLE                     = 32823, ///< 丰盛宴席NPC ID
    SPELL_ON_PLATE_TURKEY                   = 61928, ///< 盘中火鸡
    SPELL_ON_PLATE_CRANBERRIES              = 61925, ///< 盘中蔓越莓
    SPELL_ON_PLATE_STUFFING                 = 61927, ///< 盘中填料
    SPELL_ON_PLATE_SWEET_POTATOES           = 61929, ///< 盘中甜土豆
    SPELL_ON_PLATE_PIE                      = 61926, ///< 盘中派
    SPELL_PASS_THE_TURKEY                   = 66373, ///< 传递火鸡
    SPELL_PASS_THE_CRANBERRIES              = 66372, ///< 传递蔓越莓
    SPELL_PASS_THE_STUFFING                 = 66375, ///< 传递填料
    SPELL_PASS_THE_SWEET_POTATOES           = 66376, ///< 传递甜土豆
    SPELL_PASS_THE_PIE                      = 66374, ///< 传递派
    SPELL_ON_PLATE_VISUAL_PIE               = 61825, ///< 盘中派视觉效果
    SPELL_ON_PLATE_VISUAL_CRANBERRIES       = 61821, ///< 盘中蔓越莓视觉效果
    SPELL_ON_PLATE_VISUAL_POTATOES          = 61824, ///< 盘中甜土豆视觉效果
    SPELL_ON_PLATE_VISUAL_TURKEY            = 61822, ///< 盘中火鸡视觉效果
    SPELL_ON_PLATE_VISUAL_STUFFING          = 61823, ///< 盘中填料视觉效果
    SPELL_A_SERVING_OF_CRANBERRIES_PLATE    = 61833, ///< 一份蔓越莓（盘子）
    SPELL_A_SERVING_OF_TURKEY_PLATE         = 61835, ///< 一份火鸡（盘子）
    SPELL_A_SERVING_OF_STUFFING_PLATE       = 61836, ///< 一份填料（盘子）
    SPELL_A_SERVING_OF_SWEET_POTATOES_PLATE = 61837, ///< 一份甜土豆（盘子）
    SPELL_A_SERVING_OF_PIE_PLATE            = 61838, ///< 一份派（盘子）
    SPELL_A_SERVING_OF_CRANBERRIES_CHAIR    = 61804, ///< 一份蔓越莓（椅子）
    SPELL_A_SERVING_OF_TURKEY_CHAIR         = 61807, ///< 一份火鸡（椅子）
    SPELL_A_SERVING_OF_STUFFING_CHAIR       = 61806, ///< 一份填料（椅子）
    SPELL_A_SERVING_OF_SWEET_POTATOES_CHAIR = 61808, ///< 一份甜土豆（椅子）
    SPELL_A_SERVING_OF_PIE_CHAIR            = 61805  ///< 一份派（椅子）
};

/**
 * @class spell_pilgrims_bounty_on_plate
 * @brief 盘中食物法术脚本 - 处理在餐桌上传递食物给其他玩家
 *
 * 这个脚本处理感恩节餐桌上的食物传递机制：
 * 1. 玩家可以将自己盘子里的食物传递给同一桌的其他玩家
 * 2. 支持"分享即关爱"成就
 * 3. 支持"食物大战"场景（直接对其他玩家投掷食物）
 */
/* 66250 - Pass The Turkey
   66259 - Pass The Stuffing
   66260 - Pass The Pie
   66261 - Pass The Cranberries
   66262 - Pass The Sweet Potatoes */
class spell_pilgrims_bounty_on_plate : public SpellScript
{
    PrepareSpellScript(spell_pilgrims_bounty_on_plate);

    uint32 _triggeredSpellId1; ///< 传递给玩家的法术ID（食物大战）
    uint32 _triggeredSpellId2; ///< "分享即关爱"成就积分法术ID
    uint32 _triggeredSpellId3; ///< 盘中食物视觉效果法术ID
    uint32 _triggeredSpellId4; ///< 椅子可进食法术ID

public:
    /**
     * @brief 构造函数
     * @param triggeredSpellId1 传递给玩家的法术ID
     * @param triggeredSpellId2 分享成就积分法术ID
     * @param triggeredSpellId3 盘中视觉效果法术ID
     * @param triggeredSpellId4 椅子可进食法术ID
     */
    spell_pilgrims_bounty_on_plate(uint32 triggeredSpellId1, uint32 triggeredSpellId2, uint32 triggeredSpellId3, uint32 triggeredSpellId4) : SpellScript(),
        _triggeredSpellId1(triggeredSpellId1), _triggeredSpellId2(triggeredSpellId2), _triggeredSpellId3(triggeredSpellId3), _triggeredSpellId4(triggeredSpellId4) { }

private:
    /**
     * @brief 验证所需法术是否存在
     * @param spell 法术信息（未使用）
     * @return 所有依赖法术都存在返回true
     */
    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo(
        {
            _triggeredSpellId1,
            _triggeredSpellId2,
            _triggeredSpellId3,
            _triggeredSpellId4
        });
    }

    /**
     * @brief 获取目标所在的餐桌载具
     * @param target 目标单位
     * @return 如果目标在餐桌上返回载具指针，否则返回nullptr
     */
    Vehicle* GetTable(Unit* target)
    {
        if (target->GetTypeId() == TYPEID_PLAYER)
        {
            // 玩家坐在椅子上，椅子是餐桌的乘客
            if (Unit* vehBase = target->GetVehicleBase())
                if (Vehicle* table = vehBase->GetVehicle())
                    if (table->GetCreatureEntry() == NPC_BOUNTIFUL_TABLE)
                        return table;
        }
        else if (Vehicle* veh = target->GetVehicle())
            if (veh->GetCreatureEntry() == NPC_BOUNTIFUL_TABLE)
                return veh;

        return nullptr;
    }

    /**
     * @brief 获取指定座位的盘子单位
     * @param table 餐桌载具
     * @param seat 座位索引
     * @return 盘子单位指针，如果不存在返回nullptr
     */
    Unit* GetPlateInSeat(Vehicle* table, uint8 seat)
    {
        if (Unit* holderUnit = table->GetPassenger(SEAT_PLATE_HOLDER))
            if (Vehicle* holder = holderUnit->GetVehicleKit())
                if (Unit* plate = holder->GetPassenger(seat))
                    return plate;

        return nullptr;
    }

    /**
     * @brief 处理虚拟效果 - 传递食物
     * @param effIndex 效果索引（未使用）
     *
     * 执行食物传递逻辑：
     * 1. 验证施法者和目标在同一餐桌
     * 2. 给予"分享即关爱"成就积分
     * 3. 如果目标是玩家：执行食物大战（直接对玩家施放食物）
     * 4. 如果目标是椅子：将食物放到目标的盘子里
     */
    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();
        if (!target || caster == target)
            return;

        // 确保施法者和目标在同一餐桌
        Vehicle* table = GetTable(caster);
        if (!table || table != GetTable(target))
            return;

        if (Vehicle* casterChair = caster->GetVehicleKit())
            if (Unit* casterPlr = casterChair->GetPassenger(SEAT_PLAYER))
            {
                if (casterPlr == target)
                    return;

                // 给予"分享即关爱"成就积分（总是）
                casterPlr->CastSpell(casterPlr, _triggeredSpellId2, true);

                // 获取目标座位索引
                uint8 seat = target->GetTransSeat();
                if (target->GetTypeId() == TYPEID_PLAYER && target->GetVehicleBase())
                    seat = target->GetVehicleBase()->GetTransSeat();

                if (Unit* plate = GetPlateInSeat(table, seat))
                {
                    if (target->GetTypeId() == TYPEID_PLAYER)
                    {
                        // 食物大战场景：直接对玩家施放食物
                        casterPlr->CastSpell(target, _triggeredSpellId1, true);
                        caster->CastSpell(target->GetVehicleBase(), _triggeredSpellId4, true);
                    }
                    else
                    {
                        // 正常传递：将食物放到目标的盘子里
                        casterPlr->CastSpell(plate, _triggeredSpellId3, true);
                        caster->CastSpell(target, _triggeredSpellId4, true);
                    }
                }
            }
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_pilgrims_bounty_on_plate::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

/**
 * @class spell_pilgrims_bounty_a_serving_of
 * @brief 一份食物光环脚本 - 管理玩家盘子里的食物视觉效果
 *
 * 当玩家获得"一份食物"光环时，会在玩家面前的盘子上显示相应的食物视觉效果。
 * 光环移除时，视觉效果也会随之移除。
 */
/* 61804 - A Serving of Cranberries
   61805 - A Serving of Pie
   61806 - A Serving of Stuffing
   61807 - A Serving of Turkey
   61808 - A Serving of Sweet Potatoes
   61793 - Cranberry Server
   61794 - Pie Server
   61795 - Stuffing Server
   61796 - Turkey Server
   61797 - Sweet Potatoes Server */
class spell_pilgrims_bounty_a_serving_of : public AuraScript
{
    PrepareAuraScript(spell_pilgrims_bounty_a_serving_of);

    uint32 _triggeredSpellId; ///< 盘中视觉效果法术ID

public:
    /**
     * @brief 构造函数
     * @param triggeredSpellId 盘中视觉效果法术ID
     */
    spell_pilgrims_bounty_a_serving_of(uint32 triggeredSpellId) : AuraScript(), _triggeredSpellId(triggeredSpellId) { }

private:
    /**
     * @brief 验证所需法术是否存在
     * @param spell 法术信息（未使用）
     * @return 所有依赖法术都存在返回true
     */
    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo({ _triggeredSpellId });
    }

    /**
     * @brief 光环应用时的处理 - 显示盘中食物视觉效果
     * @param aurEff 光环效果
     * @param mode 处理模式（未使用）
     */
    void OnApply(AuraEffect const* aurEff, AuraEffectHandleModes /*mode*/)
    {
        Unit* target = GetTarget();
        target->CastSpell(target, uint32(aurEff->GetAmount()), true);
        HandlePlate(target, true);
    }

    /**
     * @brief 光环移除时的处理 - 移除盘中食物视觉效果
     * @param aurEff 光环效果
     * @param mode 处理模式（未使用）
     */
    void OnRemove(AuraEffect const* aurEff, AuraEffectHandleModes /*mode*/)
    {
        Unit* target = GetTarget();
        target->RemoveAurasDueToSpell(aurEff->GetAmount());
        HandlePlate(target, false);
    }

    /**
     * @brief 处理盘子视觉效果
     * @param target 目标单位
     * @param apply true表示应用效果，false表示移除效果
     *
     * 在玩家面前的盘子上应用或移除食物视觉效果
     */
    void HandlePlate(Unit* target, bool apply)
    {
        if (Vehicle* table = target->GetVehicle())
            if (Unit* holderUnit = table->GetPassenger(SEAT_PLATE_HOLDER))
                if (Vehicle* holder = holderUnit->GetVehicleKit())
                    if (Unit* plate = holder->GetPassenger(target->GetTransSeat()))
                    {
                        if (apply)
                            target->CastSpell(plate, _triggeredSpellId, true);
                        else
                            plate->RemoveAurasDueToSpell(_triggeredSpellId);
                    }
    }

    void Register() override
    {
        AfterEffectApply += AuraEffectApplyFn(spell_pilgrims_bounty_a_serving_of::OnApply, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
        OnEffectRemove += AuraEffectRemoveFn(spell_pilgrims_bounty_a_serving_of::OnRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

/**
 * @brief 注册感恩节事件法术脚本
 *
 * 此函数在服务器启动时被调用，用于注册所有感恩节相关的法术脚本。
 * 使用RegisterSpellScriptWithArgs宏注册需要参数的法术脚本。
 */
void AddSC_event_pilgrims_bounty()
{
    // 注册节日食物增益脚本
    RegisterSpellScriptWithArgs(spell_pilgrims_bounty_buff_food, "spell_gen_slow_roasted_turkey", SPELL_WELL_FED_AP_TRIGGER);
    RegisterSpellScriptWithArgs(spell_pilgrims_bounty_buff_food, "spell_gen_cranberry_chutney", SPELL_WELL_FED_ZM_TRIGGER);
    RegisterSpellScriptWithArgs(spell_pilgrims_bounty_buff_food, "spell_gen_spice_bread_stuffing", SPELL_WELL_FED_HIT_TRIGGER);
    RegisterSpellScriptWithArgs(spell_pilgrims_bounty_buff_food, "spell_gen_pumpkin_pie", SPELL_WELL_FED_SPIRIT_TRIGGER);
    RegisterSpellScriptWithArgs(spell_pilgrims_bounty_buff_food, "spell_gen_candied_sweet_potato", SPELL_WELL_FED_HASTE_TRIGGER);
    // 注册盛宴享用脚本
    RegisterSpellScript(spell_pilgrims_bounty_feast_on);
    // 注册饱腹和分享精神脚本
    RegisterSpellScriptWithArgs(spell_pilgrims_bounty_well_fed, "spell_pilgrims_bounty_well_fed_turkey", SPELL_WELL_FED_AP_TRIGGER);
    RegisterSpellScriptWithArgs(spell_pilgrims_bounty_well_fed, "spell_pilgrims_bounty_well_fed_cranberry", SPELL_WELL_FED_ZM_TRIGGER);
    RegisterSpellScriptWithArgs(spell_pilgrims_bounty_well_fed, "spell_pilgrims_bounty_well_fed_stuffing", SPELL_WELL_FED_HIT_TRIGGER);
    RegisterSpellScriptWithArgs(spell_pilgrims_bounty_well_fed, "spell_pilgrims_bounty_well_fed_sweet_potatoes", SPELL_WELL_FED_HASTE_TRIGGER);
    RegisterSpellScriptWithArgs(spell_pilgrims_bounty_well_fed, "spell_pilgrims_bounty_well_fed_pie", SPELL_WELL_FED_SPIRIT_TRIGGER);
    // 注册火鸡追踪器脚本
    RegisterSpellScript(spell_pilgrims_bounty_turkey_tracker);
    // 注册盘中食物传递脚本
    RegisterSpellScriptWithArgs(spell_pilgrims_bounty_on_plate, "spell_pilgrims_bounty_on_plate_turkey", SPELL_ON_PLATE_TURKEY, SPELL_PASS_THE_TURKEY, SPELL_ON_PLATE_VISUAL_TURKEY, SPELL_A_SERVING_OF_TURKEY_CHAIR);
    RegisterSpellScriptWithArgs(spell_pilgrims_bounty_on_plate, "spell_pilgrims_bounty_on_plate_cranberries", SPELL_ON_PLATE_CRANBERRIES, SPELL_PASS_THE_CRANBERRIES, SPELL_ON_PLATE_VISUAL_CRANBERRIES, SPELL_A_SERVING_OF_CRANBERRIES_CHAIR);
    RegisterSpellScriptWithArgs(spell_pilgrims_bounty_on_plate, "spell_pilgrims_bounty_on_plate_stuffing", SPELL_ON_PLATE_STUFFING, SPELL_PASS_THE_STUFFING, SPELL_ON_PLATE_VISUAL_STUFFING, SPELL_A_SERVING_OF_STUFFING_CHAIR);
    RegisterSpellScriptWithArgs(spell_pilgrims_bounty_on_plate, "spell_pilgrims_bounty_on_plate_sweet_potatoes", SPELL_ON_PLATE_SWEET_POTATOES, SPELL_PASS_THE_SWEET_POTATOES, SPELL_ON_PLATE_VISUAL_POTATOES, SPELL_A_SERVING_OF_SWEET_POTATOES_CHAIR);
    RegisterSpellScriptWithArgs(spell_pilgrims_bounty_on_plate, "spell_pilgrims_bounty_on_plate_pie", SPELL_ON_PLATE_PIE, SPELL_PASS_THE_PIE, SPELL_ON_PLATE_VISUAL_PIE, SPELL_A_SERVING_OF_PIE_CHAIR);
    // 注册一份食物视觉效果脚本
    RegisterSpellScriptWithArgs(spell_pilgrims_bounty_a_serving_of, "spell_pilgrims_bounty_a_serving_of_cranberries", SPELL_A_SERVING_OF_CRANBERRIES_PLATE);
    RegisterSpellScriptWithArgs(spell_pilgrims_bounty_a_serving_of, "spell_pilgrims_bounty_a_serving_of_turkey", SPELL_A_SERVING_OF_TURKEY_PLATE);
    RegisterSpellScriptWithArgs(spell_pilgrims_bounty_a_serving_of, "spell_pilgrims_bounty_a_serving_of_stuffing", SPELL_A_SERVING_OF_STUFFING_PLATE);
    RegisterSpellScriptWithArgs(spell_pilgrims_bounty_a_serving_of, "spell_pilgrims_bounty_a_serving_of_potatoes", SPELL_A_SERVING_OF_SWEET_POTATOES_PLATE);
    RegisterSpellScriptWithArgs(spell_pilgrims_bounty_a_serving_of, "spell_pilgrims_bounty_a_serving_of_pie", SPELL_A_SERVING_OF_PIE_PLATE);
}
