-- assume need logic.lua before this file

lang = 
{
	{1, 'Garen', 'ENERGY:4  Target opposing ally with cost 4 or less is killed'}
,	{2, 'Irelia', 'ENERGY:3  Target weapon you control gains +2 base attack, but may not gain any other bonus'}
,	{3, 'Varus', 'ENERGY:4  Target opposing ally is reduced to 1 health. Return the top Hunter card from your graveyard to your hand.'}
,	{4, 'Ashe', 'ENERGY:3  Target weapon you control gains +2 durability and has +2 attack until the start of your next turn.'}
,	{5, 'Zeus', 'Energy:5  All allies take 4 electrical damage'}
,	{6, 'Skarner', 'ENERGY:4  Target opposing ally takes 4 ice damage and is frozen for 2 turns'}
,	{7, 'Purist', 'ENERGY:4  Target friendly hero or ally has all enemy attachments and negative effects removed, or target attachment is removed.'}
,	{8, 'Sona', 'ENERGY:3  Target friendly hero or ally heals 3 damage'}
,	{9, 'Shen', 'ENERGY:4 Target friendly ally has ambush, stealth and haste until the start of your next turn.'}
,	{10, 'Katarina', 'ENERGY:3  Until the end of your turn, your weapons have +2 attack, and if Serena deals combat damage to a hero, that heros owner discards a card at random.'}
,	{11, 'Trundle', 'ENERGY:4  Target item or support ability is destroyed or an ally is returned to owner hand'}
,	{12, 'Darius', 'ENERGY:4  Until the end of your turn, your weapons gain +1 attack, and if Logan deals combat damage to an ally, that ally is killed'}
,	{13, 'Clinkz', 'ENERGY:3 Up to 2 different target opposing heroes or allies take 2 damage. This damage cannot be prevented by ally or armor abilities.'}
,	{14, '邪眼猎人', 'ENERGY:4  Target weapon in your hand is summoned at no cost and gains +1 base attack.'}
,	{15, 'Annie', 'ENERGY:4  Target opposing ally takes 3 fire damage. Draw a card'}
,	{16, 'Pugna', 'ENERGY:4  Target ally in your graveyard is return to play'}
,	{17, 'Warwick', 'ENERGY:3 Until the start of your next turn, your weapons have +2 attack and Darkclaw has ambush (his attacks cannot be defended).'}
,	{18, 'Warwick', 'ENERGY:3 Moonstalker and all friendly allies have stealth (they cannot be attacked) until the start of your next turn.'}
,	{19, '测试英雄1', 'ENERGY:1  All heros take 1 damage.'}
,	{20, '测试英雄2', 'ENERGY:2  Target hero take 1 fire damage.'}
-- ,	{19, '冰晶凤凰-艾尼维亚', '3法力：目标友方盟军+1基础攻击力和+3生命'}
-- ,	{20, '远古巫灵-泽拉斯', '3法力：目标2个以内不同的敌方英雄或盟军受到3点闪电伤害'}
,	{21, 'Nidalee', 'RES:2 Target oppoing ally cannot attack until the end of its controllers next turn.'}
,	{22, 'Talon', 'Ambush (attacks by this ally cannot be defended)'}
,	{23, 'Ahri', 'When Sandra is summoned,target player removes one of their resources from play if their resources are greater than or equal to yours.'}
,	{24, 'Morgana', 'When Lily is summoned, return the top item from your graveyard to your hand.'}
,	{25, 'Shaco', 'Haste ( This ally can attack and use abilities in the turn he is summoned'}
,	{26, 'Olaf', ''}
,	{27, 'Poppy', 'Protector (Allies without protector cannot be targeted)'}
,	{28, 'Xin Zhao', 'Kurt takes 1 less damage from abilities, and has +1 attack when there are 2 or more friendly allies in play'}
,	{29, 'Akali', 'Stealth (This ally canot be attacked.)'}
,	{30, 'Fiora', ''}
,	{31, 'Zyra', 'When an ally is damaged by Raven in combat, that ally base attack is reduced to 0.'}
,	{32, 'Jarvan IV', 'Friendly allies have +1 attack on your turn while Aldon is in play.'}
,	{33, 'Riven', 'RES:2 Target weapon you control gains +1 durability.'}
,	{34, 'Leona', 'RES:1 Target friendly ally gains +2 health. This ability cannot used again while that ally is in play.'}
,	{35, 'Kayle', 'When Priest of the Light is summoned, opposing heros lose 1 shadow energy, and your hero gains +1 health.'}
,	{36, 'Rammus', 'All damage to Armored Sandworm is reduced by 2'}
,	{37, 'Maokai', 'ENERGY:1 Target opposing ally is reduced to 0 base attack'}
,	{38, 'Swain', 'When Tainted Oracle is killed, draw 2 cards'}
,	{39, 'Malphite', 'When a friendly ally is killed while Earthen Protector is alive and can be damaged, that ally is returned to play with +2 base attack and +2 health, and Earthen Protector is killed.'}
,	{40, 'Pantheon', 'Protector (Allies without protector cannot be targeted) RES:3 Target other friendly ally gains +1 base attack and +1 health.'}
,	{41, 'Ziggs', 'When Deathbone is killed, its killer takes 2 damage'}
,	{42, 'Leoric', 'RES:2 Keldor has +2 attack until the start of your next turn'}
,	{43, 'Gargoyle', 'All damage to Infernal Gargolye is reduced by 1'}
,	{44, 'Yorick', ''}
,	{45, 'Thresh', 'RES:3 Target opposing hero or ally takes 4 damage.'}
,	{46, 'Renekton', 'RES:1 Chimera has +3 attack and reduced by 3 health until the start of your next turn'}
,	{47, 'Fizz', ''}
,	{48, 'Leblanc', 'When summoned draw a card'}
,	{49, 'Bloodwolf', 'Bad Wolf heals 1 damage at the start of each of its controller turns.'}
,	{50, 'Cassiopeia', 'RES:1 Target opposing ally cannot defend until the end of your turn.'}
,	{51, 'Felsteed', 'The first other friendly ally to attack during your turn does +1 damage'}
,	{52, 'Kassadin', 'Defender.(This ally attacks first when defending.)'}
,	{53, 'Necromancy', 'When Thaddeus is summoned, target opposing hero or ally takes 1 damage. RES:0 Target opposing hero or ally takes 2 damage.'}
,	{54, 'Amumu', 'When Carniboar kills an ally in combat, it gains +1 base attack and +1 health.'}
,	{55, 'Zed', 'When Shadow Knight is summoned, then top ally in your graveyard is returned to your hand.'}
,	{56, 'Singed', 'Defender. Alies Damaged by Cobra Demon are poisoned.\nENERGY:1 Target opposing ally takes 1 damage.'}
,	{57, 'Shyvana', 'Molten Destroyer deals 1 fire damage to any hero or ally that damages it.Allies that enter combat with Molten Destroyer are set ablaze.'}
,	{58, 'Tauren', 'When Brutal Minotaur is killed, its controller takes 2 damage.'}
,	{59, '循迹之狼', 'When Wulven Tracker deals combat damage, draw a card'}
,	{60, 'Cho Gath', 'When a ally is killed, Ogloth gains +1 base attack and +1 health. RES:3 Target other ally with cost less than Ogloth current attack is killed.'}
,	{61, '曲光屏障', 'Friendly allies cannot be attacked for the next 2 turns'}
,	{62, '备用武器', 'Your weapons have +1 attack while Reserve Weapon is in play. RES:0 Target weapon in your graveyard is return to play with +1 base attack and Reserve Weapon is destroyed.'}
,	{63, '战旗', 'Friendly allies have +1 attack while War Banner is in play'}
,	{64, '粉碎打击', 'Target enemy weapon or armor is destroyed'}
,	{65, '强化生命', 'Your hero has +10 health while Enrage is attached.'}
,	{66, '战士训练', 'Target friendly ally is a protector (allies without protector cannot be targeted) while Warrior Training is attached'}
,	{67, '虚弱', 'Target opposing ally has a maximum attack of 0 while Crippling Blow is attached'}
,	{68, '死灵狂暴', 'When an opposing ally is killed while Rampage is attached to your hero, your hero heals 2 damage.'}
,	{69, '破晓之盾', 'Target opposing ally takes 3 damage'}
,	{70, '狂战之怒', 'At the start of each of your turns while Blood Frenzy is attached to your hero, your hero takes 1 damage and you draw a card'}
,	{71, '火球术', 'Target opposing hero or ally takes 4 fire damage'}
,	{72, '冰封陵墓', 'Target opposing ally is frozen (cannot attack, defend and use abilities) for 3 turns'}
,	{73, '死亡毒气', 'Target opposing hero or ally is poisoned(it takes 1 poison damage at the start of each of its controllers turns).'}
,	{74, '闪电脉冲', 'Up to 2 different target opposing heroes or allies take 3 electrical damage'}
,	{75, '烈火燃烧', 'Target opposing hero or ally is set ablaze 1 fire damage every start of controllers turn'}
,	{76, '卡牌骗术', 'RES:2  Draw a card'}
,	{77, '野性奔腾', 'Allies you summon for the next 3 turns have haste ( they can attack and use abilities in the turn they are summoned ).'}
,	{78, '烈焰之柱', 'All heroes and allies take 5 fire damage'}
,	{79, '奥术弹幕', 'Up to 4 different target opposing heroes or allies take 2 arcane damage'}
,	{80, '结茧', 'Target oppo ally disable for 2 turns'}
,	{81, '强击光环', 'Target bow you control has +1 attack while Aimed Shot is attached.'}
,	{82, '毒性射击', 'Target oppo ally is poisoned and is disabled for 1 turn.'}
,	{83, '火箭', 'Target opposing ally takes 2 fire damage that cannot be reduced by ally or armor abilities, and is set ablaze.'}
,	{84, '快速射击', 'Your hero may attack 2 times on each of your turns while Rapid Fire is attached'}
,	{85, '死亡陷阱', 'Play face down.The next opposing ally to be summoned is killed.'}
,	{86, '隐匿森林', 'Your hero is hidden (it and its attachments cannot be targeted) until the start of your next turn'}
,	{87, '作战计划', 'You can view the top card of your deck while Battle Plans is in play. RES:1 Move the top card of your deck to the bottom.'}
,	{88, '奇袭', 'Target friendly ally has ambush while Surprise Attack is attached, and when that ally deals combat damage, draw a card.'}
,	{89, '网兜陷阱', 'Play face down.The next opposing ally to be summoned is disabled (it cannot attack, defend or use abilities) for 3 turns.'}
,	{90, '追踪器', 'You can view the hands of opposing players while Tracking Gear is in play.'}
,	{91, '神圣治疗', 'Target friendly hero or ally heals 4 damage and has all enemy attachment and negative effects removed'}
,	{92, '月之祝福', 'Target friendly ally has +2 attack while Inner Strength is attached'}
,	{93, '怒涛之啸', 'All allies are killed.'}
,	{94, '寒冰碎片', 'All oppo allies take 2 ice damage'}
,	{95, '黑暗祭祀', 'Target enemy item or support ability is destroyed'}
,	{96, '重生', 'All allies in your graveyard are return to the top of your deck'}
,	{97, '神圣庇护', 'Target friendly hero or ally takes no damage and cannot be killed until the start of your next turn'}
,	{98, '瘟疫', 'Target opposing player remove 2 resources from play, leaving a minimus of 2. Remove one of your resources from play.'}
,	{99, '奥术重击', 'Target opposing hero or ally takes 3 arcane damage'}
,	{100, '诅咒之书', 'Opposing allies have -1 attack while Book of Curses is in play'}
,	{101, '背刺', 'Target friendly ally that is not exhausted, disabled or frozen kills target opposing ally and is then exhausted.'}
,	{102, '鹰击长空', 'Look at target opponents hand. Draw a card. Your allies have +1 attack until the end of your turn.'}
,	{103, '小莫飞刀', 'Ambush (Attacks by your hero with this weapon cannt be defended.)'}
,	{104, '死亡毒液', 'While Sorcerous Poison is attached to target weapon you control, heroes and allies damaged in combat by your hero are poisoned.'}
,	{105, '枪火谈判', 'Target enemy item is destroyed, and you gain one resource.'}
,	{106, '暗影突袭', 'RES:0 Friendly allies have ambush (their attacks cannot be defended) until the end of your turn.'}
,	{107, '不义之财', 'When an opposing ally is killed or enemy item is destroyed while Ill-Gotten Gains is in play, draw a card.'}
,	{108, '隐形翅膀', 'Your hero and allies cannot attack and are hidden (they and their attachments cannot be targeted) until the end of your next turn.'}
,	{109, '木乃伊之咒', 'Target opposing ally is disabled (it cannot attack, defend or use abilities) for 1 turn. Draw a card.'}
,	{110, '深夜幽影', 'Ambush (Attacks by this ally cannot be defended.) Stealth (This ally cannot be attacked.)'}
,	{111, '无尽束缚', 'Target opposing ally is disabled (it cannot attack, defend or use abilities) while Captured Prey is attached.'}
,	{112, '狼群', 'Pack Wolf has +1 attack for every other friendly Pack Wolf in play.'}
,	{113, '鬼影重重', 'Target opposing ally with cost 3 or less is killed.'}
,	{114, '迅击', 'Your hero may attack 2 times on each of your turns while Speedstrike is attached.'}
,	{115, '巨齿', ''}
,	{116, '再生', 'Your hero heals 4 damage and has all enemy attachments and negative effects removed.'}
,	{117, '满月', 'Until the start of your next turn, friendly allies have +2 attack and your hero takes no damage.'}
,	{118, '狂犬之啮', 'Target opposing ally attacks a random opposing hero or ally if it is able to, and is disabled (cannot attack, defend or use abilities) for 1 turn.'}
,	{119, '独狼', 'Your hero heals 2 damage at the start of each of your turns while Lone Wolf is attached and there are no friendly allies in play.'}
,	{120, '狼甲', 'Target friendly Wulven ally has +2 attack and +3 health while Pack Alpha is attached. If Pack Alpha is destroyed, allies you control gain +1 health.'}
,	{121, '能量喷射', 'Target friendly ally that can be damaged is killed. All opposing allies are killed, and your hero takes 5 damage.'}
,	{122, '能量转化', 'Target friendly ally that can be damaged is killed, and your hero heals damage equal to that allys health.'}
,	{123, '刈魂', 'Remove all ally cards in your graveyard from the game. Your hero heals 2 damage for each card removed.'}
,	{124, '魂火', 'Spark has +1 attack and +1 health for every other friendly Spark in play.'}
,	{125, '迁移', 'Draw a card from target opponents deck.'}
,	{126, '精神掌控', 'Target opposing ally attacks its hero if it is able to, and then is killed.'}
,	{127, '刷新', 'All of your used resources are renewed.'}
,	{128, '能量碎片', 'Your allies have +2 attack and a maximum of 1 health while Shard of Power is in play.'}
,	{129, '星之祝福', 'Target friendly ally has +5 health while Life Infusion is attached.'}
,	{130, '无尽轮回', 'Shuffle the cards in your graveyard into your deck. Remove Eternal Renewal from the game.'}
,	{131, '月之暗面', 'Friendly allies have stealth (they cannot be attacked for the next 2 turns'}
,	{132, '撤退', 'Target ally is returned to its owner hand'}
,	{133, '强化盔甲', 'Target friendly ally has all damage to it reduced by 1 while Reinforced Armor is attached'}
,	{134, '定时炸弹', 'Target opposing hero or ally takes 3 damages'}
,	{135, '祈愿', 'All friendly allies heal 2 damage. Draw a card'}
,	{136, '正义加持', 'At the start of each of your turns while Good Ascendant is in play, all friendly allies heals 2 damage. RES:0 Target Evil Ascendant is Destoryed.'}
,	{137, '光芒四射', 'Target opposing hero loses 2 shadow energy'}
,	{138, '弱者散退', 'Target item or support ability with cost 4 or less is returned to its controllers hand'}
,	{139, '装备风化', 'While Poor Quality is attached to target enemy weapon or armor, it has -1 attack or defendse, and loses 1 additional durability when used in combat.'}
,	{140, '安魂曲', 'If you have at least 3 allies in your graveyard, draw 2 cards and your hero gains +2 health'}
,	{141, '嗜血杀戮', 'Friendly allies have +2 attack until the end of your turn.'}
,	{142, '召唤虚灵', 'Target player removes one of their resources from play if their resources are greater than or equal to yours.  Draw a card'}
,	{143, '法力损毁', 'All heroes lose their stored shadow energy and takes 2 damage'}
,	{144, '野性尖叫', 'Target enemy item or support with cost 4 or less is destroyed, one of your resources is removed from play'}
,	{145, '邪恶加持', 'At the start of each of your turns while Evil Ascendant is in play, all allies take 1 damage. RES:0 Target Good Ascendant is Destroyed.'}
,	{146, '血石祭坛', 'Friendly allies that are damaged have +1 attack while Bloodstone Altar is in play.'}
,	{147, '恐惧', 'Target opposing ally loses protector and defender, and cannot be targeted by its controller while Selfishness is attached'}
,	{148, '酸液', 'Target enemy weapon or armor loses 3 durability.'}
,	{149, '法力源泉', 'Your hero gains +3 shadow energy.'}
,	{150, '魅惑妖术', 'Target frendly ally that can be damaged is killed, Draw card equal to that ally cost, to a maximum of 3.'}
,	{151, '暴雨', 'Allies cannot attack until the end of your next turn'}
,	{152, '法力流失', 'Target opposing hero loses 1 shadow energy and cannot activate its hero ability until the start of your next turn'}
,	{153, '无言恐惧', 'Heroes cannot attack until the end of your next turn'}
,	{154, '血之狂暴', 'Target friendly ally has +2 attack until the start of your next turn'}
,	{155, '商店', 'Each player draws an extra card at the start of their turn while Bazaar is in play'}
,	{156, '切断联系', 'Target attached card is destroyed.'}
,	{157, '圣诞糖果棒', 'Each player draws 3 cards'}
,	{158, '铁匠术', 'Target weapon or armor in your graveyard is retruned to your hand.'}
,	{159, '熔化', 'Target item you control is destroyed, draw 2 cards'}
,	{160, '破坏射线', 'Target enemy item with cost 5 or greater is destroyed. Draw a card.'}
,	{161, '新星铠甲', 'At the start of each of your turns while Nova Infusion is in play, your hero has all enemy attachments and negative effects removed, and takes 1 fire damage.'}
,	{162, '冰川护甲', 'When a hero or ally attacks your hero, that hero or ally is frozen until the end of its controllers turn'}
,	{163, '雷暴护甲', 'When a hero or ally attacks your hero, that hero or ally takes 1 electrical damage.'}
,	{164, '永恒铠甲', 'Your hero cannot attack or defend while Armor of Ages is in play.'}
,	{165, '王者铠甲', 'Friendly allies have +2 attack and +1 health while The Kings Pride in play'}
,	{166, '夜行衣', 'When your hero deals combat damage to an opposing hero while Night Prowler is in play, take 1 card from that hero owner hand at random.'}
,	{167, '丛林护甲', 'When a friendly ally is killed while Wrath of the Forest is in play, draw a card.'}
,	{168, '血之铠甲', 'When an opposing ally is killed while Crimson Vest is in play, your hero heals 2 damage.'}
,	{169, '盗墓者斗篷', 'ENERGY:2  Target opposing player has a random card from their graveyard placed into your hand.'}
,	{170, '蛇皮战衣', 'When Cobraskin Wraps is summoned, all players remove 1 resource from play and you draw a card.'}
,	{171, '荆棘之甲', 'When an ally deals combat damage to your hero while Black Garb is in play, that ally is killed.'}
,	{172, '法力护盾', 'When an ally damages your hero while Dome of Energy is in play, that ally loses 1 base attack'}
,	{173, '板甲', 'ENERGY:1 Target friendly ally gains +1 health'}
,	{174, '月光护腕', 'When Moonlight Bracers is destroyed, if you have a weapon in play it gains +1 durability.'}
,	{175, '嘲讽甲胄', 'Your hero has protector (alias without protector cannot be targeted) while Mocking Armor is in play'}
,	{176, '流浪法师斗篷', 'Friendly allies takes 2 less damage from abilities while Legion United is in play'}
,	{177, '灵风魔袍', 'While your hero takes damage while Twice Enchanted Robe is in play, draw a card if your deck is not empty.'}
,	{178, '暗影铠甲', 'When an opposing ally is killed while Shadow Armor is in play, your hero gains +1 shadow energy and takes 2 damage.'}
,	{179, '黑暗法袍', 'At the start of each of your turns, Crescendo gain +1 defense. At the end of your turn, if Cresendo has 5 defense, it is destroyed and all opposing allies are killed.'}
,	{180, '破法护腕', 'Your hero takes no damage from abilities while Spelleater Bands is in play.'}
,	{181, '哀悼之锋', 'When an ally is killed, Mournblade gains +1 base attack up tp a maximum of 5'}
,	{182, '裂空刀', 'When your hero deals combat damage while Dimension Ripper is in play, each player in combat draws a card from the other player deck'}
,	{183, '神秘之剑', 'When your hero deals combat damage Berserker Edge gains +1 base attack'}
,	{184, '贪婪之刃', 'When your hero attacks while Jewelers Dream is in play, 2 used resources are renewed'}
,	{185, '勾魂猎弓', 'When your hero kills an ally in combat while Soul Seeker is in play, your hero heals 3 damage.'}
,	{186, '瞬发长弓', 'Your hero has defender(it attacks first when defending) while Guardian\'s Oath is in play. Guardian\s Oath does +1 damage when your hero defends'}
,	{188, '老头剑', 'When Rusty Longsword is summoned, your hero takes 1 damage'}
,	{189, '烈焰法杖', 'ENERGY:1 Target opposing ally takes 1 fire damage and is set ablaze'}
,	{190, '冰晶节杖', 'Allies summoned while Voice of Winter is in play are frozen (they cannot attack, defend or use abilities) for 2 turns'}
,	{191, '木杖', 'Your hero cannot attack while Wooden Staff is in play'}
,	{192, '暗影碎片', 'When Sliver of Shadow is summoned, your hero loses 1 shadow energy.'}
,	{193, '多兰法杖', 'ENERGY:1 Draw a card.'}
,	{194, '黄金拳剑', 'Golden Katar doesnot lose durability when your hero defends.'}
,	{195, '回魂法杖', 'When your hero damages another hero in combat while Ghostmaker is in play, the top ally from your graveyard is return to play with 1 health'}
,	{196, '多兰之刃', 'Old Iron Dagger does +1 damage when your hero defends.'}
,	{197, '麦瑞德之爪', 'Hero armor does not reduce the damage done in combat by your hero while Uprooted Tree is in play'}
,	{198, '碎踝鞭', 'When your hero damages an ally in combat while Anklebreaker is in play, that ally loses 1 base attack and is disabled for 1 turn.'}
,	{200, '撤退匕首', 'When your hero deals non-fatal damage to an ally in combat while Dagger of Unmaking is in play, that ally is returned to its owner hand.'}


};

job_map = {
	[1] = 'Warrior',  	-- warrior, boris etc
	[2] = 'Hunter',	-- hunter, victor etc
	[4] = 'Mage',	-- mage, nishaven etc
	[8] = 'Priest',	-- priest, zhanna etc
	[16] = 'Rogue',	-- rogue
	[32] = 'Wulven',	-- wulven
	[64] = 'Elemental',	-- elemental
	[128] = 'x_x',	--  unknown
	[256] = 'Human',  -- human  (this is camp)
	[512] = 'Shadow',		-- shadow
	[999] = 'j_j',
};

g_card_list = g_card_list or {};

-- job id to name
function job_id_name(job)
	local jname = '';
	local binary={512,256,128,64,32,16,8,4,2,1};
	for i=1,#binary do
		if job >= binary[i] then
			job = job - binary[i];
			jname = jname .. '[' .. job_map[binary[i]] .. ']';
		end
	end
	return jname;
end

function init_lang()
	local index = 1;
	if g_card_list == nil or hero_list==nil then
		return -1, "g_card_list err";
	end
	for i=1, #lang do
		local entry = lang[index];
		local id = entry[1];
		local name = entry[2];
		local skill_desc = entry[3];
		local cc;
		
		if id >= 21 then 
			cc = g_card_list[id];
		else
			cc = hero_list[id];
		end

		if cc ~= nil then
			cc.name = name;
			cc.skill_desc = skill_desc;
			cc.job_name = job_id_name(cc.job);
		end

		index = index + 1;

	end
end

init_lang();


