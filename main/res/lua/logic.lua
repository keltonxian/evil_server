-- logic.lua  
-- note: a table > 150 lines may have a red warning at the closing brace }
-- EFFECT spec: @see EFFECT START
-- [1] = 'effect_name'  -- e.g. power, move, add, remove, 
-- effect name: damage, resource, resource_max, power, add, remove, move etc

-- 
REV_STR="$Rev: 00001 $"
LOGIC_VERSION=string.gsub(REV_STR, '[ ]*$[a-zA-Z:]*[ ]*', '');

-- NOTE: skill target spec
-- @see Shield Bash  (69)  Fireball(71)
--  target_list = { $target1, $target2, $target3 ...}
--  $targetX = { side=$side, table_list={ $table1, $table2, $table3...} }
--	$side = 1 means my_side,  2 means your_side,  3 means both side
--	$tableX = T_HERO | T_ALLY | T_GRAVE| T_SUPPORT etc.
-- 
-- actual_target_list = { actual_target1, actual_target2 ... }
-- actual_target = 4 digits number (new index rule)  for normal card
-- actual_target = 5 digits number for attachment 
-- 
-- play_spec:
-- ===============
-- {ability=true|false, index=src_index,  atl={tar_index1, tar_index2...}, }
-- NOTE: some play may have no atl (atl=nil or atl={})  index is mandatory:must_have
-- @see play_to_cmd()
--
-- cmd_spec:
-- ===============
-- t index target_index1 					NOTE: support only 1 target now
-- b index target_index1 target_index2 ...  NOTE: may have no target
-- n			next 
-- s index      NOTE: index=0 for no sacrifice
-- 
-- new index rule:
--  1000 = hero of side 1
--  1101 = hand 1  (max 7)
--  1201 = ally up to 1299
--  1301 = support up to 1399
--  1401 = grave up to 1x99
--  ------------------------
--  2000 = hero of side 2
--  2101 = hand 1   (max 7)
--  2201 = ally up to 2299
--  2301 = support up to 2399 
--  2401 = grave up to 2x99  (this can be very large)
-- for attachment, it becomes 5 digits, append the attachment pos as last digit
-- e.g. 23015 is the 5th attachment of the 1st card on T_ALLY, side 2
-----------------------------

-- trigger spec
-- when self is add to target_table: 
-- trigger_add(self, pside, target_table); 

-- when target is add to target_table, loop_trigger_other_add for all cards in T_HERO, T_ALLY, T_SUPPORT
-- trigger_other_add(self, target, pside, target_table);

-- when self(dier) is kill by src(killer)
-- trigger_die(self, pside, src);

-- when self(killer) kill target, with last damage = power
-- trigger_kill(self, target, pside, power);

-- when target(dier) is kill by src(killeer), loop_trigger_other_kill for all ccards in T_HERO, T_ALLY, T_SUPPORT
-- trigger_other_kill(self, target, src, pside);

-- every turn start, loop_trigger_turn_start for all T_HERO, T_ALLY, T_SUPPORT , sss is the side id
-- trigger_turn_start(self, pside, sss);

-- when self is remove from target_table:
-- trigger_remove(self, pside, target_table);

-- when target is remove from target_table, loop_trigger_other_remove for all cards in T_HERO, T_ALLY, T_SUPPORT
-- trigger_other_remove(self, target, pside, target_table);

-- before trigger_skill, will call trigger_target_validate(self, pside, nil, 0), then call trigger_target_validate for each target in atl, where target is the card object, tid = index to atl
-- trigger_target_validate(self, pside, target, tid);

-- require('bit');
band = bit.band; -- luajit: bit.band  lua 5.2: bit32.band
-- require('bit32');
-- band = bit32.band; -- luajit: bit.band  lua 5.2: bit32.band

-- @see (193)wizend staff and (76)tome
MAX_HAND_CARD = 7;

local_ui = g_ui;
-- require caller to set g_init_test = true or false, default is true
-- true : use init_test() card suit
local_init_test = g_init_test or false; -- default to false

if local_ui ~= true then
	-- peter: test
	local_init_test = true;
end

g_gate = nil; --{{}};
g_gate_max = 0;

-- g_solo_list = nil;
g_solo_type = 0;
-- g_solo_max_ally = 0;
-- g_solo_max_energy = 99;


-- avoid lua_checker
action_add = nil;
action_cast_target = nil;
action_after_die = nil;
action_attach = nil;
action_virtual_attach = nil;
action_virtual_remove = nil;
action_damage = nil; 
action_damage_list = nil; 
action_die = nil;
action_drawcard = nil;
action_grave = nil;
action_heal = nil; 
action_move = nil;
action_move_top = nil;
action_remove = nil;
action_power_offset = nil; -- power_offset
action_power_change = nil; -- base power
action_refresh = nil;
action_resource = nil;
action_energy = nil;
action_durability_change = nil;
action_timer_table = nil;
set_not_ready = nil;
check_playable = nil;
play_to_cmd = nil;
-- side = nil; 
print_card = nil;
table_str = nil;
index_card = nil;
index_card_list = nil;
index_table_num = nil;
card_index = nil;
card_attach = nil;
table_append = nil;
append = nil;
cindex = nil;
check_ready_attack = nil;
check_ally_duplicate = nil;
check_attach_duplicate = nil;
check_attach_over = nil;
check_is_item = nil;
check_ready_ability = nil;
loop_trigger_other_remove = nil;
loop_trigger_other_add = nil;
same_job = nil;
-- effect function
-- TODO use eff_xxx without offset
eff_anim = nil;
eff_attach = nil;
eff_card = nil;
eff_trap = nil;
eff_energy_offset = nil;
eff_hp = nil;
eff_power_offset = nil;
eff_resource_max_offset = nil;
eff_resource_offset = nil;
eff_resource_value = nil;  -- eff_fix_resource
eff_add = nil;
eff_remove = nil;
eff_move = nil;
eff_view_top = nil;
eff_hide_top = nil;
eff_view_oppo = nil;
eff_hide_oppo = nil;
-- check utility
check_in_side_list = nil;
check_in_table_list = nil;
check_in_side = nil;
check_in_table = nil;
check_ctype_in_table = nil;
check_in_attach = nil;
list_attack_target = nil;

-- set_ready func
set_ready = nil;
set_not_ready = nil;
set_use_attack = nil;
set_use_ability = nil;

-- ai_weight funtion
ai_weight_ally = nil;

gate_init_array = nil;
robot_init_array = nil;
solo_init_array = nil;
game_init = nil;
-- solo_list_remove = nil;
-- solo_list_add = nil;


-- print function
print_ai_play_detail = nil;
print_eff_list = nil;
cmd_detail = nil;

-- global for main:
g_round = 1; -- TODO need to ++ when next
g_current_side = 1;   -- side, up first
g_logic_table = nil;
g_phase = 1; -- 1=PHASE_SACRIFICE   nil=PHASE_PLAY
g_card_list = nil;  -- global variable
g_seed = 0;  -- means no seed

-- chapter target
--[[
-- old logic
-- TODO set in g_logic_table[side]
g_chapter_up_ally = 0;
g_chapter_up_support = 0;
g_chapter_up_ability = 0;
g_chapter_down_ally = 0;
g_chapter_down_support = 0;
g_chapter_down_ability = 0;
g_chapter_up_card_id1 = 0;
g_chapter_up_card_count1 = 0;
g_chapter_up_card_id2 = 0;
g_chapter_up_card_count2 = 0;
g_chapter_up_card_id3 = 0;
g_chapter_up_card_count3 = 0;
g_chapter_down_card_id1 = 0;
g_chapter_down_card_count1 = 0;
g_chapter_down_card_id2 = 0;
g_chapter_down_card_count2 = 0;
g_chapter_down_card_id3 = 0;
g_chapter_down_card_count3 = 0;
]]--
update_solo_target = nil;


------------ CONSTANT START -------------


PHASE_SACRIFICE = 1
PHASE_PLAY		= 2; -- nil

-- note: something_map is the mapping from number to string
-- r_something_map is the mapping from string back to number
-- constant is defined as CAPITAL LETTER for ease of use

job_map = {
	[1] = 'WARRIOR',  	-- warrior, boris etc
	[2] = 'HUNTER',	-- hunter, victor etc
	[4] = 'MAGE',	-- mage, nishaven etc
	[8] = 'PRIEST',	-- priest, zhanna etc
	[16] = 'ROGUE',	-- rogue
	[32] = 'WULVEN',	-- wulven
	[64] = 'ELEMENTAL',	-- elemental
	[256] = 'HUMAN',  -- human  (this is camp)
	[512] = 'SHADOW',		-- shadow
	[999] = 'j_j',
};

r_job_map = {};
for k, v in pairs(job_map) do
	r_job_map[v] = k;
end

-- job
WARRIOR	= r_job_map['WARRIOR'];	-- 1
HUNTER	= r_job_map['HUNTER'];	-- 2
MAGE	= r_job_map['MAGE'];	-- 4
PRIEST	= r_job_map['PRIEST'];	-- 8 
ROGUE	= r_job_map['ROGUE'];	-- 16
WULVEN 	= r_job_map['WULVEN'];	-- 32
ELEMENTAL = r_job_map['ELEMENTAL'];  -- 64
-- camp
HUMAN	= r_job_map['HUMAN']; 	-- 256
SHADOW	= r_job_map['SHADOW'];	-- 512
assert(WARRIOR>0 and MAGE>0 and PRIEST>0 and ROGUE>0);
assert(WULVEN>0 and ELEMENTAL>0 and HUMAN>0 and SHADOW>0);

camp_map = {
	[256] = 'human',	
	[512] = 'shadow',
	[999] = 'c_c',
};


damage_map = {
	[1] = 'normal', -- 1=normal sword
	[4] = 'penetration', --4=penetration attack,ignore armor,Uprooted Tree(197)
	[5] = 'magic', 
	[6] = 'fire',
	[7] = 'ice',
	[8] = 'electrical',
	[9] = 'arcane',
	[10] = 'poison',
	[50] = 'direct', --direct attack,ignore ally ability,King Pride(165) leave
	[999] = 'd_d',
};

r_damage_map = {};
for k, v in pairs(damage_map) do
	r_damage_map[v] = k;
end

D_NORMAL = r_damage_map['normal'];
D_PENETRATION = r_damage_map['penetration'];
D_MAGIC = r_damage_map['magic'];
D_FIRE = r_damage_map['fire'];
D_ICE = r_damage_map['ice'];
D_ELECTRICAL = r_damage_map['electrical'];
D_ARCANE	= r_damage_map['arcane'];
D_POISON = r_damage_map['poison'];
D_DIRECT = r_damage_map['direct'];


-- <= 500 & >= 0, anim has orbit
A_MAJIYA	= 1;
A_BORIS		= 2;
A_NORMAL	= 3; 
A_ZHANNA	= 4;
A_FIREBALL	= 5;
A_FROSTMIRE	= 6;
A_TER		= 7;
A_AMBER		= 8;
A_ICE		= 9;
A_FIRE		= 10;
A_COMBO_HIT		= 11;
A_LIGHTNING		= 12;
-- > 500 , anim has no orbit
A_NISHAVEN	= 501; -- TODO syn with client (all ally electric damage: Nishaven)
A_WAVE		= 502; -- TODO syn with client
A_THUNDER	= 503; 
A_ARCANE	= 504; 
A_NOVA		= 505;
A_HP_UP		= 506;
A_HP_DOWN	= 507;
A_POWER_UP	= 508;
A_POWER_DOWN= 509;

virtual_map = {
	[1] = 'ablaze',
	[2] = 'cobweb',
	[3] = 'frozen',
	[4] = 'poison',
	[5] = 'stealth',
	[6] = 'no_attack',
};

r_virtual_map = {};
for k, v in pairs(virtual_map) do
	r_virtual_map[v] = k;
end

V_ABLAZE = r_virtual_map['ablaze'];
V_COBWEB = r_virtual_map['cobweb'];
V_FROZEN = r_virtual_map['frozen'];
V_POISON = r_virtual_map['poison'];
V_STEALTH = r_virtual_map['stealth'];
V_NO_ATTACK = r_virtual_map['no_attack'];


T_HERO 		= 1
T_HAND 		= 2
T_ALLY 		= 3
T_SUPPORT 	= 4  -- consider T_SUPPORT = 7, T_WEAPON = 8, ...
T_GRAVE		= 5
T_DECK 		= 6
T_RES 		= 7
T_WEAPON 	= 8
T_ARMOR 	= 9
T_ATTACH	= 10


-- offset to name (offset / 100) + 1
table_name_map = {
	'hero',  	-- [1]
	'hand',     -- [2]
	'ally',  	-- [3]
	'support',  -- [4]
	'grave',  	-- [5]
	'deck',  	-- [6] newly added
	'resource',	-- [7]
	'weapon', 	-- [8]
	'armor', 	-- [9]
};

r_table_name_map = {}
for k, v in pairs(table_name_map) do
	r_table_name_map[v] = k;
end

-- name to offset, obsolete do not use
table_offset_map = {
	hero = T_HERO * 100,
	hand = T_HAND * 100,
	ally = T_ALLY * 100,
	support = T_SUPPORT * 100,
	grave = T_GRAVE * 100, -- grave must be the last, may > 100 cards in grave
	deck = T_DECK * 100,
	resource = T_RES * 100,
};

-- obsolete, do not use
r_table_offset_map = {};
for k, v in pairs(table_offset_map) do
	r_table_offset_map[v] = k;
end


-- card type
ctype_map = {
    [10] = 'hero',  
    [20] = 'ally', 
    [30] = 'attach',
    [40] = 'ability',
    [50] = 'support',  -- 50 to 59 are all support items, include weapon etc
    [51] = 'weapon',
    [52] = 'armor',
    [53] = 'artifact',
    [54] = 'trap',
	[999] = 'XTYPE',
};

r_ctype_map = {};
for k, v in pairs(ctype_map) do
	r_ctype_map[v] = k;
end

HERO = r_ctype_map['hero'];
ALLY = r_ctype_map['ally'];
ATTACH = r_ctype_map['attach'];
ABILITY = r_ctype_map['ability'];
SUPPORT = r_ctype_map['support'];
WEAPON = r_ctype_map['weapon']; -- kind of support
ARMOR = r_ctype_map['armor']; -- kind of support
ARTIFACT = r_ctype_map['artifact']; -- kind of support
TRAP = r_ctype_map['trap']; -- kind of support

-- fix list: it is support or attach (long term)
-- 70=blood frency(warrior),   76=Tome(mage), 193=wizents staff(priest)
-- 155=bazaar (general, low level ai), 
-- it is an ability, draw once only:
-- 157=bad santa(general) : 3 cards,  155=sacrifice lamb (kill ally and draw)
DRAW_LIST = { [70]=1, [76]=1, [193]=1, [155]=1, [157]=1, [150]=1, [38]=1};
DRAW_FIX_LIST = { [70]=1, [76]=1, [193]=1, [155]=1 };
DRAW_ONCE_LIST = { [157]=1, [150]=1, [38]=1 }; -- once only

CONTROL_LONG_LIST = { [67]=1 }; -- long term 
-- short term control: 1006=frozen2, 1021=no_attack, 1072=frozen3, 1080=web
-- 1082=poison_arrow2(disable 1 turn), 1089=nettrap, 1162=frozen1
CONTROL_SHORT_LIST = { [1006]=1, [1021]=1, [1072]=1, [1080]=1, [1082]=1, 
	[1089]=1, [1162]=1 };

-- true if card is a support, weapon, armor etc.
function is_support(card)
	if card.ctype >= SUPPORT and card.ctype <= SUPPORT + 9 then
		return true;
	end
	return false;
end

function fun_ctype_map(ct)
	local ctype_name;
	ctype_name = ctype_map[ct];
	if  ctype_name == nil then
		return ctype_map[999];
	end
	return ctype_name;
end


function fun_job_map(job) 
	local name;
	name = job_map[job];
	if  name == nil then
		return job_map[999];
	end

	return name;
end

function fun_camp_map(camp) 
	local name;
	name = camp_map[camp];
	if  name == nil then
		return camp_map[999];
	end
	return name;
end

function fun_damage_map(num)
	local name;
	name = damage_map[num];
	if name == nil then 
		return damage_map[999];
	end
	return name;
end

--------- CONSTANT END -------------

function logic_traceback(msg)
    local str = "";
	print('BUGBUG logic_traceback START ', msg);
    str = "----------------------------------------\n";
    str = str .. "LUA ERROR: " .. tostring(msg) .. "\n";
    str = str .. debug.traceback() .. "\n";
    str = str .. "----------------------------------------\n";
    print("BUGBUG logic_traceback\n" .. str);
end

------- UTILITY START --------

-- general utility function 

old_print = nil;
-- since Xcode will screw up the ANSI color , disable it in g_ui mode
if local_ui == nil then
-- peter: note: replace the original print function to enable color
old_print = print;  -- for colorprint
print = function(...) -- {
    -- lua -e "print('This is red->\27[31mred \27[0mNormal\n')"
    local arglist = {...}; -- arglist[1] , arglist[2]
    local str = ''; 
    local color = ''; -- default no color
    local COLOR_DEBUG = '\27[36m';
    local COLOR_WARN = '\27[33m';
    local COLOR_ERROR = '\27[31m';
    local COLOR_BUG = '\27[35m\27[4m';
    local COLOR_NORMAL = '\27[0m';
    for i=1, #arglist do
        local onestr = tostring(arglist[i]);

        if i==1 then 
            if 1 == string.find(onestr, 'DEBUG', 1, true) then
                color = COLOR_DEBUG;
            end 
            if 1 == string.find(onestr, 'WARN', 1, true) then
                color = COLOR_WARN;
            end 
            if 1 == string.find(onestr, 'ERROR', 1, true) then
                color = COLOR_ERROR;
            end 
            if 1 == string.find(onestr, 'BUGBUG', 1, true) then
                color = COLOR_BUG;
				old_print(debug.traceback());
            end 
                
            if #color > 0 then
                str = str .. color;
            end 
            str = str .. onestr;
        else        
            str = str .. '\t' .. onestr;  -- need a tab after first param
        end 
    end           
    if #color > 0 then str = str .. COLOR_NORMAL; end 
    old_print(str); 
end -- print }
end


-- this is deep clone
clone = nil;  -- avoid lua_checker warning
function clone(t)
	if type(t) ~= 'table' then
		return t;
	end

	-- implicit: type(t) == 'table'

	local copy = {};
	local meta;
	for k,v in pairs(t) do
		local kk, vv;
		kk = clone(k)
		vv = clone(v);
		copy[kk] = vv;
	end
	meta = getmetatable(t);
	if meta ~= nil then 
		meta = clone(meta);
		setmetatable(copy, meta);
	end
	return copy;  
end

-- return a shallow clone table which contains all the value of t,
-- the value does not clone
-- note: only clone for the numeric index (ipairs)
function shallow_clone(t)
	local copy = {};
	for k, v in ipairs(t) do
		copy[k] = v;
	end
	return copy;
end

-- split the string to table
-- str = 'aa bb 33'    ouptut = {'aa', 'bb', '33'}
-- note: number will be interpreted as string, 
-- e.g. output[3] = '33'  not output[3] = 33
function split(str)
	local ret = {}
	-- XXX need to check split [%w,.-] spec, e.g. 'aaa sol*o1 bbb' will be bug
	for w in string.gmatch(str, "[%w,.-;]+") do
		local x = tonumber(w);  -- '11' will convert to 11
		-- 20 is a magic length to detect a normal number
		if x ~= nil and string.len(w) < 20 then 
			w = x;
		end
		table.insert(ret, w)
	end
	return ret
end

function split_str(str)
	local ret = {}
	for w in string.gmatch(str, "[%w,.-;]+") do
		table.insert(ret, w)
	end
	return ret
end

-- split string to table, try to convert number-like string to number
-- e.g. str = 'aa bb22 33 44cc'  output = { 'aa', 'bb', 22, 33, 44, 'cc'}
-- note: the best use is t1 -> {'t', 1}, u can omit space between t and 1
function split_num(str)
	local ret = {}
	if str == nil or str == '' then
		return {};
	end
	str = string.gsub(str, '(%a+)(%d+)', '%1 %2');
	str = string.gsub(str, '(%d+)(%a+)', '%1 %2');
	-- print('split_num: ' .. str);
	for w in string.gmatch(str, "[%w,.-;]+") do
		-- print('split_num : ' , w);
		local x = tonumber(w);  -- '11' will convert to 11
		if x ~= nil then 
			w = x;
		end
		table.insert(ret, w)
	end
	return ret
end

function csplit(str, delim)
    --print('DEBUG csplit str, delim: ', str, delim);
    local result = {};
    if nil == str then
        return result;
    end
	delim = delim or ' ';
    local count = 2000; -- max 2000 token
    local token;

    repeat
        local s_pos, e_pos = string.find(str, delim);
        if s_pos==nil or e_pos == nil then
            break;
        end
        -- print('s_pos = ', s_pos, ' e_pos = ', e_pos);
        token = string.sub(str, 1, s_pos-1);
        if string.len(token) > 0 then
            result[ #result + 1] = token;
        end
        str = string.sub(str, e_pos+1); -- missing len means up to full len
        -- print('Result i : ', result[#result], '  str=', str);
        count = count - 1;
    until count <= 0;

    if string.len(str) > 0 then
        result[ #result + 1] = str ;
    end

    return result;
end

-- join array from index start to stop, with space as separator
-- start, stop are the index of array (inclusive)
function join_str(array, start, stop)
	local str = '';
	-- array out bound check
	if start > #array or stop > #array then
		print('ERROR join_str:start_stop_out_bound ', start, stop, #array);
		return 'bug_join_str';
	end
	for i=start,stop do
		if i~=start then
			str = str .. ' ';
		end
		str = str .. array[i]; 
	end
	return str;	
end

-- return:  next, content(joined str)
-- next is the next index for next nscan
function nscan(array, index)
	local start, stop, count;
	local str;
	if index > #array or index <= 0 then
		print('ERROR nscan:index_out_bound ', index, #array);
		return -2;
	end
	count = tonumber(array[index]) or 0;
	if count < 0 then
		print('ERROR nscan:count ', count, index, #array);
		return -12;
	end
	-- TODO check count is number
	start = index+1;  
	stop = index + count;
	if start > #array or stop > #array then
		print('ERROR nscan:start_stop_out_bound ', start, stop, #array);
		return -22;
	end
	str = join_str(array, start, stop);
	return index + count + 1, str;
end

-- check whether all the item in list(t) is number type (true)
-- any one of them is non-number, return false
function list_isnumber(t)
	for k, v in pairs(t) do
		if type(v)~='number' then
			return false;
		end
	end
	return true; -- all number
end

-- modify t1, note: t1 must be a numeric index'ed table
function table_append(t1, t2)
	local offset = #t1;
	if t2 == nil or #t2==0 then
		return t1;
	end
	if t1 == nil then
		print('WARN: table_append t1 = nil');
		t1 = {};
	end
	for i=1, #t2 do
		t1[i+offset] = t2[i];
	end
	return t1; -- this is not necessary but for ease of use, let's keep it
end

-- same as table_append
function append(t1, t2)
	local offset = #t1;
	if t2 == nil or #t2==0 then
		return t1;
	end
	if t1 == nil then
		print('WARN: append t1 = nil');
		t1 = {};
	end
	for i=1, #t2 do
		t1[i+offset] = t2[i];
	end
	return t1; -- this is not necessary but for ease of use, let's keep it
end

-- return the index of tb where tb[index]==num
-- return nil if not find
-- if there are more than one value in tb matching num, return the
--     first match according to the order of pairs
function table_find(tb, num)
    -- no need to check type
    for k, v in pairs(tb or {}) do 
        if v==num then
            return k;
        end
    end
    return nil;
end

-- handy function for error check
function error_return(errmsg)
	errmsg = errmsg or 'DEBUG _no_errmsg_';
	print(errmsg);
	return nil, errmsg;  -- first nil is eff_list, should be nil
end

function sort_damage_list(target_list)
	local target;
	local earthen_list = {};
	if target_list == nil or #target_list == 0 then
		return target_list;
	end
	for i=#target_list, 1, -1 do 
		target = target_list[i];
		if target == nil then
			return target_list;
		end
		if target.id == 39 then 
			table.insert(earthen_list, 1, target);
			table.remove(target_list, i);
		end
	end

	for i=#earthen_list, 1, -1 do 
		target = earthen_list[i];
		if target == nil then
			return target_list;
		end
		table.insert(target_list, 1, target);
	end
	
	return target_list;
end

---------- UTILITY END ------------

---------- CARD CLASS -------------

-- card class template
card_class = {}  -- avoid lua_checker warning
card_class = {

	-- OO proxy model @see http://lua-users.org/wiki/ObjectOrientedProgramming
	new = function (self, c)
		local proxy = {};
		if c == nil then
			print('BUGBUG new card is nil');
			assert(c ~= nil); -- let it breaks!
			c = {};
		end
		proxy.c = c;
		setmetatable(proxy, self);

		-- init some value
		card_class.init(c);

		return proxy;
	end,

	init = function (self) 
		-- avoid nil
		self.hp = self.hp or 0;  
		self.hp_max = self.hp or 0;
		self.power = self.power or 0; 
		self.cost = self.cost or 0; -- this is dangerous?
		self.ready = 2; -- ready for attack, ability
		if self.ctype==HERO then
			self.pos = 1; -- special pos for hero
			self.cost = 10; -- very high cost for hero
			self.energy = 0; -- default=0 DEBUG @see logic_init_test()
		end
	end,

	-- card.hp >>  __index(card, 'hp')
	__index = function(t, k)
		-- note: t is the instance

		if k == 'safe_home' then
			-- avoid nil home case
			return card_class.get_home(t, k);  -- early exit
		end

		-- order of get_??? :  self.c[key] (rawget) -> support -> attach_list 

		if k == 'hp' then
			-- note: card_class:get_hp() is the same (1st param auto self)
			return card_class.get_hp(t, k);  -- early exit
		end

		if k == 'power' then
			return card_class.get_power(t, k);
		end

		if k == 'weapon' then
			return card_class.get_weapon(t, k);
		end

		if k == 'attack_dtype' then
			return card_class.get_attack_dtype(t, k);
		end

		if k == 'vtype' then
			return card_class.get_vtype(t, k);
		end

		if k=='ambush' then
			return card_class.get_ambush(t, k);
		end

		if k=='haste' then  -- add by kelton
			return card_class.get_haste(t, k);
		end

		if k=='stealth' then
			return card_class.get_stealth(t, k);
		end

		if k=='protector' then
			return card_class.get_protector(t, k);
		end

		if k=='no_help' then
			return card_class.get_no_help(t, k);
		end

		if k=='defender' then
			return card_class.get_defender(t, k);
		end

		if k=='disable' then
			return card_class.get_disable(t, k);
		end

		if k=='no_attack' then
			return card_class.get_no_attack(t, k);
		end

		if k=='no_defend' then
			return card_class.get_no_defend(t, k);
		end

		if k=='no_ability' then
			return card_class.get_no_ability(t, k);
		end

		if k=='hidden' then
			return card_class.get_hidden(t, k);
		end

		-- avoid nil for cost
		if k=='skill_cost_energy' then
			return t.c.skill_cost_energy or 0;
		end

		if k=='skill_cost_resource' then
			return t.c.skill_cost_resource or 0;
		end

		if k == 'dtype' then
			local dtype = rawget(t.c, k);
			if nil == dtype then
				print('BUGBUG dtype is nil, id ', t.c.id);
			end
			return dtype or D_MAGIC; 
		end

		-- trigger or function, return function pointer directly (not "call")::

		if k == 'trigger_calculate_attack' then
			return card_class.trigger_calculate_attack;
		end

		if k == 'trigger_calculate_defend' then
			return card_class.trigger_calculate_defend;
		end
		
		--[[
		if k == 'trigger_turn_start' then
			return card_class.trigger_turn_start;
		end
		]]--

		if k=='trigger_attack' then
			return card_class.trigger_attack;
		end

		-- new index rule
		if k=='index' then
			return card_class.index;
		end

		-- generic case
		local v;
		v = rawget(t.c, k); -- first try the instance, then try the class
		if v == nil then
			-- print('*** use super class');
			v = rawget(card_class, k);
		end
		return v;
	end,

	__newindex = function(t,k,v)  -- t=table, k=key, v=value
		if k == 'hp' then
			print('BUGBUG cannot set hp');
		elseif k=='power' then
			print('BUGBUG cannot set power');
		elseif k=='stealth' then
			print('BUGBUG cannot set stealth');
		else
			-- set real card t.c
			rawset(t.c, k, v); -- use raw the avoid feedback to __newindex
			-- t[k] = v  -- this will trigger stack overflow
		end
 	end,

	-- return the numeric index according to [new index rule]
	index = function(self)
		-- checking inside card_index
		return card_index(self.side, self.table, self.pos, self.attpos);
	end,

	die = function(self)
		if self.table==T_GRAVE then
			return true;
		else
			-- double check on hp, when it is not in grave!
			if self.hp <= 0 then
				return true;
			else
				return false;
			end
		end
	end,

	-- avoid nil home
	get_home = function(self) 
		local v;
		assert(self ~= nil and self.c ~= nil); -- this is bad, break
		local cc = self.c;
		v = rawget(cc, 'home') or rawget(cc, 'side') ;  -- avoid nil
		if nil == rawget(cc, 'home') then
			print('ERROR safe_home return nil, card id:', self.id);
		end
		return v;
	end,


	-- get real card .hp
	get_base_hp = function(self)
		local cc = self.c;
		return cc.hp;  -- note: can be negative
	end,

	-- internal function, also callable by card:get_hp()
	get_hp = function(self) 
		local v;
		-- assert(self ~= nil and self.c ~= nil); -- this is bad, break
		if (self == nil or self.c==nil) then
			print('BUGBUG get_hp self=nil or self.c=nil');
			return 99;
		end
		local cc = self.c;
		v = rawget(cc, 'hp') or 0;  -- avoid nil

		if cc.ctype==ATTACH then  -- 30 is attach
			return v;
		end

		-- attach_list is in proxy, not in self.c
		for k, ac in ipairs(cc.attach_list or {}) do  -- peter: luajit fix
			v = v + (ac.c.hp or 0); -- peter: luajit fix
		end

		if v < 0 then 
			v = 0;  -- max hp is zero
		end
		return v;
	end,

	change_hp = function(self, offset)
		local cc = self.c;

		-- if we are dying, offset will be limited by self.hp (visible hp)
		-- not the real card hp (cc.hp)
		if self.hp + offset < 0 then
			offset = -self.hp;
		end

		cc.hp = (cc.hp or 0) + offset; -- note: changes on real card, not proxy
		return offset; -- real offset
	end,

	change_base_hp = function(self, offset)
		local cc = self.c;

		if offset >= 0 then
			cc.hp = cc.hp + offset;
			cc.hp_max = cc.hp_max + offset;
			return offset;
		end

		if (cc.hp + offset < 0) then
			offset = -cc.hp;
		end
		cc.hp = cc.hp + offset;
		cc.hp_max = cc.hp_max + offset;

		-- offset < 0
		if cc.hp_max <= 0 then
			print('BUGBUG change_base_hp() cc.hp_max <= 0, cc.id', cc.id);
			cc.hp_max = 0;
			cc.hp = 0;
			return offset;
		end

		return offset;

	end,

	-- XXX hp may bigger then hp_max
	-- this func just change hp_max
	change_max_hp = function(self, offset)
		local cc = self.c;
		cc.hp_max = cc.hp_max + offset;

		-- offset < 0
		if cc.hp_max <= 0 then
			cc.hp_max = 0;
			return offset;
		end

		return offset;
	end,

	-- heal must have positive offset and must obey hp <= hp_max after heal
	heal_hp = function(self, offset) 
		local cc = self.c;

		-- print('HEAL_HP cardid, offset=', cc.id, offset);
		-- fixed:  we should calculate all healing based on cc.hp and cc.hp_max

		if cc.hp >= cc.hp_max then
			return 0;
		end

		-- was:  self.hp
		if cc.hp + offset > cc.hp_max then
			offset = cc.hp_max - cc.hp;
		end

		if offset < 0 then
			return 0;
		end

		cc.hp = cc.hp + offset;
		return offset;
	end,

	-- this only works with HERO
	get_weapon = function(self)
		local cc;
		cc = self.c;
		if cc.ctype ~= HERO then
			return nil;
		end
		
		for k, wc in ipairs(cc[T_SUPPORT]) do
			-- assume only one weapon
			if wc.ctype==WEAPON then
				return wc;
			end
		end
		return nil; -- no weapon in support
	end,

	get_power = function(self, key)
		local v;
		local po;
		assert(self ~= nil and self.c ~= nil); -- this is bad, break
		local cc = self.c;
		-- power for ability is rely on ab_power
		-- peter: need to separate power and ab_power, else ai may bug
		-- v = rawget(cc, key) or 0;  -- avoid nil
		v = cc[key];
		-- for bloodstone(146)
		if type(v) == 'function' then
			-- print('-------- class.get_power (146)bloodstone, v=', v);
			-- TODO if v datatype is function 
			v = v(cc, g_logic_table); -- global access!!!
		end
		v = v or 0; -- avoid nil

		-- reflect real power for attachment
		if cc.ctype==ATTACH then  
			return v;
		end

		-- for hero, power will be the weapon power
		if cc.ctype==HERO then
			for k, wc in ipairs(cc[T_SUPPORT]) do
				-- assume only one weapon
				if wc.ctype==WEAPON then
					v = wc.power; -- note: it is not add-on
					--- print('DEBUG get_power hero v=' .. v);
					break;
				end
			end
		end

		local no_power_offset = false;

		-- attach_list is in proxy, not in self.c
		for k, ac in ipairs(cc.attach_list or {}) do  -- peter: luajit fix
			-- local ppp = rawget(ac.c, key) or 0;
			local ppp = ac.power;
			v = v + ppp;
			--v = v + rawget(ac.c, 'power'); -- since we need -negative
			if ac.id == 1002 then -- very hard code, avoid amber
				no_power_offset = true;
			end
		end

		-- for war banner, kurt whitehelm, aldon the brave, book of curse etc
		-- no_power_offset avoid (2)amber duplicate power add
		if no_power_offset == false then 
			po = rawget(cc, 'power_offset') or 0;
			v = v + po;
		end

		if v < 0 then 
			v = 0;  -- min power is zero
		end
		return v;
	end,

	get_attack_dtype = function(self, key)
		local v = nil;
		assert(self ~= nil and self.c ~= nil); -- this is bad, break
		local cc = self.c;
		-- power for ability is rely on ab_power
		-- peter: need to separate power and ab_power, else ai may bug
		v = rawget(cc, key) or D_NORMAL;  -- avoid nil

		-- reflect real power for attachment
		if cc.ctype==ATTACH then  
			return v;
		end

		-- for hero, power will be the weapon power
		if cc.ctype==HERO then
			for k, wc in ipairs(cc[T_SUPPORT]) do
				-- assume only one weapon
				if wc.ctype==WEAPON then
					v = wc[key]; -- note: it is not add-on
					--- print('DEBUG get_power hero v=' .. v);
					break;
				end
			end
		end

		-- attach_list is in proxy, not in self.c
		for k, ac in ipairs(cc.attach_list or {}) do -- peter: luajit fix
			v = rawget(ac.c, key) or v;
		end

		return v;
	end,

	get_vtype = function(self, key)
		local v = nil;
		assert(self ~= nil and self.c ~= nil); -- this is bad, break
		local cc = self.c;
		-- power for ability is rely on ab_power
		-- peter: need to separate power and ab_power, else ai may bug
		v = rawget(cc, key) or 0;  -- avoid nil

		-- reflect real power for attachment
		if cc.ctype==ATTACH then  
			return v;
		end

		-- attach_list is in proxy, not in self.c
		for k, ac in ipairs(cc.attach_list or {}) do -- peter: luajit fix
			v = rawget(ac.c, key) or v;
		end

		return v;
	end,
	
	-- BUG sample:  please keep for future reference
	change_power_BUG = function(self, offset)
		local cc = self.c; -- get the real card
		local v = self.power; -- note: this will call get_power eventually

		print('DEBUG change_power self.power=', v);
		if v + offset < 0 then
			offset = -v;
		end
		print('DEBUG change_power offset=', offset);

		cc.power = cc.power + offset; -- note: changes on real card, not proxy
		print('DEBUG change_power cc.power=', cc.power);

		return offset; -- real offset
	end,

	-- this changes the base power
	change_power = function(self, offset)
		local cc = self.c

		-- note: cc.power is the base power, not include the add-on
		if cc.power + offset < 0 then
			-- update the offset
			offset = -cc.power;
		end
		cc.power = cc.power + offset;
		return offset;
	end,

	get_ambush = function(self, key) -- {
		local v;
		local cc = self.c;
		v = rawget(cc, key) or false;

		if cc.ctype==ATTACH then
			return v;
		end

		for k, wc in ipairs(g_logic_table[self.side][T_SUPPORT] or {}) do
			if wc.c[key] == true then
				-- for hero, should check the weapon and armor
				if cc.ctype==HERO and (wc.ctype==WEAPON or wc.ctype==ARMOR) then
					v = true;
				end
				-- other effects to ally should be as virtual attach card
				if cc.ctype==ALLY and (wc.ctype==ARTIFACT or wc.ctype==SUPPORT) then
					v = true;
				end
			end
		end

		for k, ac in ipairs(cc.attach_list or {}) do -- luajit fix
			-- false override @see 103 Nowhere to hide
			if ac.c[key] == false then -- nil will skip
				print('DEBUG ambush override = false ac=', ac.id, ac.name);
				return false;
			end
			if ac.c[key] == true then
				v = true;
				-- do not return, other attach with false may override
			end
		end

		return v;
	end, -- }

	-- true, false, nil : v=true,  return false,  nil skip
	get_haste = function(self, key) -- { add by kelton
		local v;
		local cc = self.c;
		v = rawget(cc, key) or false;

		if cc.ctype==ATTACH then
			return v;
		end

		for k, sc in ipairs(g_logic_table[self.side][T_SUPPORT] or {}) do
			if sc.c[key] == true then
				return true;
			end
		end

		for k, ac in ipairs(cc.attach_list or {}) do -- luajit fix
			-- false override @see 103 Nowhere to hide
			if ac.c[key] == false then
				return false;
			end
			if ac.c[key] == true then
				v = true;
				-- do not return, other attach with false may override
			end
		end

		return v;
	end, -- }

	get_no_attack = function(self, key) -- {
		local cc = self.c;
		local v;
		v = rawget(cc, key) or false;
		if v then return v; end

		-- ready=2: fully ready,   ready=1:used ability
		if self.ready==0 or (self.ready==-1 and true ~= self.haste) then
			return true;
		end
		
		-- beware of cyclic reference: disable should not ref no_attack
		if card_class.get_disable(self) then
			return true;
		end
		
		-- check support for both side: rain delay usually
		for _, oneside in ipairs(g_logic_table) do
			for k, sv in ipairs(oneside[T_SUPPORT] or {}) do 
				-- no_attack is global effect!
				if sv.c.no_attack then  -- avoid cyclic ref
					if nil == sv.c.no_attack_target then -- for rain delay
						return true;
					end
					local nat = sv.c.no_attack_target; 
					if (nat.side == 1 and self.c.side == sv.c.side) or 
					   (nat.side == 2 and (1 - self.c.side) == sv.c.side) or
					   (nat.side == 3)  then

						for i = 1, #nat.table_list do
							local ttt = nat.table_list[i];
							local ccc = index_table_num(cindex(self.c));
							if ttt == ccc then
								return true;
							end
						end
					end
				end
			end
		end -- end for oneside

		-- atfer support ??
		for k, ac in ipairs(cc.attach_list or {}) do -- luajit fix
			if ac.c[key] == true then
				return true;
			end
		end

		return false;
	end, --}

	get_no_defend = function(self, key) -- {
		local cc = self.c;
		local v;
		v = rawget(cc, key) or false;
		if v then return v; end
		
		-- beware of cyclic reference: disable should not ref no_attack
		if card_class.get_disable(self) then
			return true;
		end

		if cc.ctype == HERO then
			for k, su in ipairs(g_logic_table[self.side][T_SUPPORT] or {}) do
				if (su.ctype == ARMOR or su.ctype == WEAPON) 
				and su.c[key] == true then
					return true;
				end

				if (su.ctype == ARMOR or su.ctype == WEAPON) 
				and su.c[key] == false then
					return false;
				end
			end

		end

		for k, ac in ipairs(cc.attach_list or {}) do -- luajit fix
			if ac.c[key] == true then
				return true;
			end
		end

		return false;
	end, --}

	get_no_ability = function(self, key) -- { add by kelton
		local cc = self.c;
		local v;
		v = rawget(cc, key) or false;
		if v then 
			return v; 
		end
		--[[
		if cc.ctype == ATTACH then
		print('DEBUG get_no_ability c, v: ', cc.name, v);
			return v;
		end
		]]--
		-- ready=2: fully ready,   ready=1:used ability
		if self.ready<=0 then
			return true;
		end
		
		-- beware of cyclic reference: disable should not ref no_ability
		if card_class.get_disable(self) then
			return true;
		end

		for k, ac in ipairs(cc.attach_list or {}) do
			if true == ac.c.no_ability then
				return true;
			end
		end
		
		-- check support for both side: rain delay usually
		for _, oneside in ipairs(g_logic_table) do
			for k, sv in ipairs(oneside[T_SUPPORT] or {}) do 
				if sv.c.no_ability then  -- avoid cyclic ref
					if nil == sv.c.no_ability_target then
						return true;
					end
					local nat = sv.c.no_ability_target; 
					if (nat.side == 1 and self.c.side == sv.c.side) or 
					   (nat.side == 2 and (1 - self.c.side) == sv.c.side) or
					   (nat.side == 3)  then

						for i = 1, #nat.table_list do
							local ttt = nat.table_list[i];
							local ccc = index_table_num(cindex(self.c));
							if ttt == ccc then
								return true;
							end
						end
					end
				end
			end
		end -- end for oneside
		return false;
	end, --}

	get_stealth = function(self, key) -- {
		local v;
		local cc = self.c;
		v = rawget(cc, key) or false;

		if cc.ctype==ATTACH then
			return v;
		end

		for k, sc in ipairs(g_logic_table[self.side][T_SUPPORT] or {}) do
			if sc.c[key]== true then
				-- WEAPON & ARMOR only affect hero
				if cc.ctype==HERO and (sc.ctype==WEAPON or sc.ctype==ARMOR) then
					v = true;
				end
				-- SUPPORT & ARTIFACT only affect ally
				if cc.ctype==ALLY and (sc.ctype==SUPPORT or sc.ctype==ARTIFACT) then
					v = true;
				end
			end
--			if sc.c[key]==true and self.ctype ~= HERO then
--				v = true;
--			end
		end

		for k, ac in ipairs(cc.attach_list or {}) do -- luajit fix
			-- false override @see 103 Nowhere to hide
			if ac.c[key] == false then
				return false;
			end
			if ac.c[key] == true then
				v = true;
				-- do not return, other attach with false may override
			end
		end

		return v;
	end, -- }

	get_protector = function(self, key)
		local v;
		local cc = self.c;
		v = rawget(cc, key) or false;

		if cc.ctype==ATTACH then
			return v;
		end

		-- for Mocking Armor(175)
		if cc.ctype==HERO then
			for k, su in ipairs(g_logic_table[self.side][T_SUPPORT] or {}) do
				if (su.ctype == ARMOR or su.ctype == WEAPON) 
				and su.c[key] == true then
					return true;
				end

				if (su.ctype == ARMOR or su.ctype == WEAPON) 
				and su.c[key] == false then
					return false;
				end
			end
		end

		for k, ac in ipairs(cc.attach_list or {}) do -- luajit fix
			-- false override @see 103 Nowhere to hide
			if ac.c[key]== false then
				return false;
			end
			if ac.c[key] == true then
				v = true;
				-- do not return, other attach with false may override
			end
		end

		

		return v;
	end,

	get_no_help = function(self, key)
		local v;
		local cc = self.c;
		v = rawget(cc, key) or false;

		if cc.ctype==ATTACH then
			return v;
		end

		for k, ac in ipairs(cc.attach_list or {}) do -- luajit fix
			-- false override @see 103 Nowhere to hide
			if ac.c[key]== false then
				return false;
			end
			if ac.c[key] == true then
				v = true;
				-- do not return, other attach with false may override
			end
		end

		

		return v;
	end,

	get_defender = function(self, key)
		local v;
		local cc = self.c;
		v = rawget(cc, key) or false;

		if cc.ctype==ATTACH then
			return v;
		end

		-- for Guardians Oath(186)
		if cc.ctype==HERO then
			for k, su in ipairs(g_logic_table[self.side][T_SUPPORT] or {}) do
				if (su.ctype == ARMOR or su.ctype == WEAPON) 
				and su.c[key] == true then
					return true;
				end

				if (su.ctype == ARMOR or su.ctype == WEAPON) 
				and su.c[key] == false then
					return false;
				end
			end
		end

		for k, ac in ipairs(cc.attach_list or {}) do -- luajit fix
			if ac.c[key]== false then
				return false;
			end
			if ac.c[key] == true then
				v = true;
				-- do not return, other attach with false may override
			end
		end

		return v;
	end,

	get_disable = function(self, key)
		local v;
		local cc = self.c;
		v = rawget(cc, key) or false;

		if cc.ctype==ATTACH then
			return v;
		end

		for k, ac in ipairs(cc.attach_list or {}) do -- luajit fix
			if ac.c[key] == true then
				return true;
			end
		end

		return v;
	end,

	get_hidden = function(self, key)
		local v;
		local cc = self.c;
		v = rawget(cc, key) or false;

		if cc.ctype == ATTACH then
			return v;
		end

		for k, ac in ipairs(cc.attach_list or {}) do -- luajit fix
			-- false override @see 103 Nowhere to hide
			if ac.c[key] == false then
				return false;
			end
			if ac.c[key] == true then
				v = true;
				-- do not return, other attach with false may override
			end
		end

		return v;
	end,

	refresh = function(self)
		local cc = self.c; -- cc is the "real" card
		local ocard = g_card_list[cc.id];
		cc.hp = ocard.hp;
		cc.hp_max = ocard.hp_max;
		cc.power = ocard.power;
		cc.cost = ocard.cost;
		cc.hp_offset = nil;
		cc.power_offset = nil;
		cc.cost_offset = nil;
		cc.timer = ocard.timer;
		cc.ready = 2; -- ready for attack, ability
		if cc.attach_list ~= nil and #cc.attach_list>0 then
			print('BUGBUG refresh attach_list not nil # ', #cc.attach_list);
		end
		cc.attach_list = nil;  -- use this with care!!!
	end,

	trigger_calculate_attack = function (self, pside, target, power, dtype, attack_flag)
		local cc = self.c; -- cc is the "real" card
		if nil ~= cc.trigger_calculate_attack then
			power = cc.trigger_calculate_attack(self, pside, target, 
				power, dtype, attack_flag);
		end
		if cc.ctype==HERO then
			local tname = 'trigger_calculate_attack';
			local trigger;
			for k, wc in ipairs(cc[T_SUPPORT]) do
				if wc.ctype==WEAPON then
					trigger = wc[tname];
					if nil ~= trigger then
						power = trigger(wc, pside, target, power
						, dtype, attack_flag);
					end
				end
			end
		end

		-- TODO consider support and attach list

		return power;
	end,
	
	-- self is the target
	trigger_calculate_defend = function (self, pside, src, power, dtype)
		local cc = self.c; -- cc is the "real" card
		if nil ~= cc.trigger_calculate_defend then
			-- print('trigger_calculate_defend not null');
			power = cc.trigger_calculate_defend(self, pside, src, 
				power, dtype);
			-- cc:trigger_xxx(power,...) =  cc.trigger_xxx(cc, power, ...)
		end
		-- print('trigger_calculate_defend name=' .. self.name);
		-- loop all the attached card

		for k, ac in ipairs(cc.attach_list or {}) do  -- luajit fix
			-- note: ac:trigger route back to __index
			if nil ~= ac.trigger_calculate_defend then 
				power = ac:trigger_calculate_defend(pside, src, power, dtype);
			end
		end
		if cc.ctype==HERO then
			local tname = 'trigger_calculate_defend';
			local trigger;
			for k, wc in ipairs(cc[T_SUPPORT]) do
				if wc.ctype==ARMOR then
					trigger = wc[tname];
					if nil ~= trigger then
						power = trigger(wc, pside, src, power, dtype);
					end
				end
			end
		end
		return power;
	end,

	-- move to loop_trigger_turn_start()
	--[[
	trigger_turn_start = function (self, ...)
		local cc = self.c; -- proxy
		local tname = 'trigger_turn_start';
		local eff_list = {};
		local eff_list2;
		local trigger;


		trigger = cc[tname];
		if nil ~= trigger then
			eff_list2 = trigger(self, ...);  -- was (cc)
			table_append(eff_list, eff_list2);
		end

		for k, ac in ipairs(cc.attach_list or {}) do  -- luajit fix
			-- note: ac:trigger route back to __index
			trigger = ac[tname]; --  ac.trigger_turn_start
			if nil ~= trigger then 
				eff_list2 = trigger(ac, ...);
				table_append(eff_list, eff_list2);
			end
		end
		return eff_list;
	end,
	]]--

	trigger_attack = function (self, ...)
		local cc = self.c; -- proxy
		local tname = 'trigger_attack';
		local eff_list = {};
		local eff_list2;
		local trigger;

		-- TODO shall we consider attachment ?

		trigger = cc[tname]; -- must use cc here
		if nil ~= trigger then
			-- must use self in trigger
			eff_list2 = trigger(self, ...); -- was trigger(cc,...)
			table_append(eff_list, eff_list2);
		end

		for k, ac in ipairs(cc.attach_list or {}) do  -- luajit fix
			trigger = ac[tname]; --  ac.trigger_turn_start
			if nil ~= trigger then 
				eff_list2 = trigger(ac, ...);
				table_append(eff_list, eff_list2);
			end
		end

		if cc.ctype==HERO then
			for k, wc in ipairs(cc[T_SUPPORT] or {}) do  -- peter: safety
				if wc.ctype==WEAPON then
					trigger = wc[tname];
					if nil ~= trigger then
						eff_list2 = trigger(wc, ...);
						table_append(eff_list, eff_list2);
					end
				end
			end
		end


		return eff_list;
	end, 


	trigger_defend = function (self, ...)
		local cc = self.c; -- proxy
		local tname = 'trigger_defend';
		local eff_list = {};
		local eff_list2;
		local trigger;

		-- TODO shall we consider attachment ?

		-- @see trigger_attack
		trigger = cc[tname];
		if nil ~= trigger then
			eff_list2 = trigger(self, ...); -- was trigger(cc, ...)
			table_append(eff_list, eff_list2);
		end

		if cc.ctype==HERO then
			for k, wc in ipairs(cc[T_SUPPORT]) do
				if wc.ctype==ARMOR then
					trigger = wc[tname];
					if nil ~= trigger then
						eff_list2 = trigger(wc, ...);
						table_append(eff_list, eff_list2);
					end

					-- for Poor Quality(139)
					for _, ac in ipairs(wc.attach_list or {}) do 
						trigger = ac[tname];
						if nil ~= trigger then
							eff_list2 = trigger(ac, ...);
							table_append(eff_list, eff_list2);
						end
					end
				end
			end
		end

		return eff_list;
	end, 


};



-- TODO move to utility
-- actual target list compare
function atl_compare(a, b) 
    if a.pos > b.pos then  -- reverse order
		return true; 
	end 
	return false; 
end


---------- AI START	----------

-- WEIGHT_KILL  				+= weight * oppo.cost
-- WEIGHT_KILL_POWER      		+= weight * oppo.power
-- note: no more diff between ally and hero
-- WEIGHT_DAMAGE_ALLY		 	+= wieght * oppo.cost
-- WEIGHT_DAMAGE_ALLY_POWER		+= weight * oppo.power
-- WEIGHT_DAMAGE_ALLY_DAMAGE	+= weight * damage
-- WEIGHT_DAMAGE_HERO			damage hero ++
-- WEIGHT_DAMAGE_HERO_DAMAGE
-- WEIGHT_DISABLE				+= weight * disable turns (card.power)
-- WEIGHT_SUPPORT_MY_ALLY		+= weight * #myside.ally
-- WEIGHT_SUPPORT_OPPO_ALLY		+= weight * #oppo.ally
-- WEIGHT_SUPPORT_

-- TODO : peter refactor, all weight should be positive for easy code reading
WEIGHT_KILL			= 200;  -- * target.cost  -- peter: was 300
WEIGHT_KILL_POWER	= 200;	-- * target.power -- was: 50
WEIGHT_DIE 			= -150; -- * self.cost  (also for used ability card)
WEIGHT_DIE_POWER	= -30;	-- * self.power
-----
-- peter: W_DAMAGE: 25->20
WEIGHT_DAMAGE		= 20;	-- * damage * power * target.cost @ref: 1/5 to 1/10 KILL
WEIGHT_DAMAGE_POWER = 20;	-- * damage * power * target.power
-- peter: W_HURT why -30 ?  why not -15 ?
WEIGHT_HURT			= -15;	-- * opower(hurt) * self.cost @ref: -DAMAGE
WEIGHT_HURT_POWER	= -8;	-- * opower(hurt) * self.power
-----
WEIGHT_HEAL			= 25;	-- heal * target.cost
WEIGHT_HEAL_POWER	= 25;	-- heal * target.power
----- trigger skill that use energy / res
WEIGHT_RESOURCE = - 140;
WEIGHT_ENERGY = - 50;  -- ???  compare logic: oppo cost
WEIGHT_CONTROL = 500;   -- * #oppo_ally - #my_ally

-- increase WEIGHT_GENERAL, -WEIGHT_GENERAL means cost of using ability card
WEIGHT_GENERAL	= 100;		-- * self.cost
WEIGHT_GENERAL_TARGET = 50;	-- * target.cost

WEIGHT_ALLY	= 150; 	-- * self.cost
WEIGHT_PROTECTOR = 1.0;  -- 

WEIGHT_EQUIPMENT	= 130; 	-- * self.cost

WEIGHT_POWER 		= 350;		-- * power_offset 


function ai_disable(target, weight)

	if check_in_attach(target, CONTROL_LONG_LIST) then
		return 0;
	end

	-- this may be duplicate target.disable ?
	if check_in_attach(target, CONTROL_SHORT_LIST) then
		return weight / 2; 
	end

	if target.disable then
		return weight / 2;
	end

	-- TODO check power=0, check no_defend, other status
	return weight;
end

-- return the weight to damage an target ally
-- second param is boolean kill == true or false
-- usage:  weight, kill = ai_damage(target, pside, src, power, dtype)
-- caller should handle:  target kill case (die_power) e.g.:
-- if (target.die_power or 0) > 0 then
-- TODO hpleft = hpleft - target.die_power
--      die = hpleft <= 0
--      if die then www -= WEIGHT_DIE * src.cost
--			weight = weight + WEIGHT_HURT * src.cost;
--			weight = weight + WEIGHT_HURT_POWER * src.power;
--		end
function ai_damage(target, pside, sss, src, power)
	local kill = false;
	local weight = 0;
	local damage;
	kill = power >= target.hp;
	damage = math.min(power, target.hp);
	if kill then
		weight = weight + WEIGHT_KILL * target.cost;
		weight = weight + WEIGHT_KILL_POWER * target.power;
		if target.ctype == HERO then
			weight = weight + 5000; -- very large, make sure it kill hero
		end
	else -- no kill, will do defend
		weight = weight + WEIGHT_DAMAGE * damage * target.cost;
		weight = weight + WEIGHT_DAMAGE_POWER * damage * target.power;
	end

	weight = ai_disable(target, weight); -- discount if the target is disable
	-- TODO check target.trigger_xxx ~= nil ?
	-- e.g. trigger_die : -weight  (not good to kill)
	-- trigger_add : -weight (like (23)Sandra and (24)Lily)
	-- trigger_skill : + weight  e.g. (21) jasmine (33)silversmith
	-- trigger_turn_start : + weight, e.g. (49)bad wolf, (32)Aldon
	if nil ~= target.trigger_die then
		weight = weight * 0.5;  -- half
	end
	if nil ~= target.trigger_skill and target.ctype ~= HERO then
		weight = weight * 1.1;  -- was: 1.5 
	end
	if nil ~= target.trigger_add then
		weight = weight * 0.8;  -- was: 0.7
	end

	if nil ~= target.trigger_other_add then
		weight = weight * 1.2;  -- this is more for aldon and kurt
	end
	-- XXX card_class has trigger_turn_start, so every card has trigger_turn_start
	if nil ~= target.trigger_turn_start then
		--print("ai_damage:trigger_turn_start target.name", target.name);
		weight = weight * 1.2;   -- was: 1.5
	end


	-- TODO change weight when target is protector
	-- if protector : add ai_weight_ally * WEIGHT_PROTECTOR
	if target.protector == true then
		local weight_protect = 0;
		local ally_list = pside[target.side][T_ALLY];
		for i=1, #ally_list do
			local ally;
			ally = ally_list[i];
			if ally.protector ~= true then
				weight_protect = weight_protect + ai_weight_ally(ally, {}, pside, sss, false);
			end
		end
		weight = weight + weight_protect * WEIGHT_PROTECTOR;
	end
	
	-- note: Aldon will have trigger_add and trigger_turn_start, so 
	return weight, kill;
end

-- src: the one who is being hurt
-- target: target hurt src
-- power is the damage (positive)
function ai_hurt(target, pside, src, power, srchp)
	local weight = 0;
	local die;
	srchp = srchp - power;
	die = srchp <= 0;
	if die then
		weight = weight + WEIGHT_DIE * src.cost;
		if src.ctype == HERO then
			weight = weight - 5000;  -- very large
		end
	else
		weight = weight + WEIGHT_HURT * power * src.cost;
		weight = weight + WEIGHT_HURT_POWER * power * src.power;
	end
	return weight, srchp;
end

-- when the target die, the 
-- return the weight when the target has die_power and feedback damage
-- to src, usually negative
-- no check whether target die or not (caller need to check)
-- usage:
-- w, kill, d = ai_damage(target, pside, src, power, dtype);
-- weight = weight + w;
-- damage = damage + d; -- for statistics
-- if kill then
--    w, srchp = ai_die_power(target, pside, src, srchp);
--    weight = weight + w;
-- end
function ai_die_power(target, pside, src, srchp)
	local weight = 0;
	local power;
	if (target.die_power == nil or target.die_power == 0) then
		return 0;
	end
	power = src:trigger_calculate_defend(pside, target, target.die_power
		, D_MAGIC);

	return ai_hurt(target, pside, src, power, srchp);
end

-- calculate the weight for casting an ally from hand to T_ALLY
function ai_weight_ally(self, atl, pside, sss, ability) -- {
	local weight = 0;
	weight = weight + WEIGHT_ALLY * self.cost;
	-- TODO consider power and hp etc?
	return weight;
end -- ai_weight_ally }


-- weight normal attack
-- usage:   for ally card with normal attack only, use this
-- e.g.
-- card = {
--  name = "Puwen",
-- 	ai_weight = ai_weight_attack;
-- }
-- 
-- for ally with special ability, e.g. plasma 
-- card = {
-- 		name = "Plasma Behemoth",
-- 		ai_weight = function(index, atl, pside, sss, ability)
--			if ability then
--				return ai_weight_ability_damage(index, atl, pside, sss, ability);
--			else
--				return ai_weight_attack(...);
--			end
--		end,
-- }
-- self and atl={ target } are all object not index
function ai_weight_attack(self, atl, pside, sss, ability) -- start {
	if self.table==T_HAND then
		return ai_weight_ally(self, atl, pside, sss, ability);
	end

	if true == ability then
		return -9977, 0, 0;
	end

--	local self = index_card(index, pside);
	local target = atl[1]; -- index_card(atl[1], pside);
	local power = 0;
	local kill = false;
--	local die = false;
	local weight = 0;
	local w = 0;
	local selfhp = self.hp;
	power = self.power;
	power = self:trigger_calculate_attack(pside, target, power, D_NORMAL);
	power = target:trigger_calculate_defend(pside, self, power, D_NORMAL);

	w, kill = ai_damage(target, pside, sss, self, power); 
	-- after this line, power is not useful for above
	weight = weight + w;

	if kill then
		w, selfhp = ai_die_power(target, pside, self, selfhp);
	else 
		-- no kill, need defend
		power = target.power;
		power = target:trigger_calculate_attack(pside, self, power, 
			D_NORMAL);
		power = self:trigger_calculate_defend(pside, target, power, 
			D_NORMAL);
		if self.ambush then
			power = 0;
		end
		w, selfhp = ai_hurt(target, pside, self, power, selfhp);
	end
	weight = weight + w;


	-- do this at last before return, if target is hero, always weight >= 1
	if weight <= 0 and target.ctype == HERO then 
		weight = 1;
	end

	return weight;

	-- check whether the target can be killed
	--[[
	kill = power >= target.hp;

	-- default die = false
	if kill == false then
		opower = target.power;
		opower = target:trigger_calculate_attack(pside, self, opower, 
			D_NORMAL);
		opower = self:trigger_calculate_defend(pside, target, opower, 
			D_NORMAL);
		if self.ambush then
			opower = 0;
		end
	else
		-- die_power is the feedback damage when the enemy die
		opower = target.die_power or 0;
		opower = self:trigger_calculate_defend(pside, target, opower, 
			D_MAGIC);
	end

	-- core logic: out of if structure
	-- check whether i will die
	die = opower >= self.hp;

	-- print('power, opower: ', power, opower);
	weight = 0;
	if kill then
		-- hero has 100 cost, so it will be very attractive to kill hero
		-- print('kill true target: ', target.name);
		weight = weight + WEIGHT_KILL * target.cost ;
		weight = weight + WEIGHT_KILL_POWER * target.power ;
		-- if it is hero, WEIGHT + 5000, which is large enough to decide!
		if target.ctype == HERO then
			weight = weight + 5000;
		end
	else
		-- print('damage target,target.power: ', target.name, target.power);
		weight = weight + WEIGHT_DAMAGE * power * target.cost;
		weight = weight + WEIGHT_DAMAGE_POWER * power * target.power;
	end

	weight = ai_disable(target, weight); -- discount for disable target

	if die then
		-- print('die true target: ', target.name);
		weight = weight + WEIGHT_DIE * self.cost ;
		weight = weight + WEIGHT_DIE_POWER * self.power ;
		if self.ctype == HERO then
			weight = weight - 5000; -- my hero die is the worst case
		end
	else
		weight = weight + WEIGHT_HURT * opower * self.cost;
		weight = weight + WEIGHT_HURT_POWER * opower * self.power;
	end
	-- note: kill vs die,  damage vs hurt, oppo_ally vs my_ally 

	-- return kill, die for debug, 
	-- non-kill will return damage
	-- non-die, return defender damager
	return weight, (kill or power), (die or opower);  
	]]--
end -- ai_weight_attack end }



-- calculate 
-- only kill and damage(power)
-- if ctype == ABILITY then weight -= WEIGHT_DIE * self.cost
-- test case 1: {
--      index = lightning strike ( power=3,  2 targets)
-- 		oppo:
--      puwen
--      kurt
--      deathbone
--      dirk
-- }
-- 
-- self, atl are now objects
function ai_weight_ability_damage(self, atl, pside, sss, ability) -- {
	if not ability then
		print('BUGBUG ai_weight_ability for non-ability');
		return 0, 0, 0;
	end

	local power = 0; -- damage to one target
	local damage = 0; -- total damage, accumulated
	-- local self = index_card(index, pside);
	-- local atl = index_card_list(atl, pside);
	local src;

	-- if self is an ally, then src is the self 
	-- else it is an ability card or support card, src is the hero
	if self.ctype == ALLY then
		src = self;
	else
		src = pside[self.side][T_HERO][1];
	end

	if false == check_playable(self, pside, sss, ability) then
		print('BUGBUG ai_weight_ability not playable : ', self.id);
		return 0, '_', 0;
	end

	if nil == self.ab_power then
		print('BUGBUG ai_weight_ability self.ab_power==nil : ', self.id);
		return 0, '_', 0;
	end

	local weight = 0;
	local kill_count = '';
	local hh ; -- hero
	hh = pside[sss][T_HERO][1];

	-- print(self.name .. '------');
	for i=1, #atl do -- start {
		local target;
		local www = 0;
		local w = 0;
		local kill;
		local selfhp;
		target = atl[i]; -- index_card(atl[i], pside);
		if nil == target then
			print('BUGBUG: ai_weight_ability_damage target nil: ', atl[i]);
			break;
		end
		power = self.ab_power; -- TODO ab_power may be nil
		power = target:trigger_calculate_defend(pside, hh, power, self.dtype);

		w, kill = ai_damage(target, pside, sss, src, power);
		www = www + w;

		if kill then
			selfhp = src.hp;
			kill_count = kill_count .. 'K';
			w, selfhp = ai_die_power(target, pside, src, selfhp);
		else
			kill_count = kill_count .. '_';
		end
		www = www + w;

		--[[
		-- TODO use ai_damage() 
		local kill;
		kill = power >= target.hp;
		if kill then
			www = www + WEIGHT_KILL * target.cost;
			www = www + WEIGHT_KILL_POWER * target.power;
			kill_count = kill_count .. 'K';
			if nil ~= target.die_power and target.die_power > 0 then
				-- TODO hpleft = hpleft - target.die_power
				--      die = hpleft <= 0
				--      if die then www -= WEIGHT_DIE * src.cost
				weight = weight + WEIGHT_HURT * src.cost;
				weight = weight + WEIGHT_HURT_POWER * src.power;
			end
		else
			damage = damage + power;
			www = www + WEIGHT_DAMAGE * power * target.cost;
			www = www + WEIGHT_DAMAGE_POWER * power * target.power;
			kill_count = kill_count .. '_';
		end
		]]--

		-- hard coded : if target is already disabled or 
		-- no attack/ability power, better not to use ability on it
		--[[
		if target.disable then -- or 0==(target.power + (target.ab_power or 0)) then
			-- consider to use this logic in ai_weight_attack, not now
			-- why / 2?   disabled ally is less important but never 0
			www = www / 2;  -- peter: how about: www = www / 10  ??
		end
		]]--
		www = ai_disable(target, www);
		-- print('\t target:' .. target.name .. ' ' .. www);

		if target.side == sss then
			weight = weight - www;  -- same side is BAD
		else
			weight = weight + www; -- oppo side is good
		end
	end -- end for #atl }

	if self.ctype == ABILITY then
		-- TODO using ability card is not important as ally die
		-- so weight lower here
		-- note:  ability does not count DIE_POWER, so it is lower
		weight = weight - WEIGHT_GENERAL * self.cost;
	else
		-- plasma case:  ally use ability
		weight = weight + WEIGHT_RESOURCE * self.skill_cost_resource;
		weight = weight + WEIGHT_ENERGY * self.skill_cost_energy;
	end

	return weight, kill_count, damage;
end -- end ai_weight_ability_damage }

function ai_weight_heal(self, atl, pside, sss, ability) -- {
	if not ability then
		print('BUGBUG ai_weight_heal for non-ability');
		return 0, 0, 0;
	end

	local power = 0; -- healing power effective to one target
	local total_power = 0; -- total power, accumulated
	-- local self = index_card(index, pside);
	-- local atl = index_card_list(atl, pside);
	local src;

	-- if self is an ally, then src is the self 
	-- else it is an ability card or support card, src is the hero
	if self.ctype == ALLY then
		src = self;
	else
		src = pside[self.side][T_HERO][1];
	end

	-- this include resource/energy check
	if false == check_playable(self, pside, sss, ability) then
		print('BUGBUG ai_weight_heal not playable : ', self.id);
		return 0, '_', 0;
	end

	if nil == self.ab_power then
		print('BUGBUG ai_weight_heal self.ab_power=nil : ', self.id);
		return 0, '_', 0;
	end

	local weight = 0;
	local kill_count = '';
	local hh ; -- hero
	hh = pside[sss][T_HERO][1];
	for i=1, #atl do -- start {
		local target;
		target = atl[i]; -- index_card(atl[i], pside);
		-- print('DEBUG weight_heal target:', target.id, target.hp, target.hp_max);
		if nil == target then
			print('BUGBUG: ai_weight_heal target nil: ', atl[i]);
			break;
		end
		power = self.ab_power or 0; -- TODO ab_power may be nil
		-- zhanna(8) lose 1 hp = 25, then attach enrage(65), hero.hp_max < hero.hp
		-- if target.hp_max < target.hp then
		if target.hp_max < target:get_base_hp() then
			print('BUGBUG ai_weight_heal  hp_max < hp: ', 
				target.hp_max, target:get_base_hp());
			break;
		end
		power = math.min(power, target.hp_max - target.hp);
		power = math.max(power, 0); -- must be positive
		-- print('DEBUG ai_weight_heal heal power = ', power, ' target.id,cost=', target.id, target.cost);
		total_power = total_power + power;
		weight = weight + WEIGHT_HEAL * target.cost * power;
		weight = weight + WEIGHT_HEAL_POWER * target.power * power;
		-- TODO need check
	end -- end for #atl }

	if self.ctype == ABILITY then
		-- TODO using ability card is not important as ally die
		-- so weight lower here
		-- note:  ability does not count DIE_POWER, so it is lower
		weight = weight - WEIGHT_GENERAL * self.cost;
	else
		-- ally/hero ability case:  use ability
		weight = weight + WEIGHT_RESOURCE * self.skill_cost_resource;
		weight = weight + WEIGHT_ENERGY * self.skill_cost_energy;
	end

	return weight, 0, total_power;
end -- end ai_weight_heal }

-- weighting for control card
-- e.g. Freezing Grip(72), Clinging Webs(80), Crippling Blow(67)
-- Eladwen Frostmire(6) skill should add this weight
function ai_weight_control(self, atl, pside, sss, ability) -- {
	-- local self = index_card(index, pside);
	-- local atl = index_card_list(atl);
	if nil == self then
		-- weight, round, power
		print('BUGBUG ai_weight_control self = nil');
		return 0, 0, 0;
	end

	-- 67:crippling,  72:freezing,  80:webs, 6:skill cause freezing
	-- if self.id ~= 67 and self.id ~= 72 and self.id ~= 80 then
	if self.id ~= 67 and self.id ~= 72 and self.id ~= 80 and self.id ~= 6 then
		print('WARN ai_weight_control non-control (67,72,80) id=', self.id);
	end

	if #atl <= 0 then
		print('BUG: ai_weight_control #atl <= 0');
		return 0, 0, 0;
	end

	-- check ability cost, disable etc
	if false == check_playable(self, pside, sss, ability) then
		print('BUGBUG ai_weight_control not playable : ', self.id);
		return 0, 0, 0;
	end


	local target = nil;
	local round = 0;
	local weight = 0;
	round = math.abs(self.power);
	if round > 5 then 
		round = 5  -- avoid crippling blow 99 round case!
	end
	target = atl[1]; 

	-- hard coded : if target is already disabled or 
	-- no attack/ability power, we do not prefer to control it
	if target.disable or 0==(target.power + (target.ab_power or 0)) then
		return 0;
	end
	
	
	weight = weight + WEIGHT_DAMAGE * target.cost * round ;
	-- note: this is bad for crippling blow
	weight = weight + WEIGHT_DAMAGE_POWER * target.power * round * 2;
	weight = weight + WEIGHT_DAMAGE_POWER * (target.die_power or 0);
	if self.ctype == ABILITY then
		weight = weight - WEIGHT_GENERAL * self.cost;
	else
		weight = weight + WEIGHT_RESOURCE * self.skill_cost_resource;
		weight = weight + WEIGHT_ENERGY * self.skill_cost_energy;
	end
	weight = weight + WEIGHT_CONTROL * (#pside[3-sss][T_ALLY] - #pside[sss][T_ALLY]);
	return weight, round, target.power;
end -- end ai_weight_control }


function ai_weight_equipment(self, atl, pside, sss, ability) -- {
	local weight = 0;
	local self_support = pside[self.side][T_SUPPORT];
	weight = weight + WEIGHT_EQUIPMENT * self.cost;
	if self.ctype == WEAPON then
		for i=1, #self_support do
			local card;
			card = self_support[i];
			if card.ctype == WEAPON then
				-- weight = weight - WEIGHT_EQUIPMENT * card.cost;
				return 0;
			end
		end
	end

	if self.ctype == ARMOR then
		for i=1, #self_support do
			local card;
			card = self_support[i];
			if card.ctype == ARMOR then
				-- weight = weight - WEIGHT_EQUIPMENT * card.cost;
				return 0;
			end
		end
	end

	return weight;
end -- ai_weight_equipment }

-- general support or un-implemented card
function ai_weight_general(self, atl, pside, sss, ability) -- {
	-- local self = index_card(index, pside);
	-- local atl = index_card_list(atl, pside);

	if nil == self then
		-- weight, round, power
		return 0;
	end

	-- check resource/energy, disable etc
	if false == check_playable(self, pside, sss, ability) then
		print('BUGBUG ai_weight_general not playable : ', self.id);
		return 0, 0, 0;
	end

	local weight = 0;
	weight = weight + WEIGHT_GENERAL * self.cost;
	for i=1, #atl do
		local target;
		target = atl[i]; 
		if nil == target then
			print('BUGBUG ai_weight_general target=nil');
			break;
		end
		weight = weight + WEIGHT_GENERAL_TARGET * target.cost;
	end
	return weight;
end -- ai_weight_general }

---------- AI END ----------

---------- HERO START ------------
hero_list = { }; -- init the hero list table

-- TODO implement damage hp logic like normal card
-- warrior 1
hero = {
	id = 1,
	ctype = HERO,
	cost = 10,
	name = LOGIC_VERSION .. 'Boris Skullcrusher',
	star = 5,
	job = WARRIOR + HUMAN,  -- 1, 2, 4, 8, 16, 32... 1=warrior, 2=hunter, 4=mage
	camp = HUMAN,  --  good=256,  evil=512
	hp = 30, -- consider: fix and runtime
	power = 0, -- set as zero, cannot attack without weapon 
	skill_cost_energy = 4, -- how many energy to trigger skill
	skill_desc = 'ENERGY:4  Target opposing ally with cost 4 or less is killed',
	target_list = {
		-- DEBUG add ally, should be only hero
		{side=2, table_list={T_ALLY} }
	},

	-- target is the target card
	-- tid is the num id, correspond to target_list[tid]
	-- normally, we should check something out of the scope
	-- in target_list[tid]
	trigger_target_validate = function(self, pside, target, tid)
		if tid == 0 then
			return true;
		end
		if tid ~= 1 or target == nil then
			print('BUGBUG: boris(1) target_validate tid, target', tid, target);
			return false;
		end

		-- implicit: tid==1,   target non-nil
		if target.cost <= 4 then
			return true;
		end
		--print('DEBUG boris(1) cost too large skip :', target.name, target.cost);
		return false;
	end,

	trigger_skill = function(self, pside, actual_target_list) 
		local index;
		local target;
		local eff_list = {};
		if actual_target_list==nil or #actual_target_list~=1 then
			local err = 'BUGBUG: boris(1) skill atl=nil or empty';
			return nil, err;
		end

		local target_list = index_card_list(actual_target_list, pside);
		for i=1, #target_list do
			target = target_list[i];
			if false == self:trigger_target_validate(pside, target, i) then
				return error_return(
					'BUGBUG: boris(1) invalid target ' .. i .. 
					' target.id, cindex:', target.id, cindex(target)
				);
			end
		end

		index = actual_target_list[1];
		target = index_card(index, pside);

		-- print('DEBUG Boris index, target.id=', index, target.id);
		
		if self.energy < self.skill_cost_energy then
			print('ERROR Boris skill energy not enough');
			return {};
		end

		eff_list = action_damage(target, pside, self, 99, D_MAGIC, A_BORIS);
		-- eff_list = action_grave(target, pside);

		return eff_list; 
	end,

	-- runtime attribute
	energy = 0,  -- need 3 round to 

	ai_weight = function(self, atl, pside, sss, ability) -- start {
		local target;
		local weight = 0;
		if ability then
			if self.energy < self.skill_cost_energy then
				return 0;
			end
			target = atl[1];
			if target == nil then
				print('BUGBUG boris(1) ai_weight target=nil');
				return 0;
			end
			weight = weight + WEIGHT_KILL * target.cost;
			weight = weight + WEIGHT_KILL_POWER * target.power;
			-- TODO reduce weight by WEIGHT_ENERGY
			return weight; -- early exit
		else
			-- normal attack
			return	ai_weight_attack(self, atl, pside, sss, ability);
		end
		return weight;
	end, -- ai_weight }
};
hero_list[hero.id] = card_class:new(hero);

hero = {
	id = 2,
	ctype = HERO,
	cost = 10,
	name = LOGIC_VERSION .. 'Amber Rain',
	star = 5,
	job = WARRIOR + HUMAN,  
	camp = HUMAN,
	hp = 30, -- consider: fix and runtime
	atype = A_AMBER,
	skill_desc = 'ENERGY:3  Target weapon you control gains +2 base attack, but may not gain any other bonus',
	skill_cost_energy = 3, 

	-- runtime attribute
	energy = 0,  

	-- TODO consider to auto search the weapon ?  need to fix the ai
	target_list = {
		{side=1, table_list={T_WEAPON} },
	},

	trigger_skill = function(self, pside, atl)  -- {
		local index;
		local target;
		local eff_list = {};
		local eff_list2 = {};
		local err = nil;

		target = index_card(atl[1], pside);
		if nil == target then
			return error_return('BUGBUG (2)amber target=nil');
		end

		if (self.atype ~= nil) then
			local target_list = {};
			target_list[1] = target:index();
			eff_list[#eff_list+1] = eff_anim(self:index(), self.id, self.atype, target_list);
		end

		-- print('DEBUG (2)amber add virtual card 1002 to ' .. atl[1]);
		eff_list2, err = action_virtual_attach(target, pside, 1002, self);
		table_append(eff_list, eff_list2);
		return eff_list, err; 
	end, -- trigger_skill }


	ai_weight = function(self, atl, pside, sss, ability) -- start {
		local target;
		local weight = 0;
		if ability then
			-- use ability if the weapon has no attach:
			-- (1002) or other enemy attach are bad
			if #(atl or {}) == 0 or atl[1]==nil then
				print('BUGBUG (2)amber ai weapon is nil');
				return 0;
			end
			target = atl[1];
			if #(target.attach_list or {})>0 then
				return 0;
			end

			if (self.no_attack or self.ready <= 0) then
				return 0;
			end

			return 2000; -- nearly must do first (was 1000)
			
		else
			-- normal attack
			weight = ai_weight_attack(self, atl, pside, sss, ability);
			-- when (184) is on hand and used >=2 resource, attack first!
			if true == check_in_side(pside[sss], 184) then

				if pside[sss].resource_max - pside[sss].resource >= 2 then
					weight = weight + 800;
				else
					weight = 1;  -- hard code! 1 will always attack (later)
				end
			end
			return weight;
		end
		return weight;
	end, -- ai_weight }
}
hero_list[hero.id] = card_class:new(hero);

-- 
hero = {
	id = 3,
	ctype = HERO,
	cost = 10,
	name = 'Victor Heartstriker',
	star = 5,
	job = HUNTER + HUMAN,  -- 1, 2, 4, 8, 16, 32... 1=warrior, 2=hunter, 4=mage
	camp = HUMAN,  --  good=256,  evil=512
	hp = 28, -- consider: fix and runtime
	power = 0, -- set as zero, cannot attack without weapon 
	-- dtype = D_DIRECT,
	skill_cost_energy = 4, -- how many energy to trigger skill
	skill_desc = 'ENERGY:4  Target opposing ally is reduced to 1 health. Return the top Hunter card from your graveyard to your hand.',
	target_list = {
		{side=2, table_list={T_ALLY} },
	},

	energy = 0,  -- need 3 round to 

	-- target is the target card
	-- tid is the num id, correspond to target_list[tid]
	-- normally, we should check something out of the scope
	-- in target_list[tid]
	trigger_target_validate = function(self, pside, target, tid)
		if tid == 0 then
			return true;
		end
		if tid ~= 1 or target == nil then
			print('BUG: victor(3) target_validate tid, target', tid, target);
			return false;
		end

		return target.table==T_ALLY;
	end,

	trigger_skill = function(self, pside, actual_target_list) 
		local index;
		local target;
		local eff_list = {};
		local eff_list2 = {};
		if actual_target_list==nil or #actual_target_list~=1 then
			local err = 'BUG: victor(3) skill atl=nil or empty';
			return nil, err;
		end

		local target_list = index_card_list(actual_target_list, pside);
		for i=1, #target_list do
			target = target_list[i];
			if false == self:trigger_target_validate(pside, target, i) then
				return error_return(
					'BUG: victor(3) invalid target ' .. i .. 
					' target.id, cindex:', target.id, cindex(target)
				);
			end
		end

		index = actual_target_list[1];
		target = index_card(index, pside);
		
		eff_list = action_damage(target, pside, self, target.hp-1, D_DIRECT);

		local my_grave = pside[self.side][T_GRAVE];
		local my_hand = pside[self.side][T_HAND];
		for i = #my_grave, 1, -1 do
			local cc = my_grave[i];
			print('DEBUG victor(3) grave cc: ', cc.name);
			if same_job(self, cc) then
				eff_list2 = action_move(cc, pside, my_grave, my_hand);
				table_append(eff_list, eff_list2);
				break;
			end
		end

		return eff_list; 
	end,

	ai_weight = function(self, atl, pside, sss, ability) -- start {
		local target;
		local weight = 0;
		if ability then
			target = atl[1];
			if target == nil then
				print('BUGBUG victor(3) ai_weight target=nil');
				return 0;
			end
			-- weight = weight + WEIGHT_KILL * target.cost;
			-- weight = weight + WEIGHT_KILL_POWER * target.power;
			weight = weight + WEIGHT_KILL * target.cost;
			weight = weight + WEIGHT_KILL_POWER * target.power;
			-- TODO reduce weight by WEIGHT_ENERGY
			return weight; -- early exit
		else
			-- normal attack
			return	ai_weight_attack(self, atl, pside, sss, ability);
		end
		return weight;
	end, -- ai_weight }
};
hero_list[hero.id] = card_class:new(hero);

hero = {
	id = 4,
	ctype = HERO,
	cost = 10,
	name = 'Gwenneth Truesight',
	star = 5,
	job = HUNTER + HUMAN,  
	camp = HUMAN,
	hp = 28, -- consider: fix and runtime
	skill_desc = 'ENERGY:3  Target weapon you control gains +2 durability and has +2 attack until the start of your next turn.',
	skill_cost_energy = 3, 

	-- runtime attribute
	energy = 0,  

	-- TODO consider to auto search the weapon ?  need to fix the ai
	target_list = {
		{side=1, table_list={T_WEAPON} },
	},

	trigger_skill = function(self, pside, atl)  -- {
		local index;
		local target;
		local eff_list = {};
		local eff_list2 = {};
		local err = nil;

		target = index_card(atl[1], pside);
		if nil == target then
			return error_return('BUGBUG gwenneth(4) target=nil');
		end
		
		if target.ctype~=WEAPON then
			print('BUGBUG Gwenneth(4) target is not weapon');
			return eff_list;
		end

		eff_list2 = action_durability_change(target, pside, 2);
		table_append(eff_list, eff_list2);

		print('DEBUG gwenneth(4)add virtual card 1004 to ' .. atl[1]);
		eff_list2, err = action_virtual_attach(target, pside, 1004, self);
		if nil == eff_list2 then
			print('BUGBUG weapon power2(1004) cannot attach: ', err);
			return eff_list, err;
		end
		table_append(eff_list, eff_list2);

		return eff_list;
	end, -- trigger_skill }


	ai_weight = function(self, atl, pside, sss, ability) -- start {
		local target;
		local weight = 0;
		if ability then
			if self.energy < self.skill_cost_energy then
				return 0;
			end
			-- use ability if the weapon has no attach
			
			if #(atl or {}) == 0 or atl[1]==nil then
				print('BUGBUG gwenneth(4)ai weapon is nil');
				return 0;
			end
			target = atl[1];
			if #(target.attach_list or {})>0 then
				return 0;
			end

			return 1000; -- nearly must do
			
		else
			-- normal attack
			return	ai_weight_attack(self, atl, pside, sss, ability);
		end
		return weight;
	end, -- ai_weight }
}
hero_list[hero.id] = card_class:new(hero);

-- electrical mage:
hero = {
	id = 5,
	ctype = HERO,
	cost = 10,
	name = 'Nishaven',
	star = 5,
	job = MAGE + HUMAN,  -- 4
	camp = HUMAN,  --  human=256,  shadow=512
	hp = 26, -- consider: fix and runtime
	power = 0,   -- peter: debug test, do not set power
	ab_power = 4,
	atype = A_NISHAVEN,
	dtype = D_ELECTRICAL,
	skill_desc = 'Energy:5  All allies take 4 electrical damage',
	skill_cost_energy = 5, -- need 3 skill points to trigger 
	-- skill_cost_resource

	-- runtime attribute
	energy = 0,  -- need 3 round to 

	trigger_skill = function(self, pside, actual_target_list) 
		local index;
		local target;
		local target_list = {};
		local index_list = {};
		local eff_list = {};
		local eff_list2;
		local side_src, side_opp;

		side_src = pside[self.side];
		side_opp = pside[3-self.side];

		for _, oneside in ipairs({side_opp, side_src}) do
			for k, v in ipairs(oneside[T_ALLY]) do 
				target_list[#target_list + 1] = v;
				index_list[#index_list + 1] = v:index();
			end
		end

		-- animation list
		eff_list[#eff_list + 1] = eff_anim(self:index(), self.id, self.atype
		, index_list);

		eff_list2 = action_damage_list(target_list, pside, self
		, self.ab_power, self.dtype, self.atype);
		table_append(eff_list, eff_list2);

		return eff_list; 
	end,


	ai_weight = function(self, atl, pside, sss, ability) -- start {
		local target;
		local weight = 0;
		if ability then
			if self.energy < self.skill_cost_energy then
				return 0;
			end
			-- use ability
			-- need to patch the atl, because target to all is not counted
			atl = {};
			for k, v in ipairs(pside[1][T_ALLY] or {}) do 
				table.insert(atl, v);
			end
			for k, v in ipairs(pside[2][T_ALLY] or {}) do 
				table.insert(atl, v);
			end
			return ai_weight_ability_damage(self, atl, pside, sss, ability);
		else
			-- normal attack
			return ai_weight_attack(self, atl, pside, sss, ability);
		end
		return weight;
	end, -- ai_weight }
}
hero_list[hero.id] = card_class:new(hero);

-- ice mage:
hero = {
	id = 6,
	ctype = HERO,
	cost = 10,
	name = LOGIC_VERSION .. 'Eladwen Frostmire',
	star = 5,
	job = MAGE + HUMAN,  -- 4=mage, 256=human
	camp = HUMAN,
	hp = 26, -- consider: fix and runtime
	-- power = 0, -- peter: debug test, do not set power --kelton: close for 152
	ab_power = 4,
	atype = A_FROSTMIRE,
	dtype = D_ICE,
	skill_desc = 'ENERGY:4  Target opposing ally takes 4 ice damage and is frozen for 2 turns', 
	skill_cost_energy = 4, -- need 3 skill points to trigger 
	-- skill_cost_resource

	-- runtime attribute
	energy = 0,  -- need 3 round to 

	target_list = {
		-- DEBUG add ally, should be only hero
		{side=2, table_list={T_ALLY} },
	},

	trigger_skill = function(self, pside, atl)  -- {
		local index;
		local target;
		local eff_list = {};
		local eff_list2 = {};
		local err = nil;

		target = index_card(atl[1], pside);
		if nil == target then
			return error_return('BUGBUG frostmire(6) target=nil');
		end

		eff_list = action_damage(target, pside, self, self.ab_power, 
			D_ICE, A_FROSTMIRE);

		-- hp is not reliable to check die
		if target:die() then 
			-- print('DEBUG frostmire(6) target die, no frozen');
			return eff_list;
		end


		-- check if ally reborn
		if target.reborn == true then
			print('DEBUG (6)frostmire_target_reborn');
			return eff_list;
		end

		-- TODO use action_attach(),  check duplicate because 
		-- the same target having 2 frozen is incorrect, use the new
		-- one or the longest one?

		--[[
		local ac;
		ac = clone(g_card_list[1006]);  -- frozen2
		-- consider action_virtual_attach

		local src = pside[self.side][T_HERO][1]; -- damage src
		eff_list2, err = action_attach(target, pside, ac, src);
		if nil == eff_list2 then
			print('BUGBUG frostmire(6) cannot attach: ', err);
			return eff_list, err;
		end
		table_append(eff_list, eff_list2);
		]]--

		eff_list2, err = action_virtual_attach(target, pside, 1006, self);
		if nil == eff_list2 then
			print('BUGBUG frostmire(1006) cannot attach: ', err);
			return eff_list, err;
		end
		table_append(eff_list, eff_list2);

		return eff_list; 
	end, -- trigger_skill }


	ai_weight = function(self, atl, pside, sss, ability) -- start {
		local target;
		local weight = 0;
		if ability then
			if self.energy < self.skill_cost_energy then
				return 0;
			end
			-- use ability
			return ai_weight_ability_damage(self, atl, pside, sss, ability) + ai_weight_control(self, atl, pside, sss, ability);
		else
			-- normal attack
			return	ai_weight_attack(self, atl, pside, sss, ability);
		end
		return weight;
	end, -- ai_weight }
}
hero_list[hero.id] = card_class:new(hero);

hero = {
	id = 7,
	ctype = HERO,
	cost = 10,
	name = 'Jericho Spellbane',
	star = 5,
	job = PRIEST + HUMAN, -- 8
	camp = HUMAN,  --  human=256,  shadow=512
	hp = 26, 
	power = 0,   -- peter: debug test, do not set power
	atype = A_ZHANNA,
	skill_desc = 'ENERGY:4  Target friendly hero or ally has all enemy attachments and negative effects removed, or target attachment is removed.',
	skill_cost_energy = 4, -- need 4 skill points to trigger 

	-- runtime attribute
	energy = 0,  -- need 3 round to 

	target_list = {
		-- DEBUG add ally, hero or attach
		{side=3, table_list={T_HERO, T_ALLY, T_ATTACH} },
	},

	-- avoid full hp
	trigger_target_validate = function(self, pside, target, tid) -- {
		if tid == 0 then
			return true;
		end
		if tid ~= 1 or target == nil then
			print('BUGBUG Jericho Spellbane(7)  target_validate tid, target:', tid, target);
			return false; -- BUG case
		end

		-- early exit on true,  last return false

		-- need to check any enemy attachment (attach_card.home ~= target.side)
		for k, ac in ipairs(target.attach_list or {}) do
			if ac.src==nil then
				print('BUGBUG Jericho Spellbane(7) ac.src=nil ac.id=', ac.id);
			end
			if nil ~= ac.src and ac.src.side ~= target.side then
				-- useful DEBUG msg do not remove
				print('DEBUG Jericho Spellbane(7) found enemy attach, target.name : ', ac.name, target.name);
				return true; -- early exit
			end
		end

		-- T_ATTACH, target is an attachment
		if target.ctype == ATTACH and target.id < 1000 then
			local index;
			index = cindex(target);
			-- print('DEBUG (156) sever ties validate index=', index);
			if index >= 9999 then
				return true;
			end
			print('ERROR (7) not an attach index: ', index);
		end

		return false;
	end, -- trigger_target_validate }

	trigger_skill = function(self, pside, atl)  -- {
		local index;
		local target;
		local eff_list = {};
		local eff_list2 = {};
		local err = nil;

		if atl == nil or #atl < 1 then
			print('BUGBUG (7) server ties atl=nil or #atl<1');
			return {};
		end

		index = atl[1];
		target = index_card(index, pside);
		if nil == target then
			return error_return('BUGBUG Jericho Spellbane(7) target=nil');
		end

		if target.ctype == HERO or target.ctype == ALLY then
			local aclist = target.attach_list or {};
			for i = #aclist, 1, -1 do
				local ac = aclist[i];
				if nil ~= ac.src and ac.src.side ~= target.side then
					eff_list2 = action_grave(ac, pside);
					table_append(eff_list, eff_list2);
				end
			end
		elseif target.ctype == ATTACH then
			eff_list2 = action_grave(target, pside);
			table_append(eff_list, eff_list2);
		else
			return error_return('BUGBUG Jericho Spellbane(7) target.ctype error');
		end

		return eff_list;

	end, -- trigger_skill }


	ai_weight = function(self, atl, pside, sss, ability) -- start {
		local target;
		local weight = 0;
		local source;	-- source of target
		local count;
		if ability then
			if self.energy < self.skill_cost_energy then
				return 0;
			end
			if #atl<=0 or atl[1]== nil then
				print('BUGBUG (7) ai_weight #atl=0 or atl[1]=nil', #atl);
				return 0;
			end
			target = atl[1];
			if target.ctype == HERO or target.ctype == ALLY then
			-- use ability
				return ai_weight_general(self, atl, pside, sss, ability);
			end

			source = target.src;
			if source == nil then
				print('BUGBUG (7) ai_weight target.src=nil',  target.id);
				return 0;
			end
			-- test: (92)inner strength and (67)crippling blow
			-- our people do this attachment, do not destroy it!
			if source.side == self.side then
				return 0;
			end
		
			-- TODO consider parent cost, power etc.
			weight = ai_weight_general(self, atl, pside, sss, ability);
			count = target.timer or 10; -- no timer is long term, let it be 10
			weight = weight + 50 * count;
			return weight;

		else
			-- normal attack
			return	ai_weight_attack(self, atl, pside, sss, ability);
		end
		return weight;
	end, -- ai_weight }
};
hero_list[hero.id] = card_class:new(hero);

hero = {
	id = 8,
	ctype = HERO,
	cost = 10,
	name = 'Zhanna Mist',
	star = 5,
	job = PRIEST + HUMAN, -- 8
	camp = HUMAN,  --  human=256,  shadow=512
	hp = 26, 
	power = 0,   -- peter: debug test, do not set power
	ab_power = 3,
	atype = A_ZHANNA,
	skill_desc = 'ENERGY:3  Target friendly hero or ally heals 3 damage',
	skill_cost_energy = 3, -- need 3 skill points to trigger 

	-- runtime attribute
	energy = 0,  -- need 3 round to 

	target_list = {
		-- DEBUG add ally, should be only hero
		{side=1, table_list={T_HERO, T_ALLY} },
	},

	-- avoid full hp
	trigger_target_validate = function(self, pside, target, tid) -- {
		if tid == 0 then
			return true;
		end
		if tid ~= 1 or target == nil then
			print('BUGBUG zhanna(8)  target_validate tid, target:', tid, target);
			return false; -- BUG case
		end

		-- early exit on true,  last return false

		-- TODO target.c.hp 
		if target:get_base_hp()  < target.hp_max then
			-- print('DEBUG zhanna(8)  not full hp, can target :', target.name, target:get_base_hp(), target.hp_max);
			return true;  -- early exit
		end

		-- implicit: full hp
		return false;
	end, -- trigger_target_validate }

	trigger_skill = function(self, pside, atl)  -- {
		local index;
		local target;
		local eff_list = {};
		local eff_list2 = {};
		local err = nil;

		target = index_card(atl[1], pside);
		if nil == target then
			return error_return('BUGBUG zhanna(8) target=nil');
		end

		eff_list = action_heal(target, pside, self, self.ab_power);

		return eff_list;

	end, -- trigger_skill }


	ai_weight = function(self, atl, pside, sss, ability) -- start {
		local target;
		local weight = 0;
		if ability then
			if self.energy < self.skill_cost_energy then
				return 0;
			end
			-- use ability
			return ai_weight_heal(self, atl, pside, sss, ability);
		else
			-- normal attack
			return	ai_weight_attack(self, atl, pside, sss, ability);
		end
		return weight;
	end, -- ai_weight }
};
hero_list[hero.id] = card_class:new(hero);

hero = {
	id = 9,
	ctype = HERO,
	cost = 10,
	name = 'Lance Shadowstalker',
	star = 5,
	job = ROGUE + HUMAN,  
	camp = HUMAN,
	hp = 27, -- consider: fix and runtime
	skill_desc = 'ENERGY:4 Target friendly ally has ambush, stealth and haste until the start of your next turn.',
	skill_cost_energy = 4, 

	-- runtime attribute
	energy = 0,  

	-- TODO consider to auto search the weapon ?  need to fix the ai
	target_list = {
		{side=1, table_list={T_ALLY} },
	},

	trigger_skill = function(self, pside, atl)  -- {
		local index;
		local target;
		local eff_list = {};
		local eff_list2 = {};
		local err = nil;

		target = index_card(atl[1], pside);
		if nil == target then
			return error_return('BUGBUG Lance Shadowstalker(9) target=nil');
		end
		
		if target.ctype~=ALLY then
			print('BUGBUG Lance Shadowstalker(9) target is not ally');
			return eff_list;
		end



		eff_list2, err = action_virtual_attach(target, pside, 1009, self);
		if nil == eff_list2 then
			print('BUGBUG Ambush stealth haste 1 round(1009) cannot attach: ', err);
			return eff_list, err;
		end
		table_append(eff_list, eff_list2);

		return eff_list;
	end, -- trigger_skill }


	ai_weight = function(self, atl, pside, sss, ability) -- start {
		local target;
		local weight = 0;
		if ability then
			if self.energy < self.skill_cost_energy then
				return 0;
			end
			-- use ability if the weapon has no attach
			
			if #(atl or {}) == 0 or atl[1]==nil then
				print('BUGBUG gwenneth(4)ai weapon is nil');
				return 0;
			end
			target = atl[1];
			if #(target.attach_list or {})>0 then
				return 0;
			end

			-- TODO target value should be checked???
			return ai_weight_general(self, atl, pside, sss, ability);
		else
			-- normal attack
			return	ai_weight_attack(self, atl, pside, sss, ability);
		end
		return weight;
	end, -- ai_weight }
};
hero_list[hero.id] = card_class:new(hero);

hero = {
	id = 10,
	ctype = HERO,
	cost = 10,
	name = 'Serena Thoughtripper',
	star = 5,
	job = ROGUE + HUMAN,  
	camp = HUMAN,
	hp = 27, -- consider: fix and runtime
	skill_desc = 'ENERGY:3  Until the end of your turn, your weapons have +2 attack, and if Serena deals combat damage to a hero, that heros owner discards a card at random.',
	skill_cost_energy = 3, 

	-- runtime attribute
	energy = 0,  

	-- TODO consider to auto search the weapon ?  need to fix the ai
	target_list = {
		{side=1, table_list={T_WEAPON} },
	},

	trigger_skill = function(self, pside, atl)  -- {
		local index;
		local target;
		local eff_list = {};
		local eff_list2 = {};
		local err = nil;

		target = index_card(atl[1], pside);
		if nil == target then
			return error_return('BUGBUG Serena(10) target=nil');
		end
		
		if target.ctype~=WEAPON then
			print('BUGBUG Serena(10) target is not weapon');
			return eff_list;
		end



		eff_list2, err = action_virtual_attach(target, pside, 1010, self);
		if nil == eff_list2 then
			print('BUGBUG Weapon Power2,Damage Discard1(1010) cannot attach: ', err);
			return eff_list, err;
		end
		table_append(eff_list, eff_list2);

		return eff_list;
	end, -- trigger_skill }

	trigger_attack = function(self, pside, target, power)
		local eff_list = {};
		local eff_list2;
		local weapon;
		local discard;

		if power <= 0 or target.table ~= T_HERO then
			return eff_list;
		end

		weapon = self.weapon;
		if nil == weapon or (#weapon.attach_list or {}) <= 0 then
			return eff_list;
		end
		for k, ac in ipairs(weapon.attach_list or {}) do
			if ac.id == 1010 then
				discard = true;
				break;
			end
		end
		if true ~= discard then
			return eff_list;
		end

		local oppo = 3 - self.side;
		local oppo_hand = pside[oppo][T_HAND];
		if #oppo_hand > 0 then
			local index;
			-- TODO NEED TEST
			index = math.random(1, #oppo_hand);
			print('random index = ' .. index);
			local cc = oppo_hand[index];

			eff_list2 = action_grave(cc, pside);
			table_append(eff_list, eff_list2);
		end

		return eff_list;
	end,

	ai_weight = function(self, atl, pside, sss, ability) -- start {
		local target;
		local weight = 0;
		if ability then
			if self.energy < self.skill_cost_energy then
				return 0;
			end
			-- use ability if the weapon has no attach
			
			if #(atl or {}) == 0 or atl[1]==nil then
				print('BUGBUG Serena(10)ai weapon is nil');
				return 0;
			end
			target = atl[1];
			if #(target.attach_list or {})>0 then
				return 0;
			end

			return 1000; -- nearly must do
		else
			-- normal attack
			return	ai_weight_attack(self, atl, pside, sss, ability);
		end
		return weight;
	end, -- ai_weight }
}
hero_list[hero.id] = card_class:new(hero);


hero = {
	id = 11,
	ctype = HERO,
	cost = 10,
	name = 'Ter Adun',
	star = 5,
	job = WARRIOR + SHADOW,  -- 1, 2, 4, 8, 16, 32... 1=warrior, 2=hunter, 4=mage
	camp = SHADOW,  --  good=256,  evil=512
	hp = 30, -- consider: fix and runtime
	power = 0, -- set as zero, cannot attack without weapon 
	atype = A_TER,
	skill_cost_energy = 4, -- how many energy to trigger skill
	skill_desc = 'ENERGY:4  Target item or support ability is destroyed, or target ally is return to owner hand.',
	target_list = {
		{side=3, table_list={T_SUPPORT, T_ALLY} },
	},

	-- target is the target card
	-- tid is the num id, correspond to target_list[tid]
	-- normally, we should check something out of the scope
	-- in target_list[tid]
	trigger_target_validate = function(self, pside, target, tid)
		if tid == 0 then
			return true;
		end
		if tid ~= 1 or target == nil then
			print('BUG: ter(11) target_validate tid, target', tid, target);
			return false;
		end
		if target.table==T_SUPPORT or target.table==T_ALLY then
			return true;
		else 
			return false;
		end
	end,

	trigger_skill = function(self, pside, actual_target_list) 
		local index;
		local target;
		local eff_list = {};
		local eff_list2 = {};
		if actual_target_list==nil or #actual_target_list~=1 then
			local err = 'BUG: ter(11) skill atl=nil or empty';
			return nil, err;
		end

		local target_list = index_card_list(actual_target_list, pside);
		for i=1, #target_list do
			target = target_list[i];
			if false == self:trigger_target_validate(pside, target, i) then
				return error_return(
					'BUG: ter(11) invalid target ' .. i .. 
					' target.id, cindex:', target.id, cindex(target)
				);
			end
		end

		index = actual_target_list[1];
		target = index_card(index, pside);

		-- print('DEBUG Boris index, target.id=', index, target.id);
		
		if self.energy < self.skill_cost_energy then
			print('ERROR ter(11) skill energy not enough');
			return nil, 'not enough energy';
		end

		-- item move to grave
		if target.table == T_SUPPORT then

			if (self.atype ~= nil) then
				local t_list = {};
				t_list[1] = target:index();
				eff_list[#eff_list+1] = eff_anim(self:index(), self.id, self.atype, t_list);
			end

			eff_list2 = action_durability_change(target, pside, -999);
			table_append(eff_list, eff_list2);
			-- skill change to move that support to my hand
			--[[
			eff_list2 = action_refresh(target, pside);
			table_append(eff_list, eff_list2);

			eff_list2 = action_move(target, pside, pside[target.side][T_SUPPORT], pside[self.side][T_HAND]);
			table_append(eff_list, eff_list2);
			]]--
			return eff_list; 
		end

		-- assume target.table == T_ALLY
		local side_home = pside[target.home];

		eff_list2 = action_refresh(target, pside);
		table_append(eff_list, eff_list2);

		-- move to hand (home)
		eff_list2 = action_move(target, pside, pside[target.side][target.table]
		, side_home[T_HAND]);
		table_append(eff_list, eff_list2);
		return eff_list;

	end,

	-- runtime attribute
	energy = 0,  -- need 3 round to 
	hp = 30,

	ai_weight = function(self, atl, pside, sss, ability) -- start {
		local target;
		local weight = 0;
		if ability then
			-- use ability to destroy support : same as general first
			-- return ai_weight_general(self, atl, pside, sss, ability);
			if self.energy < self.skill_cost_energy then
				return 0;
			end
			if #(atl or {}) == 0 or atl[1]==nil then
				print('BUGBUG ter adun(10)target is nil');
				return 0;
			end
			target = atl[1];
			-- if is own side, only return weapon or armor
			if target.side == self.side then
				if target.ctype == WEAPON or target.ctype == ARMOR then
					weight = WEIGHT_EQUIPMENT * target.cost;  -- 150*cost
					-- peter: hp * power for weapon = damage to enemy
					-- e.g. Dimension ripper: cost=5, power=2, hp=3
					-- new weight = 150*5 - 350*2*3 = -1350 (no callback)
					-- when dura(hp)=1:
					-- weight = 150*5 - 350*2*1 = 50 (positive, will do!)
					-- (184)jewelers dream:  cost=4, power=1, hp=4
					-- new weight=150*4 - 350*1*4 = -800
					-- when dura=1:  weight=150*4 - 350*1*1 = 250 (remove)
					-- note: Mournblade is an exception, since power=0
					weight = weight - WEIGHT_POWER * target.hp * target.power;
					return weight;
				end
				return -WEIGHT_EQUIPMENT; 
			end

			return 1000; -- nearly must do
		else
			-- normal attack
			return	ai_weight_attack(self, atl, pside, sss, ability);
		end
		return weight;
	end, -- ai_weight }
};
hero_list[hero.id] = card_class:new(hero);

hero = {
	id = 12,
	ctype = HERO,
	cost = 10,
	name = 'Logan Stonebreaker',
	star = 5,
	job = WARRIOR + SHADOW,  -- 1, 2, 4, 8, 16, 32... 1=warrior, 2=hunter, 4=mage
	camp = SHADOW,  --  good=256,  evil=512
	hp = 30, -- consider: fix and runtime
	power = 0, -- set as zero, cannot attack without weapon 
	skill_cost_energy = 4, -- how many energy to trigger skill
	skill_desc = 'ENERGY:4  Until the end of your turn, your weapons gain +1 attack, and if Logan deals combat damage to an ally, that ally is killed',
	target_list = {
		{side=1, table_list={T_WEAPON} },
	},

	trigger_skill = function(self, pside, atl)  -- {
		local index;
		local target;
		local eff_list = {};
		local eff_list2 = {};
		local err = nil;

		target = index_card(atl[1], pside);
		if nil == target then
			return error_return('BUGBUG (12)logan target=nil');
		end

		-- print('DEBUG (12)logan add virtual card 1012 to ' .. atl[1]);
		eff_list, err = action_virtual_attach(target, pside, 1012, self);
		return eff_list, err; 
	end, -- trigger_skill }

	-- kill ally when attack with power > 0 
	trigger_attack = function(self, pside, target, power)
		local eff_list = {};
		local weapon;
		local ac; -- attach card

		if power <= 0 or target.table ~= T_ALLY then
			return {};
		end
		weapon = self.weapon;
		if weapon == nil then
			print('DEBUG (12)logan no weapon');
			return {};
		end
		-- TODO check weapon, if the 1012 attach is there 
		-- check_attach_duplicate(c, ac)
		ac = clone(g_card_list[1012]);  -- no need to clone, for read-only
		if 0 == check_attach_duplicate(weapon, ac) then
			return {}; -- no dup means no skill attached
		end

		-- check if ally reborn
		if target.reborn == true then
			print('DEBUG (12)logan_target_reborn');
			return {};
		end

		eff_list = action_damage(target, pside, self, 99, D_NORMAL);
		return eff_list;
	end,

	-- runtime attribute
	energy = 0,  -- need 3 round to 
	hp = 30,

	ai_weight = function(self, atl, pside, sss, ability) -- start {
		local target;
		local weight = 0;
		if ability then
			-- TODO : check condition of weapon
			return ai_weight_general(self, atl, pside, sss, ability);
		else
			-- normal attack
			return	ai_weight_attack(self, atl, pside, sss, ability);
		end
		return weight;
	end, -- ai_weight }
};
hero_list[hero.id] = card_class:new(hero);

hero = {
	id = 13,
	ctype = HERO,
	cost = 10,
	name = 'Banebow',
	star = 5,
	job = HUNTER + SHADOW, 
	camp = SHADOW,  --  good=256,  evil=512
	hp = 28, -- consider: fix and runtime
	power = 0, -- set as zero, cannot attack without weapon 
	ab_power = 2,
	dtype = D_DIRECT,
	skill_cost_energy = 3, -- how many energy to trigger skill
	skill_desc = 'ENERGY:3 Up to 2 different target opposing heroes or allies take 2 damage. This damage cannot be prevented by ally or armor abilities.',
	target_list = {
		{side=2, table_list={T_HERO, T_ALLY} },
		{side=2, table_list={T_HERO, T_ALLY}, optional=true },
	},
	energy = 0,  -- need 3 round to 

	trigger_skill = function(self, pside, actual_target_list) 
		local index;
		local target_list = {};
		local eff_list = {};
		local eff_list2 = {};
		if actual_target_list==nil then
			local err = 'BUG: banebow(13) skill atl=nil or empty';
			return nil, err;
		end

		for k, at in ipairs(actual_target_list) do 
			target_list[k] = index_card(at, pside);
			if nil == target_list[k] then
				print('ERROR: banebow(13) target=nil');
				return {};
			end
		end

		eff_list2 = action_damage_list(target_list, pside, self, 
			self.ab_power, D_DIRECT);
		table_append(eff_list, eff_list2);

		--[[
		for k, target in ipairs(target_list) do
			-- note on self.ab_power, self.dtype
			eff_list2 = action_damage(target, pside, self, 
				self.ab_power, D_DIRECT);
			table_append(eff_list, eff_list2);
		end
		]]--

		return eff_list; 
	end,

	ai_weight = function(self, atl, pside, sss, ability) -- start {
		local target;
		local weight = 0;
		if ability then
			if self.energy < self.skill_cost_energy then
				return 0;
			end
			-- use ability
			return ai_weight_ability_damage(self, atl, pside, sss, ability);
		else
			-- normal attack
			return	ai_weight_attack(self, atl, pside, sss, ability);
		end
		return weight;
	end, -- ai_weight }
};
hero_list[hero.id] = card_class:new(hero);


hero = {
	id = 14,
	ctype = HERO,
	cost = 10,
	name = 'Baduruu',
	star = 5,
	job = HUNTER + SHADOW,  
	camp = SHADOW,
	hp = 28, -- consider: fix and runtime
	skill_desc = 'ENERGY:4  Target weapon in your hand is summoned at no cost and gains +1 base attack.',
	skill_cost_energy = 4, 

	-- runtime attribute
	energy = 0,  

	-- TODO consider to auto search the weapon ?  need to fix the ai
	target_list = {
		{side=1, table_list={T_HAND} },
	},

	trigger_target_validate = function (self, pside, target, tid)
		if tid == 0 then
			return true;
		end

		if tid ~= 1 or target == nil then
			print('BUGBUG (13)baduruu target_validate tid, target', tid, target);
			return false;
		end

		if target.ctype == WEAPON then
			-- implicit: unique=false or (unique==true && not_duplicate)
			return true;
		end

		return false;
	end,

	trigger_skill = function(self, pside, atl)  -- {
		local index;
		local target;
		local eff_list = {};
		local eff_list2 = {};
		local err = nil;

		target = index_card(atl[1], pside);
		if nil == target then
			return error_return('BUGBUG baduruu(14) target=nil');
		end
		
		if target.ctype~=WEAPON then
			print('BUGBUG baduruu(14) target is not weapon');
			return eff_list;
		end

		local my_supp = pside[self.side][T_SUPPORT];	
		for i = #my_supp, 1 , -1 do
			local sc = my_supp[i];
			if sc.ctype == WEAPON then
				eff_list2 = action_grave(sc, pside);
				table_append(eff_list, eff_list2);
				-- assume only one weapon/armor in the same time
			end

		end

		local my_hand = pside[self.side][T_HAND];	
		eff_list2 = action_move(target, pside, my_hand, my_supp);
		table_append(eff_list, eff_list2);

		if target.table ~= T_SUPPORT then
			print('BUGBUG baduruu(14) target weapon move to T_SUPPORT fail');
			return eff_list;
		end

		print('DEBUG baduruu(14)add virtual card 1014 to ' .. atl[1]);
		eff_list2, err = action_virtual_attach(target, pside, 1014, self);
		if nil == eff_list2 then
			print('BUGBUG weapon power1(1014) cannot attach: ', err);
			return eff_list, err;
		end
		table_append(eff_list, eff_list2);

		return eff_list;
	end, -- trigger_skill }


	ai_weight = function(self, atl, pside, sss, ability) -- start {
		local target;
		local weight = 0;
		if ability then
			if self.energy < self.skill_cost_energy then
				return 0;
			end
			-- use ability if the weapon has no attach
			
			if #(atl or {}) == 0 or atl[1]==nil then
				print('BUGBUG gwenneth(4)ai weapon is nil');
				return 0;
			end
			target = atl[1];
			if #(target.attach_list or {})>0 then
				return 0;
			end

			return WEIGHT_EQUIPMENT * target.cost; -- nearly must do
			
		else
			-- normal attack
			return	ai_weight_attack(self, atl, pside, sss, ability);
		end
		return weight;
	end, -- ai_weight }
}
hero_list[hero.id] = card_class:new(hero);

-- fire mage:
hero = {
	id = 15,
	ctype = HERO,
	cost = 10,
	name = 'Majiya',
	star = 5,
	dtype = D_FIRE,
	job = MAGE + SHADOW,  -- 1, 2, 4, 8, 16, 32...
	camp = SHADOW,  --  human=256,  shadow=512
	hp = 26, -- consider: fix and runtime
	power = 0,   -- peter: debug test, do not set power
	ab_power = 3,
	skill_desc = 'ENERGY:4  Target opposing ally takes 3 fire damage. Draw a card', 
	skill_cost_energy = 4, -- need 3 skill points to trigger 
	-- skill_cost_resource

	-- runtime attribute
	energy = 0,  -- need 3 round to 

	target_list = {
		-- DEBUG add ally, should be only hero
		{side=2, table_list={T_ALLY} },
	},

	trigger_skill = function(self, pside, atl)  -- {
		local index;
		local target;
		local eff_list = {};
		local eff_list2 = nil;

		target = index_card(atl[1], pside);
		if nil == target then
			return error_return('BUGBUG majiya(15) target=nil');
		end

		eff_list2 = action_damage(target, pside, self, self.ab_power, D_FIRE, A_MAJIYA);
		table_append(eff_list, eff_list2);

		eff_list2 = action_drawcard(pside, self.side);
		table_append(eff_list, eff_list2);

		return eff_list; 
	end, -- trigger_skill }


	ai_weight = function(self, atl, pside, sss, ability) -- start {
		local target;
		local weight = 0;
		if ability then
			if self.energy < self.skill_cost_energy then
				return 0;
			end
			-- use ability
			return ai_weight_ability_damage(self, atl, pside, sss, ability);
		else
			-- normal attack
			return	ai_weight_attack(self, atl, pside, sss, ability);
		end
		return weight;
	end, -- ai_weight }
}
hero_list[hero.id] = card_class:new(hero);

-- like ghostmaker and reserve weapon
hero = {
	id = 16,
	ctype = HERO,
	cost = 10,
	name = 'Gravebone',
	star = 5,
	job = MAGE + SHADOW,  -- 1, 2, 4, 8, 16, 32...
	camp = SHADOW,  --  human=256,  shadow=512
	hp = 26, -- consider: fix and runtime
	power = 0,   -- peter: debug test, do not set power
	skill_desc = 'ENERGY:4  Target ally in your graveyard is return to play',
	skill_cost_energy = 4, -- need 3 skill points to trigger 
	-- skill_cost_resource

	-- runtime attribute
	energy = 0,  -- need 3 round to 

	target_list = {
		{side=1, table_list={T_GRAVE} },
	},

	trigger_target_validate = function (self, pside, target, tid)
		if tid == 0 then
			return true;
		end

		if tid ~= 1 or target == nil then
			print('BUGBUG (16)gravebone target_validate tid, target', tid, target);
			return false;
		end

		if target.ctype == ALLY then
			if true==target.unique 
			and true==check_ally_duplicate(pside, self.side, target.id) then
				return false;
			end

			-- implicit: unique=false or (unique==true && not_duplicate)
			return true;
		end

		return false;
	end,

	trigger_skill = function(self, pside, atl)  -- {
		local index;
		local target;
		local eff_list = {};
		local eff_list2 = nil;
		local my_grave = pside[self.side][T_GRAVE];
		local my_ally = pside[self.side][T_ALLY];

		target = index_card(atl[1], pside);
		if nil == target then
			return error_return('BUGBUG gravebone(16) target=nil');
		end

		if target.unique == true 
		and check_ally_duplicate(pside, self.side, target.id) then
			print('BUGBUG gravebone(16) unique ally duplicate id:', target.id);
			return {};
		end

		if true ~= target.haste then 
			set_not_ready(target); 
		end

		eff_list2 = action_move(target, pside, my_grave, my_ally);
		table_append(eff_list, eff_list2);

		return eff_list; 
	end, -- trigger_skill }


	ai_weight = function(self, atl, pside, sss, ability) -- start {
		local target;
		local weight = 0;
		if ability then
			if self.energy < self.skill_cost_energy then
				return 0;
			end
			-- TODO get the best cost in grave!
			-- use ability
			return ai_weight_general(self, atl, pside, sss, ability);
		else
			-- normal attack
			return	ai_weight_attack(self, atl, pside, sss, ability);
		end
		return weight;
	end, -- ai_weight }
}
hero_list[hero.id] = card_class:new(hero);

hero = {
	id = 19,
	ctype = HERO,
	cost = 10,
	name = 'AHERO',
	star = 5,
	job = WARRIOR + HUMAN,  -- 1, 2, 4, 8, 16, 32...
	camp = HUMAN,  --  human=256,  shadow=512
	hp = 1, -- consider: fix and runtime
	power = 0,   -- peter: debug test, do not set power
	ab_power = 1,
	skill_desc = 'ENERGY:1  All heros take 1 damage.', 
	skill_cost_energy = 1, -- need 3 skill points to trigger 
	-- skill_cost_resource

	-- runtime attribute
	energy = 0,  -- need 3 round to 

	trigger_skill = function(self, pside, atl)  -- {
		local index;
		local target = nil;
		local target_list = {};
		local eff_list = {};
		local eff_list2 = {};

		target_list[1] = pside[1][T_HERO][1];
		target_list[2] = pside[2][T_HERO][1];
		for i=1, #target_list do 
			target = target_list[i];
			if nil == target then
				return error_return('BUGBUG AHERO(19) target=nil');
			end
			eff_list2 = action_damage(target, pside, self
			, self.ab_power, D_MAGIC, A_NORMAL);
			table_append(eff_list, eff_list2);
		end

		return eff_list; 
	end, -- trigger_skill }


	ai_weight = function(self, atl, pside, sss, ability) -- start {
		local target;
		local weight = 0;
		if ability then
			if self.energy < self.skill_cost_energy then
				return 0;
			end
			-- use ability
			return ai_weight_ability_damage(self, atl, pside, sss, ability);
		else
			-- normal attack
			return	ai_weight_attack(self, atl, pside, sss, ability);
		end
		return weight;
	end, -- ai_weight }
}
hero_list[hero.id] = card_class:new(hero);

-- for gate game hero
hero = {
	id = 20,
	ctype = HERO,
	cost = 10,
	name = 'GATE_HERO',
	star = 5,
	job = MAGE + SHADOW,  -- 1, 2, 4, 8, 16, 32...
	camp = SHADOW,  --  human=256,  shadow=512
	hp = 99, -- consider: fix and runtime
	power = 0,   -- peter: debug test, do not set power
	-- ab_power = 1,
	ab_power = 0,
	-- skill_desc = 'ENERGY:2  Target hero take 1 fire damage.', 
	skill_desc = '',
	-- skill_cost_energy = 2, -- need 3 skill points to trigger 
	-- skill_cost_resource

	-- runtime attribute
	energy = 0,  -- need 3 round to 
	--[[

	target_list = {
		{side=3, table_list={T_HERO} },
	},

	trigger_skill = function(self, pside, atl)  -- {
		local index;
		local target;
		local eff_list = {};

		target = index_card(atl[1], pside);
		if nil == target then
			return error_return('BUGBUG THERO(20) target=nil');
		end

		eff_list = action_damage(target, pside, self, self.ab_power, 
			D_FIRE, A_NORMAL);

		return eff_list; 
	end, -- trigger_skill }


	ai_weight = function(self, atl, pside, sss, ability) -- start {
		local target;
		local weight = 0;
		if ability then
			if self.energy < self.skill_cost_energy then
				return 0;
			end
			-- use ability
			return ai_weight_ability_damage(self, atl, pside, sss, ability);
		else
			-- normal attack
			return	ai_weight_attack(self, atl, pside, sss, ability);
		end
		return weight;
	end, -- ai_weight }
	]]--
}
hero_list[hero.id] = card_class:new(hero);

------------- HERO END ---------------

------------ CARD START ----------------
g_card_list = {}

card = {
	id = 21,
	ctype = ALLY,
	job = HUMAN,  -- human only
	camp = HUMAN,  -- human only
	name = 'Jasmine Rosecult',
	star = 4,
	cost = 3,
	power = 3,  -- attack
	hp = 4,  
	skill_desc = 'RES:2 Target oppoing ally cannot attack until the end of its controllers next turn.',
	skill_cost_resource = 2,
	target_list =  {
		{side=2, table_list={T_ALLY}},
	},

	trigger_target_validate = function (self, pside, target, tid)
		if tid == 0 then
			return true;
		end

		if tid ~= 1 or target == nil then
			return false;
		end

		if check_attach_over(target) < 0 then
			return false;
		end

		return true;
	end,

	trigger_skill = function (self, pside, atl)  -- {
		local eff_list = {};
		local eff_list2;
		local index = 0;
		local err = nil;

		if #atl <= 0 then
			print('BUGBUG Jasmine(21) #atl <= 0');
			return eff_list;
		end

		local target = index_card(atl[1], pside);

		if target == nil then
			print('BUGBUG Jasmine(21) target == nil');
			return eff_list;
		end

		if target.table ~= T_ALLY then
			return eff_list;
		end

		--[[
		local ac = clone(g_card_list[1021]);  -- 1021 is no_attack,  
		ac.src = self; -- damage src
		-- ac.home = 3 - target.side;  -- XXX exchange card may break
		-- peter: consider action_attach() with src as parameter
		if true==card_attach(target, ac) then
			-- attach = 0 means from void
			eff_list[#eff_list + 1] = eff_attach(atl[1], 0, target.id, ac.id);
			-- eff_list[1] = {'attach', target=index, attach=0, 
			-- cid=target.id, acid=ac.id};
		else
			print('ERROR Jasmine(21) cannot attach');
		end
		]]--

		eff_list2, err = action_virtual_attach(target, pside, 1021, self);
		if nil == eff_list2 then
			print('BUGBUG Jasmine(1021) cannot attach: ', err);
			return eff_list, err;
		end
		table_append(eff_list, eff_list2);


		return eff_list;  
	end, -- }

}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 22,
	ctype = ALLY,
	job = HUMAN,  -- human only
	camp = HUMAN,  -- human only
	name = 'Dirk Saber',
	star = 2,
	cost = 2,
	power = 2,  -- attack
	hp = 2,  
	skill_desc = 'Ambush (attacks by this ally cannot be defended)',
	-- TODO ambush change to boolean type, not a trigger
	ambush = true,

	ai_weight = ai_weight_attack;
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 23,
	ctype = ALLY, 
	job = HUMAN,  -- human only
	camp = HUMAN, -- human only
	name = 'Sandra Trueblade',
	star = 3,
	cost = 4,
	power = 2, 
	hp = 3,
	attack_anim = A_ICE,
	skill_desc = 'When Sandra is summoned,target player removes one of their resources from play if their resources are greater than or equal to yours.',
	trigger_add = function (target, pside, target_table)
	-- side1, side2, src, target, target_table) 
		-- side2 is my side,  side1 is opp
		local side2 = pside[target.side];
		local side1 = pside[3-target.side];
		if target_table.name==T_ALLY and side1.resource_max >= side2.resource_max 
		and side1.resource_max > 0 then
			-- notify resource_max - 1
			local eff;
			local eff_list = {};
			side1.resource_max = side1.resource_max - 1;
			eff = eff_resource_max_offset(-1, side1.id);
			eff_list[1] = eff;
			if side1.resource > side1.resource_max then
				side1.resource = side1.resource_max;
				eff = eff_resource_value(side1.resource, side1.id);
				eff_list[2] = eff;
			end
			return eff_list;
		end
		return {}; -- not prefer to return nil
	end,

	ai_weight = ai_weight_attack;
}
g_card_list[card.id] = card_class:new(card);

card = { 
	id = 24,
	ctype = ALLY,
	job = HUMAN,  -- human only
	camp = HUMAN,
	name = 'Lily Rosecult', 
	star = 4,
	cost = 4,
	power = 3,
	hp = 4,
	attack_anim = A_FIRE,
	skill_desc = 'When Lily is summoned, return the top item from your graveyard to your hand.',

	trigger_add = function (self, pside, target_table)
		local eff_list = {};
		local eff_list2;
		local offset = 0;
		local target_list = {};

		if target_table.name ~= T_ALLY then
			return eff_list;
		end

		local my_grave = pside[self.side][T_GRAVE];
		local my_hand = pside[self.side][T_HAND];
		for i = #my_grave, 1, -1 do
			local cc = my_grave[i];
			--print('DEBUG Shadow Knight grave cc: ', cc.name);
			if check_is_item(cc) then
				eff_list2 = action_move(cc, pside, my_grave, my_hand);
				table_append(eff_list, eff_list2);
				break;
			end
		end
		
		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {  -- add by kelton
	id = 25,
	ctype = ALLY,
	job = HUMAN,  -- human only
	camp = HUMAN, -- human only
	name = 'Kristoffer Wyld',
	star = 2,
	cost = 1,
	power = 1, 
	hp = 1,
	skill_desc = 'Haste ( This ally can attack and use abilities in the turn he is summoned',

	haste = true,

	ai_weight = ai_weight_attack;
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 26,
	ctype = ALLY,
	job = HUMAN,  -- human only
	camp = HUMAN,
	name = 'Puwen Bloodhelm',
	star = 2,
	cost = 2,  
	power = 2,  -- attack
	hp = 3,  
	skill_desc = '',
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 27,
	ctype = ALLY,
	job = HUMAN,  -- human only
	camp = HUMAN,
	name = 'Birgitte Skullborn',
	star = 2,
	cost = 1,
	power = 0,  -- attack  TODO power should be 0, 1 for testing
	hp = 2,  -- 
	attack_anim = A_COMBO_HIT,
	skill_desc = 'Protector (Allies without protector cannot be targeted)',
	protector = true,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 28,
	ctype = ALLY,
	job = HUMAN,  -- human only
	camp = HUMAN,
	name = 'Kurt Whitehelm',
	star = 3,
	cost = 4,
	power = 3,  -- attack  TODO power should be 0, 1 for testing
	hp = 4,  -- 
	skill_desc = 'Kurt takes 1 less damage from abilities, and has +1 attack when there are 2 or more friendly allies in play', 

	trigger_calculate_defend = function (self, pside, src, power, dtype)
	-- target, damage_type)
		-- no reduction if it is not magic (ability damage)
		if dtype < D_MAGIC or dtype >= D_DIRECT then 
			return power;
		end
		power = power - 1;
		if power < 0 then
			power = 0;
		end
		return power;
	end,

	trigger_add = function (target, pside, target_table)
	-- side_src, side_target, src, target, target_table)
		local eff;
		local eff_list = {};
		local side_target = pside[target.side]; -- or target.side

		-- print('DEBUG Kurt Whitehelm  #side_target[T_ALLY] = ', #side_target[T_ALLY]);
		-- only effective in ally table
		if side_target[T_ALLY] ~= target_table then
			return {}; -- empty, early exit
		end

		if #target_table >= 3 then 
			eff_list = action_power_offset(target, 1);
		end
		return eff_list;
	end,

	-- other card is added to this, src is 'self', target is the other card
	trigger_other_add = function(src, target, pside, target_table)
	-- side_src, side_target, src, target, target_table)
		local eff;
		local eff_list = {};
		local flag = false;
		local side_target = pside[target.side];
		-- need to check src, target are in the same side
		if src.side ~= target.side or target_table ~= side_target[T_ALLY] then
			return {};
		end;

		-- < 3 : no need
		-- > 3 : already buff'ed
		-- = 3 : need buff
		if #target_table ~= 3 then
			return {}; -- nothing to +power
		end

--		if target ~= target_table[#target_table] then
--			print('WARN: kurt not real add maybe Earthen');
--			return {};
--		end


		-- avoid double add for myself
		if src==target then
			return {};
		end

		-- double check whether Kurt is in target_table (this is strange?)
		flag = false;
		for k, v in ipairs(target_table or {}) do
			if v == src then
				flag = true;
			end;
		end
		if flag == false then
			print('BUGBUG: (28)Kurt Whitehelm not in ally but trigger_other_add');
			return {};
		end
		-- ok, flag is true and #target_table == 4
		eff_list = action_power_offset(src, 1);
		return eff_list;
	end,

	trigger_other_remove = function(src, target, pside, target_table)
		local eff;
		local eff_list = {};
		local flag = false;
		local side_target = pside[target.side];

		-- print('DEBUG Kurt trigger_other_remove : ', src.id, target.id, target_table.name);
		-- need to check whether they are the same side
		if src.side~=target.side or target_table ~= side_target[T_ALLY] then
			return {};
		end;
		if target.ctype == ATTACH then
			-- do not count attachment
			-- print('DEBUG kurt remove_other avoid attach');
			return {};
		end

		-- < 2 : no need
		-- > 2 : still >= 3, no need to change
		-- = 2 : need debuff
		if #target_table ~= 2 then
			return {}; -- nothing to +power
		end

		-- avoid double add for myself
		if src==target then
			return {};
		end

		-- ok, #target_table == 2 and this is remove
		-- so power offset must not nil
		if src.power_offset==nil then
			print('BUGBUG kurt remove_other power_offset=nil');
			return eff_list;
		end
		eff_list = action_power_offset(src, -1);
		return eff_list;
	end,
}
-- WARN: kurt whitehelm is only partially implemented
g_card_list[card.id] = card_class:new(card);

card = {
	id = 29,
	ctype = ALLY,
	job = HUMAN,  -- human only
	camp = HUMAN,
	name = 'Erika Shadowhunter',
	star = 4,
	cost = 5,
	power = 4,
	hp = 3,
	stealth = true,
	skill_desc = 'Stealth (This ally canot be attacked.)',
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 30,
	ctype = ALLY,
	job = HUMAN,  -- human only
	camp = HUMAN,
	name = 'Blake Windrunner',
	star = 1,
	cost = 2,
	power = 3,
	hp = 1,
	skill_desc = '',
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 31,
	ctype = ALLY,
	job = HUMAN,  -- human only
	camp = HUMAN,
	name = 'Raven Wildheart',
	star = 5,
	cost = 5,
	power = 3,
	hp = 6,
	skill_desc = 'When an ally is damaged by Raven in combat, that ally base attack is reduced to 0.',

	trigger_attack = function(self, pside, target, power)

		local eff_list = {};
		local eff_list2 = {};
		if nil == target then
			print('BUGBUG Raven(31) nil target');
			return eff_list;
		end

		if power == 0 then
			return eff_list;
		end

		-- need to check target table
		if target:die() then
			return eff_list;
		end

		if target.table ~= T_ALLY then
			return eff_list;
		end

		-- check target is reborn by earthen protector(39)
		if target.reborn == true then
			return eff_list;
		end


		eff_list2 = action_power_change(target, pside, -99);
		table_append(eff_list, eff_list2);
		-- local offset;
		-- offset = target:change_power(-99); -- hardcode base attack should be 0
		-- eff_list[#eff_list+1] = eff_power_offset(cindex(target), offset);

		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 32, -- TODO
	ctype = ALLY,
	job = HUMAN,  -- human only
	camp = HUMAN,
	name = 'Aldon the Brave',
	star = 4,
	cost = 3,
	power = 2,
	hp = 4,
	unique = true,
	skill_desc = 'Friendly allies have +1 attack on your turn while Aldon is in play.',

	target_list = {
		{ side = 1, table_list = { T_ALLY } },
	},

	trigger_add = function (self, pside, target_table)
		local offset = 0;
		local eff_list = {};
		local eff_list2;
		local err;
		local list = pside[self.side][T_ALLY];
		local index_list = {};

		-- need to check target table
		if target_table.name ~= T_ALLY then 
			return eff_list;
		end

		for k, v in ipairs(list) do
			index_list[#index_list + 1] = v:index();
		end
		-- only for client anim
		eff_list[#eff_list + 1] = eff_anim(self:index(), self.id
		, A_POWER_UP, index_list);

		for k, target in ipairs(list) do
			-- peter: self is also +1 attack
			--[[
			eff_list2 = action_power_offset(target, 1);
			table_append(eff_list, eff_list2);
			]]--
			eff_list2, err = action_virtual_attach(target, pside, 1032, self);
			if nil == eff_list2 then
				print('BUGBUG (1032)Aldon cannot attach: ', err);
				return eff_list, err;
			end
			table_append(eff_list, eff_list2);
		end
		return eff_list;
	end,

	-- call this when Aldon then Brave is already in ALLY and other cards are added
	trigger_other_add = function(src, target, pside, target_table)
	-- (side_src, side_target, src, target, target_table)
		-- src is self, this Aldon then Brave, so we need to check whether
		-- src and target are the same side
		local eff_list = {};
		local eff_list2;
		local err;
		local offset = 0;

		if target_table.name ~= T_ALLY then 
			return eff_list;
		end
		-- Aldon then Brave is not in the same side of the add_other target
		if src.side ~= target.side then
			return eff_list; -- early exit
		end

		if target == src then
			return eff_list;
		end

		--[[
		eff_list2 = action_power_offset(target, 1);
		table_append(eff_list, eff_list2);
		]]--
		eff_list2, err = action_virtual_attach(target, pside, 1032, src);
		if nil == eff_list2 then
			print('BUGBUG (1032)Aldon cannot attach: ', err);
			return eff_list, err;
		end
		table_append(eff_list, eff_list2);

		return eff_list;
	end,

	-- fix: (200)dagger of umaking : retreat Aldon but power not --
	-- trigger_die = function (self, pside, src)
	trigger_remove = function(self, pside, src_table)
		-- self is the one who die, src is the killer
		local offset = 0;
		local eff_list = {};
		local eff_list2;
		-- masha: use old_side
		local list = {};
		local index_list = {};
		list = pside[src_table.side][T_ALLY];

		if src_table.name ~= T_ALLY then
			return {}; -- aldon may be remove from deck,hand,grave etc
		end

		for k, v in ipairs(list) do
			index_list[#index_list + 1] = v:index();
		end
		-- only for client anim
		eff_list[#eff_list + 1] = eff_anim(self:index(), self.id
		, A_POWER_DOWN, index_list);

		eff_list2 = action_virtual_remove(list, pside, self);
		table_append(eff_list, eff_list2);

		--[[
		for k, target in ipairs(list) do
			for _, ac in ipairs(target.attach_list or {}) do
				if ac.src == self then
					eff_list2 = action_grave(ac, pside); -- XXX 
					table_append(eff_list, eff_list2);
				end
			end
		end
		]]--

		return eff_list;
	end,

	-- trigger that works when turn start
	trigger_turn_start = function(self, pside, sss)
		-- self, pside
		local eff_list = {};
		local eff_list2;
		local err;

		if sss ~= self.side then
			return {}
		end

		local list = pside[self.side][T_ALLY];

		local index_list = {};
		for k, v in ipairs(list) do
			index_list[#index_list + 1] = v:index();
		end
		-- only for client anim
		eff_list[#eff_list + 1] = eff_anim(self:index(), self.id
		, A_POWER_UP, index_list);

		-- my side
		for k, target in ipairs(list) do
			--peter: self is also +1 attack
			--[[
			eff_list2 = action_power_offset(target, 1);
			table_append(eff_list, eff_list2);
			]]--
			eff_list2, err = action_virtual_attach(target, pside, 1032, self);
			if nil == eff_list2 then
				print('BUGBUG (1032)Aldon cannot attach: ', err);
				return eff_list, err;
			end
			table_append(eff_list, eff_list2);
		end

		return eff_list;
	end,

}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 33,
	ctype = ALLY,
	job = HUMAN,  -- human only
	camp = HUMAN,
	name = 'Zoe Silversmith',
	star = 3,
	cost = 3,
	power = 2,
	hp = 4,
	skill_desc = 'RES:2 Target weapon you control gains +1 durability.',
	skill_cost_resource = 2,
	target_list = {
		{ side = 1, table_list = { T_WEAPON } },
	},
	trigger_skill = function (self, pside, actual_target_list)
		local offset = 0;
		local index;
		local target;
		local eff_list = {};
		local eff_list2;
		if actual_target_list==nil or #actual_target_list<1 then
			print('BUGBUG Zoe Silversmith(33) atl=nil or #<1');
			return eff_list;
		end
		index = actual_target_list[1];
		target = index_card(index, pside);
		
		if target.ctype~=WEAPON then
			print('BUGBUG Zoe Silversmith(33) target is not weapon or armor');
			return eff_list;
		end

		--[[
		offset = target:change_hp(1);
		eff_list[#eff_list+1] = eff_hp(cindex(target), offset);
		]]--
		eff_list2 = action_durability_change(target, pside, 1);
		table_append(eff_list, eff_list2);

		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 34,
	ctype = ALLY,
	job = HUMAN,  -- human only
	camp = HUMAN,
	name = 'Katrin the Shieldmaiden',
	star = 3,
	cost = 2,
	power = 0,
	hp = 4,
	skill_desc = 'RES:1 Target friendly ally gains +2 health. This ability cannot used again while that ally is in play.',
	skill_cost_resource = 1,
	target_list = {
		{ side = 1, table_list = { T_ALLY } },
	},

	-- peter: check if 2 x katrin can do 2 virtual attach +2 hp?
	-- also is this change hp_max ???  can it be heal?
	-- "heal" +2 is different from gain +2 heal, very likely: hp_max+2
	trigger_target_validate = function(self, pside, target, tid) -- {
		-- first test on tid=0

		if tid == 0 then 
			local my_ally = pside[self.side][T_ALLY];
			for i, t in ipairs(my_ally or {}) do
				for k, ac in ipairs(t.attach_list or {}) do
					if ac.src==nil then
						print('BUGBUG Katrin(34) ac.src=nil ac.id=', ac.id);
					end
					if nil ~= ac.src and ac.src == self then
						-- print('DEBUG Katrin(34) T_ALLY found attach, t.name, ac.src.name=', t.name, ac.src.name);
						return false; -- early exit
					end
				end
			end
			return true;
		end

		if tid ~= 1 or target == nil then
			print('BUGBUG Katrin(34)  target_validate tid, target:', tid, target);
			return false; -- BUG case
		end

		local my_ally = pside[self.side][T_ALLY];
		for i, t in ipairs(my_ally or {}) do
			for k, ac in ipairs(t.attach_list or {}) do
				if ac.src==nil then
					print('BUGBUG Katrin(34) ac.src=nil ac.id=', ac.id);
				end
				if nil ~= ac.src and ac.src == self then
					-- print('DEBUG Katrin(34) T_ALLY found attach, t.name, ac.src.name=', t.name, ac.src.name);
					return false; -- early exit
				end
			end
		end

		for k, ac in ipairs(target.attach_list or {}) do
			if ac.src==nil then
				print('BUGBUG Katrin(34) ac.src=nil ac.id=', ac.id);
			end
			if nil ~= ac.src and ac.id == (self.id + 1000) then
				-- print('DEBUG Katrin(34)  target already attach target.name : ', ac.name, target.name);
				return false; -- early exit
			end
		end

		if check_attach_over(target) < 0 then
			return false;
		end


		return true;

	end, -- trigger_target_validate }

	trigger_skill = function (self, pside, actual_target_list)
		local offset = 0;
		local index;
		local target;
		local eff_list = {};
		local eff_list2;
		local err;
		if actual_target_list==nil or #actual_target_list<1 then
			print('BUGBUG Katrin(34) atl=nil or #<1');
			return eff_list;
		end
		index = actual_target_list[1];
		target = index_card(index, pside);

		eff_list2, err = action_virtual_attach(target, pside, 1034, self);
		if nil == eff_list2 then
			print('BUGBUG Katrin(1034) cannot attach: ', err);
			return eff_list, err;
		end
		table_append(eff_list, eff_list2);

		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 35,
	ctype = ALLY, 
	job = HUMAN,  -- human only
	camp = HUMAN, -- human only
	name = 'Priest of the Light',
	star = 3,
	cost = 3,
	power = 3, 
	hp = 3,
	skill_desc = 'When Priest of the Light is summoned, opposing heros lose 1 shadow energy, and your hero gains +1 health.',
	trigger_add = function (target, pside, target_table)
	-- side1, side2, src, target, target_table) 
		-- side2 is my side,  side1 is opp

		local side2 = pside[target.side];
		local side1 = pside[3-target.side];

		if target_table.name ~= T_ALLY then
			return {};
		end
		

		local eff_list = {};

		local oppo_hero = side1[T_HERO][1];

		if oppo_hero == nil then
			print('BUGBUG Priest of the Light(35) oppo_hero is nil');
			return {};
		end

		-- peter: fix using action_energy, no need <= 0 check
		eff_list = action_energy(-1, pside, 3-target.side);

		local my_hero = side2[T_HERO][1];

		local offset = my_hero:change_base_hp(1);
		eff_list[#eff_list + 1] = eff_hp(cindex(my_hero), offset);

		return eff_list; -- not prefer to return nil
	end,

	ai_weight = ai_weight_attack;
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 36,
	ctype = ALLY,
	job = HUMAN,  -- human only
	camp = HUMAN,
	name = 'Armored Sandworm',
	star = 4,
	cost = 5,
	power = 2,
	hp = 5,
	attack_anim = A_COMBO_HIT,
	skill_desc = 'All damage to Armored Sandworm is reduced by 2',

	trigger_calculate_defend = function (self, pside, src, power, dtype)
		if dtype >= D_DIRECT then
			return power;
		end
		-- regardless of damage_type
		power = power - 2;
		if power < 0 then
			power = 0;
		end
		return power;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 37,
	ctype = ALLY,
	job = HUMAN,  -- human only
	camp = HUMAN,
	name = 'Marshland Sentinel',
	star = 3,
	cost = 4,
	power = 2,
	hp = 4,
	attack_anim = A_COMBO_HIT,
	skill_desc = 'ENERGY:1 Target opposing ally is reduced to 0 base attack',
	target_list = {
		{side = 2, table_list = { T_ALLY }},	
	},
	skill_cost_energy = 1,

	trigger_skill = function(self, pside, atl)

		local eff_list = {};
		local eff_list2;
		local target;
		local index;

		if atl==nil or #atl<1 then
			print('BUGBUG Marshland(37) atl=nil or #<1');
			return eff_list;
		end
		index = atl[1];
		target = index_card(index, pside);
		if nil == target then
			print('BUGBUG Marshland(37) nil target');
			return eff_list;
		end

		if target.table ~= T_ALLY then
			return eff_list;
		end

		--[[
		local offset;
		offset = target:change_power(-99); -- hardcode base attack should be 0
		eff_list[#eff_list+1] = eff_power_offset(cindex(target), offset);
		]]--
		local cc = target.c;
		local base_power = cc.power;
		eff_list2 = action_power_change(target, pside, -base_power);
		table_append(eff_list, eff_list2);

		return eff_list;

	end,

}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 38,
	ctype = ALLY,
	job = HUMAN,  -- human only
	camp = HUMAN,
	name = 'Tainted Oracle',
	star = 3,
	cost = 4,
	power = 2,
	hp = 2,
	attack_anim = A_FIRE,
	skill_desc = 'When Tainted Oracle is killed, draw 2 cards',

	trigger_die = function (self, pside, src)
		-- self is the one who die, src is the killer
		local eff_list = {};
		local eff_list2 = {};
		-- TODO check what if the hero does not have card in deck?
		-- shall we -1 hp ?

		for i=1, 2 do
			eff_list2 = action_drawcard(pside, self.side);
			table_append(eff_list, eff_list2);
		end

		return eff_list;
	end
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 39,
	ctype = ALLY,
	job = HUMAN,  -- human only
	camp = HUMAN,
	name = 'Earthen Protector',
	star = 4,
	cost = 5,
	power = 4,
	hp = 5,
	attack_anim = A_FIRE,
	unique = true,  -- TODO need to implement unique in core logic
	skill_desc = 'When a friendly ally is killed while Earthen Protector is alive and can be damaged, that ally is returned to play with +2 base attack and +2 health, and Earthen Protector is killed.',

	-- self = me , target = dier , src = killer
	trigger_other_kill = function(self, target, src, pside, old_side)
		local eff_list = {};
		local eff_list2;
		local offset = 0;

		if self == target then
			return eff_list;
		end

		-- TODO use old_side to check! check with dimension ripper
		if old_side ~= self.side or target.ctype ~= ALLY then
			-- print('BUGBUG Earthen Protector(39) target illegal ' ,target.side, target.table);
			return eff_list;
		end

		if target.table ~= T_GRAVE then
			print('BUGBUG (39)earthen target not in grave! ', target.table);
			return eff_list;
		end

		-- if Earthen cannot be damage, no eff
		if self:trigger_calculate_defend(pside, src, 99, D_MAGIC) == 0 then
			return eff_list;
		end

		-- core logic
		-- 1. kill earthen, 2. move target from T_GRAVE to T_ALLY
		-- 3. add power+2, hp+2

		local my_grave = pside[target.side][T_GRAVE]; -- note: maybe in oppo grave
		local my_ally = pside[self.side][T_ALLY];
		local my_hero = pside[self.side][T_HERO][1];
		-- 1. kill earthen
		-- kill=2 is ok, checked mournblade in SE
		eff_list2 = action_damage(self, pside, my_hero, 99, D_DIRECT);
		table_append(eff_list, eff_list2);

		-- 2. move target from T_GRAVE to T_ALLY
		eff_list2 = action_move(target, pside, my_grave, my_ally);
		table_append(eff_list, eff_list2);

		-- if target die after action_move, (Death Trap(85)), early exit
		if target:die() == true then
			return eff_list;
		end

		-- 3. power+2, hp+2
		-- change base power, not power offset
		eff_list2 = action_power_change(target, pside, 2); 
		table_append(eff_list, eff_list2);

		-- since previous damage has reduce hp, we should use full_hp + 2 
		offset = target:change_base_hp(2); -- peter: fix offset on client?
		eff_list[#eff_list+1] = eff_hp(cindex(target), offset);

		-- defend flag for action_attack, target cannot defend
		-- BUGBUG use magic to kill target, target resurrect, use another ally
		-- to attack target, cannot defend attack
		set_not_ready(target); -- haste does not have effect (tested)
		target.stop_defend = true;
		target.reborn = true;

		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 40,
	ctype = ALLY,
	job = HUMAN,  -- human only
	camp = HUMAN,
	name = 'Aeon Stormcaller',
	star = 5,
	cost = 6,
	power = 3,  -- attack  TODO power should be 0, 1 for testing
	hp = 8,  -- 
	attack_anim = A_COMBO_HIT,
	unique = true,
	skill_desc = 'Protector (Allies without protector cannot be targeted) RES:3 Target other friendly ally gains +1 base attack and +1 health.',
	protector = true,
	skill_cost_resource = 3,
	target_list = {
		{ side = 1, table_list = { T_ALLY } },
	},

	trigger_target_validate = function(self, pside, target, tid)
		if tid == 0 then
			return true;
		end
		if tid ~= 1 or target == nil then
			print('BUG: Aeon(40) target_validate tid, target', tid, target);
			return false;
		end

		if target == self then
			return false;
		end

		return true;
	end,

	trigger_skill = function(self, pside, actual_target_list) 
		local eff_list = {};
		local eff_list2 = {};
		local offset = 0;
		local target;

		-- because when this is called, target already go to grave
		-- so cannot use condition 'target.table ~= T_ALLY'

		target = index_card(actual_target_list[1], pside);
		if nil == target then
			return error_return('BUGBUG Aeon(40) target=nil');
		end

		if target.ctype ~= ALLY then
			return eff_list;
		end

		
		eff_list2 = action_power_change(target, pside, 1); -- was power_offset
		table_append(eff_list, eff_list2);

		offset = target:change_base_hp(1);
		eff_list[#eff_list + 1] = eff_hp(cindex(target), offset);
		
		return eff_list;
	end,

}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 41,
	ctype = ALLY,
	job = SHADOW,  -- shadow only
	camp = SHADOW,
	name = 'Deathbone',
	star = 3,
	cost = 3,
	power = 2,
	die_power = 2,  -- damage when die
	hp = 2,
	attack_anim = A_FIRE,
	skill_desc = 'When Deathbone is killed, its killer takes 2 damage',

	trigger_die = function (self, pside, src) -- self = self
		-- src is the attacker, self is usually 'me'
		-- damage type is 2, magic
		local dtype = r_damage_map['magic'];
		local eff_list;
		if src:die()==true then
			return {};
		end
		-- print ('DEBUG deathbone die,  return 2 damage, dtype=' .. dtype);
		eff_list = action_damage(src, pside, self, self.die_power, dtype);
		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 42,
	ctype = ALLY,
	job = SHADOW,  -- shadow only
	camp = SHADOW,
	name = 'Keldor',
	star = 2,
	cost = 3,
	power = 2,
	hp = 4,
	skill_desc = 'RES:2 Keldor has +2 attack until the start of your next turn',
	skill_cost_resource = 2,
	target_list = {
		{ side = 1, table_list = { T_ALLY } },
	},

	trigger_target_validate = function(self, pside, target, tid)
		if tid == 0 then
			return true;
		end
		if tid ~= 1 or target == nil then
			print('BUG: Keldor(42) target_validate tid, target', tid, target);
			return false;
		end

		if target ~= self then
			return false;
		end

		if check_attach_over(target) < 0 then
			return false;
		end

		return true;
	end,
	trigger_skill = function (self, pside, actual_target_list) 
		local eff_list = {};
		local eff_list2;
		local ac;
		local err = nil;
		-- self:change_power(2);
		-- print ('DEBUG keldor +2 attack virtual attach');

		--[[
		ac = clone(g_card_list[1042]);
		ac.src = self; -- cyclic reference?
		-- TODO eff_power +2 maybe wrong becuse of crippling blow
		if true == card_attach(self, ac) then
			eff_list[1] = eff_power_offset(self:index(), 2);
		else
			print('ERROR 42 keldor cannot attach');
		end
		]]--

		eff_list2, err = action_virtual_attach(self, pside, 1042, self);
		if nil == eff_list2 then
			print('BUGBUG keldor(1042) cannot attach: ', err);
			return eff_list, err;
		end
		table_append(eff_list, eff_list2);

		return eff_list;  
	end,

	ai_weight = function(self, atl, pside, sss, ability)
		-- shall we check resource/energy?
		if ability then
			return WEIGHT_POWER * 2; -- TODO shall we + WEIGHT_RESOURCE * 2 ?
		else
			return ai_weight_attack(self, atl, pside, sss, ability);
		end
	end, -- ai_weight
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 43,
	ctype = ALLY,
	job = SHADOW,  -- shadow only
	camp = SHADOW,
	name = 'Infernal Gargolye',
	star = 4,
	cost = 3,
	power = 2,
	hp = 4,
	attack_anim = A_COMBO_HIT,
	skill_desc = 'All damage to Infernal Gargolye is reduced by 1', 
	trigger_calculate_defend = function (self, pside, src, power, dtype)
		if dtype >= D_DIRECT then
			return power;
		end
		power = power - 1;
		-- reduction not less than zero
		if power < 0 then
			power = 0;
		end
		return power;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 44,
	ctype = ALLY,
	job = SHADOW,  -- shadow only
	camp = SHADOW,
	name = 'Brutalis',
	star = 1,
	cost = 2,
	power = 1,
	hp = 4,
	skill_desc = '',
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 45,
	ctype = ALLY,
	job = SHADOW,  -- shadow only
	camp = SHADOW,
	name = 'Plasma Behemoth',
	star = 4,
	cost = 5,
	power = 3,
	ab_power = 4,
	dtype = D_MAGIC,
	atype = A_NORMAL,
	hp = 5,
	attack_anim = A_FIRE,
	skill_desc = 'RES:3 Target opposing hero or ally takes 4 damage.',
	skill_cost_resource = 3,
	target_list =  {
		{side=2, table_list={T_HERO, T_ALLY}},
	},
	trigger_skill = function (self, pside, atl)  -- {
		local eff_list = {};
		if #atl <= 0 then
			print('BUGBUG plasma skill #atl<=0');
			return error_return('BUGBUG plasma(45) #atl<=0');
		end

		local target;
		target = index_card(atl[1], pside);
		if nil == target then
			return error_return('BUGBUG plasma(45) nil target');
		end
		eff_list = action_damage(target, pside, self, 
			self.ab_power, self.dtype, self.atype);
		return eff_list;  
	end, -- }

	ai_weight = function(self, atl, pside, sss, ability)
		-- shall we check resource/energy?
		local weight;
		if self.table == T_HAND then
			return ai_weight_ally(self, {}, pside, sss, ability);
		end
		if ability then
			weight =  ai_weight_ability_damage(self, atl, pside, sss, ability);
		else
			weight =  ai_weight_attack(self, atl, pside, sss, ability);
		end
		return weight;
	end, -- ai_weight

}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 46,
	ctype = ALLY,
	job = SHADOW,  -- shadow only
	camp = SHADOW,
	name = 'Chimera',
	star = 3,
	cost = 4,
	power = 2,
	hp = 5,
	skill_desc = 'RES:1 Chimera has +3 attack and reduced by 3 health until the start of your next turn',
	skill_cost_resource = 1,
	target_list = {
		{ side = 1, table_list = { T_ALLY } },
	},

	trigger_target_validate = function(self, pside, target, tid)
		if tid == 0 then
			return true;
		end
		if tid ~= 1 or target == nil then
			print('BUG: Chimera(46) target_validate tid, target', tid, target);
			return false;
		end

		if self.hp <=3 then
			return false;
		end 

		if target ~= self then
			return false;
		end

		if check_attach_over(target) < 0 then
			return false;
		end

		return true;
	end,

	trigger_skill = function (self, pside, actual_target_list) 
		local eff_list = {};
		local eff_list2;
		local ac;
		local err = nil;
		-- self:change_power(2);
		print ('DEBUG Chimera +3 attack -3 health virtual attach');
		if self.hp <= 3 then
			print( 'BUGBUG Chimera (46) hp <= 3');
			return {};
		end

		local old_power = self.power;
		local new_power;

		--[[
		ac = clone(g_card_list[1046]);
		ac.src = self; -- cyclic reference?
		if true == card_attach(self, ac) then
			new_power = self.power;

			eff_list[1] = eff_power_offset(self:index()
						, new_power - old_power);
			eff_list[2] = eff_hp(self:index(), -3);
		else
			print('ERROR 46 Chimera cannot attach');
		end
		]]--

		eff_list2, err = action_virtual_attach(self, pside, 1046, self);
		if nil == eff_list2 then
			print('BUGBUG chimera(1046) cannot attach: ', err);
			return eff_list, err;
		end
		table_append(eff_list, eff_list2);
		-- eff_list[#eff_list + 1] = eff_power(self:index(), 3);
		-- eff_list[#eff_list + 1] = eff_hp(self:index(), -3);

		return eff_list;  
	end,

	ai_weight = function(self, atl, pside, sss, ability)
		-- shall we check resource/energy?
		if ability then
			return WEIGHT_POWER * 3 + WEIGHT_HURT * self.cost * 3
					+ WEIGHT_HURT_POWER * self.power
					+ WEIGHT_RESOURCE; -- 
		else
			return ai_weight_attack(self, atl, pside, sss, ability);
		end
	end, -- ai_weight
}
g_card_list[card.id] = card_class:new(card);


card = {
	id = 47,
	ctype = ALLY,
	job = SHADOW,  -- shadow only
	camp = SHADOW,
	name = 'Fire Snake',
	star = 1,
	cost = 1,
	power = 1,
	hp = 2,
	attack_anim = A_ICE,
	skill_desc = '',
}
g_card_list[card.id] = card_class:new(card);


card = {
	id = 48,
	ctype = ALLY,
	job = SHADOW,  -- shadow only
	camp = SHADOW,
	name = 'Belladonna',
	star = 3,
	cost = 4,
	power = 4,
	hp = 2,
	attack_anim = A_LIGHTNING,
	skill_desc = 'When summoned draw a card',
	trigger_add = function(self, pside, target_table) -- {
		local eff;
		local eff_list = {};
		if pside[self.side][T_ALLY] ~= target_table then
			return eff_list;
		end

		-- ok, we are on ally

		eff_list = action_drawcard(pside, self.side);
		return eff_list;
	end, -- trigger_add }
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 49,
	ctype = ALLY,
	job = SHADOW,  -- shadow only
	camp = SHADOW,
	name = 'Bad Wolf',
	star = 3,
	cost = 3,
	power = 3,
	hp = 4,
	attack_anim = A_FIRE,
	skill_desc = 'Bad Wolf heals 1 damage at the start of each of its controller turns.',

	-- trigger that works when turn start
	trigger_turn_start = function(self, pside, sss)
		-- self, pside
		local eff_list = {};
		local eff_list2;

		if sss ~= self.side then
			return {}
		end

		eff_list2 = action_heal(self, pside, self, 1);
		table_append(eff_list, eff_list2);

		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 50,
	ctype = ALLY,
	job = SHADOW,  -- shadow only
	camp = SHADOW,
	name = 'Medusil',
	star = 3,
	cost = 3,
	power = 3,
	hp = 4,
	skill_desc = 'RES:1 Target opposing ally cannot defend until the end of your turn.',
	skill_cost_resource = 1,

	target_list = {
		{ side = 2, table_list = { T_ALLY } },
	},

	trigger_target_validate = function (self, pside, target, tid)
		if tid == 0 then
			return true;
		end

		if tid ~= 1 or target == nil then
			return false;
		end

		if check_attach_over(target) < 0 then
			return false;
		end

		return true;
	end,

	trigger_skill = function (self, pside, atl) 
		local eff_list = {};
		local eff_list2;
		local ac;
		local err = nil;

		if #atl <= 0 then
			print('BUGBUG Medusil skill #atl<=0');
			return error_return('BUGBUG Medusil(50) #atl<=0');
		end

		local target;
		target = index_card(atl[1], pside);
		if nil == target then
			return error_return('BUGBUG Medusil(50) nil target');
		end

		--[[
		ac = clone(g_card_list[1050]);
		ac.src = self; -- cyclic reference?
		if true == card_attach(target, ac) then
			eff_list[1] = eff_attach(atl[1], 0, target.id, ac.id);
		else
			print('ERROR Medusil(50) cannot attach');
		end
		]]--

		eff_list2, err = action_virtual_attach(target, pside, 1050, self);
		if nil == eff_list2 then
			print('BUGBUG Medusil(1050) cannot attach: ', err);
			return eff_list, err;
		end
		table_append(eff_list, eff_list2);

		return eff_list;  
	end,

	ai_weight = function(self, atl, pside, sss, ability)
		-- shall we check resource/energy?
		if ability then
			return WEIGHT_POWER * 2; -- 
		else
			return ai_weight_attack(self, atl, pside, sss, ability);
		end
	end, -- ai_weight
}
g_card_list[card.id] = card_class:new(card);

card = { 
	id = 51,
	ctype = ALLY,
	job = SHADOW,  -- shadow only
	camp = SHADOW,
	name = 'Hellsteed',
	star = 1,
	cost = 1,
	power = 0,
	hp = 2,
	attack_anim = A_FIRE,
	skill_desc = 'The first other friendly ally to attack during your turn does +1 damage',
	-- make a table
	--  { target_type, max_use_count, used_count, eff_function }
	use_max = 1,
	-- if we have warbanner, hellsteed+1, ready=1 implementation not work
	used = 0, -- @see trigger_turn_start to update it (this is for turn 1)
	-- testcase:  hero (16) gravebone : has hellsteed, find someone attack
	-- nova (all ally die), gravebone use skill to resurrect hellsteed
	-- need portal?
	trigger_calculate_other_attack = function(self, target, power)
		--local eff_list = {};
		--local eff_list2;
		-- condition
		local target_type = ALLY;
		local target_id = { 0 }; -- {} contains 0 means everyone in T_ALLY
		--
		if self.side ~= target.side then
			return power;
		end
		if self == target then
			return power;
		end
		-- may need to change when we have (187) beetle demon bow
		if target_type ~= target.ctype then
			return power;
			--return eff_list, target.power;
		end
		if self.used >= self.use_max then
			return power;
			--return eff_list, target.power;
		end

		local condition = false;
		for i=1, #target_id do
			local t_id = target_id[i];
			if 0 == t_id or target.id == t_id then
				condition = true;
				break;
			end
		end
		if false == condition then
			return power;
			--return eff_list, target.power;
		end
		
		print('DEBUG hellsteed(51) +1 attack for ', target.id);

		self.used = self.used + 1;
		return power + 1;
		--[[
		local index;
		target.power_offset = (target.power_offset or 0) + 1;
		index = card_index(target.side, target.table, target.pos);
		eff_list[#eff_list + 1] = {'power_offset', offset=1, index=index};
		print_eff_list(eff_list);
		return eff_list, target.power;
		]]--
	end,

	trigger_turn_start = function(self, pside, sss)
		if sss == self.side then
			self.used = 0;
		else
			self.used = 1;
		end
		--print('DEBUG Hellsteed used, use_max: ', self.used, self.use_max);
	end,
}
g_card_list[card.id] = card_class:new(card);

card = { 
	id = 52,
	ctype = ALLY,
	job = SHADOW,  -- shadow only
	camp = SHADOW,
	name = 'Dark Flayer', 
	star = 2,
	cost = 2,
	power = 2,
	hp = 1,
	-- TODO need to implement defender logic
	skill_desc = 'Defender.(This ally attacks first when defending.)',

	defender = true,
}
g_card_list[card.id] = card_class:new(card);

card = { 
	id = 53,
	ctype = ALLY,
	job = SHADOW,  -- shadow only
	camp = SHADOW,
	name = 'Death Mage Thaddeus', 
	star = 4,
	cost = 3,
	power = 0,
	ab_power = 2,
	dtype = D_MAGIC, -- always has dtype when we have ab_power (ai_weight_ability_damage)
	hp = 4,
	attack_anim = A_FIRE,
	unique = true,
	skill_desc = 'When Thaddeus is summoned, target opposing hero or ally takes 1 damage. RES:0 Target opposing hero or ally takes 2 damage.',
	target_list = {
		{ side = 2, table_list = { T_HERO, T_ALLY } },
	},
	skill_cost_energy = 0,

	trigger_cast_target = function(self, pside, atl)
		local eff_list = {};
		local target;

		if self.table ~= T_ALLY then
			print('BUGBUG Thaddeus(53) not in T_ALLY', self.table);
			return eff_list;
		end

		-- XXX should send atl to action_cast_target
		if atl == nil or atl[1] == nil then
			atl = {};
			-- XXX hard code opposing hero
			local oppo_hero = pside[3-self.side][T_HERO][1];
			atl[1] = cindex(oppo_hero);
		end

		target = index_card(atl[1], pside);

		eff_list = action_damage(target, pside, self, 1, D_MAGIC);
		return eff_list;
	end,


	trigger_skill = function(self, pside, actual_target_list) 
		local index;
		local target;
		local eff_list = {};

		target = index_card(actual_target_list[1], pside);
		if nil == target then
			return error_return('BUGBUG Thaddeus(53) target=nil');
		end
		
		eff_list = action_damage(target, pside, self, self.ab_power, D_MAGIC);

		return eff_list; 
	end,
}
g_card_list[card.id] = card_class:new(card);

card = { 
	id = 54,
	ctype = ALLY,
	job = SHADOW,  -- shadow only
	camp = SHADOW,
	name = 'Carniboar',
	star = 2,
	cost = 2,
	power = 2,
	hp = 2,
	attack_anim = A_COMBO_HIT,
	skill_desc = 'When Carniboar kills an ally in combat, it gains +1 base attack and +1 health.',

	trigger_kill = function(self, target, pside)
		local eff_list = {};
		local eff_list2 = {};
		local offset = 0;

		-- because when this is called, target already go to grave
		-- so cannot use condition 'target.table ~= T_ALLY'
		if target.ctype ~= ALLY then
			return eff_list;
		end

		
		eff_list2 = action_power_offset(self, 1);
		table_append(eff_list, eff_list2);

		offset = self:change_base_hp(1);
		eff_list[#eff_list + 1] = eff_hp(cindex(self), offset);
		
		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = { 
	id = 55,
	ctype = ALLY,
	job = SHADOW,  -- shadow only
	camp = SHADOW,
	name = 'Shadow Knight', -- TODO Shadow Ally - Undead
	star = 5,
	cost = 5,
	power = 5,
	hp = 4,
	skill_desc = 'When Shadow Knight is summoned, then top ally in your graveyard is returned to your hand.',

	trigger_add = function (self, pside, target_table)
		local eff_list = {};
		local eff_list2;
		local offset = 0;
		local target_list = {};

		if target_table.name ~= T_ALLY then
			return eff_list;
		end

		local my_grave = pside[self.side][T_GRAVE];
		local my_hand = pside[self.side][T_HAND];
		for i = #my_grave, 1, -1 do
			local cc = my_grave[i];
			--print('DEBUG Shadow Knight grave cc: ', cc.name);
			if cc.ctype == ALLY then
				eff_list2 = action_move(cc, pside, my_grave, my_hand);
				table_append(eff_list, eff_list2);
				break;
			end
		end
		
		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = { 
	id = 56,
	ctype = ALLY,
	job = SHADOW,  -- shadow only
	camp = SHADOW,
	name = 'Cobra Demon', 
	star = 3,
	cost = 4,
	power = 1,
	ab_power = 1,
	dtype = D_MAGIC, -- always has dtype when we have ab_power (ai_weight_ability_damage)
	hp = 5,
	attack_anim = A_COMBO_HIT,
	-- TODO need to implement defender logic
	skill_desc = 'Defender. Alies Damaged by Cobra Demon are poisoned.\nENERGY:1 Target opposing ally takes 1 damage.',
	target_list = {
		{ side = 2, table_list = { T_ALLY } },
	},
	skill_cost_energy = 1,
	defender = true,

	trigger_attack = function(self, pside, target, power)
		local eff_list = {};
		local ac = nil;
		local eff_list2;
		local err = nil;
		--print('DEBUG Cobra Demon trigger_attack self, target', self.name, cindex(self), target.name, cindex(target.c));	
		if nil == target then
			print('BUGBUG Cobra Demon(56) nil target');
			return eff_list;
		end

		if target:die() or target.reborn == true then
			return eff_list;
		end

		if target.table ~= T_ALLY then
			return eff_list;
		end

		print('DEBUG Cobra Demon(56) add virtual card(1073)to' .. cindex(target));
		--[[
		ac = clone(g_card_list[1073]);  -- 73 is poison gas, add 1000
		ac.src = self; -- pside[self.side][T_HERO][1]; -- damage src

		-- ac.home = 3 - target.side;
		if true==card_attach(target, ac) then
			-- attach = 0 means from void
			eff_list[1] = eff_attach(cindex(target), 0, target.id, ac.id);
		else
			print('ERROR Demon(56) cannot attach');
		end
		]]--
		
		-- check target is reborn by earthen protector(39)
		if target.reborn == true then
			return eff_list;
		end

		eff_list2, err = action_virtual_attach(target, pside, 1073, self);
		if nil == eff_list2 then
			print('BUGBUG Demon(1073) cannot attach: ', err);
			return eff_list, err;
		end
		table_append(eff_list, eff_list2);
		return eff_list;
	end,

	trigger_skill = function(self, pside, actual_target_list) 
		local index;
		local target;
		local eff_list = {};

		target = index_card(actual_target_list[1], pside);
		if nil == target then
			return error_return('BUGBUG Cobra Demon(56) target=nil');
		end
		
		eff_list = action_damage(target, pside, self, self.ab_power, D_MAGIC);

		return eff_list; 
	end,
}
g_card_list[card.id] = card_class:new(card);

card = { 
	id = 57,
	ctype = ALLY,
	job = SHADOW,  -- shadow only
	camp = SHADOW,
	name = 'Molten Destroyer', 
	star = 5,
	cost = 5,
	power = 4,
	hp = 5,
	attack_anim = A_FIRE,
	skill_desc = 'Molten Destroyer deals 1 fire damage to any hero or ally that damages it.Allies that enter combat with Molten Destroyer are set ablaze.',

	trigger_damage = function(self, pside, src, power, dtype)
		local eff_list = {};
		local eff_list2;

		if src.table ~= T_HERO and src.table ~= T_ALLY then
			return eff_list;
		end

		eff_list2 = action_damage(src, pside, self, 1, D_FIRE, A_FIRE);
		table_append(eff_list, eff_list2);


		return eff_list;

	end,


	trigger_defend = function(self, pside, src, power)
	-- src --> attacker, self --> defender
		local eff_list = {};
		local eff_list2;
		local index = 0;
		local ac = nil;
		local err = nil;

		if src.table ~= T_ALLY then
			return eff_list;
		end

		if src:die() then
			return eff_list;
		end

		index = cindex(src);
		-- print('DEBUG Molten(57) defend add virtual card(1075) to ' .. index);
		--[[
		ac = clone(g_card_list[1075]);  -- 75 is flame, add 1000
		ac.src = self; -- damage src
		-- ac.home = 3 - target.side;  -- XXX exchange card may break
		if true==card_attach(src, ac) then
			-- attach = 0 means from void
			eff_list[#eff_list + 1] = eff_attach(index, 0, src.id, ac.id);
			print('ac:index() = ', ac:index());
			-- eff_list[1] = {'attach', target=index, attach=0, 
			-- cid=target.id, acid=ac.id};
		else
			print('ERROR Molten(57) cannot attach');
		end
		]]--

		eff_list2, err = action_virtual_attach(src, pside, 1075, self);
		if nil == eff_list2 then
			print('BUGBUG Molten(1075) cannot attach: ', err);
			return eff_list, err;
		end
		table_append(eff_list, eff_list2);

		return eff_list;
	end,

	trigger_attack = function(self, pside, target, power)
		local eff_list = {};
		local eff_list2;
		local index = 0;
		local ac = nil;
		local err = nil;

		if target.side == self.side then
			return eff_list;
		end

		if target.table ~= T_ALLY then
			return eff_list;
		end

		if target:die() then
			return eff_list;
		end

		index = cindex(target);
		-- print('DEBUG Molten(57) attack add virtual card(1075) to ' .. index);
		--[[
		ac = clone(g_card_list[1075]);  -- 75 is flame, add 1000
		ac.src = self; -- damage src
		-- ac.home = 3 - target.side;  -- XXX exchange card may break
		if true==card_attach(target, ac) then
			-- attach = 0 means from void
			eff_list[#eff_list + 1] = eff_attach(index, 0, target.id, ac.id);
			-- eff_list[1] = {'attach', target=index, attach=0, 
			-- cid=target.id, acid=ac.id};
		else
			print('ERROR Molten(57) cannot attach');
		end
		]]--

		-- check target is reborn by earthen protector(39)
		if target.reborn == true then
			return eff_list;
		end

		eff_list2, err = action_virtual_attach(target, pside, 1075, self);
		if nil == eff_list2 then
			print('BUGBUG Molten(1075) cannot attach: ', err);
			return eff_list, err;
		end
		table_append(eff_list, eff_list2);

		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = { 
	id = 58,
	ctype = ALLY,
	job = SHADOW,  -- shadow only
	camp = SHADOW,
	name = 'Brutal Minotaur', 
	star = 5,
	cost = 5,
	power = 6,
	hp = 6,
	die_power = 2, -- damage when die
	attack_anim = A_COMBO_HIT,
	skill_desc = 'When Brutal Minotaur is killed, its controller takes 2 damage.',

	trigger_die = function (self, pside, src) 
		local eff_list = {};
		local offset = 0;
		local hh = pside[self.side][T_HERO][1];
		if hh:die() then
			return eff_list;
		end

		--[[
		offset = hh:change_hp(-self.die_power);
		eff_list[#eff_list+1] = eff_hp(cindex(hh), offset);
		]]--
		eff_list = action_damage(hh, pside, self, self.die_power, D_MAGIC);

		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {  
	id = 59, 
	ctype = ALLY,
	job = SHADOW,  -- shadow only
	camp = SHADOW,
	name = 'Wulven Tracker',
	star = 3,
	cost = 4,
	power = 2,
	hp = 4,  
	skill_desc = 'When Wulven Tracker deals combat damage, draw a card',
	
	trigger_attack = function(self, pside, target, power)
		local eff_list = {}
		if power <= 0 then
			return {}; -- early exit, if damage is 0 or less
		end

		eff_list = action_drawcard(pside, self.side);

		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 60,
	ctype = ALLY,
	job = SHADOW,  -- shadow only
	camp = SHADOW,
	name = 'Ogloth the Glutton',
	star = 5,
	cost = 6,
	power = 3,
	hp = 6,
	unique = true,
	skill_desc = 'When a ally is killed, Ogloth gains +1 base attack and +1 health. RES:3 Target other ally with cost less than Ogloth current attack is killed.',
	skill_cost_resource = 3,
	target_list = {
		-- DEBUG add ally, should be only hero
		{side=2, table_list={T_ALLY} },
	},

	trigger_target_validate = function(self, pside, target, tid)
		if tid == 0 then
			return true;
		end
		if tid ~= 1 or target == nil then
			print('BUG: Ogloth(60) target_validate tid, target', tid, target);
			return false;
		end

		if target.cost < self.power then
			return true;
		end

		return false;
	end,

	trigger_skill = function(self, pside, actual_target_list) 
		local index;
		local target;
		local eff_list = {};
		if actual_target_list==nil or #actual_target_list~=1 then
			local err = 'BUGBUG (60)Ogloth skill atl=nil or empty';
			return nil, err;
		end


		index = actual_target_list[1];
		target = index_card(index, pside);

		
		eff_list = action_damage(target, pside, self, 99, D_NORMAL);

		return eff_list; 
	end,
	-- self = me , target = dier , src = killer
	trigger_other_kill = function(self, target, src, pside)
		local eff_list = {};
		local eff_list2;
		local offset = 0;

		if self == target then
			return eff_list;
		end

		if target.ctype ~= ALLY then
			return eff_list;
		end

		-- peter: change base power(not offset), 
		-- also, use action_... to avoid ++ when crippling blow attached
		eff_list2 = action_power_change(self, pside, 1);
		table_append(eff_list, eff_list2);
		-- offset = self:change_power(1); 
		-- eff_list[#eff_list + 1] = eff_power_offset(cindex(self), offset);
		offset = self:change_base_hp(1);
		eff_list[#eff_list+1] = eff_hp(cindex(self), offset);

		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 61,
	ctype = SUPPORT,
	job = WARRIOR, 
	name = 'Valiant Defender', 
	star = 1,
	cost = 2,
	power = 0, 
	hp = 0,    
	skill_desc = 'Friendly allies cannot be attacked for the next 2 turns',
	stealth = true; -- core logic!!
	timer = 4;
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 62,
	ctype = ARTIFACT,
	job = WARRIOR, 
	name = 'Reserve Weapon',
	star = 5,
	cost = 5,
	power = 0,   -- add-on to power
	hp = 0,    
	skill_desc = 'Your weapons have +1 attack while Reserve Weapon is in play. RES:0 Target weapon in your graveyard is return to play with +1 base attack and Reserve Weapon is destroyed.',
	skill_cost_resource = 0,

	target_list = {
		{side=1, table_list={T_GRAVE}  },    -- 2 means oppo side, 1 = myside
	},

	trigger_target_validate = function (self, pside, target, tid)
		if tid == 0 then
			return true;
		end

		if tid ~= 1 or target == nil then
			print('BUGBUG (62)reserve target_validate tid, target', tid, target);
			return false;
		end

		if target.ctype == WEAPON then
			return true;
		end

		return false;
	end,

	-- target is self (this card)
	trigger_add = function (self, pside, target_table) -- {
		local eff;
		local eff_list = {};
		local eff_list2;
		local side_target = pside[self.side]; 
		-- side_target power - 1 (if > 1)
		-- curses only effective in support table
		if pside[self.side][T_SUPPORT] ~= target_table then
			if target_table.name==T_SUPPORT then
				print('BUGBUG (62)reserve table: ', side_target[T_SUPPORT], target_table);
			end
			return;
		end
		for k, v in ipairs(side_target[T_SUPPORT]) do
			if v.ctype == WEAPON then
				eff_list2 = action_power_offset(v, 1);
				table_append(eff_list, eff_list2);
			end
		end
		return eff_list;
	end, -- trigger_add }

	-- self is this card (curses)
	trigger_remove = function (self, pside, target_table) -- {
		local eff_list = {};
		local eff_list2;
		local eff;
		local side_target = pside[target_table.side];  
		if target_table.name ~= T_SUPPORT then
			return {}; -- early exit empty list
		end
		for k, v in ipairs(side_target[T_SUPPORT]) do
			if v.ctype == WEAPON then
				eff_list2 = action_power_offset(v, -1);
				table_append(eff_list, eff_list2);
			end
		end
		return eff_list;
	end, -- trigger_remove }

	-- call this when war banner is already in support and other cards are added
	trigger_other_add = function(self, target, pside, target_table)
		-- self is this curses, 
		-- need check: self and target are oppo side
		local eff_list = {};
		local eff_list2;

		if target_table.name ~= T_SUPPORT then 
			return {};
		end

		if target.ctype ~= WEAPON then 
			return {};
		end

		-- curses and target on same side, early exit
		if self.side ~= target.side then
			return {}; -- early exit
		end

		eff_list2 = action_power_offset(target, 1);
		table_append(eff_list, eff_list2);
		-- order is important , after target.power is updated (power_offset)
		return eff_list;
	end,

	trigger_skill = function(self, pside, actual_target_list)
		local at; -- at is the number (2101 = side 2 hand 1)
		local cc;
		local eff_list = {};
		local eff_list2 = {};
		local side_target;
		local side_home;  -- retreat back to home T_HAND
		at = actual_target_list[1]; -- hard code the first target
		if nil == at then
			print('ERROR (62)reserve a nil at index');
			return eff_list;
		end

		cc = index_card(at, pside);
		if nil == cc then
			print('ERROR (62)reserve a nil card at=' .. at);
			return eff_list;
		end
	
		side_target = pside[cc.side];
		side_home = pside[cc.home];

		-- cc:refresh();
		eff_list2 = action_refresh(cc, pside);
		table_append(eff_list, eff_list2);

		-- remove exist weapon
		local supp_list = side_target[T_SUPPORT] or {};
		for i = #supp_list, 1 , -1 do
			local sc = supp_list[i];
			if WEAPON == sc.ctype then
				eff_list2 = action_grave(sc, pside);
				table_append(eff_list, eff_list2);
			end
		end

		eff_list2 = action_grave(self, pside);
		table_append(eff_list, eff_list2);

		-- assume cc must be in grave
		eff_list2 = action_move(cc, pside, side_target[cc.table], 
			side_home[T_SUPPORT]);
		table_append(eff_list, eff_list2);

		-- XXX why not use action_power_change before?
		eff_list2 = action_power_change(cc, pside, 1);
		-- eff_list2 = action_power_offset(cc, 1);
		table_append(eff_list, eff_list2);

		return eff_list;
	end,

	ai_weight = function(self, atl, pside, sss, ability) -- start {
		local target;
		local weight = 0;
		local my_hero = pside[sss][T_HERO][1];
		local my_weapon = my_hero.weapon;
		-- in hand
		if self.table == T_HAND then
			weight = 1;
			-- if we have weapon on support or grave better weight, else lower
			if my_hero.ready == 0 then
				return 1;
			end

			if my_weapon ~= nil then 
				weight = weight + 300; -- should be less than cost3 (Aldon)
			end

			if true == check_ctype_in_table(pside[sss][T_GRAVE], WEAPON) then
				-- TODO find the best weapon in grave
				-- weight = weight + ai_weight_equipment(best_weapon....) + 50
				weight = weight + 700; -- dimension ripper weight 650
			end

			return weight;	
		end

		-- in support

		if my_weapon ~= nil then 
			return 0;
		end

		-- hero finish attack, and weapon broken
		if my_hero.ready == 0 then
			return 0;
		end

		target = atl[1];
		if target == nil then
			print('BUGBUG reserve(62) nil target');
			return 0;
		end
		weight = ai_weight_equipment(target, atl, pside, sss, ability);
		weight = weight + 300;
				
		return weight;
	end, -- ai_weight }
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 63,
	ctype = ARTIFACT,
	job = WARRIOR,
	name = 'War Banner',
	star = 3,
	cost = 3,
	power = 0,  
	hp = 0,  
	skill_desc = 'Friendly allies have +1 attack while War Banner is in play',

	-- tid=0 case
	trigger_target_validate = function(self, pside, target, tid)
		local supp;
		supp = pside[self.side][T_SUPPORT];
		for k, v in ipairs(supp or {}) do
			if (v.id == self.id) then
				-- print('DEBUG warbanner[63] duplicated');
				return false;
			end
		end
		return true;
	end,

	trigger_add = function (self, pside, target_table)
		local eff;
		local eff_list = {};
		local eff_list2;
		local side_target = pside[self.side];
		-- side2 is the target side, for each card in side2[T_ALLY] power+1
		-- war banner only effective in support table
		if side_target[T_SUPPORT] ~= target_table then
			if target_table.name==T_SUPPORT then
				print('ERROR warbanner(63) bug1: ', side_target[T_SUPPORT], target_table);
			end
			return;
		end

		local index_list = {};
		for k, v in ipairs(side_target[T_ALLY]) do
			index_list[#index_list + 1] = v:index();
		end
		-- only for client anim
		eff_list[#eff_list + 1] = eff_anim(self:index(), self.id
		, A_POWER_UP, index_list);

		for k, v in ipairs(side_target[T_ALLY]) do
			--[[
			local new_power, old_power;
			old_power = v.power;
			v.power_offset = (v.power_offset or 0) + 1;
			new_power = v.power;

			-- peter: TODO use action_power_offset @see (60)Ogloth
			-- TODO in case of crippling blow it may not change!
			if new_power ~= old_power then
				eff = eff_power_offset(v:index(), new_power-old_power);
				eff_list[ #eff_list + 1 ] = eff;
			end
			]]--
			eff_list2 = action_power_offset(v, 1);
			table_append(eff_list, eff_list2);
		end
		return eff_list;
	end,
	trigger_remove = function (self, pside, target_table)
	-- (side_src, side_target, src, target, target_table)
		local eff_list = {};
		local eff_list2;
		local eff;
		local side_target = {};
		side_target = pside[target_table.side];
		if side_target[T_SUPPORT] ~= target_table then
			return eff_list; -- early exit empty list
		end

		local index_list = {};
		for k, v in ipairs(side_target[T_ALLY]) do
			index_list[#index_list + 1] = v:index();
		end
		-- only for client anim
		eff_list[#eff_list + 1] = eff_anim(self:index(), self.id
		, A_POWER_DOWN, index_list);

		for k, v in ipairs(side_target[T_ALLY]) do
			--[[
			local new_power, old_power;
			old_power = v.power;
			v.power_offset = (v.power_offset or 0) - 1;
			new_power = v.power;

			-- TODO use action_power_offset()
			-- TODO in case of crippling blow it may not change!
			if new_power ~= old_power then
				eff = eff_power_offset(v:index(), new_power-old_power);
				eff_list[ #eff_list + 1 ] = eff;
			end
			]]--
			eff_list2 = action_power_offset(v, -1);
			table_append(eff_list, eff_list2);
		end
		return eff_list;
	end,

	-- call this when war banner is already in support and other cards are added
	trigger_other_add = function(src, target, pside, target_table)
	-- (side_src, side_target, src, target, target_table)
		-- src is self, this war banner, so we need to check whether
		-- src and target are the same side
		local eff_list = {};
		local eff_list2;

		if target_table.name ~= T_ALLY then 
			return eff_list;
		end
		-- war banner is not in the same side of the add_other target
		if src.side ~= target.side then
			return eff_list; -- early exit
		end

		--[[
		-- core logic 
		local old_power, new_power;
		old_power = target.power;
		target.power_offset = (target.power_offset or 0) + 1; 
		new_power = target.power;
		-- target.power = target.power + 1; 
		-- order is important , after target.power is updated (power_offset)

		-- peter: TODO use action_power_offset @see (60)Ogloth
		if new_power ~= old_power then
			eff_list[1] = eff_power_offset(target:index(), 
				new_power-old_power);
		end
		]]--
		eff_list2 = action_power_offset(target, 1);
		table_append(eff_list, eff_list2);
		return eff_list;
	end,
		
	-- XXX let action_refresh do this?
	--[[
	trigger_other_remove = function(src, target, pside, target_table)
		local eff;
		local eff_list = {};

		if target.side ~= src.side then
			return {};
		end

		eff_list = action_power_offset(src, -1);
		return eff_list;
	end,
	]]--
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 64,
	ctype = ABILITY,
	job = WARRIOR,
	name = 'Smashing Blow',
	star = 3,
	cost = 3,
	power = 0,  
	hp = 0,  
	skill_desc = 'Target enemy weapon or armor is destroyed',
	target_list =  {
		-- note: weapon and armor is in support table, with diff ctype
		{side=2, table_list={T_WEAPON, T_ARMOR}},
	},
	trigger_skill = function (self, pside, actual_target_list)
		local index;
		local target;
		local eff_list = {};
		local eff_list2 = {};
		if actual_target_list==nil or #actual_target_list<1 then
			print('BUGBUG Smashing Blow(64) atl=nil or #<1');
			return {};
		end
		index = actual_target_list[1];
		target = index_card(index, pside);
		-- print('DEBUG smashing index=', index);
		
		if target.ctype~=WEAPON and target.ctype~=ARMOR then
			print('BUGBUG Smashing blow(64) target is not weapon or armor');
			return {};
		end

		eff_list2 = action_durability_change(target, pside, -999);
		table_append(eff_list, eff_list2);
		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 65,
	ctype = ATTACH,
	job = WARRIOR, -- warrior
	name = 'Enrage',
	star = 3,
	cost = 4,
	power = 0,  
	hp = 10,    -- core logic, it is that simple!
	skill_desc = 'Your hero has +10 health while Enrage is attached.',
	target_list = {
		-- DEBUG add ally, should be only hero
		{side=1, table_list={T_HERO} },
	},
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 66,
	ctype = ATTACH,
	job = WARRIOR,
	name = 'Warrior Training',
	star = 1,
	cost = 2,
	power = 0,  
	hp = 0,    
	skill_desc = 'Target friendly ally is a protector (allies without protector cannot be targeted) while Warrior Training is attached', 
	target_list = {
		{side=1, table_list={T_ALLY} },
	},

	protector = true,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 67,
	ctype = ATTACH,
	job = WARRIOR,
	name = 'Crippling Blow',
	star = 3,
	cost = 2,
	power = -999,   -- very large negative number, core logic
	hp = 0,    
	skill_desc = 'Target opposing ally has a maximum attack of 0 while Crippling Blow is attached',
	target_list = {
		-- DEBUG add ally, should be only hero
		{side=2, table_list={T_ALLY} },
	},

	-- TODO ai_weight : only consider cost >= 3, consider power, cost and hp

	ai_weight = function(self, atl, pside, sss, ability) 
		local target;
		target = atl[1];
		if target.cost < 3 then
			return 0;
		end
		return ai_weight_control(self, atl, pside, sss, ability) ;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = { -- add by masha
	id = 68,
	ctype = ATTACH,
	job = WARRIOR, -- warrior
	name = 'Rampage',
	star = 4,
	cost = 4,
	skill_desc = 'When an opposing ally is killed while Rampage is attached to your hero, your hero heals 2 damage.';
	-- hp = 0,    
	-- power = 0,   
	target_list = {
		{side=1, table_list={T_HERO} },
	},

	-- when someone die, all card will be call 
	-- trigger_other_kill(card, someone, killer,pside)
	trigger_other_kill = function(self, target, src, pside, old_side)
		local eff_list = {};

		-- use old_side instead of target.side (side before enter grave)
		if old_side == self.side then
			return eff_list;
		end

		local hh = pside[self.side][T_HERO][1];
		
		eff_list = action_heal(hh, pside, self, 2);

		return eff_list;
	
	end,

	--[[
	trigger_hero_attach = function(self, attacker, dier, pside)
		local eff_list = {};

		if self.side == dier.side then
			return eff_list;
		end
		-- Q: kelton: base hp, max hp ???  heal is OK
		local h = pside[self.side][T_HERO][1];
		eff_list = action_heal(h, pside, self, 2);

		return eff_list;
	end,
	]]--

}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 69,
	ctype = ABILITY,
	job = WARRIOR,
	name = 'Shield Bash',
	star = 1,
	cost = 3,
	ab_power = 3,  
	dtype = D_MAGIC,
	atype = A_NORMAL,
	hp = 1,  
	skill_desc = 'Target opposing ally takes 3 damage',
	target_list = { 
		{side=2, table_list={T_ALLY}  },    -- 2 means oppo side, 1 = myside
	}, -- TODO check_target_list(card)
	-- side1, side2 = side[1], side[2]
	trigger_skill = function(self, pside, actual_target_list)
		-- src is the hero who trigger this skill
		local target;
		local actual_target;
		local eff;
		local eff_list = {};
		local eff_list2 = {};
		local src;
		src = pside[self.side][T_HERO][1];

		if 1 ~= #actual_target_list then
			print('BUGBUG shield bash(69) #atl=', #actual_target_list);
			return eff_list;
		end
		actual_target = actual_target_list[1]; -- actual_target is index
		if actual_target == nil then
			print('ERROR shield bash(69) target=nil');
			return eff_list;  -- let previous action execute
		end
		target = index_card(actual_target, pside);

		if target.ctype~=ALLY then
			print('ERROR shield bash(69) cannot target ctype=' .. target.ctype);
			return eff_list; -- let previous action execute
		end 
		eff_list2 = action_damage(target, pside, src, 
			self.ab_power, self.dtype, self.atype);
		table_append(eff_list, eff_list2);

		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 70,
	ctype = ATTACH,
	job = WARRIOR, -- warrior
	name = 'Blood Frenzy',
	star = 3,
	cost = 3,
	power = 0,   
	hp = 0,    
	skill_desc = 'At the start of each of your turns while Blood Frenzy is attached to your hero, your hero takes 1 damage and you draw a card', 
	target_list = {
		{side=1, table_list={T_HERO} },
	},
	trigger_turn_start = function(self, pside, sss)
		local eff_list = {};
		local eff_list2 ;
		local hh;

		hh = pside[self.side][T_HERO][1];
		-- not my side, quit!
		if (hh.side ~= sss) then
			return {}; -- early exit
		end

		-- self, pside
		eff_list = action_drawcard(pside, sss);
		-- rather hard code hero!

		-- lazy:  src is also 
		eff_list2 = action_damage(hh, pside, hh, 1, D_MAGIC);
		table_append(eff_list, eff_list2);
		
		return eff_list;
	end,

	ai_weight = function(self, atl, pside, sss, ability) 
		if true == check_in_side_list(pside[sss], DRAW_LIST) then
			return 0; -- already there, no need
		else
			return 1000; -- very good weight
		end
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 71,
	ctype = ABILITY,
	job = MAGE,
	name = 'Fireball',
	star = 4,
	cost = 3,
	ab_power = 4,  -- damage
	dtype = D_FIRE,
	atype = A_FIREBALL,

	hp = 1,  
	skill_desc = 'Target opposing hero or ally takes 4 fire damage',
	target_list = {
		{side=2, table_list={T_HERO, T_ALLY,}  },    -- hero must be the first 
	}, 

	trigger_skill = function(self, pside, actual_target_list)
		-- src is the hero who trigger this skill
		local eff;
		local eff_list = {};
		local eff_list2 = {};
		local src;
		local actual_target;
		local target;
		src = pside[self.side][T_HERO][1]; 

		if 1 ~= #actual_target_list then
			print('BUGBUG fireball(71) #atl=', #actual_target_list);
			return eff_list;
		end
		actual_target = actual_target_list[1]; -- actual_target is index
		if actual_target == nil then
			print('ERROR fireball(71) target=nil');
			return eff_list;  -- let previous action execute
		end
		target = index_card(actual_target, pside);

		eff_list2 = action_damage(target, pside, src, 
			self.ab_power, self.dtype, self.atype);
		table_append(eff_list, eff_list2);


		return eff_list;
	end,

}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 72,
	ctype = ABILITY,
	job = MAGE, -- mage
	name = 'Freezing Grip',
	star = 2,
	cost = 3,
	power = 3,   -- how many turns to disable TODO setup virtual card
	hp = 0,    
	skill_desc = 'Target opposing ally is frozen (cannot attack, defend and use abilities) for 3 turns', 
	target_list = {
		{side=2, table_list={T_ALLY} },
	},

	-- add a virtual attach card
	trigger_skill = function(self, pside, actual_target_list) 
		local index;
		local target;
		local ac;  -- attach card, the virtual card
		local eff_list = {};
		local eff_list2;
		local err = nil;

		-- TODO check #atl==1
		index = actual_target_list[1];
		target = index_card(index, pside);

		if nil==target then
			print('BUGBUG freezing(72) nil target');
			return {};
		end

		-- print('DEBUG freezing add virtual card(1072) to ' .. index);
		--[[
		ac = clone(g_card_list[1072]);  -- 75 is flame, add 1000
		ac.src = pside[self.side][T_HERO][1]; -- damage src
		-- TODO ac.home == 3 - target.side (for exchange card case)
		-- need testing!
		-- ac.home = 3 - target.side;
		if true==card_attach(target, ac) then
			-- attach = 0 means from void
			eff_list[1] = eff_attach(index, 0, target.id, ac.id);
			-- eff_list[1] = {'attach', target=index, attach=0, 
			-- cid=target.id, acid=ac.id};
		else
			print('ERROR freezing(72) cannot attach');
		end
		]]--

		eff_list2, err = action_virtual_attach(target, pside, 1072, pside[self.side][T_HERO][1]);
		if nil == eff_list2 then
			print('BUGBUG freezing(72) cannot attach: ', err);
			return eff_list, err;
		end
		table_append(eff_list, eff_list2);

		return eff_list;
	end,

}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 73,
	ctype = ABILITY,
	job = MAGE, -- mage
	name = 'Poison Gas',
	star = 1,
	cost = 4,
	power = 0,   -- add-on to power
	hp = 0,    
	skill_desc = 'Target opposing hero or ally is poisoned(it takes 1 poison damage at the start of each of its controllers turns).',
	target_list = {
		{side=2, table_list={T_HERO, T_ALLY, } },
	},

	-- add a virtual attach card
	trigger_skill = function(self, pside, actual_target_list) 
		local index;
		local target;
		local ac;  -- attach card, the virtual card
		local eff_list = {};
		local eff_list2;
		local err = nil;

		if 1~=#actual_target_list then
			print('ERROR 73 poison atl~=1, ', #actual_target_list);
			return {};
		end
		-- TODO check #atl==1
		index = actual_target_list[1];
		target = index_card(index, pside);

		if nil==target then
			print('BUGBUG poison(73) nil target');
			return {};
		end

		-- print('DEBUG poison add virtual card(1073) to ' .. index);
		--[[
		ac = clone(g_card_list[1073]);  -- 73 is poison gas, add 1000
		ac.src = pside[self.side][T_HERO][1]; -- damage src

		-- ac.home = 3 - target.side;
		if true==card_attach(target, ac) then
			-- attach = 0 means from void
			eff_list[1] = eff_attach(index, 0, target.id, ac.id);
			-- eff_list[1] = {'attach', target=index, attach=0, 
			-- cid=target.id, acid=ac.id};
		else
			print('ERROR poison(73) cannot attach');
		end
		]]--

		eff_list2, err = action_virtual_attach(target, pside, 1073, pside[self.side][T_HERO][1]);
		if nil == eff_list2 then
			print('BUGBUG poison(1073) cannot attach: ', err);
			return eff_list, err;
		end
		table_append(eff_list, eff_list2);

		return eff_list;
	end,

}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 74,
	ctype = ABILITY,
	job = MAGE,
	name = 'Lightning Strike',
	star = 3,
	cost = 4,
	ab_power = 3,  -- ability power
	dtype = D_ELECTRICAL,
	atype = A_THUNDER,
	hp = 2,   -- two target
	skill_desc = 'Up to 2 different target opposing heroes or allies take 3 electrical damage',
	target_list = {
		{side=2, table_list={T_HERO, T_ALLY, }  },    -- hero must be the first 
		{side=2, table_list={T_HERO, T_ALLY}, optional=true },
		-- {side=2, table_list={T_HERO, T_ALLY}, optional=true  },
	}, 

	trigger_skill = function(self, pside, actual_target_list)
		-- src is the hero who trigger this skill
		local eff;
		local eff_list = {};
		local eff_list2 = {};
		local target_list = {};
		local src;
		src = pside[self.side][T_HERO][1]; 

		-- new_index -> obj		
		-- TODO use index_card_list()
		for k, at in ipairs(actual_target_list) do 
			target_list[k] = index_card(at, pside);
			if nil == target_list[k] then
				print('ERROR: lightning strike(74) target=nil ', at);
				return {};
			end
		end

		eff_list[#eff_list + 1] = eff_anim(self:index(), self.id, self.atype
		, actual_target_list);

		eff_list2 = action_damage_list(target_list, pside, src
		, self.ab_power, self.dtype, self.atype);
		table_append(eff_list, eff_list2);

		return eff_list;

		-- check ally or hero only
		-- debug: no check now
--		if target.ctype~=ALLY and target.ctype~=10 then
-- 			print('ERROR: fireball cannot target ctype=' .. target.ctype);
-- 			return {};
-- 		end 

	end,

}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 75,
	ctype = ABILITY,
	job = MAGE, -- mage
	name = 'Engulfing Flames',
	star = 1,
	cost = 4,
	power = 0,   -- add-on to power
	hp = 0,    
	skill_desc = 'Target opposing hero or ally is set ablaze 1 fire damage every start of controllers turn',
	target_list = {
		{side=2, table_list={T_HERO, T_ALLY, } },
	},

	-- add a virtual attach card
	trigger_skill = function(self, pside, actual_target_list) 
		local index;
		local target;
		local ac;  -- attach card, the virtual card
		local eff_list = {};
		local eff_list2;
		local err = nil;

		if 1~=#actual_target_list then
			print('ERROR flame(75) atl~=1, ', #actual_target_list);
			return {};
		end
		-- TODO check #atl==1
		index = actual_target_list[1];
		target = index_card(index, pside);

		if nil==target then
			print('BUGBUG flame(75) nil target');
			return {};
		end

		-- print('DEBUG flame add virtual card(1075) to ' .. index);
		--[[
		ac = clone(g_card_list[1075]);  -- 75 is flame, add 1000
		ac.src = pside[self.side][T_HERO][1]; -- damage src
		-- ac.home = 3 - target.side;  -- XXX exchange card may break
		if true==card_attach(target, ac) then
			-- attach = 0 means from void
			eff_list[1] = eff_attach(index, 0, target.id, ac.id);
			-- eff_list[1] = {'attach', target=index, attach=0, 
			-- cid=target.id, acid=ac.id};
		else
			print('ERROR 75 flame cannot attach');
		end
		]]--

		eff_list2, err = action_virtual_attach(target, pside, 1075, pside[self.side][T_HERO][1]);
		if nil == eff_list2 then
			print('BUGBUG flame(1075) cannot attach: ', err);
			return eff_list, err;
		end
		table_append(eff_list, eff_list2);

		return eff_list;
	end,

}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 76,
	ctype = ARTIFACT,
	job = MAGE, -- mage
	name = 'Tome of Knowledge',
	star = 3,
	cost = 2,
	power = 0,   -- add-on to power
	hp = 0,    
	skill_desc = 'RES:2  Draw a card',
	skill_cost_resource = 2,

	-- avoid user press when no card in deck
	trigger_target_validate = function(self, pside, target, tid)
		local num_deck;
		local num_hand;
		if self.table==T_HAND then
			return true;
		end
		num_deck = #(pside[self.side][T_DECK]);
		num_hand = #(pside[self.side][T_HAND]);
		if num_deck > 0 and num_hand < MAX_HAND_CARD then
			return true;
		else
			return false;
		end
	end,
	
	trigger_skill = function(self, pside, actual_target_list) 
		local myside = pside[self.side];
		if myside.resource < self.skill_cost_resource then
			print('ERROR Tome(76) resource not enough');
			return {};
		end
		
		local eff_list = {};
		eff_list = action_drawcard(pside, self.side);

		return eff_list;
	end,

	-- @see (193)wizent staff
	ai_weight = function(self, atl, pside, sss, ability) 
		-- note: need to check whether self is on hand or support
		-- hand: check whether any card in draw_list is in my side, 
		--       if yes:no need(0),  if no:we need a draw card(1000)
		-- support: check the resource < 4 :  smaller than weight of cost2 ally
		--          >=4 : smaller than weight for killing an ally (lightning)

		if self.table == T_HAND then
			-- print('DEBUG (76)tome on hand');
			if true == check_in_side_list(pside[sss], DRAW_LIST) then
				return 0; -- already there, no need
			else
				return 1000; -- very good weight
			end
		end

		-- now, self should be in support
		if self.table ~= T_SUPPORT then
			print('BUGBUG (76)tome ai_weight self.table ~= T_SUPPORT ', self.table);
			return 0; -- this is bad, better no move!
		end

		-- print('DEBUG (76)tome on support');
		-- special hardcode: if #hand >=6, stop drawing card, use card first
		if #pside[sss][T_HAND] >= 6 then
			return 0;
		end

		local weight;
		if pside[sss].resource < 4 then
			-- create a mock up of typical cost2 ally like (22)dirk
			local ally2 = { id=9999, cost=2, power=2, hp=2, ctype=ALLY };
			weight = ai_weight_ally(ally2, {}, pside, sss, true);
			-- print('DEBUG (76)tome ally2 weight=' .. weight);
			return weight - 10;  -- slightly smaller is ok
		else
			-- create a mockup of cost3 ally like (21)Jasmine 
			-- and calculate the weight for killing it
			local target3 = { id=9998, cost=3, power=3, hp=4, ctype=ALLY };
			weight = ai_damage(target3, pside, sss, self, 4);
			-- print('DEBUG (76)tome target3 kill weight=' .. weight);
			return weight + 100; -- can be more
		end
	end,

}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 77,
	ctype = SUPPORT,
	job = MAGE, -- mage
	name = 'Portal',
	star = 5,
	cost = 4,
	power = 0,   -- add-on to power
	hp = 0,    
	skill_desc = 'Allies you summon for the next 3 turns have haste ( they can attack and use abilities in the turn they are summoned ).',
	timer = 5, -- TODO need to check next 3 turns real definition!
	haste = true; 

	-- TODO ai_weight: weight = sum of ai_weight_ally() on hand
}
g_card_list[card.id] = card_class:new(card);

card = { -- add by kelton
	id = 78,
	ctype = ABILITY,
	job = MAGE,
	name = 'Supernova',
	star = 5,
	cost = 5,
	ab_power = 5,  -- attack
	dtype = D_FIRE,
	atype = A_NOVA,
	hp = 0,  
	skill_desc = 'All heroes and allies take 5 fire damage',
	-- empty or nil target_list means NO need to have target
	target_list = {
	}, 

	trigger_skill = function(self, pside, actual_target_list)
		local eff_list = {};
		local eff_list2 = {};
		local side_src;
		local side_opp;
		local target_list = {};
		local index_list = {};
		local src;
		side_src = pside[self.side];
		side_opp = pside[3-self.side];
		src = side_src[T_HERO][1]; -- assert non-nil ?

		-- target oppo side first 
		for _, oneside in ipairs({side_opp, side_src}) do
			target_list[#target_list + 1] = oneside[T_HERO][1];
			index_list[#index_list + 1] = oneside[T_HERO][1]:index();
			for k, v in ipairs(oneside[T_ALLY]) do
				target_list[#target_list + 1] = v;
				index_list[#index_list + 1] = v:index();
			end
		end

		eff_list[#eff_list + 1] = eff_anim(self:index(), self.id, self.atype
		, index_list);

		eff_list2 = action_damage_list(target_list, pside, src
		, self.ab_power, self.dtype);
		table_append(eff_list, eff_list2);

		return eff_list;

	end,

	-- @see (5)nishaven
	ai_weight = function(self, atl, pside, sss, ability) -- start {
		local target;
		local weight = 0;
		if ability then
			-- use ability
			-- need to patch the atl, because target to all is not counted
			atl = {};
			table.insert(atl, pside[1][T_HERO][1]);
			table.insert(atl, pside[2][T_HERO][1]);
			for k, v in ipairs(pside[1][T_ALLY] or {}) do 
				table.insert(atl, v);
			end
			for k, v in ipairs(pside[2][T_ALLY] or {}) do 
				table.insert(atl, v);
			end
			return ai_weight_ability_damage(self, atl, pside, sss, ability);
		else
			-- normal attack is not valid for ability
			return	-9078;
		end
		return weight;
	end, -- ai_weight }
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 79,
	ctype = ABILITY,
	job = MAGE,
	name = 'Arcane Burst',
	star = 2,
	cost = 4,
	ab_power = 2,  -- attack
	dtype = D_ARCANE,
	atype = A_ARCANE,
	hp = 4,  
	skill_desc = 'Up to 4 different target opposing heroes or allies take 2 arcane damage',
	target_list = {
		{side=2, table_list={T_HERO, T_ALLY }  },    
		{side=2, table_list={T_HERO, T_ALLY }, optional=true  }, 
		{side=2, table_list={T_HERO, T_ALLY }, optional=true  }, 
		{side=2, table_list={T_HERO, T_ALLY }, optional=true  }, 
	}, 

	trigger_skill = function(self, pside, actual_target_list)
		-- src is the hero who trigger this skill
		local eff_list = {};
		local eff_list2 = {};
		local target_list = {};
		local src;
		src = pside[self.side][T_HERO][1]; 

		-- new_index -> obj		
		for k, at in ipairs(actual_target_list) do 
			target_list[k] = index_card(at, pside);
			if nil == target_list[k] then
				print('ERROR: arcane burst(79) target=nil');
				return {};
			end
		end

		eff_list[#eff_list + 1] = eff_anim(self:index(), self.id, self.atype
		, actual_target_list);

		eff_list2 = action_damage_list(target_list, pside, src
		, self.ab_power, self.dtype, self.atype);
		table_append(eff_list, eff_list2);

		return eff_list;

		-- check ally or hero only
		-- debug: no check now
--		if target.ctype~=ALLY and target.ctype~=10 then
-- 			print('ERROR: fireball cannot target ctype=' .. target.ctype);
-- 			return {};
-- 		end 

	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 80,
	ctype = ABILITY,
	job = MAGE, -- mage
	name = 'Clinging Webs',
	star = 1,
	cost = 2,
	power = 2,    -- 2 turns
	hp = 0,    
	skill_desc = 'Target oppo ally disable for 2 turns', 
	target_list = {
		{side=2, table_list={T_ALLY} },
	},

	-- TODO write custom ai to check if target is already disabled (web/frozen)

	-- add a virtual attach card
	trigger_skill = function(self, pside, actual_target_list) 
		local index;
		local target;
		local ac;  -- attach card, the virtual card
		local eff_list = {};
		local eff_list2;
		local err = nil;

		-- TODO check #atl==1
		index = actual_target_list[1];
		target = index_card(index, pside);

		if nil==target then
			print('BUGBUG web(80) nil target');
			return {};
		end

		-- print('DEBUG web add virtual card(1080) to ' .. index);
		--[[
		ac = clone(g_card_list[1080]);  -- 80 is web, add 1000
		ac.src = pside[self.side][T_HERO][1]; -- damage src
		-- XXX exchange card may break!
		if true==card_attach(target, ac) then
			-- attach = 0 means from void
			eff_list[1] = eff_attach(index, 0, target.id, ac.id);
			-- eff_list[1] = {'attach', target=index, attach=0, 
			-- cid=target.id, acid=ac.id};
		else
			print('ERROR web(80) cannot attach');
		end
		]]--

	
		eff_list2, err = action_virtual_attach(target, pside, 1080, pside[self.side][T_HERO][1]);
		if nil == eff_list2 then
			print('BUGBUG web(80) cannot attach: ', err);
			return eff_list, err;
		end
		table_append(eff_list, eff_list2);

		return eff_list;
	end,

}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 81,
	ctype = ATTACH,
	job = HUNTER,
	name = 'Aimed Shot',
	star = 4,
	cost = 2,
	power = 1,  
	hp = 0,    -- core logic, it is that simple!
	skill_desc = 'Target bow you control has +1 attack while Aimed Shot is attached.',
	target_list = {
		{side=1, table_list={T_WEAPON} },
	},
	trigger_target_validate = function(self, pside, target, tid) -- {
		-- first test on tid=0
		if tid == 0 then 
			return true;
		end

		if tid ~= 1 or target == nil then
			print('BUGBUG aimed shot(81)  target_validate tid, target:', tid, target);
			return false; -- BUG case
		end

		if target.is_bow == true then
			return true;
		end

		return false;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 82,
	ctype = ABILITY,
	job = HUNTER, 
	name = 'Poison Arrow',
	star = 4,
	cost = 2,
	power = 1,    -- 1 turns
	hp = 0,    
	skill_desc = 'Target oppo ally is poisoned and is disabled for 1 turn.', 
	target_list = {
		{side=2, table_list={T_ALLY} },
	},

	-- TODO write custom ai to check if target is already disabled (web/frozen)

	-- add a virtual attach card
	trigger_skill = function(self, pside, actual_target_list) 
		local index;
		local target;
		local ac;  -- attach card, the virtual card
		local eff_list = {};
		local eff_list2;
		local err = nil;

		-- TODO check #atl==1
		index = actual_target_list[1];
		target = index_card(index, pside);

		if nil==target then
			print('BUGBUG poison arrow(82) nil target');
			return {};
		end

		print('DEBUG poison arrow add virtual card(1082) to ' .. index);

		-- poison
		eff_list2, err = action_virtual_attach(target, pside, 1073, pside[self.side][T_HERO][1]);
		if nil == eff_list2 then
			print('BUGBUG poison arrow(1073) cannot attach: ', err);
			return eff_list, err;
		end
		table_append(eff_list, eff_list2);
	
		-- disabled
		eff_list2, err = action_virtual_attach(target, pside, 1082, pside[self.side][T_HERO][1]);
		if nil == eff_list2 then
			print('BUGBUG poison arrow(1082) cannot attach: ', err);
			return eff_list, err;
		end
		table_append(eff_list, eff_list2);

		return eff_list;
	end,

}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 83,
	ctype = ABILITY,
	job = HUNTER,
	name = 'Flaming Arrow',
	star = 4,
	cost = 3,
	power = 2,
	hp = 0,    
	skill_desc = 'Target opposing ally takes 2 fire damage that cannot be reduced by ally or armor abilities, and is set ablaze.',
	target_list = {
		{side=2, table_list={T_ALLY} },
	},

	-- add a virtual attach card
	trigger_skill = function(self, pside, actual_target_list) 
		local index;
		local target;
		local ac;  -- attach card, the virtual card
		local eff_list = {};
		local eff_list2;
		local err = nil;

		if 1~=#actual_target_list then
			print('ERROR flaming arrow(83) atl~=1, ', #actual_target_list);
			return {};
		end
		-- TODO check #atl==1
		index = actual_target_list[1];
		target = index_card(index, pside);

		if nil==target then
			print('BUGBUG flaming arrow(83) nil target');
			return {};
		end

		-- print('DEBUG flaming arrow add virtual card(1075) to ' .. index);


		-- need set ablaze first, avoid Earthen(39) ability
		-- ablaze
		local hh;
		hh = pside[self.side][T_HERO][1];
		eff_list2, err = action_virtual_attach(target, pside, 1075, hh);
		if nil == eff_list2 then
			print('BUGBUG flame(1075) cannot attach: ', err);
			return eff_list, err;
		end
		table_append(eff_list, eff_list2);

		-- fire damage
		eff_list2 = action_damage(target, pside, hh, self.power, D_DIRECT);
		table_append(eff_list, eff_list2);

		return eff_list;
	end,

}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 84,
	ctype = ATTACH,
	job = HUNTER,
	name = 'Rapid Fire',
	star = 5,
	cost = 5,
	power = 0,  
	attack_timer = 0,
	hp = 0,    -- core logic, it is that simple!
	skill_desc = 'Your hero may attack 2 times on each of your turns while Rapid Fire is attached',
	target_list = {
		{side=1, table_list={T_HERO} },
	},

	trigger_attack_serial = function(self, pside, target, sss) 
		if target.ctype ~= HERO then
			return;
		end
		if self.side ~= sss then 
			return;
		end
		if self.attack_timer == 0 then
			self.attack_timer = 1;
		else 
			self.attack_timer = 0;
			set_not_ready(target);
		end
	end,
}
g_card_list[card.id] = card_class:new(card);


card = {
	id = 85,
	ctype = TRAP,
	job = HUNTER,
	name = 'Death Trap',
	star = 4,
	cost = 3,
	power = 0,  
	hp = 0,  
	skill_desc = 'Play face down.The next opposing ally to be summoned is killed.',
	has_actived = false;

	trigger_trap = function(self, target, pside, target_table)
	-- (side_src, side_target, src, target, target_table)
		-- src and target are the same side
		local eff_list = {};
		local eff_list2;

		if target_table.name ~= T_ALLY then 
			return eff_list;
		end

		if self.side == target.side then
			return eff_list; -- early exit
		end

		-- TODO need test
		self.has_actived = true; -- peter: fix self.has_actived

		eff_list[#eff_list + 1] = eff_trap(cindex(self), self.id);
		local my_hero = pside[self.side][T_HERO][1]; -- assert non-nil ?

		eff_list2 = action_damage(target, pside, my_hero, 99, D_DIRECT, A_NORMAL);
		table_append(eff_list, eff_list2);

		-- put self to grave
		eff_list2 = action_grave(self, pside);
		table_append(eff_list, eff_list2);

		return eff_list;
	end,
		
}
g_card_list[card.id] = card_class:new(card);


card = {
	id = 86,
	ctype = ATTACH,
	job = HUNTER, -- hunter
	name = 'Into The Forest',
	star = 4,
	cost = 1,
	power = 0,  
	hp = 0,    
	skill_desc = 'Your hero is hidden (it and its attachments cannot be targeted) until the start of your next turn', 
	hidden = true,   -- core logic!
	target_list = {
		{side=1, table_list={T_HERO} },
	},
	timer = 2,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 87,
	ctype = ARTIFACT,
	job = HUNTER, -- hunter
	name = 'Battle Plans',
	star = 5,
	cost = 2,
	power = 0,   -- add-on to power
	hp = 0,    
	skill_desc = 'You can view the top card of your deck while Battle Plans is in play. RES:1 Move the top card of your deck to the bottom.',
	skill_cost_resource = 1,
	view_top = true,	-- for client refresh

	trigger_add = function (target, pside, target_table)
		local eff_list = {};
		local eff_list2 = {};
		local top_card;
		local index;

		if target == nil or target_table.name ~= T_SUPPORT then
			return eff_list;
		end

	--	my_deck = pside[target.side][T_DECK];
	--	if #my_deck <= 0 then
	--		return eff_list;
	--	end
	--	top_card = my_deck[1];
	--	if top_card == nil then
	--		return eff_list;
	--	end

	--	index = cindex(top_card);
		index = card_index(target.side, T_DECK, 0);
		eff_list2[1] = eff_view_top(index);
		table_append(eff_list, eff_list2);

		return eff_list;
	end,

	trigger_remove = function (self, pside, src_table)
		local eff_list = {};
		local eff_list2 = {};
		local index;

		if src_table.name ~= T_SUPPORT then
			return eff_list;
		end

	--	my_deck = pside[target.side][T_DECK];
	--	if #my_deck <= 0 then
	--		return eff_list;
	--	end
	--	target = my_deck[1];
	--	if target == nil then
	--		return eff_list;
	--	end
	--	index = cindex(target);
		index = card_index(self.side, T_DECK, 0);
		eff_list2[1] = eff_hide_top(index);
		table_append(eff_list, eff_list2);

		return eff_list;
	end,

	trigger_target_validate = function(self, pside, target, tid)
		local my_deck = pside[self.side][T_DECK] or {}
		if #my_deck <= 0 then
			return false;
		end
		return true;
	end,

	trigger_skill = function (self, pside, atl)
		local eff_list = {}
		local eff_list2;
		local target;
		local my_deck;
		my_deck = pside[self.side][T_DECK];

		target = my_deck[1];
		eff_list2 = action_move(target, pside, my_deck, my_deck);
		table_append(eff_list, eff_list2);
		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 88,
	ctype = ATTACH,
	job = HUNTER, -- hunter
	name = 'Surprise Attack',
	star = 5,
	cost = 3,
	power = 0,  
	hp = 0,    
	skill_desc = 'Target friendly ally has ambush while Surprise Attack is attached, and when that ally deals combat damage, draw a card.',
	ambush = true,
	target_list = {
		{side=1, table_list={T_ALLY} },
	},

	trigger_attack = function(self, pside, target, power)
		local eff_list = {};
		local eff_list2;

		print('power = ' .. power);

		if power > 0 then
			eff_list2 = action_drawcard(pside, self.side);
			table_append(eff_list, eff_list2);
		end

		return eff_list;
	end,

}
g_card_list[card.id] = card_class:new(card);


card = {
	id = 89,
	ctype = TRAP,
	job = HUNTER,
	name = 'Net Trap',
	star = 5,
	cost = 2,
	power = 0,  
	hp = 0,  
	skill_desc = 'Play face down.The next opposing ally to be summoned is disabled (it cannot attack, defend or use abilities) for 3 turns.',
	has_actived = false;

	trigger_trap = function(self, target, pside, target_table)
	-- (side_src, side_target, src, target, target_table)
		-- src and target are the same side
		local eff_list = {};
		local eff_list2;
		local err;
		local src;
		src = pside[self.side][T_HERO][1];

		if target_table.name ~= T_ALLY then 
			return eff_list;
		end

		if self.side == target.side then
			return eff_list; -- early exit
		end

		self.has_actived = true;

		eff_list[#eff_list + 1] = eff_trap(cindex(self), self.id);

		eff_list2, err = action_virtual_attach(target, pside, 1089, src);
		if nil == eff_list2 then
			print('BUGBUG NetTrap3(1089) cannot attach: ', err);
			return eff_list, err;
		end
		table_append(eff_list, eff_list2);

		eff_list2 = action_grave(self, pside);
		table_append(eff_list, eff_list2);

		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 90,
	ctype = ARTIFACT,
	job = HUNTER, -- hunter
	name = 'Tracking Gear',
	star = 5,
	cost = 3,
	power = 0,   -- add-on to power
	hp = 0,    
	skill_desc = 'You can view the hands of opposing players while Tracking Gear is in play.',
	view_oppo = true,	-- for client refresh

	trigger_add = function (target, pside, target_table)
		local eff_list = {};
		local eff_list2 = {};
		local top_card;
		local index;

		if target == nil or target_table.name ~= T_SUPPORT then
			return eff_list;
		end

		index = card_index(3-target.side, T_HAND, 0);
		eff_list2[1] = eff_view_oppo(index);
		table_append(eff_list, eff_list2);

		return eff_list;
	end,

	trigger_remove = function (self, pside, src_table)
		local eff_list = {};
		local eff_list2 = {};
		local index;

		if src_table.name ~= T_SUPPORT then
			return eff_list;
		end

		index = card_index(3-self.side, T_HAND, 0);
		eff_list2[1] = eff_hide_oppo(index);
		table_append(eff_list, eff_list2);

		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 91,
	ctype = ABILITY,
	job = PRIEST, -- priest
	name = 'Healing Touch',
	star = 3,
	cost = 3,
	ab_power = 4,   -- was: power
	hp = 0,    
	skill_desc = 'Target friendly hero or ally heals 4 damage and has all enemy attachment and negative effects removed',
	target_list = {
		{side=1, table_list={T_HERO, T_ALLY} },
	},

	trigger_target_validate = function(self, pside, target, tid) -- {
		-- first test on tid=0
		if tid == 0 then 
			return true;
		end

		if tid ~= 1 or target == nil then
			print('BUGBUG healing touch(91)  target_validate tid, target:', tid, target);
			return false; -- BUG case
		end

		-- early exit on true,  last return false

		if target:get_base_hp()  < target.hp_max then
			-- useful DEBUG msg, do not remove
			-- print('DEBUG healing touch(91)  not full hp, can target :', target.name, target:get_base_hp(), target.hp_max);
			return true;  -- early exit
		end

		-- implicit: full hp

		-- need to check any enemy attachment (attach_card.home ~= target.side)

		for k, ac in ipairs(target.attach_list or {}) do
			if ac.src==nil then
				print('BUGBUG healing(91) ac.src=nil ac.id=', ac.id);
			end
			if nil ~= ac.src and ac.src.side ~= target.side then
				-- useful DEBUG msg do not remove
				print('DEBUG healing(91)  found enemy attach, target.name : ', ac.name, target.name);
				return true; -- early exit
			end
		end

		-- implicit: no enemy attachment
		-- useful DEBUG msg do not remove:
		-- print('DEBUG healing(91) full hp and no enemy attach, cannot use on : ', target.name);
		return false;

	end, -- trigger_target_validate }

	trigger_skill = function(self, pside, atl)  -- {
		local index;
		local target;
		local eff_list = {};
		local eff_list2 = {};
		local offset;

		index = atl[1];
		target = index_card(index, pside);

		if nil == target then
			return error_return('ERROR Healing Touch(91) target is nil');
		end

		offset = target:heal_hp(self.ab_power);
		-- TODO eff_hp_offset()
		eff_list[#eff_list + 1] = eff_hp(cindex(target), offset);
		
		-- TODO boris use exchange card , get a posion then apply to opposite mage
		local aclist = target.attach_list or {};
		for i = #aclist, 1, -1 do
			local ac = aclist[i];
			if nil ~= ac.src and ac.src.side ~= target.side then
				eff_list2 = action_grave(ac, pside);
				table_append(eff_list, eff_list2);
			end
		end

		return eff_list;
	end, -- trigger_skill }

	ai_weight = function(self, atl, pside, sss, ability) 
		return ai_weight_heal(self, atl, pside, sss, ability);
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 92,
	ctype = ATTACH,
	job = PRIEST, -- priest
	name = 'Inner Strength',
	star = 1,
	cost = 3,
	power = 2,   -- add-on to power
	hp = 0,    
	skill_desc = 'Target friendly ally has +2 attack while Inner Strength is attached',
	target_list = {
		{side=1, table_list={T_ALLY} },
	},
}
g_card_list[card.id] = card_class:new(card);

card = { 
	id = 93,
	ctype = ABILITY,
	job = PRIEST,
	name = 'Tidal Wave',
	star = 5,
	cost = 5,
	ab_power = 99,  -- attack
	dtype = D_MAGIC,
	atype = A_WAVE,
	hp = 0,  
	skill_desc = 'All allies are killed.',
	-- empty or nil target_list means NO need to have target
	target_list = {
	}, 

	trigger_target_validate = function(self, pside, target, tid) 
		local oppo_ally;
		local my_ally;

		my_ally = pside[self.side][T_ALLY];
		oppo_ally = pside[3-self.side][T_ALLY];
		-- when nobody is in ally (both my and oppo side),
		-- not allow tidal wave
		if (#oppo_ally + #my_ally) <= 0 then
			return false;
		end
		-- here: at least someone in ally
		return true;
	end,

	trigger_skill = function(self, pside, actual_target_list)
		local eff_list = {};
		local eff_list2 = {};
		local side_src;
		local target_list = {};
		local index_list = {};
		local src;
		side_src = pside[self.side];
		src = side_src[T_HERO][1]; -- assert non-nil ?

		-- oppo side first
		for k, v in ipairs(pside[3-self.side][T_ALLY]) do
			index_list[#index_list + 1] = v:index();
		end
		-- my side next
		for k, v in ipairs(pside[self.side][T_ALLY]) do
			index_list[#index_list + 1] = v:index();
		end
		eff_list[#eff_list + 1] = eff_anim(self:index(), self.id, self.atype
		, index_list);

		-- is order important?  shall we start with opposite side?
		-- oppo side
		for k, v in ipairs(pside[3-self.side][T_ALLY]) do
			target_list[#target_list + 1] = v;
		end

		eff_list2 = action_damage_list(target_list, pside, src
		, self.ab_power, self.dtype);
		table_append(eff_list, eff_list2);

		-- my side
		target_list = {};
		for k, v in ipairs(pside[self.side][T_ALLY]) do
			target_list[#target_list + 1] = v;
		end

		eff_list2 = action_damage_list(target_list, pside, src
		, self.ab_power, self.dtype);
		table_append(eff_list, eff_list2);


		return eff_list;

	end,

	ai_weight = function(self, atl, pside, sss, ability) -- start {
		local target;
		local weight = 0;
		-- use ability
		-- need to patch the atl, because target to all is not counted
		if ability  ~= true then
			return -9;
		end
		atl = {};
		for k, v in ipairs(pside[1][T_ALLY] or {}) do 
			table.insert(atl, v);
		end
		for k, v in ipairs(pside[2][T_ALLY] or {}) do 
			table.insert(atl, v);
		end
		return ai_weight_ability_damage(self, atl, pside, sss, ability);
	end, -- ai_weight }
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 94,
	ctype = ABILITY,
	job = PRIEST, -- 8
	name = 'Ice Storm',
	star = 3,
	cost = 3,
	ab_power = 2,
	dtype = D_ICE,
	atype = A_FROSTMIRE, -- peter: TODO need an ice animation for multi-target
	-- A_ARCANE
	hp = 0,
	skill_desc = 'All oppo allies take 2 ice damage',

	trigger_target_validate = function(self, pside, target, tid) 
		local oppo_ally;

		oppo_ally = pside[3-self.side][T_ALLY];
		if #oppo_ally <= 0 then
			return false;
		end
		return true;
	end,

	-- TODO check #oppo > 1
	trigger_skill = function(self, pside, atl) 
		local eff_list = {};
		local eff_list2 = {};
		local target_list;
		local src;
		local target;
		local oppo_ally;
		local index_list = {};

		src = pside[self.side][T_HERO][1];
		-- do a shallow clone of oppo ally, because it may die and
		-- change during damage!
		target_list = {};
		oppo_ally = pside[3-self.side][T_ALLY];
		for i=1, #(oppo_ally or {}) do
			target_list[i] = oppo_ally[i];
			index_list[#index_list + 1] = oppo_ally[i]:index();
		end

		eff_list[#eff_list + 1] = eff_anim(self:index(), self.id, self.atype
		, index_list);

		eff_list2 = action_damage_list(target_list, pside, src
		, self.ab_power, self.dtype);
		table_append(eff_list, eff_list2);

		return eff_list;
	end,

	-- copied from ai_weight_ability_damage
	ai_weight = function (self, atl, pside, sss, ability) -- {
		if not ability then
			print('BUGBUG icestorm(94) ai_weight for non-ability');
			return 0, 0, 0;
		end

		atl =  pside[3-sss][T_ALLY]; -- core logic: override atl as oppo ally
		return ai_weight_ability_damage(self, atl, pside, sss, ability);
	

		--[[
		local power = 0; -- damage to one target
		local damage = 0; -- total damage, accumulated
		local src;

		-- if self is an ally, then src is the self , else hero
		if self.ctype == ALLY then
			src = self;
		else
			src = pside[self.side][T_HERO][1];
		end

		if false == check_playable(self, pside, sss, ability) then
			print('BUGBUG icestorm(94)ai_weight not playable : ', self.id);
			return 0, '_', 0;
		end

		if nil == self.ab_power then
			print('BUGBUG ai_weight(94) self.ab_power==nil : ', self.id);
			return 0, '_', 0;
		end

		local weight = 0;
		local kill_count = '';
		local hh ; -- hero
		hh = pside[sss][T_HERO][1];
		atl =  pside[3-sss][T_ALLY]; -- core logic: override atl as oppo ally
		for i=1, #atl do -- start {
			local target;
			target = atl[i]; -- index_card(atl[i], pside);
			if nil == target then
				print('BUG: ai_weight(94) target nil: ', atl[i]);
				break;
			end
			power = self.ab_power or 0; 
			power = target:trigger_calculate_defend(pside, hh, power, 
				self.dtype);
			local kill;
			kill = power >= target.hp;
			if kill then
				weight = weight + WEIGHT_KILL * target.cost;
				weight = weight + WEIGHT_KILL_POWER * target.power;
				kill_count = kill_count .. 'K';
				if nil ~= target.die_power and target.die_power > 0 then
					-- TODO hpleft = hpleft - target.die_power
					--      die = hpleft <= 0
					--      if die then weight -= WEIGHT_DIE * src.cost
					weight = weight + WEIGHT_HURT * src.cost;
					weight = weight + WEIGHT_HURT_POWER * src.power;
				end
			else
				damage = damage + power;
				weight = weight + WEIGHT_DAMAGE * power * target.cost;
				weight = weight + WEIGHT_DAMAGE_POWER * power * target.power;
				kill_count = kill_count .. '_';
			end
		end -- end for #atl }

		if self.ctype == ABILITY then
			-- TODO using ability card is not important as ally die
			-- so weight lower here
			-- note:  ability does not count DIE_POWER, so it is lower
			weight = weight + WEIGHT_DIE * self.cost;
		else
			-- plasma case:  ally use ability
			weight = weight + WEIGHT_RESOURCE * self.skill_cost_resource;
			weight = weight + WEIGHT_ENERGY * self.skill_cost_energy;
		end

		return weight, kill_count, damage;
		]]--
	end -- end ai_weight_ability_damage }

}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 95,
	ctype = ABILITY,
	job = PRIEST, -- 8
	name = 'Focus Prayer',
	star = 3,
	cost = 3,
	power = 0,  
	hp = 0,  
	skill_desc = 'Target enemy item or support ability is destroyed',
	target_list =  {
		-- note: does it include weapon and armor? assume yes
		{side=2, table_list={T_SUPPORT}},
	},
	trigger_skill = function (self, pside, actual_target_list)
		local index;
		local target;
		local eff_list = {};
		local eff_list2 = {};
		if actual_target_list==nil or #actual_target_list<1 then
			print('BUGBUG: focus prayer(95) atl=nil or #<1');
			return {};
		end
		index = actual_target_list[1];
		target = index_card(index, pside);
		if target.side == self.side or target.table ~= T_SUPPORT then
			print('BUGBUG focus prayer(95) same side or not support');
			return {};
		end

		eff_list2 = action_durability_change(target, pside, -999);
		table_append(eff_list, eff_list2);

		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 96,
	ctype = ABILITY,
	job = PRIEST, -- 8
	name = 'Resurrection',
	star = 4,
	cost = 3,
	hp = 0,
	skill_desc = 'All allies in your graveyard are return to the top of your deck',

	trigger_target_validate = function(self, pside, target, tid) -- 
		local my_grave = pside[self.side][T_GRAVE] or {};
		for i=1, #my_grave do
			if my_grave[i].ctype == ALLY then
				return true; -- someone in grave
			end
		end
		return false;  -- no one in grave
	end,

	-- TODO check #ally in grave > 1 ?
	trigger_skill = function(self, pside, atl)  -- {
		local eff_list = {};
		local eff_list2;
		local target_list;
		local target;
		local my_grave;
		local my_deck;

		my_grave = pside[self.side][T_GRAVE];
		my_deck = pside[self.side][T_DECK];

		-- target_list contains all ally in my grave
		target_list = {};
		for i=1, #my_grave do
			if my_grave[i].ctype == ALLY then
				target_list[ #target_list + 1] = my_grave[i];
			end
		end
		if #target_list <= 0 then
			print('ERROR resurrect(96) no one in grave ', #target_list);
		end

		-- peter: what is the order? bottom = last die?
		for i=1, #target_list do
			target = target_list[i];
			eff_list2 = action_move_top(target, pside, my_grave, my_deck);
			table_append(eff_list, eff_list2);
		end
		return eff_list;
	end, -- resurrection trigger_skill }


}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 97,
	ctype = ATTACH,
	job = PRIEST, -- 8
	name = 'Holy Shield',
	star = 3,
	cost = 3,
	timer = 2,
	skill_desc = 'Target friendly hero or ally takes no damage and cannot be killed until the start of your next turn',
	target_list = {
		{side=1, table_list={T_HERO, T_ALLY} },
	},

	trigger_calculate_defend = function(pside, src, power, dtype) -- {
		-- D_DIRECT cannot damage it
		return 0;  -- always reduce damage power = 0
	end, -- trigger_calculate defend }
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 98,
	ctype = ABILITY,
	job = PRIEST, -- 8
	name = 'Plague',
	star = 3,
	cost = 4,
	power = 0, 
	hp = 1,
	-- TODO check when oppsing player resources max <= 2
	skill_desc = 'Target opposing player remove 2 resources from play, leaving a minimus of 2. Remove one of your resources from play.',

	target_list = {},
	-- TODO trigger_target_validate : check oppo_resource > 0

	trigger_skill = function(self, pside, actual_target_list)
		local eff_list = {};
		local my_side = pside[self.side];
		local other_side = pside[3-self.side];
		local res_offset = 0;

		if other_side.resource_max > 2 then
			local eff_list2 = {};
			res_offset = 2 - other_side.resource_max;  
			if res_offset <= -2 then
				res_offset = -2
			end
			other_side.resource_max = other_side.resource_max + res_offset;
			eff_list2[1] = eff_resource_max_offset(res_offset, other_side.id);
			if other_side.resource > other_side.resource_max then
				other_side.resource = other_side.resource_max;
				eff_list2[2] = eff_resource_value(other_side.resource, other_side.id);
			end
			table_append(eff_list, eff_list2);
		end

		res_offset = -1;
		local eff_list2 = {}

		-- assume my_side.resource_max > 1

		my_side.resource_max = my_side.resource_max + res_offset;
		eff_list2[1] = eff_resource_max_offset(res_offset, my_side.id);
		table_append(eff_list, eff_list2);

		return eff_list; 
	end,

}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 99,
	ctype = ABILITY,
	job = PRIEST, -- 8
	name = 'Smite',
	star = 2,
	cost = 3,
	ab_power = 3,  -- damage
	dtype = D_ARCANE,
	atype = A_ARCANE,
	hp = 1,  
	skill_desc = 'Target opposing hero or ally takes 3 arcane damage',
	target_list = {
		{side=2, table_list={T_HERO, T_ALLY,}  },    -- hero must be the first 
	}, 

	trigger_skill = function(self, pside, actual_target_list) -- {
		-- src is the hero who trigger this skill
		local eff_list = {};
		local eff_list2 = {};
		local target;
		local src;
		src = pside[self.side][T_HERO][1]; -- assert non-nil ?

		-- new_index -> obj		
		if (actual_target_list==nil or actual_target_list[1]==nil) then
			print('BUG: smite(99) atl or atl[1]=nil');
			return {};
		end
		target = index_card(actual_target_list[1], pside);
		if target==nil then
			print('BUGBUG smite(99) target=nil');
			return {};
		end

		eff_list = action_damage(target, pside, src, 
			self.ab_power, self.dtype, self.atype);
		return eff_list;
	end, -- trigger_skill }

}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 100,
	ctype = ARTIFACT,
	job = PRIEST, -- 8
	name = 'Book of Curses',
	star = 3,
	cost = 3,
	power = 0,  
	hp = 0,  
	skill_desc = 'Opposing allies have -1 attack while Book of Curses is in play',

	-- note: this is obsolete, because core logic already check duplicate
	trigger_target_validate = function(self, pside, target, tid)
		local supp;
		supp = pside[self.side][T_SUPPORT];
		for k, v in ipairs(supp or {}) do
			if (v.id == self.id) then
				--print('DEBUG curses(100) duplicated curses');
				return false;
			end
		end
		return true;
	end,
	-- target is self (this card)
	trigger_add = function (self, pside, target_table) -- {
		local eff;
		local eff_list = {};
		local eff_list2;
		local side_target = pside[3-self.side]; -- oppo side
		-- side_target power - 1 (if > 1)
		-- curses only effective in support table
		if pside[self.side][T_SUPPORT] ~= target_table then
			if target_table.name==T_SUPPORT then
				print('BUGBUG curses(100) table: ', side_target[T_SUPPORT], target_table);
			end
			return;
		end

		local index_list = {};
		for k, v in ipairs(side_target[T_ALLY]) do
			index_list[#index_list + 1] = v:index();
		end
		-- only for client anim
		eff_list[#eff_list + 1] = eff_anim(self:index(), self.id
		, A_POWER_DOWN, index_list);

		for k, v in ipairs(side_target[T_ALLY]) do
			--[[
			local new_power, old_power;
			-- even power = 0, we will still reduce power_offset
			old_power = v.power;
			v.power_offset = (v.power_offset or 0) - 1;
			new_power = v.power;
			if new_power ~= old_power then
				eff = eff_power_offset(v:index(), new_power - old_power);
				eff_list[ #eff_list + 1 ] = eff;
			end
			]]--
			eff_list2 = action_power_offset(v, -1);
			table_append(eff_list, eff_list2);
		end
		return eff_list;
	end, -- trigger_add }

	-- self is this card (curses)
	trigger_remove = function (self, pside, target_table) -- {
		local eff_list = {};
		local eff_list2;
		local eff;
		local side_target = {};
		side_target = pside[3-target_table.side];  -- oppo side
		if target_table.name ~= T_SUPPORT then
			return {}; -- early exit empty list
		end

		local index_list = {};
		for k, v in ipairs(side_target[T_ALLY]) do
			index_list[#index_list + 1] = v:index();
		end
		-- only for client anim
		eff_list[#eff_list + 1] = eff_anim(self:index(), self.id
		, A_POWER_UP, index_list);

		for k, v in ipairs(side_target[T_ALLY]) do
			--[[
			local old_power;
			local new_power;

			old_power = v.power;
			v.power_offset = (v.power_offset or 0) + 1;
			new_power = v.power;

			-- v.power = v.power - 1;
			if new_power ~= old_power then
				eff = eff_power_offset(v:index(), new_power - old_power);
				eff_list[ #eff_list + 1 ] = eff;
			end
			]]--
			eff_list2 = action_power_offset(v, 1);
			table_append(eff_list, eff_list2);
		end
		return eff_list;
	end, -- trigger_remove }

	-- call this when war banner is already in support and other cards are added
	trigger_other_add = function(self, target, pside, target_table)
		-- self is this curses, 
		-- need check: self and target are oppo side
		local eff_list = {};
		local eff_list2;

		if target_table.name ~= T_ALLY then 
			return {};
		end
		-- curses and target on same side, early exit
		if self.side == target.side then
			return {}; -- early exit
		end

		-- core logic 
		--[[
		local old_power, new_power;
		old_power = target.power;
		target.power_offset = (target.power_offset or 0) - 1; -- curses
		new_power = target.power;
		if (old_power ~= new_power) then
			eff_list[1] = eff_power_offset(target:index(), 
				new_power - old_power);
		end

		]]--
		eff_list2 = action_power_offset(target, -1);
		table_append(eff_list, eff_list2);
		-- order is important , after target.power is updated (power_offset)
		return eff_list;
	end,
		
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 101,
	ctype = ABILITY,
	job = ROGUE, 
	name = 'Assassination',
	star = 5,
	cost = 3,
	power = 0,   
	hp = 0,    
	skill_desc = 'Target friendly ally that is not exhausted, disabled or frozen kills target opposing ally and is then exhausted.',
	target_list = {
		{side=1, table_list={T_ALLY} },
		{side=2, table_list={T_ALLY} },
	},

	trigger_target_validate = function(self, pside, target, tid)
		if tid == 0 then
			local my_ally = pside[self.side][T_ALLY];
			local oppo_ally = pside[3-self.side][T_ALLY];
			if #my_ally <= 0 or #oppo_ally <= 0 then
				return false;
			end
			return true;
		end  -- for tid=0 check

		if tid > 2 or target == nil then
			print('BUGBUG assassination(101) target_validate tid, target:',
				tid, target);
			return false;
		end
		-- make sure src is not exhausted
		if tid==1 then
			-- ready==0 or 1 are ok???
			if false==check_ready_attack(target) then
				return false;
			end
			-- disable check
			if true==target.disable then
				return false;
			end
		end
		return true;
	end,

	trigger_skill = function(self, pside, atl)
		local src;
		local target;
		local eff_list = {};
		local eff_list2 = nil;

		src = index_card(atl[1], pside);
		target = index_card(atl[2], pside);
		if nil == src or nil == target then
			print('Assassination(101) skill error:nil_src or nil_target!');
			return eff_list;
		end

		set_not_ready(src); -- exhausted it

		-- TODO eff_ready(), it may put inside set_not_ready
--		eff_list2 = action_grave(target, pside);
		eff_list2 = action_damage(target, pside, src, 99, D_NORMAL, A_NORMAL);

		table_append(eff_list, eff_list2);
		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 102,
	ctype = SUPPORT,
	job = ROGUE,  
	name = 'Reconnaissance', 
	star = 4,
	cost = 3,
	power = 0, 
	hp = 0,    
	skill_desc = 'Look at target opponents hand. Draw a card. Your allies have +1 attack until the end of your turn.',
	timer = 1,
	view_oppo = true,	-- for client refresh

	trigger_add = function(self, pside, target_table)
		local eff_list = {};
		local eff_list2 = {};
		local err;
		local index;

		if target_table.name ~= T_SUPPORT then
			return {};  -- early exit
		end

		-- open view oppo card 
		index = card_index(3-self.side, T_HAND, 0);
		eff_list2[1] = eff_view_oppo(index);
		table_append(eff_list, eff_list2);

		-- draw a card
		eff_list2 = action_drawcard(pside, self.side);
		table_append(eff_list, eff_list2);

		-- add power+1
		local my_side = pside[self.side];
		local list = my_side[T_ALLY];
		for _, target in ipairs(list) do
			eff_list2, err = action_virtual_attach(target, pside, 1102, self);
			table_append(eff_list, eff_list2);
		end
			
		return eff_list;

	end,

	trigger_other_add = function(src, target, pside, target_table)

		local eff_list = {};
		local eff_list2 = {};
		local err;
		local my_side = pside[src.side];

		if src.side ~= target.side or target_table ~= my_side[T_ALLY] then
			return eff_list;
		end

		eff_list2, err = action_virtual_attach(target, pside, 1102, src);
		table_append(eff_list, eff_list2);

		return eff_list;

	end,

	trigger_remove = function(target, pside, src_table)
		local eff_list = {};
		local eff_list2 = {};
		local index;

		-- if pside[target.side][T_SUPPORT] ~= target_table then
		if T_SUPPORT ~= src_table.name then
			return eff_list; -- early exit empty list
		end

		index = card_index(3 - target.side, T_HAND, 0);
		eff_list2[1] = eff_hide_oppo(index);
		table_append(eff_list, eff_list2);

		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 103, -- virtual card > 1000
	ctype = WEAPON, 
	job = ROGUE,  -- neutral
	name = 'Throwing Knife',
	star = 4,
	cost = 4,
	power = 1,  
	hp = 4,  
	skill_desc = 'Ambush(Attacks by your hero with this weapon can not be defended)',
	ambush = true,

	trigger_attack = function(self, pside, target, power)
		local eff_list = {};
		local eff_list2;

		-- reduce durability
		eff_list2 = action_durability_change(self, pside, -1);
		table_append(eff_list, eff_list2);
		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 104,
	ctype = ATTACH, 
	job = ROGUE,  
	name = 'Sorcerous Poison',
	star = 4,
	cost = 2,
	power = 0,  
	hp = 0, 
	skill_desc = 'While Sorcerous Poison is attached to target weapon you control, heroes and allies damaged in combat by your hero are poisoned.',

	target_list = {
		{side=1, table_list={T_WEAPON } },
	},

	trigger_attack = function(self, pside, target, power)
		local eff_list = {};
		local eff_list2;

		if nil == target then
			print('BUGBUG Sourcerous Poison(104) nil target');
			return eff_list;
		end

		if target:die() or target.reborn == true then
			return eff_list;
		end

		if target.table ~= T_ALLY then
			return eff_list;
		end

		if power > 0 then
			local err;
			-- poison
			eff_list2, err = action_virtual_attach(target, pside, 1073, pside[self.side][T_HERO][1]);
			if nil == eff_list2 then
				print('BUGBUG Sorcerous Poison(1073) cannot attach: ', err);
				return eff_list, err;
			end
			table_append(eff_list, eff_list2);
		end

		return eff_list;
	end,

}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 105,
	ctype = ABILITY,
	job = ROGUE,
	name = 'Stop, Thief!',
	star = 4,
	cost = 4,
	power = 0,  
	hp = 0,  
	skill_desc = 'Target enemy item is destroyed, and you gain one resource.',
	target_list =  {
		-- note: does it include weapon and armor? assume yes
		{side=2, table_list={T_SUPPORT}},
	},
	trigger_target_validate = function(self, pside, target, tid)
		if tid == 0 then
			return true;
		end
		if tid ~= 1 or target == nil then
			print('BUGBUG Stop,Thief(105)  target_validate tid, target:', tid, target);
			return false; -- BUG case
		end

		if check_is_item(target) == false then
			return false;
		end

		return true;
	end,
	trigger_skill = function(self, pside, atl) -- start {
		local eff_list = {};
		local eff;
		local eff_list2;
		local res_offset;
		local myside = pside[self.side];

		local target = index_card(atl[1], pside);
		-- print_card(target);

		if #atl < 1 or nil == target then
			return error_return('BUGBUG Stop,Thief!(105) #atl<1 or target=nil' .. 
				#(atl or {}) .. ', ' .. tostring(nil==target));
		end

		-- use durability change instead of move to grave directly
		eff_list2 = action_durability_change(target, pside, -999);
		table_append(eff_list, eff_list2);

		res_offset = 1;
		myside.resource_max = myside.resource_max + res_offset;
		eff_list2[1] = eff_resource_max_offset(res_offset, myside.id);
		myside.resource = myside.resource + res_offset;
		eff_list2[2] = eff_resource_offset(res_offset, myside.id);
		table_append(eff_list, eff_list2);

		return eff_list;
	end, -- trigger_skill }
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 106,
	ctype = ARTIFACT, 
	job = ROGUE, 
	name = 'Assassin\'s Cloak',
	star = 5,
	cost = 3,
	power = 0,  
	hp = 0,  
	skill_desc = 'RES:0 Friendly allies have ambush (their attacks cannot be defended) until the end of your turn.',
	skill_cost_resource = 0,

	target_list = {
		{side=1, table_list={T_ALLY}},
	},

	trigger_skill = function(self, pside, atl)
		local err;
--		local target;
		local eff_list = {};
		local eff_list2;

		--TODO add a virtual attach to self for stealth
		-- get_stealth() will check ally stealth

--		target = index_card(atl[1], pside);
		local list = pside[self.side][T_ALLY];
		-- need to check target table
		if self.table ~= T_SUPPORT then 
			return eff_list;
		end

		for k, target in ipairs(list) do
			eff_list2, err = action_virtual_attach(target, pside, 1106, self);
			if nil == eff_list2 then
				print('BUGBUG Assassin\'s Cloak(1106) cannot attach: ', err);
				return eff_list, err;
			end
			table_append(eff_list, eff_list2);
		end
		return eff_list;
	end,

	trigger_other_add = function(src, target, pside, target_table)
	-- side_src, side_target, src, target, target_table)
		local eff;
		local err;
		local eff_list = {};
		local eff_list2 = {};
		local side_target = pside[target.side];
		-- need to check src, target are in the same side
		if src.side ~= target.side or target_table ~= side_target[T_ALLY] then
			return {};
		end;

		-- not use skill in this turn
		if check_ready_ability(src) == true then
			return {};
		end

		-- TODO need test  (peter fix self -> src)
		eff_list2, err = action_virtual_attach(target, pside, 1106, src);
		if nil == eff_list2 then
			print('BUGBUG Assassin\'s Cloak(1106) cannot attach: ', err);
			return eff_list, err;
		end
		table_append(eff_list, eff_list2);
		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 107,
	ctype = ARTIFACT,
	job = ROGUE,
	name = 'III-Gotten Gains',
	star = 5,
	cost = 4,
	power = 0,
	hp = 0,
	skill_desc = 'When an opposing ally is killed or enemy item is destroyed while Ill-Gotten Gains is in play, draw a card.',

	-- when someone die, all card will be call 
	-- trigger_other_kill(card, someone, killer,pside)
	trigger_other_kill = function(self, target, src, pside)
		local eff_list = {};

		-- use old_side instead of target.side (side before enter grave)
		if target.ctype ~= ALLY or target.side == self.side then
			return eff_list;
		end

		eff_list = action_drawcard(pside, self.side);
		return eff_list;
	end,

	trigger_other_remove = function(src, target, pside, target_table)
		local eff;
		local eff_list = {};
		local side_target = pside[target.side];

		-- need to check whether they are the same side
		if src.side==target.side or target_table ~= side_target[T_SUPPORT] then
			return {};
		end
		if check_is_item(target) == false then
			return {};
		end

		eff_list = action_drawcard(pside, src.side);
		return eff_list;
	end,

}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 108,
	ctype = SUPPORT, 
	job = ROGUE,  
	name = 'Lay Low',
	star = 4,
	cost = 3,
	power = 0,  
	hp = 0,  
	skill_desc = 'Your hero and allies cannot attack and are hidden (they and their attachments cannot be targeted) until the end of your next turn.',
	timer = 3,

	no_attack = true,
	no_attack_target = { side = 1, table_list = { T_ALLY, T_HERO } },
	hidden = true,   -- core logic!
	target_list = {
		{ side=1, table_list={ T_ALLY, T_HERO } },
	},
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 109,
	ctype = ABILITY,
	job = ROGUE,
	name = 'Mugged!',
	star = 4,
	cost = 3,
	skill_desc = 'Target opposing ally is disabled (it cannot attack, defend or use abilities) for 1 turn. Draw a card.',
	target_list =  {
		{side=2, table_list={T_ALLY}},
	},

	-- add a virtual attach card
	trigger_skill = function(self, pside, actual_target_list) 
		local index;
		local target;
		local ac;  -- attach card, the virtual card
		local eff_list = {};
		local eff_list2;
		local err = nil;

		index = actual_target_list[1];
		target = index_card(index, pside);

		if nil==target then
			print('BUGBUG Mugged(109) nil target');
			return {};
		end
	
		eff_list2, err = action_virtual_attach(target, pside, 1109, pside[self.side][T_HERO][1]);
		if nil == eff_list2 then
			print('BUGBUG Mugged(109) cannot attach: ', err);
			return eff_list, err;
		end
		table_append(eff_list, eff_list2);

		eff_list2 = action_drawcard(pside, self.side);
		table_append(eff_list, eff_list2);

		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 110,
	ctype = ALLY,
	job = ROGUE,
	name = 'Nightshade',
	star = 3,
	cost = 3,
	power = 1,  -- attack
	hp = 2,
	skill_desc = 'Ambush (Attacks by this ally cannot be defended.)\nStealth (This ally cannot be attacked.)',
	ambush = true,
	stealth = true,

	ai_weight = ai_weight_attack;
}
g_card_list[card.id] = card_class:new(card);


card = {
	id = 131,
	ctype = SUPPORT,
	job = HUMAN, 
	name = 'Cover of Night', 
	star = 1,
	cost = 3,
	power = 0, 
	hp = 0,    
	skill_desc = 'Friendly allies have stealth (they cannot be attacked for the next 2 turns', 
	stealth = true; -- core logic!!
	timer = 4;
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 132,
	ctype = ABILITY,
	job = HUMAN,  -- human
	name = 'Retreat',
	star = 2,
	cost = 2,
	power = 0,
	hp = 0,  
	skill_desc = 'Target ally is returned to its owner hand',
	target_list = { 
		{side=3, table_list={T_ALLY,}  },    
	}, 

	trigger_target_validate = function(self, pside, target, tid)
		if tid == 0 then
			return true;
		end
		if tid ~= 1 or target == nil then
			print('BUG: Retreat(132) target_validate tid, target', tid, target);
			return false;
		end

		-- print('#target[T_HAND] = ', #pside[target.side][T_HAND]);
		if #pside[target.side][T_HAND] <= 7 then
			return true;
		end

		return false;
	end,

	trigger_skill = function(self, pside, actual_target_list)
		local at; -- at is the number (2101 = side 2 hand 1)
		local cc;
		local eff_list = {};
		local eff_list2 = {};
		local side_target;
		local side_home;  -- retreat back to home T_HAND
		at = actual_target_list[1]; -- hard code the first target

		cc = index_card(at, pside);
		if cc==nil then
			print('ERROR retreat a nil card  at=' .. at);
			return {}; -- empty effect
		end

		-- print('RETREAT:  self.side=' .. self.side , ' cc.side=' .. cc.side);
		
		-- first put the attach card to grave
		--[[
		for k, ac in ipairs(cc.attach_list or {}) do
			-- print('DEBUG att_card grave  ac.side,table,pos,attpos:', ac.side, ac.table, ac.pos, ac.attpos);
			eff_list2 = action_grave(ac, pside);
			table_append(eff_list, eff_list2);
		end
		]]--
		local aclist = cc.attach_list or {};
		for i = #aclist, 1, -1 do
			local ac = aclist[i];
			eff_list2 = action_grave(ac, pside);
			table_append(eff_list, eff_list2);
		end
	
		side_target = pside[cc.side];
		side_home = pside[cc.home];

		-- cc:refresh();
		eff_list2 = action_refresh(cc, pside);
		table_append(eff_list, eff_list2);

		assert(cc.attach_list==nil or #cc.attach_list==0);
		-- cc.attach_list = nil; -- for garbage collection 

		-- assume cc must be in ally
		eff_list2 = action_move(cc, pside, side_target[cc.table], 
			side_home[T_HAND]);
		table_append(eff_list, eff_list2);

		return eff_list;
	end,

	ai_weight = function(self, atl, pside, sss, ability) -- {
	-- local self = index_card(index, pside);
	-- local atl = index_card_list(atl, pside);

	if nil == self then
		-- weight, round, power
		return 0;
	end

	-- check resource/energy, disable etc
	if false == check_playable(self, pside, sss, ability) then
		print('BUGBUG (132)retreat:ai_weight not playable:', self.id);
		return 0, 0, 0;
	end

	local weight = 0;
	for i=1, #atl do
		local target;
		local same; -- 1 for oppo side , -1 for same side
		target = atl[i]; -- index_card(atl[i]);
		if nil == target then
			print('BUGBUG (132)retreat:ai_weight target=nil');
			break;
		end
		same = 1 - 2 * math.abs(target.side - sss);
		-- power=0 can be (27)birgitte or (53)dmt
		if target.disable or (target.power <= 0 and target.id ~= 53 and target.id ~= 27) then
			same = -same;
		end
		weight = weight - same * WEIGHT_GENERAL_TARGET * target.cost ;
	end
	return weight;
	end,  -- ai_weight for retreat }
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 133,
	ctype = ATTACH,
	job = HUMAN,  -- human
	name = 'Reinforced Armor',
	star = 1,
	cost = 2,
	power = 0,  -- attack
	hp = 0,  
	skill_desc = 'Target friendly ally has all damage to it reduced by 1 while Reinforced Armor is attached',
	target_list = { -- attach only has one target!
		{side=1, table_list={T_ALLY,}  },    -- hero must be the first 
	}, 

	-- TODO card_class handle trigger_calculate_defend
	trigger_calculate_defend = function (self, pside, src, power, dtype)
		power = power - 1;
		if power < 0 then
			power = 0;
		end
		return power;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 134,
	ctype = ABILITY,
	job = HUMAN,  -- human
	name = 'Special Delivery',
	star = 1,
	cost = 4,
	ab_power = 3,  -- attack
	dtype = D_MAGIC,
	atype = A_NORMAL,
	hp = 0,  
	skill_desc = 'Target opposing hero or ally takes 3 damages', 
	target_list = { -- attach only has one target!
		{side=2, table_list={T_HERO, T_ALLY, }  },    -- hero must be the first?
	}, 

	trigger_skill = function(self, pside, actual_target_list)
		-- src is the hero who trigger this skill
		local target;
		local at;
		local eff_list = {};
		local eff_list2 = {};
		local src;

		src = pside[self.side][T_HERO][1];

		at = actual_target_list[1];
		target = index_card(at, pside);
		if target == nil then
			print('ERROR: special delivery target is nil');
			return {};
		end

		-- TODO check ally or hero type for target
		eff_list = action_damage(target, pside, src, 
			self.ab_power, self.dtype, self.atype);
		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 135,
	ctype = ABILITY, 
	job = HUMAN,  -- human
	name = 'Campfire Stories',
	star = 2,
	cost = 2,
	power = 0,  
	ab_power = 2,  
	hp = 0,
	skill_desc = 'All friendly allies heal 2 damage. Draw a card',


	trigger_skill = function(self, pside, actual_target_list)
		-- no check on actual_target_list,  
		-- heal+2 all allies in pside[self.side][T_ALLY] 
		local eff_list = {};
		local eff_list2;

		local target_list = {};
		for k, v in ipairs(pside[self.side][T_ALLY]) do
			target_list[#target_list + 1] = v;
		end

		for k, target in ipairs(target_list) do
			-- note on self.ab_power, self.dtype
			eff_list2 = action_heal(target, pside, self, 
				self.ab_power);
			table_append(eff_list, eff_list2);
		end

		eff_list2 = action_drawcard(pside, self.side, -1);
		table_append(eff_list, eff_list2);
		return eff_list;
	end,

	ai_weight = function(self, atl, pside, sss, ability) 
		local weight = 0;
		atl = {};  -- re-create a new atl[] for ai_weight_heal
		for k, v in ipairs(pside[self.side][T_ALLY]) do
			-- only ally has been damaged
			if v.hp < v.hp_max then
				atl[#atl + 1] = v;
			end
		end
		weight = ai_weight_heal(self, atl, pside, sss, ability);
		-- offset the negative die inside ai_weight_heal(), +50 better
		weight = weight - WEIGHT_DIE * self.cost + 50;
		
		return weight;
	end, 
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 136,
	ctype = ARTIFACT, 
	job = HUMAN, 
	name = 'Good Ascendant',
	star = 4,
	cost = 3,
	power = 2,  
	hp = 0,  
	skill_desc = 'At the start of each of your turns while Good Ascendant is in play, all friendly allies heals 2 damage. RES:0 Target Evil Ascendant is Destoryed.',
	skill_cost_resource = 0,

	target_list = {
		{side=3, table_list={T_SUPPORT}},
	},

	trigger_target_validate = function(self, pside, target, tid)
		if tid == 0 then
			return true;
		end

		if tid ~= 1 or target == nil then
			print('BUGBUG Good Ascendant(136)  target_validate tid, target:', tid, target);
			return false; -- BUG case
		end

		if target.table ~= T_SUPPORT then
			return false;
		end

		if target.id ~= 145 then
			return false;
		end

		return true;

	end,

	trigger_skill = function(self, pside, atl)

		local index = atl[1];

		local target = index_card(index, pside);

		local eff_list = action_durability_change(target, pside, -999);

		return eff_list;

	end,

	-- trigger that works when turn start
	trigger_turn_start = function(self, pside, sss)
		-- self, pside
		local eff_list = {};
		local eff_list2;

		if sss ~= self.side then
			return {}
		end

		local my_hero = pside[sss][T_HERO][1];
		local my_ally = pside[sss][T_ALLY];

		for	_, v in ipairs(my_ally) do
			eff_list2 = action_heal(v, pside, my_hero, self.power);
			table_append(eff_list, eff_list2);
		end

		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 137,
	ctype = ABILITY, 
	job = HUMAN, 
	name = 'Radiant Sunlight',
	star = 3,
	cost = 2,
	hp = 0,  -- test
	-- TODO when opposing energy = 0
	skill_desc = 'Target opposing hero loses 2 shadow energy', 

	target_list = {
		-- DEBUG add ally, should be only hero
		{side=2, table_list={T_HERO} },
	},

	trigger_skill = function (self, pside, atl)

		-- hard coded hero table always has 1 hero
		local target = index_card(atl[1], pside);
		local eff = nil;
		local eff_list = {};
		if target == nil then 
			print('BUGBUG (137) radiant sunlight target hero = nil');
			return eff_list;
		end

		-- print('DEBUG (137) radiant sunlight on :', target.name);
		-- do not check <=0, action_energy will do this (let animation go)

		local offset = -2;
		eff_list = action_energy(offset, pside, target.side);
		return eff_list;
	end,
	
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 138,
	ctype = ABILITY,
	job = HUMAN,  -- human
	name = 'Shrine of Negatia',
	star = 1,
	cost = 1,
	power = 0,
	hp = 0,  
	skill_desc = 'Target item or support ability with cost 4 or less is returned to its controllers hand',
	target_list = { 
		{side=3, table_list={T_SUPPORT,}  },    
	}, 

	trigger_target_validate = function(self, pside, target, tid)
		if tid == 0 then
			return true;
		end
		if tid ~= 1 or target == nil then
			print('BUGBUG: Shrine of Negatia(138) target_validate tid, target', tid, target);
			return false;
		end

		-- implicit: tid==1,   target non-nil
		if target.cost <= 4 then
			return true;
		end

		return false;
	end,

	trigger_skill = function(self, pside, actual_target_list)
		local eff_list = {};
		local eff_list2;
		local side_target;
		local side_home;  -- retreat back to home T_HAND

		local at = actual_target_list[1]; -- hard code the first target

		local cc = index_card(at, pside);
		if cc==nil then
			print('ERROR  a nil card  at=' .. at);
			return {}; -- empty effect
		end

		if cc.cost > 4 then
			print('BUG	Shrine(138) cost > 4');
			return {};
		end

		print('Shrine(138):  self.side=' .. self.side , ' cc.side=' .. cc.side);
		
		-- first put the attach card to grave
		-- TODO test (104) attach to weapon
		for k, ac in ipairs(cc.attach_list or {}) do
			print('DEBUG (138)att_card grave  ac.side,table,pos,attpos:', ac.side, ac.table, ac.pos, ac.attpos);
			eff_list2 = action_grave(ac, pside);
			table_append(eff_list, eff_list2);
		end
	
		side_target = pside[cc.side];
		side_home = pside[cc.side];

		-- cc:refresh();
		eff_list2 = action_refresh(cc, pside);
		table_append(eff_list, eff_list2);

		assert(cc.attach_list==nil or #cc.attach_list==0);
		-- cc.attach_list = nil; -- for garbage collection 

		-- assume cc must be in ally
		eff_list2 = action_move(cc, pside, side_target[cc.table], 
			side_home[T_HAND]);
		table_append(eff_list, eff_list2);

		return eff_list;
	end,

	ai_weight = function(self, atl, pside, sss, ability) -- {
	-- local self = index_card(index, pside);
	-- local atl = index_card_list(atl, pside);

	if nil == self then
		-- weight, round, power
		return 0;
	end

	-- check resource/energy, disable etc
	if false == check_playable(self, pside, sss, ability) then
		print('BUGBUG (138)shrin:ai_weight not playable:', self.id);
		return 0, 0, 0;
	end

	local weight = 0;
	for i=1, #atl do
		local target;
		local same; -- 1 for oppo side , -1 for same side
		target = atl[i]; -- index_card(atl[i]);
		if nil == target then
			print('BUGBUG (138)shrin:ai_weight target=nil');
			break;
		end
		same = 1 - 2 * math.abs(target.side - sss);
		weight = weight - same * WEIGHT_GENERAL_TARGET * target.cost ;
	end
	return weight;
	end,  -- ai_weight shrine }

}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 139,
	ctype = ATTACH, 
	job = HUMAN,  
	name = 'Poor Quality',
	star = 2,
	cost = 3,
	power = -1,  
	hp = 0, 
	skill_desc = 'While Poor Quality is attached to target enemy weapon or armor, it has -1 attack or defendse, and loses 1 additional durability when used in combat.', 

	target_list = {
		-- DEBUG add ally, should be only hero
		{side=2, table_list={T_WEAPON, T_ARMOR} },
	},

	--[[
	trigger_skill = function (self, pside, atl)

		local eff_list = {};
		local eff_list2;

		local target = index_card(atl[1], pside);
		if target == nil then 
			print('BUGBUG Poor Quality(139) target = nil');
			return eff_list;
		end

		if target.ctype ~= WEAPON and target.ctype ~= ARMOR then
			print('BUGBUG Poor Quality(139) target ctype wrong');
			return eff_list;
		end

		-- use power = -1
		-- eff_list = action_power_offset(target, -1);

		return eff_list;
	end,
	]]--

	-- src == defender
	trigger_attack = function(self, pside, src, power)
		local eff_list = {};
		local eff_list2;

		-- print('--- poor quality attack here self.id=', self.id);
		local index;
		index = card_index(self.side, self.table, self.pos);
		local target = index_card(index, pside);
		if target == nil then
			print('BUGBUG Poor Quality(139) trigger_attack target nil');
			return {};
		end
		if target.ctype ~= WEAPON then
			print('BUGBUG Poor Quality(139) target not weapon ' ,target.ctype);
			return {};
		end
		eff_list2 = action_durability_change(target, pside, -1);
		table_append(eff_list, eff_list2);

		return eff_list;
	end,

	-- src == attacker
	trigger_defend = function(self, pside, src, power)
		local eff_list = {};
		local eff_list2;


		-- print('--- poor quality defend here self.id=', self.id);
		local index;
		index = card_index(self.side, self.table, self.pos);
		local target = index_card(index, pside);
		if target == nil then
			print('BUGBUG Poor Quality(139) trigger_defned target nil');
			return {};
		end
		if target.ctype ~= ARMOR then
			print('BUGBUG Poor Quality(139) target not armor ' ,target.ctype);
			return {};
		end
		eff_list2 = action_durability_change(target, pside, -1);
		table_append(eff_list, eff_list2);

		return eff_list;
	end,
	
}
g_card_list[card.id] = card_class:new(card);

card = { 
	id = 140,
	ctype = ABILITY, 
	job = HUMAN,  -- neutral
	name = 'Honored Dead',
	star = 3,
	cost = 4,
	power = 0,  
	hp = 0,  
	skill_desc = 'If you have at least 3 allies in your graveyard, draw 2 cards and your hero gains +2 health',

	trigger_target_validate = function(self, pside, target, tid)
		local grave;
		local num = 0;
		grave = pside[self.side][T_GRAVE];
		for k, v in ipairs(grave or {}) do
			if (v.ctype == ALLY) then
				num = num + 1;
			end
		end
		if num >= 3 then
			return true;
		end
		return false;
	end,

	trigger_skill = function(self, pside, atl)  -- {
		local eff_list = {};
		local eff_list2 = {};

		for i=1, 2 do
			eff_list2 = action_drawcard(pside, self.side, -1);
			table_append(eff_list, eff_list2);
		end

		local my_hero = pside[self.side][T_HERO][1];

		local offset = my_hero:change_base_hp(2);
		eff_list[#eff_list + 1] = eff_hp(cindex(my_hero), offset);

		return eff_list; 
	end, -- trigger_skill }
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 141,
	ctype = SUPPORT,
	job = SHADOW,  
	camp = SHADOW, 
	name = 'Bloodlust', 
	star = 2,
	cost = 3,
	power = 0, 
	hp = 0,    
	skill_desc = 'Friendly allies have +2 attack until the end of your turn.',
	timer = 1,

	-- tid=0 case
	trigger_target_validate = function(self, pside, target, tid)
		local supp;
		supp = pside[self.side][T_SUPPORT];
		for k, v in ipairs(supp or {}) do
			if (v.id == self.id) then
				--print('DEBUG Bloodlust(141) duplicated');
				return false;
			end
		end
		return true;
	end,

	trigger_add = function(self, pside, target_table)
		local eff_list = {};
		local eff_list2;

		if target_table.name ~= T_SUPPORT then
			return {};  -- early exit
		end

		local my_side = pside[self.side];
		local list = my_side[T_ALLY];

		for _, target in ipairs(list) do
			eff_list2 = action_power_offset(target, 2);
			table_append(eff_list, eff_list2);
		end
			
		return eff_list;

	end,

	trigger_other_add = function(src, target, pside, target_table)

		local eff_list = {};
		local eff_list2 = {};
		local my_side = pside[src.side];

		if src.side ~= target.side or target_table ~= my_side[T_ALLY] then
			return eff_list;
		end

		eff_list2 = action_power_offset(target, 2);
		table_append(eff_list, eff_list2);

		return eff_list;

	end,

	trigger_remove = function(target, pside, target_table)
		local eff_list = {};
		local eff_list2 = {};

		-- if pside[target.side][T_SUPPORT] ~= target_table then
		if T_SUPPORT ~= target_table.name then
			return eff_list; -- early exit empty list
		end
		
		local list = {};
		list = pside[target_table.side][T_ALLY];

		for _, ally in ipairs(list) do
			eff_list2 = action_power_offset(ally, -2);
			table_append(eff_list, eff_list2);
		end

		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 142,
	ctype = ABILITY, 
	job = SHADOW, 
	camp = SHADOW, 
	name = 'Here Be Monsters',
	star = 2,
	cost = 3,
	power = 0,  
	hp = 0,
	skill_desc = 'Target player removes one of their resources from play if their resources are greater than or equal to yours.  Draw a card',
	trigger_skill = function(self, pside, atl) -- start {
		local eff_list = {};
		local eff;
		local eff_list2;
		local offset;
		local myside = pside[self.side];
		local opposide = pside[3-self.side];

		if myside.resource_max <= opposide.resource_max then
			opposide.resource_max = opposide.resource_max - 1;
			eff = eff_resource_max_offset(-1, opposide.id);
			eff_list[1] = eff;
			if opposide.resource > opposide.resource_max then
				opposide.resource = opposide.resource_max;
				eff = eff_resource_value(opposide.resource, opposide.id);
				eff_list[2] = eff;
			end
		end

		eff_list2 = action_drawcard(pside, self.side, -1);
		table_append(eff_list, eff_list2);
		return eff_list;
	end, -- trigger_skill }
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 143,
	ctype = ABILITY, 
	job = SHADOW,  -- shadow only
	camp = SHADOW,  -- shadow
	name = 'Shadowspawn',
	star = 3,
	dtype = D_MAGIC,
	atype = A_NORMAL,
	cost = 4,
	power = 0,  
	ab_power = 2,
	hp = 0,
	skill_desc = 'All heroes lose their stored shadow energy and takes 2 damage',
	trigger_skill = function(self, pside, atl) -- start {
		local eff_list = {};
		local eff;
		local eff_list2;
		local myside = pside[self.side];
		local opposide = pside[3-self.side];
		local myhero = myside[T_HERO][1];
		local oppohero = opposide[T_HERO][1];
		local myoffset = -myhero.energy;
		local oppooffset = -oppohero.energy;


		eff_list2 = action_energy(oppooffset, pside, opposide.id);
		table_append(eff_list, eff_list2);
		eff_list2 = action_energy(myoffset, pside, myside.id);
		table_append(eff_list, eff_list2);


		eff_list2 = action_damage(oppohero, pside, myhero, 
			self.ab_power, D_MAGIC, self.atype);
		table_append(eff_list, eff_list2);

		eff_list2 = action_damage(myhero, pside, myhero, 
			self.ab_power, D_MAGIC, self.atype);
		table_append(eff_list, eff_list2);

		return eff_list;
	end, -- trigger_skill }
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 144,
	ctype = ABILITY, 
	job = SHADOW, 
	camp = SHADOW, 
	name = 'Shriek of Vengeance',
	star = 2,
	cost = 1,
	power = 0,  
	hp = 0,
	target_list = {
		{side=2, table_list={T_SUPPORT} },
	},
	skill_desc = 'Target enemy item or support with cost 4 or less is destroyed, one of your resources is removed from play',

	trigger_target_validate = function(self, pside, target, tid)
		if tid == 0 then
			-- TODO loop all enemy support
			if 0 == #(pside[3-self.side][T_SUPPORT]) then
				return false;
			else
				return true;
			end
		end

		if tid > 1 then -- no more than 1 target
			return false;
		end

		if target.table ~= T_SUPPORT or target.cost > 4 then
			return false;
		end
		return true;	
	end,

	trigger_skill = function(self, pside, atl) -- start {
		local eff_list = {};
		local eff;
		local eff_list2;
		local myside = pside[self.side];

		local target = index_card(atl[1], pside);
		-- print_card(target);

		if #atl < 1 or nil == target then
			return error_return('BUGBUG shriek(144) #atl<1 or target=nil' .. 
				#(atl or {}) .. ', ' .. tostring(nil==target));
		end

		if target.table ~= T_SUPPORT then
			return error_return('BUGBUG shriek(144) target is not support:'
			.. target.ctype .. ' or is not in support table:' 
			.. (target.table or 'nil'));
		end

		-- use durability change to destroy it
		eff_list2 = action_durability_change(target, pside, -999);
		table_append(eff_list, eff_list2);

		-- remove my resource
		myside.resource_max = myside.resource_max - 1;
		eff = eff_resource_max_offset(-1, myside.id);
		eff_list[ #eff_list + 1] = eff;

		return eff_list;
	end, -- trigger_skill }
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 145,
	ctype = ARTIFACT, 
	job = SHADOW,  
	name = 'Evil Ascendant',
	star = 4,
	cost = 3,
	power = 1,  
	dtype = D_MAGIC,
	hp = 0,  
	skill_desc = 'At the start of each of your turns while Evil Ascendant is in play, all allies take 1 damage. RES:0 Target Good Ascendant is Destroyed.',
	skill_cost_resource = 0,

	target_list = {
		{side=3, table_list={T_SUPPORT}},
	},

	trigger_target_validate = function(self, pside, target, tid)
		if tid == 0 then
			return true;
		end

		if tid ~= 1 or target == nil then
			print('BUGBUG Evil Ascendant(145)  target_validate tid, target:', tid, target);
			return false; -- BUG case
		end

		if target.table ~= T_SUPPORT then
			return false;
		end

		if target.id ~= 136 then
			return false;
		end

		return true;

	end,

	trigger_skill = function(self, pside, atl)

		local index = atl[1];

		local target = index_card(index, pside);

		local eff_list = action_durability_change(target, pside, -999);

		return eff_list;

	end,

	-- trigger that works when turn start
	trigger_turn_start = function(self, pside, sss)
		-- self, pside
		local eff_list = {};
		local eff_list2;

		if sss ~= self.side then
			return {}
		end

		local target_list = {};
		local my_hero = pside[sss][T_HERO][1];
		local my_ally = pside[sss][T_ALLY];
		local oppo_ally = pside[3-sss][T_ALLY];


		for k, v in ipairs(oppo_ally) do
			target_list[#target_list+1] = v;
		end

		for k, v in ipairs(my_ally) do
			target_list[#target_list+1] = v;
		end

		eff_list2 = action_damage_list(target_list, pside, my_hero
		, 1, D_MAGIC);  -- TODO animation type ?
		table_append(eff_list, eff_list2);

		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 146,
	ctype = ARTIFACT, 
	job = SHADOW,  
	camp = SHADOW, 
	name = 'Bloodstone Altar',
	star = 1,
	cost = 2,
	hp = 0,  
	power = 0,

	skill_desc = 'Friendly allies that are damaged have +1 attack while Bloodstone Altar is in play.', 


	-- ref to 32
	trigger_add = function (self, pside, target_table)
		local offset = 0;
		local eff_list = {};
		local eff_list2;
		local err;
		local list = pside[self.side][T_ALLY];

		-- need to check target table
		if target_table.name ~= T_SUPPORT then 
			return eff_list;
		end

		for k, target in ipairs(list) do
			eff_list2, err = action_virtual_attach(target, pside, 1146, self);
			if nil == eff_list2 then
				print('BUGBUG (1146)bloodstone cannot attach: ', err);
				return eff_list, err;
			end
			table_append(eff_list, eff_list2);
		end
		return eff_list;
	end,

	-- call this when bloodstone is already in SUPPORT and other cards are added
	trigger_other_add = function(src, target, pside, target_table)
	-- (side_src, side_target, src, target, target_table)
		-- src is self, this Aldon then Brave, so we need to check whether
		-- src and target are the same side
		local eff_list = {};
		local eff_list2;
		local err;
		local offset = 0;

		if target_table.name ~= T_ALLY then 
			return eff_list;
		end
		-- bloodstone is not in the same side of the add_other target
		if src.side ~= target.side then
			return eff_list; -- early exit
		end

		if target == src then
			return eff_list;
		end

		eff_list2, err = action_virtual_attach(target, pside, 1146, src);
		if nil == eff_list2 then
			print('BUGBUG (1146)bloodstone cannot attach: ', err);
			return eff_list, err;
		end
		table_append(eff_list, eff_list2);

		return eff_list;
	end,

	-- fix: (200)dagger of umaking : retreat Aldon but power not --
	trigger_remove = function(self, pside, src_table)
		-- self is the one who die, src is the killer
		local offset = 0;
		local eff_list = {};
		local eff_list2;
		-- masha: use old_side
		local list = {};
		list = pside[src_table.side][T_ALLY];

		if src_table.name ~= T_SUPPORT then
			return {}; -- bloodstone may be remove from deck,hand,grave etc
		end

		eff_list2 = action_virtual_remove(list, pside, self);
		table_append(eff_list, eff_list2);

		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 147,
	ctype = ATTACH,
	job = SHADOW,
	camp = SHADOW, 
	name = 'Selfishness',
	star = 1,
	cost = 2,
	power = 0,  
	hp = 0,  
	skill_desc = 'Target opposing ally loses protector and defender, and cannot be targeted by its controller while Selfishness is attached',
	target_list = { -- attach only has one target!
		{side=2, table_list={T_ALLY} }    
	}, 
	protector = false,
	defender = false,
	no_help = true,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 148,
	ctype = ABILITY, 
	job = SHADOW,  -- shadow only
	camp = SHADOW,  -- shadow
	name = 'Acid Jet',
	star = 4,
	cost = 4,
	hp = 0,  -- test
	skill_desc = 'Target enemy weapon or armor loses 3 durability.', 
	target_list = {
		{ side = 2, table_list = { T_WEAPON, T_ARMOR } },
	},

	trigger_skill = function (self, pside, actual_target_list)
		local offset = 0;
		local index;
		local target;
		local eff_list = {};
		local eff_list2;
		if actual_target_list==nil or #actual_target_list<1 then
			print('BUGBUG Acid Jet(148) atl=nil or #<1');
			return eff_list;
		end
		index = actual_target_list[1];
		target = index_card(index, pside);
		
		if target.ctype~=WEAPON and target.ctype~=ARMOR then
			print('BUGBUG Acid Jet(148) target is not weapon or armor');
			return eff_list;
		end

		eff_list2 = action_durability_change(target, pside, -3);
		table_append(eff_list, eff_list2);

		return eff_list;
	end,

	
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 149,
	ctype = ABILITY, 
	job = SHADOW,
	camp = SHADOW,
	name = 'Shadow Font',
	star = 4,
	cost = 4,
	hp = 0,  
	skill_desc = 'Your hero gains +3 shadow energy.', 

	trigger_skill = function (self, pside, atl)
		local eff_list = {};
		local offset = 3;
		eff_list = action_energy(offset, pside, self.side);
		return eff_list;
	end,
	
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 150,
	ctype = ABILITY, 
	job = SHADOW,  
	camp = SHADOW, 
	name = 'Sacrificial Lamb',
	star = 4,
	cost = 2,
	hp = 0,  
	skill_desc = 'Target frendly ally that can be damaged is killed, Draw card equal to that ally cost, to a maximum of 3.', 
	target_list =  {
		{side=1, table_list={T_ALLY}},
	},

	trigger_skill = function (self, pside, atl)

		local eff = nil;
		local eff_list = {};
		local eff_list2;

		if #atl <= 0 then
			return error_return('BUGBUG Sacrificial(150) #atl<=0');
		end
		local target;
		target = index_card(atl[1], pside);

		if nil == target then
			return error_return('BUGBUG Sacrificial(150) nil target');
		end

		local num;
		num = target.cost;
		if num > 3 then
			num = 3;
		end


		-- XXX bug, this should use action_damage(), because, some unit 
		-- may trigger other action, e.g. Deathbone and Brutal Minotaur
		--[[
		eff_list2 = action_grave(target, pside);
		table_append(eff_list, eff_list2);
		]]--
		local my_hero = pside[self.side][T_HERO][1];
		eff_list2 = action_damage(target, pside, my_hero, 99, D_DIRECT);
		table_append(eff_list, eff_list2);

		
		for i=1, num do
			eff_list2 = action_drawcard(pside, self.side, -1);
			table_append(eff_list, eff_list2);
		end

		return eff_list;
	end,
	
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 151,
	ctype = SUPPORT, 
	job = 0,  
	name = 'Rain Delay',
	star = 1,
	cost = 2,
	power = 0,  
	hp = 0,  
	skill_desc = 'Allies cannot attack until the end of your next turn', 
	timer = 3,

	no_attack = true,
	no_attack_target = { side = 3, table_list = { T_ALLY } },
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 152,
	ctype = ATTACH, 
	job = 0,  
	name = 'Drain Power',
	star = 2,
	cost = 3,
	-- power = -1,  
	-- ab_power = -1,
	hp = 0,  -- test
	skill_desc = 'Target opposing hero loses 1 shadow energy and cannot activate its hero ability until the start of your next turn', 

	-- TODO when this card goto grave, need to reset timer
	timer = 2, -- until start of "user of this card" turn
	target_list = {
		-- DEBUG add ally, should be only hero
		{side=2, table_list={T_HERO} },
	},

	-- @see action_attach : when trigger_skill is non-nil, will be called
	trigger_skill = function (self, pside, atl)

		-- hard coded hero table always has 1 hero
		local target = index_card(atl[1], pside);
		local eff = nil;
		local eff_list = {};
		if target == nil then 
			print('BUGBUG (152) drain power target hero = nil');
			return eff_list;
		end

		print('DEBUG (152) drain power on :', target.name);
		if target.energy <= 0 then
			return eff_list;
		end
		-- implicit:  energy >= 1  (must be able to -1)
		
		eff_list = action_energy(-1, pside, target.side);
		return eff_list;
	end,
	
	-- seems no use (this is attach), only support card need no_ability_target
	no_ability_target = { side = 2, table_list = { T_HERO } }, 
	no_ability = true,
}
g_card_list[card.id] = card_class:new(card);

card = { 
	id = 153,
	ctype = SUPPORT, 
	job = 0, 
	name = 'Urgent Business',
	star = 1,
	cost = 2,
	power = 0,  
	hp = 0,  
	skill_desc = 'Heroes cannot attack until the end of your next turn', 
	timer = 2,
	
	no_attack_target = { side = 3, table_list = { T_HERO } },
	no_attack = true,
	-- no_ability_target = { side = 3, table_list = { T_HERO } },
	-- no_ability = true,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 154,
	ctype = ATTACH,
	job = 0,  
	name = 'Extra Sharp',
	star = 1,
	cost = 2,
	power = 2,  
	hp = 0,  
	skill_desc = 'Target friendly ally has +2 attack until the start of your next turn',
	target_list = { -- attach only has one target!
		{side=1, table_list={T_ALLY,}  },    
	}, 

	timer = 2;

	ai_weight = function(self, atl, pside, sss, ability) -- start {
		local target;
		target = atl[1];   -- assume one target, atl[x] in ai_weight is obj
		if target==nil then
			print('BUGBUG extrasharp(154) ai_weight target=nil');
			return -7;
		end

		-- TODO disable, power=0 etc
		if target.no_attack then
			return 0;  -- no point to give it extra sharp, it cannot attack
		end
		
		return ai_weight_general(self, atl, pside, sss, ability);
	end, -- ai_weight }
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 155,
	ctype = ARTIFACT,
	job = 0, 
	name = 'Bazaar',
	star = 1,
	cost = 2,
	skill_desc = 'Each player draws an extra card at the start of their turn while Bazaar is in play',

	-- trigger that works when turn start
	trigger_turn_start = function(self, pside, sss)
		local eff_list;
		-- self, pside
		eff_list = action_drawcard(pside, sss);
		return eff_list;
	end,

	ai_weight = function(self, atl, pside, sss, ability) 
		if true == check_in_side_list(pside[sss], DRAW_LIST) then
			return 0; -- already there, no need
		else
			return 1000; -- very good weight
		end
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 156,
	ctype = ABILITY, 
	job = 0, 
	name = 'Sever Ties',
	star = 3,
	cost = 4,
	skill_desc = 'Target attached card is destroyed.',
	target_list = {
		{side=3, table_list={T_ATTACH}  },    
	},

	trigger_target_validate = function(self, pside, target, tid) -- {
		-- first test on tid=0
		if tid == 0 then 
			return true;
		end

		if tid ~= 1 or target == nil then
			print('BUGBUG sever ties(156)  target_validate tid, target:'
			, tid, target);
			return false; -- BUG case
		end

		-- T_ATTACH, target is an attachment
		if target.ctype ~= ATTACH then
			return false;
		end

		-- avoid virtual card
		if target.id >= 1000 then
			return false;
		end

		-- do we need to check index ?  5 digits
		-- e.g. 13012

		local index;
		index = cindex(target);
		-- print('DEBUG (156) sever ties validate index=', index);
		if index < 9999 then
			print('ERROR (156) not an attach index: ', index);
			return false;
		end
		return true;

	end, -- trigger_target_validate }

	trigger_skill = function(self, pside, actual_target_list) -- {
		local cc;
		local eff_list = {};
		local eff_list2 = {};
		
		if actual_target_list==nil or #actual_target_list<1 then
			print('BUGBUG (156) sever ties atl=nil or #atl<1');
			return {}; 
		end

		local index = actual_target_list[1];
		cc = index_card(index, pside);
		if cc == nil then
			print('BUGBUG (156) sever ties cc=nil');
			return {}
		end

		eff_list2 = action_grave(cc, pside);
		table_append(eff_list, eff_list2);

		return eff_list;
	end, -- } trigger_skill

	-- target (atl[1]) is the attach card, 
	-- source = target.src ;  -- if nil return 0
	-- source.side == self.side : return 0;
	-- source.side ~= self.side : ai_weight_general
	ai_weight = function (self, atl, pside, sss, ability) -- {
		local target;
		local source;  -- source of target
		local weight;
		local count;
		if #atl<=0 or atl[1]==nil then
			print('BUGBUG (156) ai_weight #atl=0 or atl[1]=nil', #atl);
			return 0;
		end
		target = atl[1];
		source = target.src;
		if source == nil then
			print('BUGBUG (156) ai_weight target.src=nil',  target.id);
			return 0;
		end
		-- test: (92)inner strength and (67)crippling blow
		-- our people do this attachment, do not destroy it!
		if source.side == self.side then
			return 0;
		end
		
		-- TODO consider parent cost, power etc.
		weight = ai_weight_general(self, atl, pside, sss, ability);
		count = target.timer or 10; -- no timer is long term, let it be 10
		weight = weight + 50 * count;
		return weight;
	end, -- }
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 157,
	ctype = ABILITY, 
	job = 0, 
	name = 'Bad Santa',
	star = 1,
	cost = 2,
	skill_desc = 'Each player draws 3 cards',

	trigger_skill = function(self, pside, atl)  -- {
		-- print('DEBUG Bad Santa(157) each player draws 3 card!');
		local eff_list = {};
		local eff_list2 = {};
		--[[
		local both_side = {self.side, 3-self.side};
		--for _, oneside in ipairs(pside) do
		for i=1, #both_side do
			local s = both_side[i];
			for j=1, 3 do
				eff_list2 = action_drawcard(pside, s, -1);
				table_append(eff_list, eff_list2);
			end
		end
		]]--
		for j=1, 3 do
			eff_list2 = action_drawcard(pside, self.side, -1);
			table_append(eff_list, eff_list2);
		end
		for j=1, 3 do
			eff_list2 = action_drawcard(pside, (3-self.side));
			table_append(eff_list, eff_list2);
		end
		return eff_list; 
	end, -- trigger_skill }


	-- TODO ai_weight : high rate if #my_hand <=3 and #oppo_hand >= 5
	ai_weight = function(self, atl, pside, sss, ability) 
		local my_hand = #pside[sss][T_HAND];
		local oppo_hand = #pside[3-sss][T_HAND];

		if my_hand-1 > oppo_hand or my_hand >= 6 then
			return 0;
		end

		-- very good case
		if my_hand <= 4 and oppo_hand >= 4 then
			return 2000; -- very good weight
		end
		return 500; -- medium weight
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {  
	id = 158,
	ctype = ABILITY, 
	job = 0,  -- neutral
	name = 'Master Smith',
	star = 3,
	cost = 4,
	skill_desc = 'Target weapon or armor in your graveyard is retruned to your hand.',
	target_list = {
		{side=1, table_list={T_GRAVE}  },    -- 2 means oppo side, 1 = myside
	},

	trigger_target_validate = function(self, pside, target, tid)
		if tid == 0 then
			return true;
		end
		if tid ~= 1 or target == nil then
			print('BUG: Master Smith(158) target_validate tid, target', tid, target);
			return false;
		end

		if target.ctype == ARMOR or target.ctype == WEAPON then
			return true;
		end
		return false;
	end,

	trigger_skill = function(self, pside, actual_target_list)
		local at; -- at is the number (2101 = side 2 hand 1)
		local cc;
		local eff_list = {};
		local eff_list2 = {};
		local side_target;
		local side_home;  -- retreat back to home T_HAND
		at = actual_target_list[1]; -- hard code the first target
		if nil == at then
			print('ERROR (158) Master Smith a nil at index');
			return eff_list;
		end

		cc = index_card(at, pside);
		if nil == cc then
			print('ERROR (158) Master Smith a nil card at=' .. at);
			return eff_list;
		end
	
		side_target = pside[cc.side];
		side_home = pside[cc.home];

		--cc:refresh();
		eff_list2 = action_refresh(cc, pside);
		table_append(eff_list, eff_list2);

		-- assert(cc.attach_list==nil or #cc.attach_list==0);
		-- cc.attach_list = nil; -- for garbage collection 

		-- assume cc must be in grave
		eff_list2 = action_move(cc, pside, side_target[cc.table], 
			side_home[T_HAND]);
		table_append(eff_list, eff_list2);

		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 159,
	ctype = ABILITY,
	job = 0,
	name = 'Melt Down',
	star = 3,
	cost = 2,
	skill_desc = 'Target item you control is destroyed, draw 2 cards',
	target_list =  {
		{side=1, table_list={T_SUPPORT}},
	},

	trigger_target_validate = function (self, pside, target, tid)
		if tid == 0 then
			return true;
		end

		if tid ~= 1 or target == nil then
			print('BUGBUG Melt Down(159) target_validate tid, target', tid, target);
			return false;
		end

		if check_is_item(target) then
			return true;
		end

		return false;
	end,

	trigger_skill = function (self, pside, actual_target_list)
		local index;
		local target;
		local eff_list = {};
		local eff_list2;
		if actual_target_list==nil or #actual_target_list<1 then
			print('BUGBUG Melt Down(159) atl=nil or #<1');
			return {};
		end
		index = actual_target_list[1];
		target = index_card(index, pside);
		
		if check_is_item(target) == false then
			print('BUGBUG Melt Down(159) target is not support item');
			return {};
		end

		eff_list2 = action_durability_change(target, pside, -999);
		table_append(eff_list, eff_list2);


		for i=1, 2 do
			eff_list2 = action_drawcard(pside, self.side, -1);
			table_append(eff_list, eff_list2);
		end

		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 160,
	ctype = ABILITY, 
	job = 0,
	name = 'Ley Line Nexus',
	star = 4,
	cost = 5,
	skill_desc = 'Target enemy item with cost 5 or greater is destroyed. Draw a card.',
	target_list = {
		{side=2, table_list={T_SUPPORT}},
	},

	trigger_target_validate = function(self, pside, target, tid)
		if tid == 0 then
			return true;
		end

		if tid ~= 1 or target == nil then
			print('BUGBUG Ley Line(160)  target_validate tid, target:', tid, target);
			return false; -- BUG case
		end

		-- XXX shall we include the hunter trap ?
		-- if check_is_item(target) == false then
		if target.ctype ~= WEAPON 
		and target.ctype ~= ARMOR 
		and target.ctype ~= ARTIFACT then
			return false;
		end

		if target.cost < 5 then
			return false;
		end

		return true;

	end,
	trigger_skill = function(self, pside, atl) -- start {
		local eff_list = {};
		local eff;
		local eff_list2;
		local myside = pside[self.side];

		local target = index_card(atl[1], pside);
		-- print_card(target);

		if #atl < 1 or nil == target then
			return error_return('BUGBUG Ley Line(160) #atl<1 or target=nil' .. 
				#(atl or {}) .. ', ' .. tostring(nil==target));
		end

		-- use durability change instead of move to grave directly
		eff_list2 = action_durability_change(target, pside, -999);
		table_append(eff_list, eff_list2);

		-- draw a card
		eff_list2 = action_drawcard(pside, self.side);
		table_append(eff_list, eff_list2);

		return eff_list;
	end, -- trigger_skill }
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 161,
	ctype = ARMOR, 
	job = WARRIOR + ELEMENTAL,   
	name = 'Nova Infusion',
	star = 4,
	cost = 6,
	power = 3,  -- defend point
	hp = 5,  -- in weapon, it is durability NaiJiu
	skill_desc = 'At the start of each of your turns while Nova Infusion is in play, your hero has all enemy attachments and negative effects removed, and takes 1 fire damage.',

	trigger_turn_start = function(self, pside, sss)
		local eff_list = {};
		local eff_list2;

		if sss ~= self.side then
			return eff_list;
		end

		local hh;
		hh = pside[self.side][T_HERO][1];

		local aclist = hh.attach_list or {};
		for i = #aclist, 1, -1 do
			local ac = aclist[i];
			if nil ~= ac.src and ac.src.side ~= self.side then
				eff_list2 = action_grave(ac, pside);
				table_append(eff_list, eff_list2);
			end
		end


		eff_list2 = action_damage(hh, pside, hh, 1, D_FIRE);
		table_append(eff_list, eff_list2);

		return eff_list;

	end,

	trigger_defend = function(self, pside, src, power)
		local eff_list = {};
		local eff_list2;

		-- reduce durability
		eff_list2 = action_durability_change(self, pside, -1);
		table_append(eff_list, eff_list2);
		
		return eff_list;
	end,

	trigger_calculate_defend = function(self, pside, src, power, dtype)
		if dtype >= D_PENETRATION then 
			return power;
		end
		
		-- reduce power 
		power = power - self.power;
		
		if power < 0 then
			power = 0;
		end
		
		return power;
		
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 162,
	ctype = ARMOR, 
	job = WARRIOR + MAGE,   -- TODO add element
	name = 'Snow Sapphire',
	star = 4,
	cost = 6,
	power = 2,  -- defend point
	hp = 4,  -- in weapon, it is durability NaiJiu
	skill_desc = 'When a hero or ally attacks your hero, that hero or ally is frozen until the end of its controllers turn',  -- timer = 2 (which is not correct in case of defender attack)

	trigger_defend = function(self, pside, src, power)
		local eff_list = {};
		local eff_list2;
		local err;


		eff_list2, err = action_virtual_attach(src, pside, 1162, pside[self.side][T_HERO][1]);
		if nil == eff_list2 then
			print('BUGBUG Snow(1162) cannot attach: ', err);
			return eff_list, err;
		end
		table_append(eff_list, eff_list2);

		-- reduce durability
		eff_list2 = action_durability_change(self, pside, -1);
		table_append(eff_list, eff_list2);
		
		return eff_list;
	end,

	trigger_calculate_defend = function(self, pside, src, power, dtype)
		if dtype >= D_PENETRATION then 
			return power;
		end
		
		-- reduce power 
		power = power - self.power;
		
		if power < 0 then
			power = 0;
		end
		
		return power;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 163,
	ctype = ARMOR, 
	job = WARRIOR + MAGE,   -- TODO add element
	name = 'Violet Thunderstorm',
	star = 4,
	cost = 6,
	power = 2,  -- defend point
	hp = 4,  -- in weapon, it is durability NaiJiu
	skill_desc = 'When a hero or ally attacks your hero, that hero or ally takes 1 electrical damage.',  -- timer = 2 (which is not correct in case of defender attack)

	trigger_defend = function(self, pside, src, power)
		local eff_list = {};
		local eff_list2;
		local err;

		local hh = pside[self.side][T_HERO][1];
		eff_list2 = action_damage(src, pside, hh, 1, D_ELECTRICAL);
		table_append(eff_list, eff_list2);

		-- reduce durability
		eff_list2 = action_durability_change(self, pside, -1);
		table_append(eff_list, eff_list2);
		
		return eff_list;
	end,

	trigger_calculate_defend = function(self, pside, src, power, dtype)
		if dtype >= D_PENETRATION then 
			return power;
		end
		
		-- reduce power 
		power = power - self.power;
		
		if power < 0 then
			power = 0;
		end
		
		return power;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 164,
	ctype = ARMOR, 
	job = WARRIOR + ELEMENTAL,   
	name = 'Armor of Ages',
	star = 5,
	cost = 6,
	power = 6,  -- defend point
	hp = 6,  -- in weapon, it is durability NaiJiu
	skill_desc = 'Your hero cannot attack or defend while Armor of Ages is in play.',
	no_attack = true,
	no_attack_target = { side = 1, table_list = { T_HERO } },
	no_defend = true,

	trigger_defend = function(self, pside, src, power)
		local eff_list = {};
		local eff_list2;
		local err;

		-- reduce durability
		eff_list2 = action_durability_change(self, pside, -1);
		table_append(eff_list, eff_list2);
		
		return eff_list;
	end,

	trigger_calculate_defend = function(self, pside, src, power, dtype)
		if dtype >= D_PENETRATION then 
			return power;
		end
		
		-- reduce power 
		power = power - self.power;
		
		if power < 0 then
			power = 0;
		end
		
		return power;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = { 
	id = 165,
	ctype = ARMOR, 
	job = WARRIOR + PRIEST,   
	name = 'The Kings Pride',
	star = 5,
	cost = 7,
	power = 2,  
	hp = 5,  
	skill_desc = 'Friendly allies have +2 attack and +1 health while The Kings Pride in play',

	-- TODO no need, duplicate check should be done on upper level
	-- tid=0 case
	--[[
	-- we can change a new armor
	trigger_target_validate = function(self, pside, target, tid)
		local supp;
		supp = pside[self.side][T_SUPPORT];
		for k, v in ipairs(supp or {}) do
			if (v.id == self.id) then
				print('DEBUG (165)The Kings Pride duplicated');
				return false;
			end
		end
		return true;
	end,
	]]--

	trigger_defend = function(self, pside, src, power)
		local eff_list = {};
		local eff_list2;

		-- reduce durability
		eff_list2 = action_durability_change(self, pside, -1);
		table_append(eff_list, eff_list2);
		
		return eff_list;
	end,

	trigger_calculate_defend = function(self, pside, src, power, dtype)
		if dtype >= D_PENETRATION then 
			return power;
		end
		
		-- reduce power 
		power = power - self.power;
		
		if power < 0 then
			power = 0;
		end
		
		return power;
		
	end,

	trigger_add = function (self, pside, target_table)
		local eff_list = {};
		local side_target = pside[self.side];
		-- side2 is the target side, for each card in side2[T_ALLY] power+1
		-- only effective in support table
		if side_target[T_SUPPORT] ~= target_table then
			if target_table.name==T_SUPPORT then
				print('ERROR (165)The King Pride bug1: ', side_target[T_SUPPORT], target_table);
			end
			return;
		end
		local index_list = {};
		for k, v in ipairs(side_target[T_ALLY]) do
			index_list[#index_list + 1] = v:index();
		end
		-- only for client anim
		eff_list[#eff_list + 1] = eff_anim(self:index(), self.id
		, A_HP_UP, index_list);
		eff_list[#eff_list + 1] = eff_anim(self:index(), self.id
		, A_POWER_UP, index_list);
		-- add power
		for k, v in ipairs(side_target[T_ALLY]) do
			local offset = 0;
			local eff_list2 = action_power_offset(v, 2);
			table_append(eff_list, eff_list2);

			--[[
			offset = v:change_base_hp(1);
			eff_list[#eff_list + 1] = eff_hp(cindex(v), offset);
			]]--
		end
		-- add hp
		for k, v in ipairs(side_target[T_ALLY]) do
			local offset = 0;
			offset = v:change_base_hp(1);
			eff_list[#eff_list + 1] = eff_hp(cindex(v), offset);
		end
		return eff_list;
	end,

	trigger_remove = function (self, pside, target_table)
		local eff_list = {};
		local eff_list2;
		local eff;
		local power;
		local src;
		local target_list = {};
		local side_target = {};
		side_target = pside[target_table.side];
		if side_target[T_SUPPORT] ~= target_table then
			return eff_list; -- early exit empty list
		end

		src = pside[self.side][T_HERO][1];
		-- for k, v in ipairs(side_target[T_ALLY]) do
		for i=#side_target[T_ALLY], 1, -1 do
			target_list[#target_list + 1] = side_target[T_ALLY][i];
		end

		target_list = sort_damage_list(target_list);

		local index_list = {};
		for k, v in ipairs(target_list) do
			index_list[#index_list + 1] = v:index();
		end
		-- only for client anim
		eff_list[#eff_list + 1] = eff_anim(self:index(), self.id
		, A_HP_DOWN, index_list);
		eff_list[#eff_list + 1] = eff_anim(self:index(), self.id
		, A_POWER_DOWN, index_list);

		-- reduce power
		for k, v in ipairs(target_list)  do

			eff_list2 = action_power_offset(v, -2);
			table_append(eff_list, eff_list2);

			--[[
			-- shoule use action_damage
			-- if Holy Shield(97) attach on ally, what hanppen...
			-- 1.action_damage(-1), D_DIRECT for Sandworm ability...
			-- 2.if power == 0 (may attach Holy Shield), nothing hanppen
			--   if power > 0 and v:die()==false, then change_max_hp(-1)

			eff_list2, power = action_damage(v, pside
				, src, 1, D_DIRECT);  -- peter: nil animation is ok A_NORMAL
			table_append(eff_list, eff_list2);
			-- print('trigger_remove:king pride power=', power);
			if power > 0 and v:die()==false then
				v:change_max_hp(-1);
			end
			]]--

		end

		-- reduce hp
		for k, v in ipairs(target_list)  do

			--[[
			eff_list2 = action_power_offset(v, -2);
			table_append(eff_list, eff_list2);
			]]--

			-- shoule use action_damage
			-- if Holy Shield(97) attach on ally, what hanppen...
			-- 1.action_damage(-1), D_DIRECT for Sandworm ability...
			-- 2.if power == 0 (may attach Holy Shield), nothing hanppen
			--   if power > 0 and v:die()==false, then change_max_hp(-1)

			eff_list2, power = action_damage(v, pside
				, src, 1, D_DIRECT);  -- peter: nil animation is ok A_NORMAL
			table_append(eff_list, eff_list2);
			-- print('trigger_remove:king pride power=', power);
			if power > 0 and v:die()==false then
				v:change_max_hp(-1);
			end

		end
		return eff_list;
	end,

	-- call this when Kings Pride is already in support and other cards are added
	trigger_other_add = function(src, target, pside, target_table)
	-- (side_src, side_target, src, target, target_table)
		-- src is self, so we need to check whether
		-- src and target are the same side
		local eff_list = {};

		if target_table.name ~= T_ALLY then 
			return eff_list;
		end
		-- is not in the same side of the add_other target
		if src.side ~= target.side then
			return eff_list; -- early exit
		end

		local offset = 0;

		local eff_list2 = action_power_offset(target, 2);
		table_append(eff_list, eff_list2);

		-- offset = target:change_hp(1);
		-- TODO Shard of Power(128) limit hp by 1
		offset = target:change_base_hp(1);
		eff_list[#eff_list + 1] = eff_hp(cindex(target), offset);

		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 166,
	ctype = ARMOR, 
	job = HUNTER + ROGUE,   -- TODO add element
	name = 'Night Prowler',
	star = 3,
	cost = 5,
	power = 1,  -- defend point
	hp = 1,  -- in weapon, it is durability NaiJiu
	skill_desc = 'When your hero deals combat damage to an opposing hero while Night Prowler is in play, take 1 card from that hero owner hand at random.',

	trigger_defend = function(self, pside, src, power)
		local eff_list = {};
		local eff_list2;

		if src.ctype == HERO then
			local oppo_hand = pside[src.side][T_HAND];
			local my_hand = pside[self.side][T_HAND];
			if #oppo_hand > 0 then 
				local index;
				-- TODO NEED TEST
				index = math.random(1, #oppo_hand);
				print('index = ' .. index);
				local cc = oppo_hand[index];
				-- local cc = oppo_hand[1];
				eff_list2 = action_move(cc, pside, oppo_hand, my_hand);
				table_append(eff_list, eff_list2);
			end
		end
		
		-- reduce durability
		eff_list2 = action_durability_change(self, pside, -1);
		table_append(eff_list, eff_list2);
		
		return eff_list;
	end,
	
	trigger_calculate_defend = function(self, pside, src, power, dtype)
		if dtype >= D_PENETRATION then 
			return power;
		end
		
		-- reduce power 
		power = power - self.power;
		
		if power < 0 then
			power = 0;
		end
		
		return power;
		
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 167,
	ctype = ARMOR, 
	job = HUNTER + WULVEN,   
	name = 'Wrath of the Forest',
	star = 5,
	cost = 4,
	power = 1,  -- defend point
	hp = 4,  -- in weapon, it is durability NaiJiu
	skill_desc = 'When a friendly ally is killed while Wrath of the Forest is in play, draw a card.',

	trigger_defend = function(self, pside, src, power)
		local eff_list = {};
		local eff_list2;
		local err;

		-- reduce durability
		eff_list2 = action_durability_change(self, pside, -1);
		table_append(eff_list, eff_list2);
		
		return eff_list;
	end,

	trigger_calculate_defend = function(self, pside, src, power, dtype)
		if dtype >= D_PENETRATION then 
			return power;
		end
		
		-- reduce power 
		power = power - self.power;
		
		if power < 0 then
			power = 0;
		end
		
		return power;
	end,

	trigger_other_kill = function(self, target, src, pside)
		local eff_list = {};
		local eff_list2;
		local offset = 0;

		if target.side ~= self.side then
			return eff_list;
		end

		eff_list2 = action_drawcard(pside, self.side);
		table_append(eff_list, eff_list2);

		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 168,
	ctype = ARMOR, 
	job = HUNTER + ROGUE,   
	name = 'Crimson Vest',
	star = 5,
	cost = 6,
	power = 2,  -- defend point
	hp = 4,  -- in weapon, it is durability NaiJiu
	skill_desc = 'When an opposing ally is killed while Crimson Vest is in play, your hero heals 2 damage.',

	trigger_defend = function(self, pside, src, power)
		local eff_list = {};
		local eff_list2;
		local err;

		-- reduce durability
		eff_list2 = action_durability_change(self, pside, -1);
		table_append(eff_list, eff_list2);
		
		return eff_list;
	end,

	trigger_calculate_defend = function(self, pside, src, power, dtype)
		if dtype >= D_PENETRATION then 
			return power;
		end
		
		-- reduce power 
		power = power - self.power;
		
		if power < 0 then
			power = 0;
		end
		
		return power;
	end,

	trigger_other_kill = function(self, target, src, pside)
		local eff_list = {};
		local eff_list2;
		local offset = 0;

		if target.side ~= self.side then
			return eff_list;
		end

		eff_list2 = action_drawcard(pside, self.side);
		table_append(eff_list, eff_list2);

		return eff_list;
	end,

	trigger_other_kill = function(self, target, src, pside, old_side)
		local eff_list = {};

		-- use old_side instead of target.side (side before enter grave)
		if old_side == self.side then
			return eff_list;
		end

		local hh = pside[self.side][T_HERO][1];
		
		eff_list = action_heal(hh, pside, self, 2);

		return eff_list;
	
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 169,
	ctype = ARMOR, 
	job = HUNTER + ROGUE,   -- TODO add element
	name = 'Gravediggers Cloak',
	star = 3,
	cost = 4,
	power = 1,  -- defend point
	hp = 3,  -- in weapon, it is durability NaiJiu
	skill_desc = 'ENERGY:2  Target opposing player has a random card from their graveyard placed into your hand.',
	skill_cost_energy = 2,

	trigger_defend = function(self, pside, src, power)
		local eff_list = {};
		local eff_list2;
		
		-- reduce durability
		eff_list2 = action_durability_change(self, pside, -1);
		table_append(eff_list, eff_list2);
		
		return eff_list;
	end,
	
	trigger_calculate_defend = function(self, pside, src, power, dtype)
		if dtype >= D_PENETRATION then 
			return power;
		end
		
		-- reduce power 
		power = power - self.power;
		
		if power < 0 then
			power = 0;
		end
		
		return power;
		
	end,

	trigger_skill = function(self, pside, atl) 
		local eff_list = {};
		local eff_list2;
		local my_hand = pside[self.side][T_HAND];
		local oppo_grave = pside[3-self.side][T_GRAVE];
		print('#my_hand =' .. #my_hand);
		print('#oppo_grave =' .. #oppo_grave);
		-- print('#oppo_grave =' .. #oppo_grave);
		if #oppo_grave > 0 then
			local index;
			-- TODO NEED TEST
			index = math.random(1, #oppo_grave);
			print('index = ' .. index);
			local cc = oppo_grave[index];
			-- local cc = oppo_hand[1];
			eff_list2 = action_move(cc, pside, oppo_grave, my_hand);
			table_append(eff_list, eff_list2);
		end
		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 170,
	ctype = ARMOR, 
	job = HUNTER + PRIEST,   
	name = 'Cobraskin Wraps',
	star = 3,
	cost = 4,
	power = 1,  
	hp = 3,  -- in weapon, it is durability NaiJiu
	skill_desc = 'When Cobraskin Wraps is summoned, all players remove 1 resource from play and you draw a card.',

	trigger_add = function (self, pside, target_table)
	-- side1, side2, src, target, target_table) 
		if target_table.name ~= T_SUPPORT then
			return {};  -- early exit
		end

		local eff_list = {};
		local my_side = pside[self.side];
		local oppo_side = pside[3-self.side];
		local offset = -1;

		if oppo_side.resource_max >= 1 then
			local eff_list2 = {};
			oppo_side.resource_max = oppo_side.resource_max + offset;
			eff_list2[1] = eff_resource_max_offset(offset, oppo_side.id);
			if oppo_side.resource > oppo_side.resource_max then
				oppo_side.resource = oppo_side.resource_max;
				eff_list2[2] = eff_resource_value(oppo_side.resource, oppo_side.id);
			end
			table_append(eff_list, eff_list2);
		end

		local eff_list2 = {}

		-- assume my_side.resource_max > 1
		my_side.resource_max = my_side.resource_max + offset;
		eff_list2[1] = eff_resource_max_offset(offset, my_side.id);
		table_append(eff_list, eff_list2);

		eff_list2 = action_drawcard(pside, self.side);
		table_append(eff_list, eff_list2);


		return eff_list;
	end,

	trigger_defend = function(self, pside, src, power)
		local eff_list = {};
		local eff_list2;

		-- reduce durability
		eff_list2 = action_durability_change(self, pside, -1);
		table_append(eff_list, eff_list2);
		
		return eff_list;
	end,

	trigger_calculate_defend = function(self, pside, src, power, dtype)
		if dtype >= D_PENETRATION then 
			return power;
		end
		
		-- reduce power 
		power = power - self.power;
		
		if power < 0 then
			power = 0;
		end
		
		return power;
		
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 172,
	ctype = ARMOR, 
	job = MAGE + ELEMENTAL,   -- TODO add element
	name = 'Dome of Energy',
	star = 2,
	cost = 3,
	power = 2,  -- defend point
	hp = 2,  -- in weapon, it is durability NaiJiu
	skill_desc = 'When an ally damages your hero while Dome of Energy is in play, that ally loses 1 base attack',

	trigger_defend = function(self, pside, src, power)
		local eff_list = {};
		local eff_list2;
		
		if src.table == T_ALLY then
			--[[
			local offset = 0;
			offset = src:change_power(-1); 
			--eff_list[#eff_list+1] = {'power', cindex(src), offset=offset};
			-- peter: TODO use action_power_offset @see (60)Ogloth
			eff_list[#eff_list+1] = eff_power_offset(cindex(src), offset);
			]]--

			eff_list2 = action_power_change(src, pside, -1);
			table_append(eff_list, eff_list2);
		end

		-- reduce durability
		eff_list2 = action_durability_change(self, pside, -1);
		table_append(eff_list, eff_list2);
		
		return eff_list;
	end,
	
	trigger_calculate_defend = function(self, pside, src, power, dtype)
		if dtype >= D_PENETRATION then 
			return power;
		end
		
		-- reduce power 
		power = power - self.power;
		
		if power < 0 then
			power = 0;
		end
		
		return power;
		
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 173,
	ctype = ARMOR, 
	job = WARRIOR + PRIEST,   -- TODO add element
	name = 'Plate Armor',
	star = 2,
	cost = 3,
	power = 2,  -- defend point
	hp = 2,  -- in weapon, it is durability NaiJiu
	skill_cost_energy = 1, -- how many energy to trigger skill
	skill_desc = 'ENERGY:1 Target friendly ally gains +1 health', 

	trigger_defend = function(self, pside, src, power)
		local eff_list = {};
		local eff_list2;

		-- reduce durability
		eff_list2 = action_durability_change(self, pside, -1);
		table_append(eff_list, eff_list2);
		
		return eff_list;
	end,
	
	trigger_calculate_defend = function(self, pside, src, power, dtype)
		if dtype >= D_PENETRATION then 
			return power;
		end
		-- reduce power 
		power = power - self.power;
		if power < 0 then
			power = 0;
		end
		return power;
	end,

	target_list = {
		{side=1, table_list={T_ALLY} },
	},

	trigger_skill = function(self, pside, atl) 
		local offset = 0;
		local index;
		local target;
		if atl==nil or #atl~=1 then
			return error_return('BUGBUG plate armor(173) atl=nil or #~=1');
		end

		target = index_card(atl[1], pside);
		if nil == target then
			return error_return('BUGBUG plat armor(173) target=nil');
		end
		local eff_list = {};
		offset = target:change_base_hp(1); -- is it correct ???
		eff_list[#eff_list + 1] = eff_hp(cindex(target), offset);
		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 174,
	ctype = ARMOR, 
	job = HUNTER + WULVEN,   
	name = 'Moonlight Bracers',
	star = 3,
	cost = 3,
	power = 2,  
	hp = 2,  -- in weapon, it is durability NaiJiu
	skill_desc = 'When Moonlight Bracers is destroyed, if you have a weapon in play it gains +1 durability.',

	trigger_defend = function(self, pside, src, power)
		local eff_list = {};
		local eff_list2;

		-- reduce durability
		eff_list2 = action_durability_change(self, pside, -1);
		table_append(eff_list, eff_list2);
		
		return eff_list;
	end,

	trigger_calculate_defend = function(self, pside, src, power, dtype)
		if dtype >= D_PENETRATION then 
			return power;
		end
		
		-- reduce power 
		power = power - self.power;
		
		if power < 0 then
			power = 0;
		end
		
		return power;
		
	end,

	trigger_remove = function (target, pside, target_table)
		local eff_list = {};
		local eff_list2;
		local side_target = {};
		side_target = pside[target_table.side];
		-- if side_target[T_SUPPORT] ~= target_table then
		if T_SUPPORT ~= target_table.name then
			return eff_list; -- early exit empty list
		end
		for k, v in ipairs(side_target[T_SUPPORT]) do 
			if v.ctype == WEAPON then
				eff_list2 = action_durability_change(v, pside, 1);
				table_append(eff_list, eff_list2);
			end
		end
		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 175,
	ctype = ARMOR, 
	job = WARRIOR + ELEMENTAL,   -- TODO add element
	name = 'Mocking Armor',
	star = 3,
	cost = 6,
	power = 4,  -- defend point
	hp = 2,  -- in weapon, it is durability NaiJiu
	skill_desc = 'Your hero has protector (alias without protector cannot be targeted) while Mocking Armor is in play',
	protector = true,

	trigger_defend = function(self, pside, src, power)
		local eff_list = {};
		local eff_list2;
		
		-- reduce durability
		eff_list2 = action_durability_change(self, pside, -1);
		table_append(eff_list, eff_list2);
		
		return eff_list;
	end,
	
	trigger_calculate_defend = function(self, pside, src, power, dtype)
		if dtype >= D_PENETRATION then 
			return power;
		end
		
		-- reduce power 
		power = power - self.power;
		
		if power < 0 then
			power = 0;
		end
		
		return power;
		
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 176,
	ctype = ARMOR, 
	job = WARRIOR + PRIEST,   -- TODO add element
	name = 'Legion United',
	star = 5,
	cost = 5,
	power = 2,  -- defend point
	hp = 3,  -- in weapon, it is durability NaiJiu
	skill_desc = 'Friendly allies takes 2 less damage from abilities while Legion United is in play', 

	trigger_defend = function(self, pside, src, power)
		local eff_list = {};
		local eff_list2;

		-- reduce durability
		eff_list2 = action_durability_change(self, pside, -1);
		table_append(eff_list, eff_list2);
		
		return eff_list;
	end,
	
	-- this is for hero (normal damage)
	trigger_calculate_defend = function(self, pside, src, power, dtype)
		if dtype >= D_PENETRATION then 
			return power;
		end
		-- reduce power 
		power = power - self.power;
		if power < 0 then
			power = 0;
		end
		return power;
	end,
		

	trigger_add = function(self, pside, target_table)
		local eff_list = {};
		local eff_list2;
		local ac;
		local err = nil;

		if target_table ~= pside[self.side][T_SUPPORT] then
			return eff_list;
		end

		for k, v in ipairs(pside[self.side][T_ALLY]) do
			-- peter: consider to use action_virtual_attach(v, pside, 1176, pside[self.side][T_HERO][1]) @see 1006
			--[[
			ac = clone(g_card_list[1176]);  -- 1176 
			ac.src = pside[self.side][T_HERO][1]; -- damage src

			if true==card_attach(v, ac) then
				eff_list[#eff_list + 1] = eff_attach(cindex(v), 0, v.id, ac.id);
				-- eff_list[1] = {'attach', target=index, attach=0, 
				-- cid=target.id, acid=ac.id};
			else
				print('ERROR Legion(176) cannot attach');
			end
			]]--

			eff_list2, err = action_virtual_attach(v, pside, 1176, pside[self.side][T_HERO][1]);
			if nil == eff_list2 then
				print('BUGBUG Legion(1176) cannot attach: ', err);
				return eff_list, err;
			end
			table_append(eff_list, eff_list2);
		end

		return eff_list;

	end,

	trigger_other_add = function(src, target, pside, target_table)

		local eff_list = {};
		local eff_list2;
		local ac;
		local err = nil;

		if target_table ~= pside[src.side][T_ALLY] then
			return eff_list;
		end

		--[[
		ac = clone(g_card_list[1176]);  -- 1176 
		ac.src = pside[src.side][T_HERO][1]; -- damage src

		if true==card_attach(target, ac) then
			eff_list[#eff_list + 1] = eff_attach(cindex(target), 0, target.id, ac.id);
			-- eff_list[1] = {'attach', target=index, attach=0, 
			-- cid=target.id, acid=ac.id};
		else
			print('ERROR Legion(176) cannot attach');
		end
		]]--

		eff_list2, err = action_virtual_attach(target, pside, 1176, pside[src.side][T_HERO][1]);
		if nil == eff_list2 then
			print('BUGBUG Legion(1176) cannot attach: ', err);
			return eff_list, err;
		end
		table_append(eff_list, eff_list2);

		return eff_list;

	end,

	-- peter: note: all attachment is already removed when a card is move
	-- to grave @see action_grave(), only trigger_remove(this armor) is needed
	-- trigger_other_remove is NOT needed
	trigger_remove = function(self, pside, target_table)
		local eff_list = {};
		local eff_list2;

		local list = pside[target_table.side][T_ALLY];
		if T_SUPPORT ~= target_table.name then
			return eff_list; -- early exit empty list
		end

		eff_list2 = action_virtual_remove(list, pside, self);
		table_append(eff_list, eff_list2);

		--[[
		for k, v in ipairs(side_target[T_ALLY]) do
			local aclist = v.attach_list or {};
			for i = #aclist, 1, -1 do 
				local ac = aclist[i];
				if nil ~= ac and ac.id == 1176 then
					eff_list2 = action_grave(ac, pside);
					table_append(eff_list, eff_list2);
				end
			end
		end
		]]--

		return eff_list;
	end,
	

}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 177,
	ctype = ARMOR, 
	job = MAGE + PRIEST,
	name = 'Twice Enchanted Robe',
	star = 4,
	cost = 5,
	power = 3,  
	hp = 2,  
	skill_desc = 'While your hero takes damage while Twice Enchanted Robe is in play, draw a card if your deck is not empty.',

	trigger_other_damage = function(self, target, pside, src, power, dtype)
		local eff_list = {};
		local eff_list2;

		if self.table ~= T_SUPPORT then
			return eff_list;
		end

		if target.side ~= self.side then
			return eff_list;
		end

		if target.ctype ~= HERO then
			return eff_list;
		end

		if power == 0 then 
			return eff_list;
		end

		if #(pside[self.side][T_DECK]) > 0 then
			eff_list2 = action_drawcard(pside, self.side);
			table_append(eff_list, eff_list2);
		end

		return eff_list;

	end,
	
	trigger_calculate_defend = function(self, pside, src, power, dtype)
		if dtype >= D_PENETRATION then 
			return power;
		end
		-- reduce power 
		power = power - self.power;
		if power < 0 then
			power = 0;
		end
		return power;
	end,

	trigger_defend = function(self, pside, src, power)
		local eff_list = {};
		local eff_list2 = {};
		-- reduce durability
		eff_list2 = action_durability_change(self, pside, -1);
		table_append(eff_list, eff_list2);
		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 179,
	ctype = ARMOR, 
	job = MAGE + WULVEN,   
	name = 'Crescendo',
	star = 4,
	cost = 6,
	power = 1,  
	hp = 6,  
	skill_desc = 'At the start of each of your turns, Crescendo gain +1 defense. At the end of your turn, if Cresendo has 5 defense, it is destroyed and all opposing allies are killed.',

	trigger_calculate_defend = function(self, pside, src, power, dtype)
		if dtype >= D_PENETRATION then 
			return power;
		end
		-- reduce power 
		power = power - self.power;
		if power < 0 then
			power = 0;
		end
		return power;
	end,

	trigger_defend = function(self, pside, src, power)
		local eff_list = {};
		local eff_list2 = {};
		-- reduce durability
		eff_list2 = action_durability_change(self, pside, -1);
		table_append(eff_list, eff_list2);
		return eff_list;
	end,

	trigger_turn_start = function(self, pside, sss)
		local eff_list = {};
		local eff_list2 = {};
		local target_list = {};
		local src;
		if sss == self.side then
			return action_power_change(self, pside, 1);
		end

		-- this is oppo side  (sss ~= self.side)
		if self.power < 5 then
			return {};
		end

		-- kill all allies (like tidal wave)
		for k, v in ipairs(pside[3-self.side][T_ALLY]) do
			target_list[#target_list+1] = v;
		end
		src = pside[self.side][T_HERO][1];
		-- target_list = pside[3-self.side][T_ALLY]; -- shall we create another one?
		eff_list2 = action_damage_list(target_list, pside, src
		, 99, D_MAGIC);  -- TODO animation type ?
		table_append(eff_list, eff_list2);

		-- destroy self
		eff_list2 = action_grave(self, pside);
		table_append(eff_list, eff_list2);
		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 180,
	ctype = ARMOR, 
	job = HUNTER + ROGUE,   -- TODO add element
	name = 'Spelleater Bands',
	star = 3,
	cost = 6,
	power = 1,  -- defend point
	hp = 5,  -- in weapon, it is durability NaiJiu
	skill_desc = 'Your hero takes no damage from abilities while Spelleater Bands is in play.',

	trigger_defend = function(self, pside, src, power)
		local eff_list = {};
		local eff_list2;
		
		-- reduce durability
		eff_list2 = action_durability_change(self, pside, -1);
		table_append(eff_list, eff_list2);
		
		return eff_list;
	end,
	
	trigger_calculate_defend = function(self, pside, src, power, dtype)
		if dtype == D_PENETRATION then 
			return power;
		end

		-- abilities dtype >= D_MAGIC
		if dtype >= D_MAGIC then
			return 0;
		end
		
		-- reduce power 
		power = power - self.power;
		
		if power < 0 then
			power = 0;
		end
		
		return power;
		
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 181,
	ctype = WEAPON, 
	job = WARRIOR + PRIEST,   -- warrior
	name = 'Mournblade',
	star = 4,
	cost = 5,
	power = 0,  
	hp = 4,  -- in weapon, it is durability NaiJiu
	skill_desc = 'When an ally is killed, Mournblade gains +1 base attack up tp a maximum of 5',

	trigger_other_kill = function(self, target, src, pside)
		local eff_list = {};
		local eff_list2;
		local offset = 0;

		if self.power >= 5 then
			return eff_list;
		end

		--[[
		offset = self:change_power(1); 
		eff_list[#eff_list + 1] = eff_power_offset(cindex(self), offset);
		]]--
		eff_list2 = action_power_offset(self, 1); -- was power_change
		-- eff_list2 = action_power_change(self, 1);
		table_append(eff_list, eff_list2);

		return eff_list;
	end,

	trigger_attack = function(self, pside, target, power)
		local eff_list = {};
		local eff_list2;

		-- reduce durability
		eff_list2 = action_durability_change(self, pside, -1);
		table_append(eff_list, eff_list2);

		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 182,
	ctype = WEAPON, 
	job = WARRIOR + ELEMENTAL,   -- warrior
	name = 'Dimension Ripper',
	star = 5,
	cost = 5,
	power = 2,  
	hp = 3,  -- in weapon, it is durability NaiJiu
	skill_desc = 'When your hero deals combat damage while Dimension Ripper is in play, each player in combat draws a card from the other player deck',

	trigger_attack = function(self, pside, target, power)
		local eff_list = {};
		local eff_list2;

		if power > 0 then
			local my = self.side;
			local oppo = 3 - my;

			eff_list2 = action_drawcard(pside, oppo, 0, my);	
			table_append(eff_list, eff_list2);
			eff_list2 = action_drawcard(pside, my, 0, oppo);	
			table_append(eff_list, eff_list2);

		end


		-- reduce durability
		eff_list2 = action_durability_change(self, pside, -1);
		table_append(eff_list, eff_list2);

		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 183,
	ctype = WEAPON, 
	job = WARRIOR,   -- warrior
	name = 'Berserker Edge',
	star = 5,
	cost = 5,
	power = 1,  
	hp = 4,  -- in weapon, it is durability NaiJiu
	skill_desc = 'When your hero deals combat damage Berserker Edge gains +1 base attack',

	-- TODO trigger_attack + 1 power
	trigger_attack = function(self, pside, target, power)
		local eff_list = {};
		local eff_list2;

		--print('DEBUG berseker power=', power);

		if power > 0 then
			--[[
			self.power_offset = (self.power_offset or 0) + 1;
			eff_list[#eff_list + 1] = eff_power_offset(self:index(), 1); 
			]]--

			local flag = 0;
			local aclist = self.attach_list or {};
			for i = #aclist, 1, -1 do
				local ac = aclist[i];
				if ac.id == 1002 then
					flag = 1;
				end
			end
			-- for amber rain(2) skill, should use action_power_change
			-- eff_list2 = action_power_offset(self, 1);
			if flag == 0 then 
				eff_list2 = action_power_change(self, pside, 1);
				table_append(eff_list, eff_list2);
			end
		end
		-- reduce durability
		eff_list2 = action_durability_change(self, pside, -1);
		table_append(eff_list, eff_list2);

		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 184,
	ctype = WEAPON, 
	job = WARRIOR + WULVEN,   -- warrior
	name = 'Jewelers Dream',
	star = 4,
	cost = 4,
	power = 1,  
	hp = 4,  -- in weapon, it is durability NaiJiu
	skill_desc = 'When your hero attacks while Jewelers Dream is in play, 2 used resources are renewed',

	trigger_attack = function(self, pside, target, power)
		local eff_list = {};
		local eff_list2;

		local offset = 1;

		local my_side = pside[self.side];

		if my_side.resource_max <= my_side.resource then
			offset = 0;
		end

		if my_side.resource_max - my_side.resource >= 2 then
			offset = 2;
		end

		if offset > 0 then
			eff_list2 = action_resource(offset, pside, self.side);
			table_append(eff_list, eff_list2);
		end

		-- reduce durability
		eff_list2 = action_durability_change(self, pside, -1);
		table_append(eff_list, eff_list2);

		return eff_list;
	end,

	-- make this weapon better!!
	ai_weight = function(self, atl, pside, sss, ability) 
		local weight;
		-- this should return 520
		weight = ai_weight_equipment(self, atl, pside, sss, ability);
		if weight <= 0 then 
			return 0;
		end
		return weight + 600; -- this will give 1120, better than most case
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 185,
	ctype = WEAPON, 
	is_bow = true, -- it is a bow weapon, aimed shot(81) will check this
	job = HUNTER,
	name = 'Soul Seeker', -- rename
	star = 5,
	cost = 5,
	power = 2,  
	hp = 4,  -- in weapon, it is durability NaiJiu
	skill_desc = 'When your hero kills an ally in combat while Soul Seeker is in play, your hero heals 3 damage.',

	trigger_attack = function(self, pside, target, power)
		local eff_list = {};
		local eff_list2;

		-- reduce durability
		eff_list2 = action_durability_change(self, pside, -1);
		table_append(eff_list, eff_list2);

		return eff_list;
	end,

	trigger_kill = function(self, target, pside, damage_type)
		local eff_list = {};
		local eff_list2;
		if damage_type ~= D_NORMAL then 
			return eff_list;
		end

		if self.table ~= T_SUPPORT then 
			return eff_list;
		end

		if target.ctype ~= ALLY then
			return eff_list;
		end

		local hh = pside[self.side][T_HERO][1];
		eff_list = action_heal(hh, pside, self, 3);

		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 186,
	ctype = WEAPON, 
	is_bow = true, -- it is a bow weapon, aimed shot(81) will check this
	job = HUNTER,
	name = 'Guardians Oath',
	star = 5,
	cost = 5,
	power = 1,  
	hp = 5,  -- in weapon, it is durability NaiJiu
	defender = true,
	skill_desc = 'Your hero has defender (attacks first when defending) while Guardians Oath is in play. Guardians Oath does +1 damage when your hero defends.',

	trigger_attack = function(self, pside, target, power)
		local eff_list = {};
		local eff_list2;

		-- reduce durability
		eff_list2 = action_durability_change(self, pside, -1);
		table_append(eff_list, eff_list2);

		return eff_list;
	end,

	trigger_calculate_attack = function (self, pside, target, power, dtype, attack_type)

		-- attack_flag -> attack(0) or defend(1)
		if attack_type == 1 then
			power = power + 1;
		end

		return power;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 188,
	ctype = WEAPON, 
	job = WARRIOR,   -- warrior
	name = 'Rusty Longsword',
	star = 1,
	cost = 3,
	power = 2,  
	hp = 2,  -- in weapon, it is durability NaiJiu
	skill_desc = 'When Rusty Longsword is summoned, your hero takes 1 damage',

	trigger_add = function (target, pside, target_table)
	-- side1, side2, src, target, target_table) 
		local hh;
		local eff_list;
		if target_table.name ~= T_SUPPORT then
			return {};  -- early exit
		end
		--  hp-1 only when it is added to support
		hh = pside[target.side][T_HERO][1];
		eff_list = action_damage(hh, pside, hh, 1, D_MAGIC);
		return eff_list;
	end,

	trigger_attack = function(self, pside, target, power)
		local eff_list = {};
		local eff_list2;

		-- reduce durability
		eff_list2 = action_durability_change(self, pside, -1);
		table_append(eff_list, eff_list2);

		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 189,
	ctype = WEAPON, 
	job = MAGE + PRIEST,   -- warrior
	name = 'Wrath of Summer',
	star = 5,
	cost = 5,
	power = 1,  
	ab_power = 1,  
	dtype = D_FIRE,
	atype = A_NORMAL,
	hp = 4,  -- in weapon, it is durability NaiJiu
	skill_desc = 'ENERGY:1 Target opposing ally takes 1 fire damage and is set ablaze',
	skill_cost_energy = 1, -- how many energy to trigger skill
	target_list = {
		{side=2, table_list={T_ALLY,}  },    -- hero must be the first 
	}, 

	trigger_skill = function(self, pside, atl)
		-- src is the hero who trigger this skill
		local eff;
		local eff_list = {};
		local eff_list2;
		local side_src;
		local side_target;
		local target_list = {};
		local src;
		local target;
		local ac;
		local index;
		local err;

		side_src = pside[self.side];
		src = side_src[T_HERO][1]; -- assert non-nil ?

		if (atl == nil or atl[1] == nil) then
			print('BUGBUG summer(189) atl or atl[1]=nil');
			return {};
		end

		target = index_card(atl[1], pside);
		if target==nil then
			print('BUGBUG summer(189) target=nil');
			return {};
		end

		-- need set ablaze first, avoid Earthen(39) ability
		-- ablaze
		eff_list2, err = action_virtual_attach(target, pside, 1075, src);
		if nil == eff_list2 then
			print('BUGBUG summer(189 cannot attach flame(1075) : ', err);
			return eff_list, err;
		end
		table_append(eff_list, eff_list2);

		-- fire damage
		eff_list2 = action_damage(target, pside, src, 
			self.ab_power, self.dtype, self.atype);
		table_append(eff_list, eff_list2);

		return eff_list;

	end,

	trigger_attack = function(self, pside, target, power)
		local eff_list = {};
		local eff_list2;


		-- reduce durability
		eff_list2 = action_durability_change(self, pside, -1);
		table_append(eff_list, eff_list2);

		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 190,
	ctype = WEAPON, 
	job = MAGE + PRIEST,   
	name = 'Voice of Winter',
	star = 5,
	cost = 5,
	power = 1,  
	hp = 4,  
	skill_desc = 'Allies summoned while Voice of Winter is in play are frozen (they cannot attack, defend or use abilities) for 2 turns',


	trigger_other_add = function(src, target, pside, target_table)
	-- (side_src, side_target, src, target, target_table)
		-- src is self, so we need to check whether
		-- src and target are the same side
		local eff_list = {};
		local eff_list2;
		local err;

		if target_table.name ~= T_ALLY then 
			return eff_list;
		end
		
		-- local ac;
		-- TODO test voice of winter(190) + frostmire(6) (same attach 1006), can
		-- we extend the number of frozen turns ?
		-- ac = clone(g_card_list[1006]);  -- frozen2

		local my_hero = pside[src.side][T_HERO][1]; -- damage src
		-- eff_list2, err = action_attach(target, pside, ac, my_hero);
		eff_list2, err = action_virtual_attach(target, pside, 1006, my_hero);
		if nil == eff_list2 then
			print('BUGBUG (190)winter cannot attach frozen2(1006) : ', err);
			return eff_list, err;
		end
		table_append(eff_list, eff_list2);

		return eff_list;
	end,

	trigger_attack = function(self, pside, target, power)
		local eff_list = {};
		local eff_list2;

		-- reduce durability
		eff_list2 = action_durability_change(self, pside, -1);
		table_append(eff_list, eff_list2);

		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 191,
	ctype = WEAPON, 
	job = MAGE + PRIEST,   -- warrior
	name = 'Wooden Staff',
	star = 2,
	cost = 3,
	power = 1,  
	hp = 5,  -- in weapon, it is durability NaiJiu
	skill_desc = 'Your hero cannot attack while Wooden Staff is in play',
	no_attack = true,
	no_attack_target = { side = 1, table_list = { T_HERO } },

	trigger_attack = function(self, pside, target, power)
		local eff_list = {};
		local eff_list2;

		-- reduce durability
		eff_list2 = action_durability_change(self, pside, -1);
		table_append(eff_list, eff_list2);

		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 193,
	ctype = WEAPON, 
	job = PRIEST,   
	name = 'Wizents Staff',
	star = 5,
	cost = 4,
	power = 1,  
	hp = 4,  -- in weapon, it is durability NaiJiu
	skill_desc = 'ENERGY:1 Draw a card.',
	skill_cost_energy = 1,

	-- avoid user press when no card in deck
	trigger_target_validate = function(self, pside, target, tid)
		local num_deck;
		local num_hand;
		if self.table==T_HAND then
			return true;
		end
		num_deck = #(pside[self.side][T_DECK]);
		num_hand = #(pside[self.side][T_HAND]);
		if num_deck > 0 and num_hand < MAX_HAND_CARD then
			return true;
		else
			return false;
		end
	end,
	
	trigger_skill = function(self, pside, actual_target_list) 
		local myside = pside[self.side];
		
		local eff_list = {};
		eff_list = action_drawcard(pside, self.side);

		return eff_list;
	end,

	trigger_attack = function(self, pside, target, power)
		local eff_list = {};
		local eff_list2;

		-- reduce durability
		eff_list2 = action_durability_change(self, pside, -1);
		table_append(eff_list, eff_list2);

		return eff_list;
	end,

	-- @see (76)tome
	ai_weight = function(self, atl, pside, sss, ability) 
		-- note: need to check whether self is on hand or support
		-- hand: check whether any card in draw_list is in my side, 
		--       if yes:no need(0),  if no:we need a draw card(1000)
		-- support: different from (76)tome, it use ENERGY, check
		-- 1. if #hand >= 6: stop drawing cards
		-- 2. if hero.id = 8 (zhanna) and hp < 10 : stop drawing card

		local my_hand = pside[sss][T_HAND];
		local my_hero = pside[sss][T_HERO][1];

		if self.table == T_HAND then
			-- print('DEBUG (193)wizent on hand');
			if true == check_in_side_list(pside[sss], DRAW_LIST) then
				return 0; -- already there, no need
			else
				return 1000; -- very good weight
			end
		end

		-- now, self should be in support
		if self.table ~= T_SUPPORT then
			print('BUGBUG (193)wizent ai_weight self.table ~= T_SUPPORT ', self.table);
			return 0; -- this is bad, better no move!
		end

		-- print('DEBUG (193)wizent on support');
		if #my_hand >= 6 then
			return 0;
		end

		if my_hero.id == 8 and my_hero.hp <= 10 then -- zhanna check
			-- better use ENERGY to heal when low HP
			return 0;
		end
		return 1000; -- very likely to draw card first!
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 194,
	ctype = WEAPON, 
	job = HUNTER + ROGUE,   -- warrior
	name = 'Golden Katar',
	star = 1,
	cost = 5,
	power = 2,  
	hp = 2,  -- in weapon, it is durability NaiJiu
	skill_desc = 'Golden Katar doesnot lose durability when your hero defends.',

	trigger_attack = function(self, pside, target, power, attack_type)
		local eff_list = {};
		local eff_list2;

		-- attack_flag -> attack(0) or defend(1)
		-- only attack reduce durability
		if attack_type == 0 then
			eff_list2 = action_durability_change(self, pside, -1);
			table_append(eff_list, eff_list2);
		end

		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 195,
	ctype = WEAPON, 
	job = ELEMENTAL + PRIEST,   -- warrior
	name = 'Ghostmaker',
	star = 5,
	cost = 6,
	power = 1,  
	hp = 3,  -- in weapon, it is durability NaiJiu
	skill_desc = 'When your hero damages another hero in combat while Ghostmaker is in play, the top ally from your graveyard is return to play with 1 health',


	-- use: check_ally_duplicate()
	trigger_attack = function(self, pside, target, power)
		local eff_list = {};
		local eff_list2;


		--[[
		1. check target is hero, power > 0
		2. check T_GRAVE top ally is unique
		   , if unique, need to check T_ALLY has the same ally
		3. change ally hp to 1
		4. if ally not haste, set_not_ready
		5. action_move to T_ALLY
		6. action_durability_change
		]]--
		-- no damage, no effect
		if target.ctype == HERO and power > 0 then
			local my_grave = pside[self.side][T_GRAVE];
			local my_ally = pside[self.side][T_ALLY];
			for i = #my_grave, 1, -1 do
				local cc = my_grave[i];
				if cc.ctype == ALLY then
					-- if this card is unique, check T_ALLY has the same card
					if cc.unique == true 
					and check_ally_duplicate(pside, self.side, cc.id) then
						break;
					end

					-- order is important (b4 action_move)
					if true ~= cc.haste then 
						set_not_ready(cc); 
					end
					local offset = 1 - cc.hp;
					local real_offset = cc:change_hp(offset);
					eff_list[#eff_list + 1] = eff_hp(cindex(cc), real_offset);
					if cc.hp <= 0 then -- XXX why cc.hp <= 0?
						eff_list2 = action_grave(cc, pside);
						table_append(eff_list, eff_list2);
						break;
					end
					eff_list2 = action_move(cc, pside, my_grave, my_ally);
					table_append(eff_list, eff_list2);
					break;
				end
			end
		end
		------------------

		-- reduce durability
		eff_list2 = action_durability_change(self, pside, -1);
		table_append(eff_list, eff_list2);


		return eff_list;
	end, -- trigger_attack
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 196,
	ctype = WEAPON, 
	job = HUNTER + ROGUE,
	name = 'Old Iron Dagger',
	star = 5,
	cost = 3,
	power = 1,  
	hp = 2,  -- in weapon, it is durability NaiJiu
	skill_desc = 'Old Iron Dagger does +1 damage when your hero defends.',

	trigger_attack = function(self, pside, target, power)
		local eff_list = {};
		local eff_list2;

		-- reduce durability
		eff_list2 = action_durability_change(self, pside, -1);
		table_append(eff_list, eff_list2);

		return eff_list;
	end,

	trigger_calculate_attack = function (self, pside, target, power, dtype, attack_type)

		-- attack_flag -> attack(0) or defend(1)
		if attack_type == 1 then
			power = power + 1;
		end

		return power;
	end,
}
g_card_list[card.id] = card_class:new(card);


card = {
	id = 196,
	ctype = WEAPON, 
	is_bow = true, -- it is a bow weapon, aimed shot(81) will check this
	job = HUNTER + ROGUE,
	name = 'Old Iron Dagger',
	star = 3,
	cost = 3,
	power = 1,  
	hp = 2,  -- in weapon, it is durability NaiJiu
	skill_desc = 'Old Iron Dagger does +1 damage when your hero defends.',

	trigger_attack = function(self, pside, target, power)
		local eff_list = {};
		local eff_list2;

		-- reduce durability
		eff_list2 = action_durability_change(self, pside, -1);
		table_append(eff_list, eff_list2);

		return eff_list;
	end,

	trigger_calculate_attack = function (self, pside, target, power, dtype, attack_type)

		-- attack_flag -> attack(0) or defend(1)
		if attack_type == 1 then
			power = power + 1;
		end

		return power;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 197,
	ctype = WEAPON, 
	job = WARRIOR + WULVEN,   -- warrior
	name = 'Uprooted Tree',
	star = 3,
	cost = 4,
	power = 1,  
	hp = 4,  -- in weapon, it is durability NaiJiu
	attack_dtype = D_PENETRATION,	-- no reducetion in armor
	skill_desc = 'Hero armor does not reduce the damage done in combat by your hero while Uprooted Tree is in play',

	trigger_attack = function(self, pside, target, power)
		local eff_list = {};
		local eff_list2;

		-- reduce durability
		eff_list2 = action_durability_change(self, pside, -1);
		table_append(eff_list, eff_list2);
		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 200,
	ctype = WEAPON, 
	job = MAGE + ROGUE,  
	name = 'Dagger of Unmaking',
	star = 5,
	cost = 5,
	power = 1,  
	hp = 4,  -- in weapon, it is durability NaiJiu
	skill_desc = 'When your hero deals non-fatal damage to an ally in combat while Dagger of Unmaking is in play, that ally is returned to its owner hand.',

	trigger_attack = function(self, pside, target, power)
		local offset = 0;
		local eff_list = {};
		local eff_list2;

		if power > 0 and (target:die() ~= true) and target.table == T_ALLY then
			-- check if ally reborn
			if target.reborn == true then
				print('DEBUG (200)dagger_of_unmarking_target_reborn');
				return {};
			end

			eff_list2 = action_refresh(target, pside);
			table_append(eff_list, eff_list2);

			local side_target = pside[target.side];
			local side_home = pside[target.home];

			eff_list2 = action_move(target, pside, side_target[target.table]
						, side_home[T_HAND]);
			table_append(eff_list, eff_list2);

			-- XXX peter: this may have bug: when target has attach!!!
			-- target:refresh();
			set_ready(target);
		end


		-- reduce durability
		eff_list2 = action_durability_change(self, pside, -1);
		table_append(eff_list, eff_list2);

		--[[
		-- xxx this is buggy, use self:change_hp(-1)
		--self.hp = self.hp - 1;
		offset = self:change_hp(-1);
		eff_list[#eff_list+1] = eff_hp(cindex(self), offset);

		if self.hp > 0 then
			return eff_list;
		end
		self.hp = 0; -- avoid negative
		-- implicit: self.hp = 0

		-- move it to grave, bye!
		eff_list2 = action_grave(self, pside);
		table_append(eff_list, eff_list2);
		]]--

		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);


------ VIRTUAL CARD   flame, poison, frozen, disable etc

card = {
	id = 1002, -- virtual card > 1000
	ctype = ATTACH, 
	job = 0,  -- neutral
	name = 'Weapon Power2',
	cost = 99, -- large cost to avoid accidental play on hand
	power = 2,  
	hp = 0,  
	skill_desc = 'Weapon Attack +2',
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 1004, -- virtual card > 1000
	ctype = ATTACH, 
	job = 0,  -- neutral
	name = 'Weapon Power2',
	cost = 99, -- large cost to avoid accidental play on hand
	power = 2,  
	hp = 0,  
	timer = 2,
	skill_desc = 'Weapon Attack +2 until the start of your next turn',
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 1006, -- virtual card > 1000
	ctype = ATTACH, 
	job = 0,  -- neutral
	name = 'Frozen2',
	cost = 99, -- large cost to avoid accidental play on hand
	power = 0,  
	hp = 0,  
	skill_desc = 'Frozen cannot attack, defend and use abilities',
	disable = true, -- core logic
	frozen = true, -- side-track logic (useful for kill all frozen ally)
	vtype = V_FROZEN,

	timer = 4;  -- 5 or 6 or 7 ?
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 1009, -- virtual card > 1000
	ctype = ATTACH, 
	job = 0,  -- neutral
	name = 'Ambush stealth haste 1 round',
	cost = 99, -- large cost to avoid accidental play on hand
	power = 0,  
	hp = 0,  
	skill_desc = 'Ambush, stealth and haste util the start of your next turn',
	ambush = true,
	stealth = true,
	haste = true,

	-- peter: this is a rough implementation, @see 162 for details
	-- in most case 2 is correct, but for defender attack, timer should be 1
	timer = 2,  
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 1010, -- virtual card > 1000
	ctype = ATTACH, 
	job = 0,  -- neutral
	name = 'Weapon Power2,Damage Discard1',
	cost = 99, -- large cost to avoid accidental play on hand
	power = 2,  
	hp = 0,  
	timer = 1,
	skill_desc = 'Weapon Attack +2 until the end of your turn, when damage oppo hero, discards 1 card from oppo hand at random',

}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 1012,
	ctype = ATTACH, 
	job = 0,  -- neutral
	name = 'Weapon Power1',
	cost = 99, -- large cost to avoid accidental play on hand
	power = 1,  
	hp = 0,  
	skill_desc = 'Weapon Attack +1',
	timer = 1,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 1014, -- virtual card > 1000
	ctype = ATTACH, 
	job = 0,  -- neutral
	name = 'Weapon Power1',
	cost = 99, -- large cost to avoid accidental play on hand
	power = 1,  
	hp = 0,  
	skill_desc = 'Weapon Attack +1.',
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 1021,
	ctype = ATTACH, -- attach
	job = 0,  -- neutral
	name = 'no_attack 2',
	cost = 99,
	power = 0,  
	hp = 0,  
	vtype = V_NO_ATTACK,
	skill_desc = 'Target oppoing ally cannot attack until the end of its controllers next turn.', 
	timer = 2,

	no_attack = true,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 1032, -- virtual card > 1000
	ctype = ATTACH, 
	job = 0,  -- neutral
	name = 'Power1',
	cost = 99, -- large cost to avoid accidental play on hand
	power = 1,  
	hp = 0,  
	skill_desc = 'Friendly allies attack +1 on your turn',

	timer = 1,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 1034, -- virtual card > 1000
	ctype = ATTACH, 
	job = 0,  -- neutral
	name = 'Health2',
	cost = 99, -- large cost to avoid accidental play on hand
	power = 0,  
	hp = 2,  
	skill_desc = 'Target friendly ally gains +2 health.',

}
g_card_list[card.id] = card_class:new(card);


card = {
	id = 1042, -- virtual card > 1000
	ctype = ATTACH, 
	job = 0,  -- neutral
	name = 'Power2',
	cost = 99, -- large cost to avoid accidental play on hand
	power = 2,  
	hp = 0,  
	skill_desc = 'Attack +2 until start of your next turn',

	timer = 2;  -- 5 or 6 or 7 ?
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 1046, -- virtual card > 1000
	ctype = ATTACH, 
	job = 0,  -- neutral
	name = 'Power3Hp-3',
	cost = 99, -- large cost to avoid accidental play on hand
	power = 3,  
	hp = -3,  
	skill_desc = 'Attack +3 , HP -3,  until start of your next turn',

	timer = 2;  -- 5 or 6 or 7 ?
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 1050, -- virtual card > 1000
	ctype = ATTACH, 
	job = 0,  -- neutral
	name = 'Cannot defend1',
	cost = 99, -- large cost to avoid accidental play on hand
	power = 0,  
	hp = 0,  
	skill_desc = 'Target cannot defend until end of your turn',
	
	-- TODO change other?
	no_defend = true,	
	
	timer = 1,  -- 5 or 6 or 7 ?
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 1072, -- virtual card > 1000
	ctype = ATTACH, 
	job = 0,  -- neutral
	name = 'Frozen3',
	cost = 99, -- large cost to avoid accidental play on hand
	power = 0,  
	hp = 0,  
	skill_desc = 'Frozen cannot attack, defend and use abilities',
	disable = true, -- core logic
	frozen = true, -- side-track logic (useful for kill all frozen ally)
	vtype = V_FROZEN,

	timer = 6;  -- 5 or 6 or 7 ?
}
g_card_list[card.id] = card_class:new(card);


card = {
	id = 1073, -- virtual card > 1000
	ctype = ATTACH, 
	job = 0,  -- neutral
	name = 'Poison',
	cost = 99, -- large cost to avoid accidental play on hand
	power = 0,  
	hp = 0,  
	skill_desc = '1 poison damage every start of attacher turn',
	vtype = V_POISON,

	-- src = who is the one who trigger this posion attachment

	-- trigger that works when turn start
	trigger_turn_start = function(self, pside, sss)
		local eff_list;
		local target;
		local i;

		-- not my side, no damage
		if self.side~=sss then
			return {};
		end

		-- e.g. cindex()=10001 -> we need 1000
		i = cindex(self);
		-- get the attacher card index
		i = math.floor(i / 10);  -- remove last digit, attpos
		target = index_card(i, pside);
		if nil==target then
			print('BUGBUG (1073)poison attacher card is nil index=' .. i);
			return {};
		end

		-- @see 75  ac.src = pside[self.side][T_HERO][1]
		eff_list = action_damage(target, pside, self.src, 1, D_POISON);

		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);


card = {
	id = 1075, -- virtual card > 1000
	ctype = ATTACH, 
	job = 0,  -- neutral
	name = 'Flame',
	cost = 99, -- large cost to avoid accidental play on hand
	power = 0,  
	hp = 0,  
	skill_desc = '1 fire damage every start of attacher turn',
	vtype = V_ABLAZE,

	-- src = who is the one who trigger this flame attachment

	-- trigger that works when turn start
	trigger_turn_start = function(self, pside, sss)
		local eff_list;
		local target;
		local i;

		-- not my side, no damage
		if self.side~=sss then
			return {};
		end

		-- e.g. cindex()=10001 -> we need 1000
		i = cindex(self);
		-- print('i(1075)111 = ', i);
		-- get the attacher card index
		i = math.floor(i / 10);  -- remove last digit, attpos
		-- print('i(1075)222 = ', i);
		target = index_card(i, pside);
		if nil==target then
			print('BUGBUG (1075)flame attacher card is nil index=' .. i);
			return {};
		end

		-- @see 75  ac.src = pside[self.side][T_HERO][1]
		eff_list = action_damage(target, pside, self.src, 1, D_FIRE);

		return eff_list;
	end,
}
g_card_list[card.id] = card_class:new(card);


card = {
	id = 1080, -- virtual card > 1000
	ctype = ATTACH, 
	job = 0,  -- neutral
	name = 'Web2',
	cost = 99, -- large cost to avoid accidental play on hand
	power = 0,  
	hp = 0,  
	skill_desc = 'disable 2 turns',
	disable = true, -- core logic
	vtype = V_COBWEB,

	timer = 4;  -- 5 or 6 or 7 ?
}
g_card_list[card.id] = card_class:new(card);


card = {
	id = 1082, -- virtual card > 1000
	ctype = ATTACH, 
	job = 0,  -- neutral
	name = 'Poison Arrow2',
	cost = 99, -- large cost to avoid accidental play on hand
	power = 0,  
	hp = 0,  
	skill_desc = 'disable 1 turn',
	disable = true, -- core logic
	vtype = V_COBWEB,

	timer = 2;  -- 5 or 6 or 7 ?
}
g_card_list[card.id] = card_class:new(card);


card = {
	id = 1089, -- virtual card > 1000
	ctype = ATTACH, 
	job = 0,  -- neutral
	name = 'NetTrap3',
	cost = 99, -- large cost to avoid accidental play on hand
	power = 0,  
	hp = 0,  
	skill_desc = 'disable 3 turns',
	disable = true, -- core logic
	vtype = V_COBWEB,

	timer = 6;  -- 5 or 6 or 7 ?
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 1102, -- virtual card > 1000
	ctype = ATTACH, 
	job = 0,  -- neutral
	name = 'Power1',
	cost = 99, -- large cost to avoid accidental play on hand
	power = 1,  
	hp = 0,  
	skill_desc = 'Friendly allies attack +1 on your turn',

	timer = 1,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 1106, -- virtual card > 1000
	ctype = ATTACH, 
	job = 0,  -- neutral
	name = 'Ambush',
	cost = 99, -- large cost to avoid accidental play on hand
	power = 0,  
	hp = 0,  
	skill_desc = 'Ambush util the end of your turn',
	ambush = true,

	timer = 1,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 1109, -- virtual card > 1000
	ctype = ATTACH, 
	job = 0,  -- neutral
	name = 'Disable1',
	cost = 99, -- large cost to avoid accidental play on hand
	power = 0,  
	hp = 0,  
	skill_desc = 'disable 1 turns',
	disable = true, -- core logic
	vtype = V_COBWEB,

	timer = 2;
}
g_card_list[card.id] = card_class:new(card);



card = {
	id = 1146,
	ctype = ATTACH,  
	job = 0,  
	camp = 0, 
	name = 'Bloodstone +1 power',
	cost = 99,
	hp = 0,  
	power = function(self, pside)
		local index;
		local diff;
		local target; -- parent
		if (self.side or 0) == 0 then 
			return 0;
		end
		index = card_index(self.side, self.table, self.pos);
		target = index_card(index, pside);
		if target == nil then
			-- print('BUGBUG no_parent_1146');
			return 0; -- no parent 
		end
		if self == target then
			return 0;
		end
		-- print('(146)bloodstone parent id=', target.id);
		diff = target.hp_max - target.hp;
		if diff > 0 then
			return 1;
		end
		return 0; -- all other case, no bonus power
	end,
}
g_card_list[card.id] = card_class:new(card);

card = {
	id = 1176, -- virtual card > 1000
	ctype = ATTACH, 
	job = 0,  -- neutral
	name = 'Legion',
	cost = 99, -- large cost to avoid accidental play on hand
	power = 0,  
	hp = 0,  
	skill_desc = 'take 2 less abilities damage',

	trigger_calculate_defend = function (self, pside, src, power, dtype)
		if dtype >= D_MAGIC and dtype < D_DIRECT then
			power = power - 2;
		end
		if power < 0 then
			power = 0;
		end
		return power;
	end,

}
g_card_list[card.id] = card_class:new(card);


card = {
	id = 1162, -- virtual card > 1000
	ctype = ATTACH, 
	job = 0,  -- neutral
	name = 'Frozen1',
	cost = 99, -- large cost to avoid accidental play on hand
	power = 0,  
	hp = 0,  
	skill_desc = 'Frozen cannot attack, defend and use abilities',
	disable = true, -- core logic
	frozen = true, -- side-track logic (useful for kill all frozen ally)
	vtype = V_FROZEN,
	-- peter: this is a rough implementation, @see 162 for details
	-- in most case 2 is correct, but for defender attack, timer should be 1
	timer = 3,  
}
g_card_list[card.id] = card_class:new(card);


function card_compare(c1, c2)
	if c1.id < c2.id then
		return true;
	else
		return false;
	end
end

-- peter: we should not sort a sparse array!!!
-- table.sort(g_card_list);

card = nil; -- avoid code below using card variable

----- CARD END 


---- CARD FUNCTION START

-- @param tb  table    in/out parameters, new card is placed here
--        note: the table will be cleared initially
-- @param cl = card list, e.g. local cl = {22,26, 71}  (card id)
-- input: table of card id (numeric)  e.g. {22, 26, 71} 
-- output: no return
function card_init_table(tb, cl, pside)
	local cc;

	-- peter: need to clear the table first, use this init with care!!!
	local total = #tb;
	for i=1, total do
		table.remove(tb);
	end

	for k,v in ipairs(cl) do
		cc = g_card_list[v];  -- get the card from g_card_list template
		if cc == nil then 
			print ('BUGBUG card_init_table id=', v);
		else 
			cc = clone(cc);
			action_add(cc, pside, tb);
		end
	end
end

-- setup the s,t,p (side, table, pos) index of a card
-- t = table, index, e.g. T_HAND 
function card_set_pos(cc, s, t, p, attpos)
	cc.side = s;
	cc.table = t;
	cc.pos = p;
	cc.attpos = attpos;
end

-- id = hero id
function card_init_hero(oneside, id)
	local hh;
	assert( nil ~= hero_list[id]);
	hh = clone(hero_list[id]);
	oneside[T_HERO][1] = hh;  -- only 1 hero 
	card_set_pos(hh, oneside.id, T_HERO, 1); 
	hh[T_SUPPORT] = oneside[T_SUPPORT]; -- for weapon armor artifactetc
	hh.home = oneside.id;
end

--  reset the cost, hp, power and remove all add-on
function card_refresh(cc)
	-- reset the base hp and power
	cc:refresh();
end

--[[
-- old logic
function check_solo_list()
	if g_solo_list == nil then
		return 0;
	end

	local hand = g_logic_table[2][T_HAND];
	local deck = g_logic_table[2][T_DECK];
	if #g_solo_list - #hand - #deck ~= 0 then
		print('BUGBUG check_solo_list:#g_solo_list=' .. #g_solo_list
		.. ' #hand=' .. #hand .. ' #deck=' .. #deck);
	end
	return #g_solo_list - #hand - #deck;
end

-- return index for solo_list, 0 or 1 is ok
function get_solo_index(pside, cc)
	local ret = 0;
	if g_solo_list == nil then
		return 0; 
	end

	-- Dimension(182) draw opposite deck, index set 1
	if cc.side ~= 2 then
		return 1; 
	end

	if cc.table ~= T_HAND and cc.table ~= T_DECK then
		return 1; 
	end

	local index = 0;
	if cc.table == T_HAND then
		index = g_solo_list[cc.pos];
	else
		index = g_solo_list[#(pside[cc.side][T_HAND]) + cc.pos];
	end
	-- print('DEBUG get_solo_index:index=', index);
	if index ~= 0 and index ~= 1 then
		print('BUGBUG get_solo_index:index_bug ', index);
	end
	return index;
end

function solo_list_remove(pside, cc)
	local ret = 0;
	if g_solo_list == nil then
		return 0;
	end

	if cc.side ~= 2 then
		return 0;
	end

	if cc.table ~= T_HAND and cc.table ~= T_DECK then
		return 0;
	end

	local pos = 0;
	if cc.table == T_HAND then
		pos = cc.pos;
	else
		pos = #(pside[cc.side][T_HAND]) + cc.pos;
	end
	-- print('DEBUG solo_list_remove:#g_solo_list='..#g_solo_list..' pos='..pos);

	table.remove(g_solo_list, pos);

	return 0;
end

function solo_list_add(pside, cc, index)
	local ret = 0;
	if g_solo_list == nil then
		return 0;
	end

	if cc.side ~= 2 then
		return 0;
	end

	if cc.table ~= T_HAND and cc.table ~= T_DECK then
		return 0;
	end

	local pos = 0;
	if cc.table == T_HAND then
		pos = cc.pos;
	else
		pos = #(pside[cc.side][T_HAND]) + cc.pos;
	end
	-- print('DEBUG solo_list_add:#g_solo_list='..#g_solo_list..' pos='..pos);

	table.insert(g_solo_list, pos, index);

	return 0;
end
]]

-- add the card c to table t
function card_add_top(t, c)
	if c.pos~=nil and c.pos>0 then
		print ('BUGBUG card_add with pos ~= 0 : card.id=' .. c.id 
		.. ' pos=' ..c.pos);
		return 0;
	end
	table.insert(t, 1, c); -- add to position 1 (top)
	card_set_pos(c, t.side,  t.name, 1);
	-- c.pos = #t;  -- update the position
	-- c.table = t.name;  -- table name
	-- c.side = t.side;  -- make sure table.side is ready initially
	
	-- this is mainly for init
	if c.home == nil then
		c.home = c.side;
	end

	for i=2, #t do
		t[i].pos = i; 
		-- update attach pos
		for k, v in ipairs(t[i].attach_list or {}) do
			v.pos = i; -- nishaven bug fix
		end
	end

	return 1; -- seems useless return the pos after add
end

-- add the card c to table t
function card_add(t, c)
	if c.pos~=nil and c.pos>0 then
		print ('BUGBUG card_add with pos ~= 0 : card.id=' .. c.id 
		.. ' pos=' ..c.pos);
		return 0;
	end
	table.insert(t, c); -- t[#t + 1] = c;
	card_set_pos(c, t.side,  t.name, #t);
	-- obsolete:
	-- c.pos = #t;  -- update the position
	-- c.table = t.name;  -- table name
	-- c.side = t.side;  -- make sure table.side is ready initially
	
	if c.home == nil then
		c.home = c.side;
	end
	-- TODO need to assign 'home' for grave if card is place in deck

	return c.pos;
end

-- t = attach_list,  need to update c.attpos
function card_remove_attach(t, c)
	if t==nil then
		print('BUGBUG card_remove_attach list nil: id,index: ', c.id, c:index()); 
		return 0;
	end
	-- core logic 1 : table.remove
	local p = c.attpos;
	local ret = table.remove(t, p);
	-- note: need to update all the pos if the c.pos is not the last one
	if ret ~= c then
		print('BUGBUG card_remove_attach wrong target');
		if nil == ret then
			return 0; -- means donot remove any thing
		end
	end

	-- core logic 2 : set nil on attpos, src
	c.attpos = nil; -- 0 is not good enough
	c.pos = nil; -- need this for card_add to check
	c.src = nil;  -- avoid cyclic reference

	-- core logic 3:  reset attach_list card.attpos
	for i=p, #t do
		t[i].attpos = i; 
	end
	return p;
end

-- must first remove and then add
-- when ctype==ATTACH, there are 2 cases:
-- 1. card is in normal table, not attached (attpos == nil)
-- 2. card is attached to a normal card, attpos ~= nil, e.g. 23031, attpos=1
-- case 1:  card_remove(t, c)
-- case 2:  card_remove_attach(attach_list, attach_card)
function card_remove(t, c)

	if c.ctype == ATTACH and c.attpos then 
		return card_remove_attach(t[c.pos].attach_list, c);
	end

	-- order is important:  remove first and then set c.pos=nil
	-- core logic 1:  table.remove
	local p;
	p = c.pos;
	local ret = table.remove(t, p);
	-- note: need to update all the pos if the c.pos is not the last one
	if ret ~= c then
		print('BUGBUG card_remove wrong target');
		if nil == ret then
			return 0; -- means donot remove any thing
		end
	end

	-- core logic 2:
	c.pos = nil; -- peter: add for kelton check
	-- do we need c.src = nil ?


	-- core logic 3:  update the pos of the rest of cards in table t
	for i=p, #t do
		t[i].pos = i; 
		-- update attach pos
		for k, v in ipairs(t[i].attach_list or {}) do
			v.pos = i; -- nishaven bug fix
		end
	end

	return p;  -- removed position
end

-- c = card,  ac = attach card
function card_attach(c, ac)
	if c.attach_list == nil then
		c.attach_list = {};
	end

	-- TODO check duplicate here?
	for k, v in ipairs(c.attach_list) do
		if v.id == ac.id then
			print('WARN: card_attach duplicate');
			return false;
		end
	end
	table.insert(c.attach_list, ac);  -- c.attach_list[#c.attach_list + 1] = ac
	-- print('card_attach c.pos = ', c.pos);
	-- copy the s,t,p to ac
	card_set_pos(ac, c.side, c.table, c.pos, #c.attach_list);
	return true ; -- TODO return eff_list
end


-- ready = 2 : very ready
-- ready = 1 : ability used, cannot use ability, can attack
-- ready = 0 : attacked, cannot do anything
-- ready = -1: summon this turn, cannot do anything
function card_all_ready(tb)
	for k, v in ipairs(tb) do
		v.ready = 2;
	end
end

-- means:able & in T_HERO or T_ALLY & power>0 & ready>=1
function check_ready_attack(cc)
	if cc.disable then
		return false;
	end

	if cc.table ~= T_HERO and cc.table ~= T_ALLY then
		return false;
	end

	if true == cc.no_attack then
		return false;
	end

	if cc.power <= 0 then
		return false;
	end

	if cc.ready == 0 then
		return false;
	elseif cc.ready == -1 and true ~= cc.haste then
		return false;
	end
	return true;
end

function check_ready_defend(cc)
	if cc.table ~= T_HERO and cc.table ~= T_ALLY then
		return false;
	end

	if cc.disable then
		return false;
	end

	if cc.no_defend == true then
		return false;
	end

	if cc.stop_defend == true then
		return false;
	end

	if cc.power <= 0 then
		return false;
	end

	if cc:die() then
		return false;
	end

	return true;
end

function check_ready_ability(cc)
	if cc.ready >= 2 then
		return true;
	else 
		return false;
	end
end

function set_ready(cc)
	cc.ready = 2;
end

function set_not_ready(cc)
	cc.ready = 0;
end

function set_first_ready(cc)
	cc.ready = -1;
end

-- kelton:
-- if card a attack card b, and card a died,
--  card a will still attach a 'X' in grave.
-- masha:
-- use this functino after action_attack_one, check src if it can attack more then 1 time in one turn(rapid fire(84) attach on hero)
function set_use_attack(target, pside, src, sss)

	if src.table == T_GRAVE then
		return false;	
	end

	local has_attack_serial = false;

	if nil ~= src.trigger_attack_serial then
		-- must use self in trigger
		src:trigger_attack_serial(pside, src, sss);
		has_attack_serial = true;
	end

	for k, ac in ipairs(src.attach_list or {}) do  -- luajit fix
		if nil ~= ac.trigger_attack_serial then 
			ac:trigger_attack_serial(pside, src, sss);
			has_attack_serial = true;
		end
	end

	if has_attack_serial == false then
		set_not_ready(src);
	end
end

function set_use_ability(cc)
	if check_ready_ability(cc) == false then
		return false;
	end
	-- hero or ally can use ability and attack, -1 is ok
	if cc.table == T_SUPPORT or cc.table == T_ALLY or cc.table == T_HERO then
		cc.ready = cc.ready - 1;
		return true;
	end
	return false;
end

-- return integer, 0 for no-duplicate, >0 means duplicate(the index)
function check_attach_duplicate(c, ac)
	for k, v in ipairs(c.attach_list or {}) do
		if v.id == ac.id then
			local index = c:index() * 10 + k;
			-- print('WARN: check_attach_duplicate index=',  index);
			return index;
		end
	end
	return 0;
end

-- return integer, 0 for ok, <0 means over attach 9
function check_attach_over(c)
	if #(c.attach_list or {}) >= 9 then
		return -1;
	end
	return 0;
end

-- usually for SUPPORT and ARTIFACT
function check_support_duplicate(pside, sss, id)
	for k, v in ipairs(pside[sss][T_SUPPORT] or {}) do
		if v.id == id then
			return true;
		end
	end
	return false; -- no duplicate
end

-- check whether the ally (id) is already in T_ALLY
function check_ally_duplicate(pside, sss, id)
	for k, v in ipairs(pside[sss][T_ALLY] or {}) do
		if v.id == id then
			return true;
		end
	end
	return false; -- no duplicate
end

function check_is_item(target)
	if nil == target then
		print('BUGBUG:check_is_item:nil_target');
		return false;
	end
	if target.ctype == WEAPON or target.ctype == ARMOR
	or target.ctype == ARTIFACT or target.ctype == TRAP then
		return true;
	end
	return false;
end

-- target_list = {target1, target2, target3, ...}
-- targetX = { side=1|2|3, table_list={table1, table2, ...}, optional=true|false|nil }
-- tableX = T_ALLY | T_GRAVE | T_SUPPORT | T_HERO etc.
-- optional=true | false | nil  -- mean this target is optional x will not cancel
function check_target_list(target_list) 
	if target_list == nil or type(target_list) ~= 'table' then 
		return false;
	end

	for k, v in ipairs(target_list) do -- { k,v start
		-- v is the target
		for kk, vv in pairs(v) do -- { kk,vv start
			if kk=='side' then
				if vv<1 or vv>3 then
					print('ERROR target_list:target.side(1-3) invalid:' .. vv);
					return false;
				end
			elseif kk=='table_list' then
				for i=1,#vv do
					local tb = vv[i];
					-- note: hero position is not checked now
--					if table==T_HERO and i~=#vv then
--						print('ERROR target_list: table hero is not the last');
--						return false;
--					end
					if tb~=T_ALLY and tb~=T_GRAVE and tb~=T_SUPPORT
					and tb~=T_HERO and tb~=T_WEAPON and tb~=T_ARMOR then
						print('ERROR target_list: invalid table:', tb);
						return false;
					end
				end
			elseif kk=='optional' then
				if type(vv)~='boolean' then
					print('ERROR target_list : optional is not bool:' .. vv);
					return false;
				end
			end -- if kk=='side'
		end -- } kk,vv end

	end -- } for k, v in target_list
	return true; -- good finally target_list is valid
end



-- check whether cc can use normal attack
-- rename to check_playable?
-- TODO refactor 2 parts:  check_attack,  check_ability
-- attack -> ability (true -> false)
-- check_attackable() -> check_playable()
function check_playable(cc, pside, sss, ability)
	-- general case
	if cc==nil then
		print('BUGBUG check_playable nil cc');
		return false, 'BUG_nil_cc';
	end

	-- disable may be double checked in no_attack and no_ability
	if cc.disable==true then
		return false, 'disable'; -- web, frozen etc.
	end

	-- not my side
	if cc.side~=sss then
		return false, 'not_my_side';
	end

	-- check resource against card cost, for hand
	-- no need to check attack or ability
	if cc.table == T_HAND then
		-- @see play_validate_ability  T_HAND section
		if pside[sss].resource < cc.cost then
			return false, 'resource_not_enough';
		end

		-- game is solo ai game
		if g_solo_type == 1 and cc.side == 2 then
			if cc.solo_ai == 0 then
				return false, 'solo_ai_card_for_scrifice';
			end

			if pside[sss].ally_max > 0 and cc.ctype == ALLY then
				if #pside[2][T_ALLY] >= pside[sss].ally_max then
					return false, 'solo_ai_ally_too_much';
				end
			end
		end

		-- check duplicate: for SUPPORT and ARTIFACT @see check_playable
		if (cc.ctype==SUPPORT or cc.ctype==ARTIFACT) then
			if check_support_duplicate(pside, sss, cc.id) then
				return false, 'support_duplicate:check';
			end
		end
		if (cc.ctype==ALLY and cc.unique==true) then
			if check_ally_duplicate(pside, sss, cc.id) then
				return false, 'unique_ally_duplicate';
			end
		end
	end



	-- attack case:
	if true~=ability then
		if false==check_ready_attack(cc) then
			return false, 'attack_not_ready';
		end

		-- check power=0
		if cc.power <= 0 then
			return false, 'power_0';
		end

		if cc.table ~= T_ALLY and cc.table ~= T_HERO then
			return false;
		end

		-- XXX check_ready_attack() has contains no_attack checking
--		if true == cc.no_attack then
--			return false, 'no_attack';
--		end

		local atl_list;
		atl_list = list_attack_target(0, pside, sss);
		if atl_list == nil or #atl_list == 0 then
			return false, 'no_target';
		end

	end

	-- TODO Core logic should before custom checking
	-- ability case : both on T_HAND or other table (T_ALLY, T_HERO)
	if true == ability then
		-- for energy and resource, @see below, need to check T_HAND

		if cc.trigger_target_validate ~= nil then
			if cc:trigger_target_validate(pside, nil, 0) == false then
				return false, 'target_validate_0';
			end
		end
	end

	-- check skill cost for ability, non-hand
	-- usually this trigger_skill on card
	if true == ability and cc.table ~= T_HAND then
		if true == cc.no_ability then
			return false, 'no_ability';
		end

		if false == check_ready_ability(cc) then
			return false, 'ability_not_ready';
		end
		if cc.trigger_skill == nil then
			return false, 'empty_ability'; -- no skill, no ability
		end

		if pside[sss][T_HERO][1].energy < cc.skill_cost_energy then
			return false, 'energy_not_enough'; -- not enough energy
		end
		if pside[sss].resource < cc.skill_cost_resource then
			return false, 'resource_not_enough'; -- not enough resource
		end
	end

	return true;
end


-- check against protector logic
-- return true :  if the target can be target, no protector
-- return false:  target cannot be target, being protected by protector
function check_target_protector(cc, pside, side_id)
	-- print('DEBUG check_target_protector cc.name, cc.side, side_id=', cc.name, cc.side, side_id);
	-- same side does not have protector effect
	if cc.side == side_id then
		if cc.no_help == true then
			print('check_target_protector:no_help = ', cc.name);
			return false;
		end
		return true;
	end

	-- hero does not benefit from protector 
	if cc.ctype==HERO then
		return true;
	end

	-- this card is a protector, can be targeted
	if cc.protector then
		return true;
	end

	------- below are all protector related!!!
	local tb = pside[cc.side][T_ALLY];
	local prot = false;
	-- hero can be protector one day! @see [175] mocking armor
	if pside[cc.side][T_HERO][1].protector == true then
		return false;  -- hero protect this
	end

	for k, v in ipairs(tb) do 
		if v.protector == true then 
			return false; -- a protector exists
		end
	end

	-- implicit:  no protector find, so we can target
	return true;
end

-- check whether a card can be target
-- defensive approach: early exit for false case,  true at last
-- stealth : cannot be targeted (normal attack == true)
-- hidden : cannot be targeted (normal attack, ability))
-- in ally : if there is an protector in the ally, and it is not protector
--			 cannot be targeted
-- ability==true:   for ability
-- ability==false or nil: for attack
-- for attack: same side:false   oppo side:delayed true
function check_target(cc, pside, side_id, ability)
	-- general case:  for both attack and ability
	-- 1. nil case
	-- 2. hidden
	-- 3. protected by protector

	-- 1. nil case
	if cc==nil then
		return false, 'nil_target';
	end

	-- 2. hidden, and not in the same side
	if cc.hidden==true and side_id~=cc.side then
		-- print('DEBUG: check_target hidden=true');
		return false, 'hidden';
	end


	-- 3. protector
	if false == check_target_protector(cc, pside, side_id) then
		return false, 'protector';
	end


	-- attack case: same side,  stealth : false
	if ability ~= true then
		-- stealth
		if cc.stealth==true then
			return false, 'stealth';
		end

		-- cannot attack same side
		if side_id==cc.side then
			return false, 'same_side';
		end
	end

	-- check all support for stealth and hidden
	for k, sc in ipairs(pside[cc.side][T_SUPPORT] or {}) do
		--[[
		if ability ~= true and sc.stealth==true and cc.ctype ~= HERO then
			-- print('DEBUG: support STEALTH id=' .. sc.id .. ' ' .. sc.name);
			return false; -- cannot be target
		end
		]]--

		if sc.hidden==true and side_id~=sc.side then
			-- print('DEBUG: support HIDDEN id=' .. sc.id .. ' ' .. sc.name);
			return false; -- cannot be target
		end
	end

	return true; -- finally we can target it!
end

-- return true if the actual target (at) is valid
function check_actual_target(at, actual_target_list, target, side_id)
	if true==target.optional and nil==at then
		-- ok to accept nil for optional target
		return true;
	end

	if nil==at then
		return false;
	end

	if side_id~=1 and side_id~=2 then
		print('BUGBUG check_actual_target side_id is not 1, 2 : ' .. side_id);
		return false;
	end

	-- TODO check duplicate actual target, in other code, not here

	-- core logic:
	-- 1. atside = floor(at / 1000): need to check with target.side
	-- 2. at.table: check with target.table_list
	-- 3. at.pos : not check, since we already check the actual card

	local atside; -- integer
	local attable;  -- table name (not index)
	local atpos;  -- pos integer
	local index;  -- temp use
	local attach_index = nil; -- attach index 
	if at >= 10000 then -- means at is an attach index
		attach_index = at % 10;
		at = math.floor(at / 10);
	end
	atside = math.floor(at / 1000);
	if atside<1 or atside>2 then
		print('ERROR check_actual_target side error:' .. atside);
		return false;
	end

	attable = math.floor((at % 1000) / 100) ;
	if attable == nil then
		print('ERROR check_actual_target table index error:'.. index);
		return false;
	end

	atpos = at % 100; 
	-- (at - atside * 1000) - (index - 1) * 100;
	if atpos < 0 or atpos > 300 then -- 300 is magical
		print('ERROR check_actual_target pos error: ' .. atpos);
		return false;
	end


	-- logic 1. at.side and target.side check
	-- target.side = 1(my)  side_id=1 : at.side = 1
	-- target.side = 2(yr)  side_id=1 : at.side = 2 
	-- target.side = 3(both) side_id=1 : at.side = 1 or 2
	-- target.side = 3(both) side_id=2 : at.side = 1 or 2
	if target.side==1 and side_id~=atside then  
		print('ERROR: target.side=1 but side_id not match with atside');
		return false;
	end
	if target.side==2 and side_id==atside then
		print('ERROR: target.side=2 but atside is same as side_id');
		return false;
	end
	-- assume target.side=3 here
	-- something wrong here
	if atside~=1 and atside~=2 then
		print('ERROR: at.side is not 1,2 : ' .. at.side);
		return false;
	end

	-- exception: if it is T_ATTACH and attach_index ~= nil, OK

	-- logic 2: at.table
	for k, v in ipairs(target.table_list) do
		-- fix:  v may be weapon or armor, it is classified as support
		if v == T_WEAPON or v==T_ARMOR then
			v = T_SUPPORT;
		end
		if T_ATTACH==v and attach_index ~= nil then
			print('DEBUG: T_ATTACH yes attach_index=', attach_index);
			return true; -- early exit
		end
		if attable==v then 
			-- print('GOOD: table exist in table_list');
			return true; -- early exit
		end
	end
	print('ERROR attable not found in target.table_list : ' .. attable);
	return false;  -- not exist in table_list
end

-- return false if the at is duplicate inside actual_target_list
function check_duplicate_target( actual_target_list, at )
	for k, v in ipairs(actual_target_list) do
		if v == at then
			-- duplicate
			return false;
		end
	end
	return true;
end

-- return false if not valid
function check_actual_target_list( actual_target_list, target_list, side_id )
	if target_list==nil or #target_list==0 then
		if actual_target_list==nil or #actual_target_list==0 then
			return true;
		end
		return false; -- atl is more than target_list
	end

	-- implicit:  #target_list > 0

	actual_target_list = actual_target_list or {};
	-- same: if atl == nil then atl = {} end

	if #actual_target_list > #target_list then
		print('ERROR: actual target list > target_list');
		return false;
	end

	-- e.g. #target_list = 5,  #actual_target_list=3
	-- need to check target_list[4].optional is true
	if #target_list > #actual_target_list then
		local opt = target_list[#actual_target_list + 1].optional;
		if not opt then
		-- if opt==nil or opt==false then
			print('ERROR: extra target is not optional');
			return false;
		end
	end

	local count = 0; -- error count
	for k, at in ipairs(actual_target_list) do
		local t = target_list[k];
		local flag;
		flag = check_actual_target(at, actual_target_list, t, side_id);
		if flag == false then 
			count = count + 1;
		end
	end
	if count > 0 then
		return false;
	else 
		return true;
	end
end


-- from index to table name (string)
function index_table(index)
	local tbindex;
	index = tonumber(index) or 9999;
	-- avoid attach 
	if index >= 10000 then 
		index = math.floor(index / 10);
	end
	tbindex = math.floor((index % 1000) / 100);
	if tbindex < 1 or tbindex > #table_name_map then
		print('BUG: index_table, not found : ', index);
	end
	return table_name_map[tbindex];
end

function index_table_num(index)
	local tbindex;
	index = tonumber(index) or 9999;
	-- avoid attach 
	if index >= 10000 then 
		index = math.floor(index / 10);
	end
	tbindex = math.floor((index % 1000) / 100);
	return tbindex;
end

function index_side(index)
	local s;
	-- avoid attach 
	if index >= 10000 then 
		index = math.floor(index / 10);
	end

	s = math.floor( index / 1000);
	if s < 1 or s > 2 then 
		print('BUGBUG index_side out of range: ' , index);
	end
	return s;
end

function index_offset(index)
	-- avoid attach 
	if index >= 10000 then 
		index = math.floor(index / 10);
	end
	return index % 100;
end

-- return s, t, p  (side, table, pos) in numeric form
function index_stp(index)
	local s, t, p;
	local attpos = nil;
	if index >= 10000 then 
		attpos = index % 10;
		index = math.floor(index / 10);
	end
	s = math.floor(index / 1000);
	t = math.floor((index % 1000) / 100);
	p = math.floor(index % 100);
	return s, t, p, attpos;
end


-- index = 4 digits (5 for attach) number to identify a card position
-- hand (offset+0)  ally (offset+100)    support(offset+200)   grave(offset+300)
-- index to card
-- @return the card obj
function index_card(index, pside) 
	if tonumber(index)==nil then 
		return nil;
	end
	local attpos = 0;
	if index >= 10000 then
		attpos = index % 10;
		index = math.floor(index / 10);
	end
	-- BUG TODO attpos handling, need to check index 5 digits later
	local s = math.floor(index / 1000);
	local t = math.floor((index % 1000) / 100);
	local p = math.floor(index % 100);

	if (s<1) or (s>2) then
		print('ERROR index_card s invalid s,index: ', s, index);
		return nil;
	end

	-- T_RES is virtual, so cannot get the card
	if (t<T_HERO) or (t>T_DECK) then
		print('ERROR index_card t invalid t,index: ' , t, index);
		return nil;
	end


	if p < 1 or p > #pside[s][t] then
		print('ERROR index_card p invalid p,index : ', p, index);
		print('ERROR traceback: ' .. debug.traceback() .. '\n');
		return nil;
	end

	local cc = pside[s][t][p];
	if cc==nil then
		print('ERROR index_card cc=nil s,t,p=', s, t, p);
		return nil;
	end
	if attpos > 0 then
		local attlist = cc.attach_list or {};
		cc = attlist[attpos];
		-- cc = (cc.attach_list or {})[attpos];
		if cc == nil then
			print('ERROR index_card cc=nil s,t,p,attpos=', s, t, p, attpos);
			return nil;
		end
	end
	return cc;
end

-- convert a list of index to list of card object
-- return g_card_list, error_num
-- where error_num = 0 means OK, error_num > 0 means there are
-- error_num index that cannot convert to card object
-- g_card_list[i] may be nil if it cannot convert
function index_card_list(index_list, pside) 
	local cc;
	local clist = {};
	local error_num = 0;
	for i=1, #index_list do
		cc = index_card(index_list[i], pside);
		if cc == nil then 
			error_num = error_num + 1;
		end
		clist[i] = cc;
	end
	return clist, error_num;
end


-- s : side
-- t : table name
-- p : position  (for hero, p=0)
function card_index(s, t, p, attpos)
	local index;
	local table_offset;
	-- attpos can be nil
	if s==nil or t==nil or p==nil then
		print('ERROR card_index nil s:', s, 't:', t, 'p:', p, 'attpos:', attpos);
		return 0;
	end

 	if s<1 or s>2 then
		print('ERROR card_index s invalid');
		return 0;
	end

	index = s * 1000 + t * 100 + p;

	-- for attachment
	if nil ~= tonumber(attpos) then
		index = index * 10 + tonumber(attpos);
		-- print('DEBUG card_index:', index,  attpos);
	end
	return index;
end


-- from a card, return the index 
function cindex(cc) 
	return card_index(cc.side, cc.table, cc.pos, cc.attpos);
end


----- CARD FUNCTION END


-- table order:
-- deck < hand < ally < support < grave
-- order is according to the flow of card


----------- EFFECT START -----------
function eff_energy_offset(offset, s)
	return {'energy', offset=offset, side=s};
end

-- obsolete?
function eff_energy_value(value, s)
	return {'energy_value', value=value, side=s};
end

function eff_resource_offset(offset, s)
	return {'resource', offset=offset, side=s};
end

function eff_resource_value(value, s)
	return {'resource_value', value=value, side=s};
end

function eff_resource_max_offset(offset, s)
	return {'resource_max', offset=offset, side=s};
end


----- card move related
function eff_add(id, index) -- obsolete ?
	return {'add', id=id, index=index};
end

function eff_remove(index)  -- obsolete ?
	return {'remove', index=index};
end

function eff_move(index, target_index)
	return {'move', src_index=index, target_index=target_index};
end

function eff_view_top(index)
	return {'view_top', index=index};
end

function eff_hide_top(index)
	return {'hide_top', index=index};
end

function eff_view_oppo(index)
	return {'view_oppo', index=index};
end

function eff_hide_oppo(index)
	return {'hide_oppo', index=index};
end

-- index = target card to attach ( Puwen )
-- attach = attach card index(Extra Sharp), for virtual card set 0
function eff_attach(index, attach, id, acid)
	return {'attach', target_index=index, attach=attach, id=id, acid=acid};
end

function eff_card(index, id) -- for display only
	return {'card', index=index, id=id};
end

function eff_trap(index, id) -- for display only
	return {'trap', index=index, id=id};
end

function eff_anim(index, id, atype, target_list) -- for display only
	local eff = {'anim', index=index, id=id, atype=atype, total=#target_list};
	for i=1,#target_list do
		eff['target' .. i] = target_list[i];
	end
	return eff;
end

function eff_damage(src_index, target_index, dtype, atype, power)
	return {'damage', src_index=src_index, target_index=target_index, 
		dtype=dtype, atype=atype, power=power};
end

function eff_fight_start(src_index, target_index)
	return {'fight_start', src_index=src_index, target_index=target_index};
end

function eff_fight_end()
	return {'fight_end'};
end

function eff_hp(index, offset)
	return {'hp', index=index, offset=offset};
end

function eff_power_offset(index, offset)
	return {'power', index=index, offset=offset};
end

function eff_win(s)
	return {'win', side=s};
end

function eff_phase(phase)
	return {'phase', value=phase};
end


----------- EFFECT END -----------


function card_turn_start(cc, pside, sss)
	local eff_list = {};
	local eff_list2;
	if cc == nil then
		return eff_list;
	end
	if cc.trigger_turn_start ~= nil then
		eff_list2 = cc:trigger_turn_start(pside, sss);
		table_append(eff_list, eff_list2);
	end

	for k, ac in ipairs(cc.attach_list or {}) do
		if ac.trigger_turn_start ~= nil then
			eff_list2 = ac:trigger_turn_start(pside, sss);
			table_append(eff_list, eff_list2);
		end
	end
	return eff_list;
end

-- loop all trigger_turn_start
function loop_trigger_turn_start(pside, sss)
	local eff_list = {};
	local eff_list2;
	-- note: it may benefit pside[1]
	for _, oneside in ipairs(pside) do
		--[[	
		for k, v in ipairs(oneside[T_ALLY]) do
			eff_list2 = v:trigger_turn_start(pside, sss);
			table_append(eff_list, eff_list2);
		end
		for k, v in ipairs(oneside[T_SUPPORT]) do
			eff_list2 = v:trigger_turn_start(pside, sss);
			table_append(eff_list, eff_list2);
		end
		]]--
		for i=#oneside[T_ALLY], 1, -1 do
			local cc = oneside[T_ALLY][i];

			eff_list2 = card_turn_start(cc, pside, sss);
			table_append(eff_list, eff_list2);

		end
		for i=#oneside[T_SUPPORT], 1, -1 do
			local cc = oneside[T_SUPPORT][i];
			eff_list2 = card_turn_start(cc, pside, sss);
			table_append(eff_list, eff_list2);

		end
		eff_list2 = card_turn_start(oneside[T_HERO][1], pside, sss);
		table_append(eff_list, eff_list2);
	end
	return eff_list;
end


function loop_trigger_other_remove(target, pside, target_table)
-- side_src, side_target, src, target, target_table)
	local eff_list = {};
	local eff_list2 = {};
	local trigger;

	-- loop all cards in ally and support for trigger_other_remove
	-- TODO consider attach in ally?
	-- TODO loop T_HERO
	for _, oneside in ipairs(pside) do 
		for __, t in ipairs({T_ALLY, T_SUPPORT}) do
			for k, v in ipairs(oneside[t]) do
				trigger = v.trigger_other_remove;
				if trigger ~= nil then
					eff_list2 = trigger(v, target, pside, target_table);
					table_append(eff_list, eff_list2);
				end
			end
		end
	end
	return eff_list;
end

function loop_trigger_other_add(target, pside, target_table)
	local eff_list = {};
	local eff_list2 = {};
	local trigger;

	-- loop all cards in ally and support for trigger_other_add
	-- TODO consider attach in ally?
	for _, oneside in ipairs(pside) do 
		for __, t in ipairs({T_HERO, T_ALLY, T_SUPPORT}) do
			for k, c in ipairs(oneside[t]) do
				trigger = c.trigger_other_add;
				if trigger ~= nil then
					eff_list2 = trigger(c, target, pside, target_table);
					table_append(eff_list, eff_list2);
				end
				
				-- note: attach isnot consider

			end
			
		end
	end

	-- for trap, Death Trap(85), Net Trap(89)
	for _, oneside in ipairs(pside) do 
		for __, t in ipairs({T_SUPPORT}) do
			for k, c in ipairs(oneside[t]) do
				trigger = c.trigger_trap;
				if trigger ~= nil and true ~= c.has_actived then
					eff_list2 = trigger(c, target, pside, target_table);
					table_append(eff_list, eff_list2);
					-- only 1 trap can active in one time
					break;
				end
			end
		end
	end
	return eff_list;
end

-- loop all trigger_damage
-- target is defender, src is attacker
function loop_trigger_other_damage(target, pside, src, power, damage_type)
	local eff_list = {};
	local eff_list2;
	local trigger;

	for _, oneside in ipairs(pside) do  -- {
		for __, t in ipairs({T_HERO, T_ALLY, T_SUPPORT}) do -- {
			for k,c in ipairs(oneside[t]) do
				trigger = c.trigger_other_damage; --
				if trigger ~= nil then
					eff_list2 = trigger(c, target, pside, src, power, damage_type);
					table_append(eff_list, eff_list2);
				end
				for ___, ac in ipairs(c.attach_list or {}) do
					trigger = ac.trigger_other_damage; --
					if nil ~= trigger then
						eff_list2 = trigger(ac, target, pside, src, power, damage_type);
						table_append(eff_list, eff_list2);
					end
				end
			end
		end -- } for table T_HERO, T_ALLY, T_SUPPORT
	end -- }  for oneside
	return eff_list;
end

-- target = dier src = killer
function loop_trigger_other_kill(target, pside, src, old_side)
	local eff_list = {};
	local eff_list2 = {};
	local trigger;

	-- TODO consider attach in ally?
	for _, oneside in ipairs(pside) do  -- {
		for __, t in ipairs({T_HERO, T_ALLY, T_SUPPORT}) do -- {
			for k,c in ipairs(oneside[t]) do
				trigger = c.trigger_other_kill; --
				if trigger ~= nil then
					eff_list2 = trigger(c, target, src, pside, old_side);--
					table_append(eff_list, eff_list2);
				end
				for ___, ac in ipairs(c.attach_list or {}) do
					trigger = ac.trigger_other_kill; --
					if nil ~= trigger then
						eff_list2 = trigger(ac, target, src, pside, old_side);--
						table_append(eff_list, eff_list2);
					end
				end
			end
		end -- } for table T_HERO, T_ALLY, T_SUPPORT
	end -- }  for oneside
	return eff_list;
end

------  ACTION START

function action_heal(target, pside, src, power) -- {
	local offset;
	local eff_list = {};
	if target.hp <= 0 then -- Supernova&Rampage
		return eff_list;
	end

	local old_power = target.power;

	offset = target:heal_hp(power);
	-- noneed to send eff?
	if offset == 0 then
		return eff_list;
	end

	if (src.atype ~= nil) then
		local target_list = {};
		target_list[1] = target:index();
		-- all action_heal anim use A_ZHANNA
		eff_list[#eff_list+1] = eff_anim(src:index(), src.id, A_ZHANNA --src.atype
		, target_list);
	end

	-- core
	eff_list[#eff_list + 1] = eff_hp(cindex(target), offset);

	-- power may effect by bloodstone(146)
	if target.power ~= old_power then
		eff_list[#eff_list+1] = eff_power_offset(target:index(), target.power - old_power);
	end
	return eff_list;
end -- action_heal }

-- TODO damage_type is normal, electrical, fire, ice etc.
function action_damage(target, pside, src, power, damage_type, animation_type) -- {
-- param was:  side_src, side_target, src, target, power, damage_type)
	local eff_list = {}
	local eff;
	
	if target==nil then
		print('BUG: action_damage target=nil');
		return {}, 0;
	end

	if power == nil then
		print('BUG: action_damage power=nil');
		return {}, 0;
	end

	if damage_type == nil then 
		print('WARN: action_damage nil damage_type');
		damage_type = 1;  -- default to normal
	end

	if target:die() then
		print('WARN damage target is dead, no logic');
		return {}, 0;
	end

	local old_power = target.power;

	-- sandworm, kurt whitehelm etc 
	-- it is always non-nil @see card_class 
	power = target:trigger_calculate_defend(pside, src, power, damage_type);

	power = -target:change_hp(-power); -- core logic : syntactic sugar for self

	if animation_type == nil then
		animation_type = 0;
	end

	eff = eff_damage(cindex(src), cindex(target), damage_type, animation_type, power);
	-- eff = {'damage', src, target, dtype=damage_type, power=power};
	eff_list[1] = eff;

	-- power may effect by bloodstone(146)
	if target.power ~= old_power then
		eff_list[#eff_list+1] = eff_power_offset(target:index(), target.power - old_power);
	end

	if target.trigger_damage ~= nil then
		local eff_list2 = target:trigger_damage(pside, src, power, damage_type);
		table_append(eff_list, eff_list2);
	end

	local eff_list2 = loop_trigger_other_damage(target, pside, src, power, damage_type);
	table_append(eff_list, eff_list2);

	-- TODO do trigger_attack , trigger_defend here
	if damage_type == D_NORMAL then
	end

	-- fixed : do not check target.hp for die or not die!!
	if target:die()==false then
		return eff_list, power; 
	end
	
	-- implicit: target is dead
	-- someone die, either hero or ally
	eff_list2 = action_die(target, pside, src, damage_type);
	table_append(eff_list, eff_list2);

	-- eff_list2 = action_after_die(target, pside, src);
	-- table_append(eff_list, eff_list2);

	return eff_list, power;
end -- action_damage }



function action_damage_list(target_list, pside, src, power, dtype, atype)

	local eff_list = {};
	local eff_list2 = {};
	target_list = sort_damage_list(target_list);
	
	for k, target in ipairs(target_list) do
		-- note on self.ab_power, self.dtype
		eff_list2 = action_damage(target, pside, src, 
			power, dtype, atype);
		table_append(eff_list, eff_list2);
	end

	return eff_list;
end


--[[
-- src = attacker, target = defender, was action_attack()
-- obsolete
function action_attack_normal(target, pside, src) -- {

	local eff_list = {}
	local eff_list2 = {}
	local eff;
	local p;
	local attack_dtype;
	local die = false; -- true or false

	-- peter: for (target.defender==true and src.ambush ~= true) 
	--        swap(src, target)

	-- note: must have fight end!!!
	-- eff_list[1] = eff_fight_start(src:index(), target:index());

	-- attacker attack:
	-- damage_type = 1 : normal attack
	p = src.power;
	attack_dtype = src.attack_dtype;
	if src.trigger_calculate_attack ~= nil then
		p = src.trigger_calculate_attack(src, pside, target, p, attack_dtype);
	end 

	-- add by kelton
	-- Hellsteed
	-- if Hellsteed is in ally , first other ally who attack
	--  opppsite should + 1 attack
	for k, v in ipairs(pside[src.side][T_ALLY]) do
		if nil ~= v.trigger_calculate_other_attack and src ~= v then
			-- just change the value ,
			-- did not change the card power
			p = v:trigger_calculate_other_attack(src, p);
			--eff_list2, p = v:trigger_other_attack(src, p);
			--table_append(eff_list, eff_list2);
		end
	end

	-- for Bloodstone(146)
	for k, v in ipairs(pside[src.side][T_SUPPORT]) do
		if nil ~= v.trigger_calculate_other_attack then
			p = v:trigger_calculate_other_attack(src, p);
		end
	end
	----------

	set_use_attack(src); 


	-- note order is important
	-- src : hero with Dagger of Unmaking(200), action_damage should before src:trigger_attack

	-- note: trigger_calculate_defend is now moved to action_damage
	eff_list2, p = action_damage(target, pside, src, p, attack_dtype);
	table_append(eff_list, eff_list2);
	-- side_src, side_target, src, target, p, 1);

	-- attacker trigger_attack
	if src.trigger_attack ~= nil then 
		eff_list2 = src:trigger_attack(pside, target, p);
		table_append(eff_list, eff_list2);
	end
	
	if target.trigger_defend ~= nil then
		eff_list2 = target:trigger_defend(pside, src, p);
		table_append(eff_list, eff_list2);
	end

	---- check: whether defender need to defend attack

	if target:die() then
		print('DEBUG target die, no defend');
		return eff_list;
	end

	if src.ambush then
		-- print('DEBUG src has ambush, no defend');
		return eff_list;
	end

	if check_ready_defend(target) ~= true then
		-- print('DEBUG target no defend');
		return eff_list;
	end
		
--	if target.no_defend then
--		print('DEBUG target no defend');
--		return eff_list;
--	end

	-- note : if opposing has Earthen Protector(39), and target was killed, Protector  use trigger_other_kill(), the target will not die, but target cannot defend!!!
	if target.ready < 2 then -- when Earthen Protector save target, target set_not_ready
		set_ready(target);
		return eff_list;
	end
--	if check_ready_attack(target) ~= true then
--		if target.ready < 2 then -- for Earthen Protector skill
--			set_ready(target);
--		end
--		return eff_list;
--	end



	-- we cannot use hp == 0 because when the card is in grave,
	-- hp is refresh to normal
	
	--
	--if target:die() then
--		die = true; -- this may be useful later
--		print('DEBUG target die new, no defend');
--
--		return eff_list;
	--end
	--



	-------------------------------------------------------------
	------------------ defender attack start here ---------------
	-------------------------------------------------------------

	-- defender attack:
	p = target.power;
	attack_dtype = target.attack_dtype;
	if target.trigger_calculate_attack ~= nil then
		p = target.trigger_calculate_attack(target, pside, src, p, attack_dtype);
		-- side_src, side_target, src, target, p);
	end 
	-- note: calculate_defend is moved to action_damage

	-- for Bloodstone(146)
	for k, v in ipairs(pside[target.side][T_SUPPORT]) do
		if nil ~= v.trigger_calculate_other_attack then
			p = v:trigger_calculate_other_attack(target, p);
		end
	end

	-- eff_list2 = action_damage(side_target, side_src, target, src, p, 1);
	eff_list2, p = action_damage(src, pside, target, p, attack_dtype);
	table_append(eff_list, eff_list2);

	-- defender trigger_attack
	if target.trigger_attack ~= nil then 
		eff_list2 = target:trigger_attack(pside, src, p);
		table_append(eff_list, eff_list2);
	end

	-- defender trigger_defend
	if src.trigger_defend ~= nil then
		eff_list2 = src:trigger_defend(pside, target, p);
		table_append(eff_list, eff_list2);
	end


	-- eff_list[ #eff_list + 1] = eff_fight_end();
	return eff_list;
end -- action_attack_normal }
]]--



--[[
-- src = defender, target = attacker, in this case defender attack first
function action_attack_defender(target, pside, src)

	local eff_list = {}
	local eff_list2 = {}
	local eff;
	local p;
	local attack_dtype;
	local die = false; -- true or false

	-- peter: for (target.defender==true and src.ambush ~= true) 
	--        swap(src, target)

	-- note: must have fight end!!!
	-- eff_list[1] = eff_fight_start(src:index(), target:index());

	-- attacker attack:
	-- damage_type = 1 : normal attack
	p = src.power;
	attack_dtype = src.attack_dtype;
	if src.trigger_calculate_attack ~= nil then
		p = src.trigger_calculate_attack(src, pside, target, p, attack_dtype);
	end 

	-- for Bloodstone(146)
	for k, v in ipairs(pside[src.side][T_SUPPORT]) do
		if nil ~= v.trigger_calculate_other_attack then
			p = v:trigger_calculate_other_attack(src, p);
		end
	end
	----------

	-- note order is important
	-- src : hero with Dagger of Unmaking(200), action_damage should before src:trigger_attack

	-- note: trigger_calculate_defend is now moved to action_damage
	-- XXX Earthen Protector(39) will save the die attacker, and attacker is alive, it will affect by defender.trigger_attack
	eff_list2, p = action_damage(target, pside, src, p, attack_dtype);
	table_append(eff_list, eff_list2);
	-- side_src, side_target, src, target, p, 1);

	-- attacker trigger_attack
	if src.trigger_attack ~= nil then 
		eff_list2 = src:trigger_attack(pside, target, p);
		table_append(eff_list, eff_list2);
	end
	
	if target.trigger_defend ~= nil then
		eff_list2 = target:trigger_defend(pside, src, p);
		table_append(eff_list, eff_list2);
	end

	---- check: whether defender need to defend attack

	if src.ambush then
		print('DEBUG src has ambush, no defend');
		return eff_list;
	end

	-- note : if opposing has Earthen Protector(39), and target was killed, Protector  use trigger_other_kill(), the target will not die, but target cannot defend!!!
	if target.no_attack == true then
		-- when Earthen Protector save target, target set_not_ready
		print('DEBUG target no_attack');
		return eff_list;
	end

	if target:die() then
		print('DEBUG target target.die()');
		return eff_list;
	end
		
--	if check_ready_attack(target) ~= true then
--		if target.ready < 2 then -- for Earthen Protector skill
--			set_ready(target);
--		end
--		return eff_list;
--	end

	-------------------------------------------------------------
	------------------ attacker attack start here ---------------
	-------------------------------------------------------------

	-- attacker attack:
	p = target.power;
	attack_dtype = target.attack_dtype;
	if target.trigger_calculate_attack ~= nil then
		p = target.trigger_calculate_attack(target, pside, src, p, attack_dtype);
		-- side_src, side_target, src, target, p);
	end 
	-- note: calculate_defend is moved to action_damage

	-- for Hellsteed(51)
	for k, v in ipairs(pside[target.side][T_ALLY]) do
		if nil ~= v.trigger_calculate_other_attack and target ~= v then
			p = v:trigger_calculate_other_attack(target, p);
		end
	end

	-- for Bloodstone(146)
	for k, v in ipairs(pside[target.side][T_SUPPORT]) do
		if nil ~= v.trigger_calculate_other_attack then
			p = v:trigger_calculate_other_attack(target, p);
		end
	end

	set_use_attack(target); 

	-- eff_list2 = action_damage(side_target, side_src, target, src, p, 1);
	eff_list2, p = action_damage(src, pside, target, p, attack_dtype);
	table_append(eff_list, eff_list2);

	-- defender trigger_attack
	if target.trigger_attack ~= nil then 
		eff_list2 = target:trigger_attack(pside, src, p);
		table_append(eff_list, eff_list2);
	end

	-- defender trigger_defend
	if src.trigger_defend ~= nil then
		eff_list2 = src:trigger_defend(pside, target, p);
		table_append(eff_list, eff_list2);
	end


	-- eff_list[ #eff_list + 1] = eff_fight_end();
	return eff_list;
end -- action_attack }
]]--

-- src-> attacker
-- attack_flag -> attack(0) or defend(1)
function action_attack_one(target, pside, src, attack_flag)
	local eff_list = {}
	local eff_list2 = {}
	local eff;
	local p;
	local attack_dtype;

	-- attacker attack:
	-- damage_type = 1 : normal attack
	p = src.power;
	attack_dtype = src.attack_dtype;

	-- attack_flag for guardian oath(186), old iron dagger(196)
	if src.trigger_calculate_attack ~= nil then
		p = src.trigger_calculate_attack(src, pside, target, p, attack_dtype, attack_flag);
	end 

	for _, oneside in ipairs(pside) do 
		for __, tb in ipairs({T_ALLY, T_SUPPORT}) do
			for k, v in ipairs(oneside[tb]) do
				-- for Bloodstone(146), Hellsteed
				if nil ~= v.trigger_calculate_other_attack and src ~= v then
					-- just change the value ,
					p = v:trigger_calculate_other_attack(src, p);
				end
			end
		end
	end


	-- note order is important
	-- src : hero with Dagger of Unmaking(200), action_damage should before src:trigger_attack

	-- note: trigger_calculate_defend is now moved to action_damage
	eff_list2, p = action_damage(target, pside, src, p, attack_dtype, src.attack_anim);
	table_append(eff_list, eff_list2);
	-- side_src, side_target, src, target, p, 1);

	-- need check if target reborn by earthen protector(39)
	-- attacker trigger_attack
	if src.trigger_attack ~= nil then 
		eff_list2 = src:trigger_attack(pside, target, p, attack_flag);
		table_append(eff_list, eff_list2);
	end
	
	if target.trigger_defend ~= nil then
		eff_list2 = target:trigger_defend(pside, src, p);
		table_append(eff_list, eff_list2);
	end

	return eff_list;
end

function attack_finish(target, pside, src, sss)
end

-- add sss for Rapid Fire(84)
function action_attack(target, pside, src, sss)
	local eff_list = {}
	local eff_list2 = {}
	local attack_flag;

	src.stop_defend = nil;
	target.stop_defend = nil;
	-- reborn by earthen protector(39)
	src.reborn = false;
	target.reborn = false;

	-- attack_flag -> attack(0) or defend(1)
	if target.defender == true and src.ambush ~= true then
		if check_ready_defend(target) == true then
			attack_flag = 1;
			eff_list2 = action_attack_one(src, pside, target, attack_flag);
			table_append(eff_list, eff_list2);
		end

		if check_ready_attack(src) ~= true then
			return eff_list;
		end

		attack_flag = 0;
		eff_list2 = action_attack_one(target, pside, src, attack_flag);
		table_append(eff_list, eff_list2);
		-- check if attach Rapid Fire(84), src can attack 2 times
		set_use_attack(target, pside, src, sss); 
	else
		attack_flag = 0;
		eff_list2 = action_attack_one(target, pside, src, attack_flag);
		table_append(eff_list, eff_list2);

		-- check if attach Rapid Fire(84), src can attack 2 times
		set_use_attack(src, pside, src, sss); 

		-- XXX ? if target.ctype == HERO and target:die() then
		if src.ambush then
			return eff_list;
		end

		if check_ready_defend(target) ~= true then
			return eff_list;
		end

		attack_flag = 1;
		eff_list2 = action_attack_one(src, pside, target, attack_flag);
		table_append(eff_list, eff_list2);
	end

	return eff_list;
end

-- obsolete
--[[
function xxxaction_attack(target, pside, src) 

	local eff_list = {}

	if target.defender == true and src.ambush ~= true then
		print('DEBUG action_attack:target.defender==true');
		eff_list = action_attack_defender(src, pside, target);
		return eff_list;
	end

	eff_list = action_attack_normal(target, pside, src);
	return eff_list;

end
]]--

function action_power_offset(target, offset)
	local eff_list = {};

	-- check the diff of src.power, because of cripping blow
	-- real effect may not be +1
	local old_power = target.power;
	target.power_offset = target.power_offset or 0;
	target.power_offset = target.power_offset + offset;
	local new_power = target.power;
	eff_list[1] = eff_power_offset(target:index(), new_power - old_power);
	return eff_list;
end

-- for (60)ogloth  (40)aeon(skill), (39)earthern, (31)raven : base power
function action_power_change(target, pside, offset)
	local eff_list = {};
	local old_power = target.power;
	target:change_power(offset);
	local new_power = target.power;
	eff_list[1] = eff_power_offset(target:index(), new_power - old_power);
	-- hero power may change
	if target.ctype == WEAPON then
		local hh = pside[target.side][T_HERO][1];
		eff_list[2] = eff_power_offset(cindex(hh)
		, new_power - old_power);
	end
	return eff_list;
end

-- target = weapon/armor
function action_durability_change(target, pside, offset)

	local eff_list = {};
	local eff_list2;

	--[[
	if target.ctype ~= WEAPON and target.ctype ~= ARMOR then
		print('BUGBUG donnot use action_durability_change() if not weapon and armor');	
		return eff_list;
	end
	]]--

	local actual_offset = target:change_hp(offset);
	eff_list[#eff_list + 1] = eff_hp(cindex(target), actual_offset);
	
	if target.hp > 0 then
		return eff_list;
	end

	eff_list2 = action_grave(target, pside);
	table_append(eff_list, eff_list2);

	return eff_list;

end

-- src = attacker,   target=dier
function action_die(target, pside, src, damage_type)
-- was: side_src, side_target, src, target)
	local eff_list = {};
	local eff_list2 = {};
	local eff;
	local clone_target;
	local side_target;

	--[[
	if target.ctype==HERO then -- hero die
		local win_side = 3-target.side;
		-- TODO we should not stop the logic here, check after play_xxx
		eff = eff_win(win_side);
		eff_list[1] = eff;
		return eff_list;
	end
	]]--


	side_target = pside[target.side];

	if src.trigger_kill ~= nil then
		eff_list2 = src:trigger_kill(target, pside, damage_type);
		table_append(eff_list, eff_list2);
	end
	-- for Soul Seeker(185)
	if src.ctype == HERO then
		local supp_list = pside[src.side][T_SUPPORT];
		for i=1, #supp_list do
			local supp = supp_list[i];
			if nil ~= supp.trigger_kill then
				eff_list2 = supp:trigger_kill(target, pside, damage_type);
				table_append(eff_list, eff_list2);
			end
		end
	end


	-- Deathbone is be kill, Deathbone use trigger_die first, then Earthen Protector(39)before revive it
	if target.trigger_die ~= nil then 
		eff_list2 = target:trigger_die(pside, src);
		table_append(eff_list, eff_list2);
	end

	-- Earthen Protector(39) may use skill, target revive
	local old_side = target.side; -- save the side before enter grave
	if target:die() and target.ctype ~= HERO then
		eff_list2 = action_grave(target, pside);
		table_append(eff_list, eff_list2);
	end

	if target.ctype ~= HERO then
		eff_list2 = loop_trigger_other_kill(target, pside, src, old_side);
		table_append(eff_list, eff_list2);
	end

	return eff_list;
end

-- useless
-- src = attacker,   target=dier
function action_after_die(target, pside, src)
	local eff_list = {};
	local eff_list2;

	-- trigger heros attach list
	for _, oneside in ipairs(pside) do
		local h = oneside[T_HERO][1];
		local al = h.attach_list or {};
		print('DEBUG action_after_die hero, #al: ', h.name, #al);
		for i=1, #al do
			local ac = al[i];
			print('DEBUG action_after_die ac: ', ac.name);
			if nil ~= ac.trigger_hero_attach then
				print('DEBUG action_after_die trigger_hero_attach');
				eff_list2 = ac:trigger_hero_attach(src, target, pside);
				table_append(eff_list, eff_list2);
			end
		end
	end

	return eff_list;
	
end

-- when a ally is summoned, if it can use skill to a intended target, use this action
function action_cast_target(self, pside, atl)
	local eff_list = {};
	local eff_list2;

	local trigger = self.trigger_cast_target;

	if trigger ~= nil then
		eff_list2 = trigger(self, pside, atl);
		table_append(eff_list, eff_list2)
	end

	return eff_list;
end

function action_add(target, pside, target_table)
	local trigger = target.trigger_add;
	local eff_list = {};
	local eff_list2 = {};
	local eff;

	-- local index = get_solo_index(pside, target);
	card_add(target_table, target);
	-- solo_list_add(pside, target, index);
	eff = eff_add(target.id, cindex(target));
	eff_list[1] = eff;
	if  trigger ~= nil then 
		eff_list2 = trigger(target, pside, target_table);
		table_append(eff_list, eff_list2);
	end

	eff_list2 = loop_trigger_other_add(target, pside, target_table);
	table_append(eff_list, eff_list2);

	return eff_list;
end

		
function action_remove(target, pside, target_table)
	local trigger = target.trigger_remove;
	local eff_list = {};
	local eff_list2 = {};
	local index;
	-- order is important:  pos=target.pos will be changed after card_remove()
	index = cindex(target);
	eff_list[1] = eff_remove(index);
	-- solo_list_remove(pside, target);
	card_remove(target_table, target); -- core logic
	if  trigger ~= nil then 
		eff_list2 = trigger(target, pside, target_table);
		-- side_src, side_target, src, target, target_table);
		table_append(eff_list, eff_list2);
	end

	eff_list2 = loop_trigger_other_remove(target, pside, target_table);
	table_append(eff_list, eff_list2);
	return eff_list;
end


-- move target card from src_table to the top of target_table
-- e.g. [96] resurrection
function action_move_top(target, pside, src_table, target_table)
	local trigger;
	local eff;
	local eff_list = {};
	local eff_list2 = {};
	local index;
	local target_index;
	-- order is important:  target.pos will be changed after card_remove()
	index = cindex(target);
	eff = eff_move(index, nil);
	eff_list[1] = eff;
	-- local sp_index = get_solo_index(pside, target);
	-- solo_list_remove(pside, target);
	card_remove(src_table, target); -- core logic A
	trigger = target.trigger_remove;
	if trigger ~= nil then
		eff_list2 = trigger(target, pside, src_table);
		-- side_src, side_target, src, target, src_table);
		table_append(eff_list, eff_list2);
	end

	eff_list2 = loop_trigger_other_remove(target, pside, src_table);
	table_append(eff_list, eff_list2);

	------------------------------------------- 

	card_add_top(target_table, target); -- core logic B
	-- solo_list_add(pside, target, sp_index);
	-- update eff target after target add to new table
	eff.target_index = cindex(target); -- XXX need testing!!!!
	-- eff_list[1] = eff_move(index, cindex(target));

	trigger = target.trigger_add;
	if trigger ~= nil then 
		eff_list2 = trigger(target, pside, target_table);
		-- side_src, side_target, src, target, target_table);
		table_append(eff_list, eff_list2);
	end

	-- loop all cards in ally and support for trigger_other_add
	eff_list2 = loop_trigger_other_add(target, pside, target_table);
	-- side_src, side_target, src, target, target_table);
	table_append(eff_list, eff_list2);

	return eff_list;
end

-- move target card from src_table to target_table
function action_move(target, pside, src_table, target_table)
-- (side_src, side_target, src, target, src_table, target_table)
	local trigger;
	local eff;
	local eff_list = {};
	local eff_list2 = {};
	local index;
	local target_index;

	-- for hp/power change eff
	local old_power = 0;
	local new_power = 0;
	local hh; 
	local flag = false;

	-- if target is weapon, hero power may change
	if target.ctype == WEAPON then
		-- print('action_move:target.name = ', target.name);
		hh = pside[target.side][T_HERO][1];
		-- print('action_move:hh.name = ', hh.name);
		old_power = hh.power;
		-- print('action_move:old_power = ', old_power);
		flag = true;
	end

	-- order is important:  target.pos will be changed after card_remove()
	index = cindex(target);
	eff = eff_move(index, nil);
	-- {'move', index=index, target_index=nil};
	eff_list[1] = eff;
	-- local sp_index = get_solo_index(pside, target);
	-- solo_list_remove(pside, target);
	card_remove(src_table, target); -- core logic A
	trigger = target.trigger_remove;
	if trigger ~= nil then
		eff_list2 = trigger(target, pside, src_table);
		-- side_src, side_target, src, target, src_table);
		table_append(eff_list, eff_list2);
	end



	eff_list2 = loop_trigger_other_remove(target, pside, src_table);
	table_append(eff_list, eff_list2);

	------------------------------------------- 

	card_add(target_table, target); -- core logic B
	-- solo_list_add(pside, target, sp_index);
	-- update eff target after target add to new table
	eff.target_index = cindex(target); -- XXX need testing!!!!
	-- eff_list[1] = eff_move(index, cindex(target));

	trigger = target.trigger_add;
	if trigger ~= nil then 
		eff_list2 = trigger(target, pside, target_table);
		-- side_src, side_target, src, target, target_table);
		table_append(eff_list, eff_list2);
	end

	-- weapon change hero power
	if flag then
		-- print('action_move:hh.name = ', hh.name);
		new_power = hh.power;
		-- print('action_move:hh new power = ', new_power);
		-- print('-------------------');
		if old_power ~= new_power then
			eff_list[ #eff_list + 1 ] = eff_power_offset(cindex(hh), new_power-old_power);
		end
	end

	-- loop all cards in ally and support for trigger_other_add
	eff_list2 = loop_trigger_other_add(target, pside, target_table);
	-- side_src, side_target, src, target, target_table);
	table_append(eff_list, eff_list2);

	return eff_list;
end

function action_virtual_remove(target_list, pside, src)

	local eff_list = {};
	local eff_list2 = {};
	for k, v in ipairs(target_list) do
		local aclist = v.attach_list or {};
		for i = #aclist, 1, -1 do 
			local ac = aclist[i];
			if ac.src == src then
				eff_list2 = action_grave(ac, pside);
				table_append(eff_list, eff_list2);
			end
		end
	end

	return eff_list;
end

function action_virtual_attach(c, pside,index, src)
	local ac;
	local eff_list;
	local err;

	ac = clone(g_card_list[index]);  

	ac.src = src;
	eff_list, err = action_attach(c, pside, ac, src);

	return eff_list, err;

end

function action_attach(c, pside, ac, src) -- {
	local attach_index = 0; -- for virtual card
	local eff_list = {};
	local eff_list2 = {};
	if nil==ac or c==nil then
		print('BUGBUG action_attach ac==nil or c==nil');
		return nil, 'c_nil_ac_nil';
	end

	-- default src is the hero of the ac card : before attach
	if nil == src then
		src = pside[ac.side][T_HERO][1];
	end
	-- assert(nil ~= src);

	if #(src.attach_list or {}) >= 9 then 
		print('WARN over_9_attach');
		return nil, 'over_9_attach';
	end

	-- ac index must be recorded first before execution
	local hp_origin = c.hp;
	local power_origin = c.power;

	-- virtual attach: duplicate: replace
	-- normal attach: duplicate is INVALID
	local old_index = check_attach_duplicate(c, ac);
	if old_index > 0 then
		if ac.id < 1000 then 
			return nil, 'duplicate_attach';
		end
		-- new frozen timer >= old frozen timer
		-- implicit: this is virtual card, remove it and replace
		local old_ac = index_card(old_index, pside);
		eff_list2 = action_grave(old_ac, pside);
		table_append(eff_list, eff_list2);
	end

	if ac.id >= 1000 then
		attach_index = 0; -- virtual card from void
	else
		local tb;
		local p;
		attach_index = ac:index();  
		tb = pside[ac.side][ac.table];
		-- solo_list_remove(pside, ac);
		p = card_remove(tb, ac); -- remove from hand
		if p < 1 then 
			print('BUGBUG action_attach p=card_remove < 1 : ', p);
			-- continue to run
		end
	end


	if false==card_attach(c, ac) then
		return nil, 'card_attach=false';
	end
	ac.src = src;  -- core logic: for checking src of attachment

	-- TODO eff remove from hand  peter: is it still useful? client is ok?
	-- note: attach index must be before card_attach execution
	eff_list[ #eff_list + 1] = eff_attach(c:index(), attach_index, c.id, ac.id);
	
	-- eff for hp or power change
	if c.hp ~= hp_origin then
		eff_list[#eff_list + 1] = eff_hp(c:index(), (c.hp-hp_origin));
	end
	if c.power ~= power_origin then
		eff_list[#eff_list + 1] = eff_power_offset(c:index(), (c.power-power_origin));
	end


	-- some attach may have immediate effect, e.g. 152 drain power, 139
	-- then the trigger_skill will be called
	if ac.trigger_skill then
		-- trigger_skill(self, pside, atl) where self is the ac
		-- atl = { cindex(c) }  OR   atl = {c:index()}
		eff_list2 = ac:trigger_skill(pside, {cindex(c)}); -- atl need {id} list
		table_append(eff_list, eff_list2);
	end
	return eff_list;
end --  action_attach }


function action_refresh(cc, pside)
	local old_power, old_hp;
	local new_power, new_hp;
	local eff_list = {};
	local eff_list2 = {};

	-- no need to refresh virtual attach
	if cc.id > 1000 then 
		return {};
	end
	
	local list = cc.attach_list or {};
	for i=#list, 1, -1 do
		local ac = list[i];
		eff_list2 = action_grave(ac, pside);
		table_append(eff_list, eff_list2);
	end

	old_power, old_hp = cc.power, cc.hp;
	cc:refresh();
	new_power, new_hp = cc.power, cc.hp;

	if old_power ~= new_power then
		-- print('action_refresh:old_power, new_power = ', old_power, new_power);
		eff_list[ #eff_list + 1 ] = eff_power_offset(cindex(cc), new_power-old_power);
	end
	if old_hp ~= new_hp then
		-- print('action_refresh:old_hp, new_hp = ', old_hp, new_hp);
		eff_list[ #eff_list + 1 ] = eff_hp(cindex(cc), new_hp-old_hp);
	end
	return eff_list;
end

-- @see action_die, very similar
function action_grave(cc, pside)
	local eff_list = {};
	local eff_list2 = {};
	local eff;
	local clone_cc;
	local attpos = 0;
	local target_table;

	local fc; -- attached card
	local hh; -- hero card
	-- for hp/power change eff
	local old_power = 0;
	local new_power = 0;
	local old_hp = 0;
	local new_hp = 0;

	if nil == cc then 
		print('BUGBUG action_grave cc=nil');
		return nil;
	end
	
	target_table = pside[cc.side][cc.table];

	local src_index, target_index;
	-- TODO update eff spec
	-- cc:index() not work, but cindex(cc) works!
	src_index = cc:index();
	-- {'move', target={side=cc.side, table=cc.table, pos=cc.pos, 
	-- attpos=cc.attpos}, target_table='grave'};


	-- first put the attach card to grave
	-- if send power/hp eff here,  healing touch(91) action_grave the Cripping Blow(67), no power changeeff send
	if target_table.name==T_HERO or target_table.name==T_ALLY
	or target_table.name==T_SUPPORT then
		if cc.attach_list ~= nil and #cc.attach_list > 0 then
			-- peter: using ipairs is not safe for remove, use reverse forloop
			-- for k, ac in ipairs(cc.attach_list) do
			--[[
			old_power, old_hp = cc.power, cc.hp;
			]]--
			for i=#cc.attach_list, 1, -1 do
				local ac = cc.attach_list[i];
				-- print('DEBUG att_card grave  ac.side,table,pos,attpos:', 
				-- 	ac.side, ac.table, ac.pos, ac.attpos);
				eff_list2 = action_grave(ac, pside);
				table_append(eff_list, eff_list2);
			end
			--[[
			new_power, new_hp = cc.power, cc.hp;
			if old_power ~= new_power then
				eff_list[ #eff_list + 1 ] = eff_power_offset(cindex(cc), new_power-old_power);
			end
			if old_hp ~= new_hp then
				eff_list[ #eff_list + 1 ] = eff_hp(cindex(cc), new_hp-old_hp);
			end
			]]--
		end

		
		-- every attach card grave, send its attached card power or hp change eff
		-- only attach card remove can change its attached card power or hp
		if cc.ctype == ATTACH then
			local index = cindex(cc, pside);
			local findex = math.floor(index / 10);
			-- its attached card, father card
			fc = index_card(findex, pside);
			old_power, old_hp = fc.power, fc.hp;
		end

		if cc.ctype == WEAPON then
			hh = pside[cc.side][T_HERO][1];
			old_power= hh.power;
		end
	end

	-- solo_list_remove(pside, cc);
	card_remove(pside[cc.side][cc.table], cc);

	-- for virtual attach, it is from void, no need to send to grave
	if cc.id >= 1000 then
		-- note: cc.home is always non-nil, @see __index card_class.get_home 
		target_index = 0; -- target_index = 0 means void, not go to grave
		-- note: eff already in eff_list
	else
		-- clone_cc = clone(g_card_list[cc.id]);
		-- card_refresh(cc); -- peter: new implementation
		-- TODO card_refresh(cc) before remove and setup eff_list
		-- TODO check whether cc.home is nil
		card_add(pside[cc.safe_home][T_GRAVE], cc);
		target_index = cc:index(); 
	end

	eff_list[ #eff_list + 1] = eff_move(src_index, target_index);  

	-- TODO trigger_remove here?
	if  cc.trigger_remove ~= nil then 
		if cc.id == 100 then
			-- print('DEBUG action_grave book(100)');
		end
		-- TODO masha: pass old_side here
		eff_list2 = cc:trigger_remove(pside, target_table);
		-- side_src, side_target, src, target, target_table);
		table_append(eff_list, eff_list2);
	end

	eff_list2 = loop_trigger_other_remove(cc, pside, target_table);
	table_append(eff_list, eff_list2);

	-- refresh _after_ move grave
	eff_list2 = action_refresh(cc, pside);
	table_append(eff_list, eff_list2);
	-- TODO action_ready ?
	set_ready(cc); -- if card move to grave, ready=2

	if target_table.name==T_HERO or target_table.name==T_ALLY
	or target_table.name==T_SUPPORT then
		if cc.ctype == ATTACH then
			new_power, new_hp = fc.power, fc.hp;
			if old_power ~= new_power then
				eff_list[ #eff_list + 1 ] = eff_power_offset(cindex(fc), new_power-old_power);
			end
			if old_hp ~= new_hp then
				eff_list[ #eff_list + 1 ] = eff_hp(cindex(fc), new_hp-old_hp);
			end
		end

		if cc.ctype == WEAPON then
			new_power = hh.power;
			if old_power ~= new_power then
				eff_list[ #eff_list + 1 ] = eff_power_offset(cindex(hh), new_power-old_power);
			end
		end
	end

	--[[
	if  cc.trigger_remove ~= nil then 
		if cc.id == 100 then
			print('DEBUG action_grave book(100)');
		end
		eff_list2 = cc:trigger_remove(pside, target_table);
		-- side_src, side_target, src, target, target_table);
		table_append(eff_list, eff_list2);
	end

	eff_list2 = loop_trigger_other_remove(cc, pside, target_table);
	table_append(eff_list, eff_list2);
	]]--

	------------------------------
	
	return eff_list;
end


function action_timer(cc, pside)
	local eff_list = {};
	local eff_list2 = {};

	if cc.attach_list ~= nil then
		eff_list = action_timer_table(cc.attach_list, pside);
	end

	if cc.timer == nil or cc.timer <= 0 then
		return eff_list; -- no timer early exit
	end 

	cc.timer = cc.timer - 1;
	if cc.timer <= 0 then
		eff_list2 = action_grave(cc, pside);
		table_append(eff_list, eff_list2);
	end
	return eff_list;
end

function action_timer_table(tb, pside)
	local eff_list = {};
	local eff_list2 ;
	-- for k, v in ipairs(tb) do
	if tb == nil then
		return eff_list;
	end
	for k=#tb, 1, -1 do
		local v = tb[k];
		eff_list2 = action_timer(v, pside);
		table_append(eff_list, eff_list2);
	end
	return eff_list;
end

-- reduce timer by 1 if exists,  when timer is 0, move the card to grave
function action_timer_side(sss, pside)  -- TODO param order: pside, sss
	local eff_list = {};
	local eff_list2;

	-- note: reduce timer of the following card
	-- side[T_SUPPORT]
	-- side[T_ALLY] 
	-- side[T_ALLY] . attach_list
	-- side[T_HERO][1] . attach_list

	for i=1,2 do
		eff_list2 = action_timer_table(pside[i][T_SUPPORT], pside);
		table_append(eff_list, eff_list2);
		eff_list2 = action_timer_table(pside[i][T_ALLY], pside);
		table_append(eff_list, eff_list2);

		-- only 1 hero hard coded
		-- eff_list2 = action_timer_table(pside[i][T_HERO]); -- old code
		eff_list2 = action_timer(pside[i][T_HERO][1], pside);
		table_append(eff_list, eff_list2);
	end

	return eff_list;
end

-- sss -> deck side, offset -> hand card count offset, target_side -> hand side
function action_drawcard(pside, sss, offset, target_side)
	-- if target_side == nil then target_side = sss
	target_side = target_side or sss;


	local my = pside[sss];
	local target = pside[target_side];
	local eff_list = {};
	local eff_list2 = {};
	local cc;

	offset = offset or 0;

	if #target[T_HAND] + offset >= 7 then -- was my
		return eff_list;
	end

	if #my[T_DECK] >= 1 then
		cc = my[T_DECK][1];
		eff_list2 = action_move(cc, pside, my[T_DECK], target[T_HAND]);
		table_append(eff_list, eff_list2);
	else 
		-- print('DEBUG action_drawcard: no card, -1 hp');
		eff_list2 = action_damage(my[T_HERO][1], pside, my[T_HERO][1], 1, D_MAGIC);
		table_append(eff_list, eff_list2);
	end
	return eff_list;
end

-- obsolete : use action_sacrifice()
function action_phase(phase)
	if phase == nil then
		return nil, nil;
	end
	phase = nil;
	return eff_phase(g_phase), nil;  -- set phase = nil for play
end

function gate_hero_hide(pside, gate_list)
	if gate_list == nil then
		return {};
	end
	if #pside[1][T_ALLY] == 0 then
		pside[1][T_HERO][1].hidden = false;
	else 
		pside[1][T_HERO][1].hidden = true;
	end
end

function gate_turn_start(pside, sss, gate_list)
	-- print('gate_turn_start');
	
	if gate_list == nil then
		return {};
	end
	
	-- print('gate_list[2][1]=' ..  gate_list[2][1]);
	-- print('g_round=' .. g_round);
	-- print('#pside[1][T_ALLY]=' .. #pside[1][T_ALLY]);
	-- if player t_ally is empty, hero can be attack
	gate_hero_hide(pside, gate_list);
	--[[
	if #pside[1][T_ALLY] == 0 then
		pside[1][T_HERO][1].hidden = false;
	else 
		pside[1][T_HERO][1].hidden = true;
	end
	]]--

	local cc;
	local eff_list = {};
	local eff_list2;
	for k,v in ipairs(gate_list[g_round] or {}) do
		cc = g_card_list[v];  -- get the card from g_card_list template
		if cc == nil then 
			print ('BUGBUG gate_turn_start id=', v);
		else 
			cc = clone(cc); -- may need to add to deck first
			eff_list2 = action_add(cc, pside, pside[2][T_HAND]);
			-- print('DEBUG gate_add_card=' .. v);
			table_append(eff_list, eff_list2);
		end
	end

	return eff_list;
end

-- input: side,   sss(side_id)
-- output: eff_list, new_sss
-- usage:   
--        eff_list, g_current_side, g_phase = action_next(g_logic_table, g_current_side)
-- by following usage, caller wil update the global g_current_side
function action_next(pside, sss) 
	local my;
	local _side_your;
	local eff;
	local eff_list = {};
	local eff_list2 = {};
	local dtype;
	local draw_card;
	_side_your = pside[sss];
	card_all_ready(_side_your[T_ALLY]);
	card_all_ready(_side_your[T_SUPPORT]);
	set_ready(_side_your[T_HERO][1]);

	sss = 3 - sss; -- core logic
	my = pside[sss];  -- sss has change
	g_round = g_round + 1; -- only for client display
	eff_list = action_timer_side(sss, pside);  -- new side

	eff_list2 = loop_trigger_turn_start(pside, sss);
	table_append(eff_list, eff_list2);

	-- for gate game
	eff_list2 = gate_turn_start(pside, sss, g_gate);	
	table_append(eff_list, eff_list2);

	pside[sss].resource = pside[sss].resource_max;
	eff = eff_resource_value(pside[sss].resource_max, sss);
	eff_list[ #eff_list + 1] = eff;

	card_all_ready(my[T_ALLY]);
	card_all_ready(my[T_SUPPORT]);
	set_ready(my[T_HERO][1]);
--	-- peter: add energy
	eff_list2 = action_energy(1, pside, sss); 
	table_append(eff_list, eff_list2);

	-- TODO draw more cards by checking all support!
	draw_card = 1;  -- can be 2 or more, @see blood frenzy, bazaar

	for i=1,draw_card do
		eff_list2 = action_drawcard(pside, sss);
		table_append(eff_list, eff_list2);
	end
	return eff_list, sss, PHASE_SACRIFICE, nil;
end

function action_energy(offset, pside, sss)
	local hh = pside[sss][T_HERO][1];
	local max = pside[sss].energy_max or 99;

	-- print('==== action_energy  offset=', offset);
	-- range check and fix the offset
	if hh.energy + offset < 0 then
		offset = - hh.energy
	end
	if hh.energy + offset > max then
		offset = max - hh.energy;
	end

	hh.energy = hh.energy + offset;
	local eff_list = {};
	eff_list[1] = eff_energy_offset(offset, sss);
	return eff_list;
end -- end action_energy()

function action_resource(res, pside, sss)
	if (res or 0) == 0 then
		return {}; -- normal
	end
	if pside[sss].resource + res < 0 then
		print('WARN action_resource not enough res_cost, resource:',
			res, pside[sss].resource);
		res = - pside[sss].resource;
		-- do not early exit, let it reduce to 0
	end
	pside[sss].resource = pside[sss].resource + res;
	local eff_list = {};
	eff_list[1] = eff_resource_offset(res, sss);
	return eff_list;
end

-- input:  index=0 if we skip the sacrifice phase
--         side = g_logic_table,  s = g_current_side
--         phase = PHASE_SACRIFICE  (only accept this, nil means already sac)
-- output:  eff_list, new_phase, err
-- usage:
-- eff_list, g_phase, err = action_sacrifice(index, g_logic_table, g_current_side, g_phase)
function action_sacrifice(index, pside, sss, phase)
	local eff_list = {};
	local res_index;
	local tb;
	local cc;

	-- when phase already = nil, no sacrifice
	if PHASE_PLAY == phase then
		return nil, PHASE_PLAY, 'non_sac_phase';
	end

	
	-- 0 means skip, no sacrifice, consider: index==nil ?
	if 0 == index then
		return {}, PHASE_PLAY, nil;
	end

	cc = index_card(index, pside);
	if cc == nil then
		print('ERROR sac cc_nil');
		return nil, phase, 'cc_nil';  
	end

	
	if cc.side ~= sss then
		print('ERROR sac oppo card');
		return nil, phase, 'sac_oppo';
	end
	-- TODO check whether cc card is on hand ?
	if cc.table ~= T_HAND then
		return nil, phase, 'sac_non_hand';
	end

	res_index = card_index(sss, T_RES, 1); -- hard code 1
	tb = pside[sss][cc.table];
	-- solo_list_remove(pside, cc);
	card_remove(tb, cc);

	--[[
	local ret = check_solo_list();
	if ret ~= 0 then
		print('BUGBUG action_srcrifice:check_solo_list_bug ', ret);
	end
	]]

	pside[sss].resource_max = pside[sss].resource_max + 1;
	pside[sss].resource = pside[sss].resource + 1;
	if pside[sss].resource > pside[sss].resource_max then
		pside[sss].resource = pside[sss].resource_max;
	end

	eff_list[1] = eff_move(index, res_index);
	eff_list[2] = eff_resource_max_offset(1, sss);
	eff_list[3] = eff_resource_offset(1, sss);
	eff_list[4] = eff_phase(PHASE_PLAY);


	return eff_list, PHASE_PLAY, nil;
end


-- return -1: can be sacrifice
-- return 0:  no action available
-- return 1:  attack
-- return 2:  ability
-- return 3:  both  : attack + ability
function check_action(cc, pside, s)
	local ret = 0; 
	if g_phase == PHASE_SACRIFICE then
		if cc.table==T_HAND and cc.side==s then
			return -1;
		end
		return 0;
	end
	-- attack
	if true == check_playable(cc, pside, s, false) then
		ret = ret + 1;
	end

	-- ability
	if true == check_playable(cc, pside, s, true) then
		ret = ret + 2;
	end

	return ret;
end

function list_hand_index(pside, s)
	local oneside = pside[s];
	local index;
	local clist = {};
	local res = oneside.resource;
	
	for k, v in ipairs(oneside[T_HAND] or {}) do
		if v.cost <= res then
			clist[ #clist + 1] = cindex(v);
		end
	end
	return clist;
end

-- attack only has 1 target, so there is no need for atl as param
function list_attack_target(src_index, pside, sss)  -- { start
	local opposide = pside[3-sss];
	local list = {};
	local cc;

	-- 0 means not consider the src
	if src_index ~= 0 then
		cc = index_card(src_index, pside);
		if false==check_playable(cc, pside, sss, false) then
			return {}; -- cannot attack, return empty list
		end
	end

	-- check_target(..., false) means check attack
	if true == check_target(opposide[T_HERO][1], pside, sss, false) then
		list[#list + 1] = cindex(opposide[T_HERO][1]);
	end

	for k, v in ipairs(opposide[T_ALLY] or {}) do
		if true == check_target( v, pside, sss, false) then
			list[#list + 1] = cindex(v);
		end
	end

	return list;
end -- }

-- return a list of card which is available for attack
-- update: include the target list check
function list_attack_index(pside, s) -- start {
	local oneside = pside[s];
	local index;
	local clist = {}
	local flag;
	local target_list;

	-- check target first, if no target is valid, simply return empty
	target_list = list_attack_target(0, pside, s);
	if #target_list <= 0 then
		return {};
	end

	flag = check_playable(oneside[T_HERO][1], pside, s, false);
	if true == flag then
		index = cindex(oneside[T_HERO][1]);
		clist[#clist+1] = index;
	end

	for k, v in ipairs(oneside[T_ALLY] or {}) do
		flag = check_playable(v, pside, s, false);
		if true == flag then
			index = cindex(v);
			clist[#clist+1] = index;
		end
	end
	return clist;
end  -- end list_attack_index }

-- get the total target for the ability of index card
-- in case the card is on T_HAND and it is an ally or support,
-- it will return 0
-- usage: caller get a zero means execute the ability immediately with
--        atl = {}
-- return n>0, caller need to input n target
-- return nil for error case, usually with error msg (nil, 'error_msg')
function total_target(index, pside, sss)
	local cc;
	cc = index_card(index, pside);
	if cc == nil then
		return nil, 'card is nil';
	end

	if cc.target_list == nil or #cc.target_list == 0 then
		return 0;
	end

	if cc.table == T_HAND and (is_support(cc) or cc.ctype == ALLY) then
		return 0;
	end

	return #cc.target_list;
end


-- return total non-optional target
function total_must_target(index, pside, sss)  -- start {
	local cc;
	cc = index_card(index, pside);
	if cc == nil then
		return nil, 'card is nil';
	end

	if cc.target_list == nil or #cc.target_list == 0 then
		return 0;
	end

	if cc.table == T_HAND and (is_support(cc) or cc.ctype == ALLY) then
		return 0;
	end

	local ret = 0;

	-- loop all target_list, if optional = true, no ++
	-- note: we assume that target_list[i].optional = true means
	-- the rest of target_list[i + 1].optional must be true
	for i=1, #cc.target_list do
		local v = cc.target_list[i];
		if true == v.optional then
			break;
		end
		ret = ret + 1;
	end
	return ret;  
end -- end total_must_target }


-- TODO need to check duplicate
function list_target_side(target, pside, s, oneside, atl)
	local tb;
	local offset;
	local clist = {};
	local ally_flag = false;
	-- TODO check_actual_target
	for j=1, #target.table_list do -- {
		tb = target.table_list[j];
		if tb==T_WEAPON or tb==T_ARMOR then
			local ct;
			if tb==T_WEAPON then 
				ct = WEAPON;
			end
			if tb==T_ARMOR then
				ct = ARMOR;
			end
			for k, v in ipairs(oneside[T_SUPPORT] or {}) do 
				if v.ctype==ct then -- ct=51 or ct=52
					-- hard code support table, not tb, as tb=T_WEAPON
					clist[#clist+1] = cindex(v);
				end
			end
		elseif tb==T_ATTACH then
			local onplay_list = {};

			--[[
			local owner_list = {};
			for k, v in ipairs(oneside[T_HERO] or {}) do
				if true == check_target(v, pside, s, true) then
					owner_list[#owner_list + 1] = v;
				end
			end
			table_append(onplay_list, owner_list);

			owner_list = {};
			for k, v in ipairs(oneside[T_ALLY] or {}) do
				if true == check_target(v, pside, s, true) then
					owner_list[#owner_list + 1] = v;
				end
			end
			table_append(onplay_list, owner_list);
			]]--

			table_append(onplay_list, oneside[T_HERO]);
			table_append(onplay_list, oneside[T_ALLY]);
			table_append(onplay_list, oneside[T_SUPPORT]);
			for k, v in ipairs(onplay_list or {}) do 
				if true == check_target(v, pside, s, true) then
					for _, ac in ipairs(v.attach_list or {}) do
						clist[#clist+1] = cindex(ac);
					end
				end
			end
		else
			-- ally or hero, need to check_target
			ally_flag = (tb == T_ALLY) or (tb == T_HERO);
			for k, v in ipairs(oneside[tb] or {}) do
				if false == ally_flag 
				or true == check_target(v, pside, s, true) then
					clist[#clist+1] = cindex(v);
				end
			end
		end
	end -- } for j=1 end
	return clist;
end



-- return list of index of valid target for the ability card cc
-- atl is the actual target list selected (avoid duplicate usually)
-- num is the target number (start from 1) - check against cc.target_list[num]
-- @return clist = {2000, 2201, ...} list of index for valid target
function list_ability_target(src_index, pside, s, atl, num)
	local target;
	local myside = pside[s];
	local opposide = pside[3-s];
	local clist = {};
	local clist2;
	local cc = index_card(src_index, pside);

	if nil == cc then
		print('BUGBUG list_ability_target cc=nil');
		return {};
	end

	-- avoid nil target_list or out of range target_list
	if cc.target_list == nil or num > #cc.target_list then
		return {};  -- normal, nil target_list or empty
	end

	target = cc.target_list[num];

	-- TODO if cc.ctype==ATTACH, need to check the attach_list to avoid
	-- duplicate attachment

	-- my side
	if target.side==1 or target.side==3 then
		clist2 = list_target_side(target, pside, s, pside[s]);
		table_append(clist, clist2);
	end

	-- oppo side
	if target.side==2 or target.side==3 then
		clist2 = list_target_side(target, pside, s, pside[3-s]); -- oppo side
		table_append(clist, clist2);
	end

	-- duplicate with atl and clist itself
	-- migrate clist to clist2, make sure no duplicate in clist2
	clist2 = {}
	for k, v in ipairs(clist) do
--		if (cc.id == 1) then
--			print('DEBUG +++++ boris validate: ',
--				cc:trigger_target_validate(pside, index_card(v, pside), num) 
--			);
--		end

		if nil == table_find(clist2, v) and nil == table_find(atl, v) 
		-- note: this is for boris cost <= 4 kill validate
		and (cc.trigger_target_validate==nil or 
		true==cc:trigger_target_validate(pside, index_card(v, pside), num)) 
		then
			clist2[#clist2 + 1] = v;
		end
	end

	-- not attach, early exit
	if cc.ctype~=ATTACH then
		return clist2;
	end

	-- implicit:  cc is attach card
	-- check attach duplicate (last logic, need early exit)
	for i=#clist2, 1, -1 do
		local v = index_card(clist2[i], pside);

		if check_attach_duplicate(v, cc) > 0 then
			table.remove(clist2, i);
		end

		if #(v.attach_list or {}) >= 9 then
			print('WARN list_ability_target:attach_over_9');
			table.remove(clist2, i);
		end

	end

	return clist2; -- note: clist2 is a no duplicate ver of clist
end

-- return a list of card index that has ability
function list_ability_index(pside, s)
	local oneside = pside[s];
	local index;
	local clist = {}
	local res = oneside.resource;
	local target_list;

	if nil ~= oneside[T_HERO][1].trigger_skill and 
	true == check_playable(oneside[T_HERO][1], pside, s, true) then
		index = card_index(s, T_HERO, 1); -- same as cindex(oneside[T_HERO][1])
		clist[#clist+1] = index;
		-- make sure the first target is valid
		-- note: hero does not necessary has target_list!!!! nishaven
		-- below is WRONG logic! (keep)
--		target_list = list_ability_target(index, pside, s, {}, 1);
--		if #target_list >= 1 then
-- 			clist[#clist+1] = index;
-- 		end
	end

	for k, v in ipairs(oneside[T_ALLY] or {}) do
		if nil ~= v.trigger_skill and
		true == check_playable(v, pside, s, true) then
			index = card_index(s, T_ALLY, k);
			clist[#clist+1] = index;
		end
	end

	-- need to check resource against cost
	-- all hand card can be play when we have enough resource
	for k, v in ipairs(oneside[T_HAND] or {}) do
		if v.cost <= res then
			clist[#clist+1] = cindex(v);
		end
	end

	for k, v in ipairs(oneside[T_SUPPORT] or {}) do
		if nil ~= v.trigger_skill and 
		true == check_playable(v, pside, s, true) then
			index = card_index(s, T_SUPPORT, k);
			clist[#clist+1] = index;
		end
	end

	return clist;
end


-- spec for ai_move
function create_ai_play(index, atl, ability, id)
	return {index=index, atl=atl, ability=ability, id=id};
end



-- list all valid target of an ability, with source card = index
-- return  e.g. with 3 targets, 2 of them are optional
-- peter: update:  more targets go first, so that damage to ai_disable()=0 is ok
-- {
--     {t21, t22, t23b},
--     {t11, t12, t13, }
--     {t11, t12, t13b, }
--     {t21, t22, t23},
--     {t11, t12, }
--     {t21, t22,},
--     {t11, },
--     {t21, },
--     ...
-- }
function list_valid_target_list(index, pside, sss) -- list_valid start {
	local total;
	local atl = nil;
	
	total = total_target(index, pside, sss);
	-- note: it may not be a valid ability card
	if total == 0 then
		return { {} }; -- early exit
	end

	-- this is permutation : NPR 
	-- total = R
	-- tar = list_ability_target()  
	-- #tar = N
	local tar;
	local list = { {} };
	for i=1, total do   -- first for i=1,total {
		-- special handling for i=1
		for j=1, #list do -- second for #list {
			atl = list[j];
			-- print_1d(atl, 'DEBUG J=' .. i .. ' : ');
			tar = list_ability_target(index, pside, sss, atl, i);
			repeat -- start repeat {
			if #atl < i-1 then
				-- print('++++++ skipping #atl = ', #atl);
				break;
			end
			for k=1, #tar do
				local tmp;
				tmp = clone(atl);
				table.insert(tmp, tar[k])
				table.insert(list, tmp); 
			end
			break; -- must do
			until true; -- end repeat }
		end -- end second for j=1, #list }

		-- print('DEBUG --- step ' , i);
		-- print_2d(list, 'DEBUG i=' .. i .. ':' );
	end -- end first for i=1, total }

	
	local total_must;
	total_must = total_must_target(index, pside, sss);

	-- reverse loop for delete
	-- and output the list in reverse (more targets on top)
	local rev_list = {};
	for i=#list, 1, -1 do
		local ll = list[i];
--		if #ll < total_must then
--			table.remove(list, i);
--		end
		if #ll >= total_must then
			table.insert(rev_list, ll);
		end
	end

	-- return list;
	return rev_list;
end --  list_valid end }

-- add attack play to the first parameter [list]
function add_attack_play(list, tb, pside, sss, attack_list)
	-- false means attack
	local index, atl_list, ppp;
	for k, v in ipairs(tb or {}) do
		if true == check_playable(v, pside, sss, false) then
			index = cindex(v);
			for i=1, #attack_list do  -- attack list is shared!!!
				ppp = create_ai_play(index, attack_list[i], false, v.id);
				table.insert(list, ppp);
			end
		end
	end
end


-- add ability play to the first parameter [list]
function add_ability_play(list, tb, pside, sss)
	local index, atl_list, ppp;
	-- true means ability
	for k, v in ipairs(tb or {}) do
		if true == check_playable(v, pside, sss, true) then
			-- print('v.name_can_play:', v.name);
			index = cindex(v);
			atl_list = list_valid_target_list(index, pside, sss);
			for i=1, #atl_list do
				ppp = create_ai_play(index, atl_list[i], true, v.id);
				table.insert(list, ppp);
			end
		end
	end
end

-- list all possible play
-- a play :  ppp = {
-- 		ability = true / false,
-- 		weight = 0 - 9999 (ai benefit from this move)
--      -- below are starting from index[1]
--      src_index = number,  (number)
-- 		atl = {
--      target1_index,  (number)
-- 		target2_index,  (number)
--		}
--      ...
-- }
-- such that: 
-- ppp.index = src_index(number)
-- ppp.id = source card id (newly add)
-- ppp.atl[1] = target1_index
-- ppp.atl[2] = target2_index ...
-- ppp.ability = true / false
-- note: weight is not assigned here (or set 0)
function list_all_play(pside, sss) -- start list_all {
	local list = {}; -- list of ppp
	local src_list = {};
	local myside = pside[sss];
	local attack_list;
	local atl_list;

	-- construct the attack_list as { {atl_list[1]}, {atl_list[2]}, ...}
	attack_list = {};
	atl_list = list_attack_target(0, pside, sss);
	for i=1, #atl_list do
		table.insert(attack_list, {atl_list[i]});
		-- print('list all attack : ' , atl_list[i]);
	end

	-- consider order: hand(ab), hero(ab,at), ally(ab,at), support(ability)

	-- list_all_hand
	add_ability_play(list, myside[T_HAND], pside, sss);

	-- hero
	add_ability_play(list, myside[T_HERO], pside, sss);
	add_attack_play(list, myside[T_HERO], pside, sss, attack_list);


	-- ally : ability, attack
	add_ability_play(list, myside[T_ALLY], pside, sss);
	add_attack_play(list, myside[T_ALLY], pside, sss, attack_list);


	-- support : only trigger ability
	add_ability_play(list, myside[T_SUPPORT], pside, sss);

	return list;
end -- end list_all }


-- one play: @play_spec
function weight_one_play(ppp, pside, sss) -- start {
	local src = index_card(ppp.index, pside);
	if nil == src then
		print('BUGBUG weight_one_play src=nil');
		return 0; 
	end

	local atl = index_card_list(ppp.atl, pside);
	local ability = ppp.ability;  -- boolean
	local weight = 0;

	-- centralized check
	if false == check_playable(src, pside, sss, ability) then
		return 0;
	end

	if nil ~= src.ai_weight then
		weight = src:ai_weight(atl, pside, sss, ability);
		if weight==nil then
			print('BUGBUG src:ai_weight nil id=' .. src.id);
		end
		return weight; -- early exit XXX potential weight=nil
	end 

	-- default ai_weight logic:
	-- ctype==ALLY, src.table==T_ALLY : ability~=true : ai_weight_attack
	if src.ctype==ALLY and src.table==T_ALLY and ability~=true then
		weight = ai_weight_attack(src, atl, pside, sss, ability);
		return weight;
	end

	-- cast an ally from T_HAND to T_ALLY
	if src.ctype==ALLY and src.table==T_HAND then
		weight = ai_weight_ally(src, atl, pside, sss, ability);
		return weight;
	end

	if src.ctype==ABILITY and "number"==type(src.ab_power) then
		-- assume src.ab_power is numeric!
		weight = ai_weight_ability_damage(src, atl, pside, sss, ability);
		return weight;
	end

	-- ability damage for ally on T_ALLY or equipment on T_SUPPORT
	-- e.g: (189) Summer , (45) used custom AI
	if (src.table==T_ALLY or src.table == T_SUPPORT) and "number"==type(src.ab_power) then
		-- assume src.ab_power is numeric!
		weight = ai_weight_ability_damage(src, atl, pside, sss, ability);
		return weight;
	end
	-- cast an weapon or armor from T_HAND to T_SUPPORT
	if (src.ctype==WEAPON or src.ctype==ARMOR) and src.table==T_HAND then
		weight = ai_weight_equipment(src, atl, pside, sss, ability);
		return weight;
	end

	-- (67)=Crippling blow,  (72)=Freezing,  (80)=Webs
	-- TODO use CONTROL_SHORT_LIST and CONTROL_LONG_LIST, beware hero?
	if src.id==67 or src.id==72 or src.id==80 then
		weight = ai_weight_control(src, atl, pside, sss, ability);
		return weight;
	end

	weight = ai_weight_general(src, atl, pside, sss, ability);
	return weight;
end -- weight_one_play end }

-- weight each play by score:
-- input: list of play list[i] is a play
-- each play is : {index=index,  atl={tar_index1, tar_index2...}, 
--        ability=true/false}
function weight_all_play(list, pside, sss)
	local max_play = nil;
	local max_index = nil;
	local max_weight = -9999; -- this is the minimum
	for i=1, #list do
		local ppp = list[i];
		local weight = -9988;

		weight = weight_one_play(ppp, pside, sss);
		if weight==nil or weight <= -9000 then
			weight = weight or '_nil_';
			print('BUGBUG weight_one_play weight,cmd : ' .. weight .. ',' 
			.. cmd_detail(play_to_cmd(ppp),pside));
			break; -- we may do continue, but lua does not support
		end

		ppp.weight = weight;
		if weight > max_weight then
			max_weight = weight;
			max_play = ppp;
			max_index = i; -- seems useless?
		end
	end
	return max_play, max_weight;
end


function play_attack(index, atl, pside, sss)
	local src_card;
	local target_card;
	local eff_list = {};
	local err = nil;
	local flag, reason;

	src_card = index_card(index, pside);
	target_card = index_card(atl[1], pside); -- TODO check atl=nil or #0

	flag, reason=check_playable(src_card, pside, sss, false);
	if false==flag then
		return nil, 'src_cannot_attack:' .. (reason or 'nil_reason');
	end

	if false==check_target(target_card, pside, sss, false) then
		return nil, 'target cannot be attacked';
	end

	eff_list,err = action_attack(target_card, pside, src_card, sss);
		-- side_my, side_your, cc, target_card);

	if eff_list==nil or #eff_list==0 then
		return eff_list, err; -- early exit
	end

	-- change in action_damage()
	return eff_list, err;
end

-- index is the source card e.g. 1101 (hand 1st card)
-- atl = { at1, at2, ...}  e.g. {2201, 2202}
function play_ability(index, atl, pside, sss)
	-- core logic!
	local eff_list = {};
	local eff_list2;
	local err;
	local cc;
	local list;
	
	-- check the source card
	list = list_ability_index(pside, sss);
	if nil == table_find(list, index) then
		return nil, 'invalid src card';
	end
	

	-- check target
	local temp_atl = {};
	for k=1, #(atl or {}) do -- FIXME   atl may be nil
		local v = atl[k];
		list = list_ability_target(index, pside, sss, temp_atl, k);
		if nil == table_find(list, v) then
			return nil, 'invalid target';
		end
		temp_atl[#temp_atl] = v;
	end

	cc = index_card(index, pside);
	if nil == cc then
		print('ERROR play_ability cc_nil index=', index);
		return nil, 'cc_nil'; -- early exit
	end

	eff_list,err = cc:trigger_skill(pside, atl);
	-- TODO error or nil ?
	if nil==eff_list then
		err = err or 'nil_err';
		print('ERROR ability not execute index=' 
		..  index .. '(' .. cc.id .. ')' .. ' err=', err);
		return nil, err; -- early exit
	end

	-- for hand, move to grave, reduce resource! 
	-- note: resource is checked in check_playable
	-- consider: put this _before_ trigger_skill
	if cc.table == T_HAND then
		-- 1. reduce resource
		eff_list2 = action_resource(-cc.cost, pside, sss);
		table_append(eff_list, eff_list2);
		
		-- 2. move to grave
		eff_list2 = action_grave(cc, pside);
		table_append(eff_list, eff_list2);

		update_solo_target(pside, sss, ABILITY, cc.id);
	else
		-- non-hand, just reduce skill_cost_energy or skill_cost_resource
		if cc.skill_cost_resource > 0 then
			eff_list2 = action_resource(-cc.skill_cost_resource, pside, sss);
			table_append(eff_list, eff_list2);
		end

		if cc.skill_cost_energy > 0 then
			eff_list2 = action_energy(-cc.skill_cost_energy, pside, sss);
			table_append(eff_list, eff_list2);
		end
		-- set ready
		set_use_ability(cc); -- XXX bug if it is in grave
	end

	return eff_list;
end



-- now, we only handle 'attach' card @see list_ability_target
-- TODO ally card may trigger a target, e.g. DMT
function list_hand_target(index, pside, sss, atl, num)
	-- ATTACH only take 1 atl
	return list_ability_target(index, pside, sss, atl, num);
end

function update_solo_target(pside, sss, ctype, card_id)
	local s = pside[sss];
	if ctype == ALLY then
		s.num_hand_ally = s.num_hand_ally or 0;
		s.num_hand_ally = s.num_hand_ally + 1;
	elseif ctype == SUPPORT then
		s.num_hand_support = s.num_hand_support or 0;
		s.num_hand_support = s.num_hand_support + 1;
	elseif ctype == ABILITY or ctype == ATTACH then
		s.num_hand_ability = s.num_hand_ability or 0;
		s.num_hand_ability = s.num_hand_ability + 1;
	end
	s.use_card_table = s.use_card_table or {};
	s.use_card_table[card_id] = s.use_card_table[card_id] or 0;
	s.use_card_table[card_id] = s.use_card_table[card_id] + 1;


	--[[
	-- old logic
	if sss == 1 then
		if ctype == ALLY then
			g_chapter_up_ally = g_chapter_up_ally + 1;
		elseif ctype == SUPPORT then
			g_chapter_up_support = g_chapter_up_support + 1;
		elseif ctype == ABILITY or ctype == ATTACH then
			g_chapter_up_ability = g_chapter_up_ability + 1;
		end

		if card_id == g_chapter_up_card_id1 then
			g_chapter_up_card_count1 = g_chapter_up_card_count1 + 1;
		elseif card_id == g_chapter_up_card_id2 then
			g_chapter_up_card_count2 = g_chapter_up_card_count2 + 1;
		elseif card_id == g_chapter_up_card_id3 then
			g_chapter_up_card_count3 = g_chapter_up_card_count3 + 1;
		end
	else
		if ctype == ALLY then
			g_chapter_down_ally = g_chapter_down_ally + 1;
		elseif ctype == SUPPORT then
			g_chapter_down_support = g_chapter_down_support + 1;
		elseif ctype == ABILITY or ctype == ATTACH then
			g_chapter_down_ability = g_chapter_down_ability + 1;
		end

		if card_id == g_chapter_down_card_id1 then
			g_chapter_down_card_count1 = g_chapter_down_card_count1 + 1;
		elseif card_id == g_chapter_down_card_id2 then
			g_chapter_down_card_count2 = g_chapter_down_card_count2 + 1;
		elseif card_id == g_chapter_down_card_id3 then
			g_chapter_down_card_count3 = g_chapter_down_card_count3 + 1;
		end
	end
	]]--
end

--[[
-- old logic, remove later
function set_chapter_target_card(up_id1, up_id2, up_id3, down_id1, down_id2, down_id3)
	g_chapter_up_card_id1 = up_id1;
	g_chapter_up_card_id2 = up_id2;
	g_chapter_up_card_id3 = up_id3;
	g_chapter_down_card_id1 = down_id1;
	g_chapter_down_card_id2 = down_id2;
	g_chapter_down_card_id3 = down_id3;
end
]]--


function play_hand(index, atl, pside, sss)
	local eff_list = {};
	local eff_list2;
	local cc;
	local side_my = pside[sss];
	-- TODO check table in hand?
	cc = index_card(index, pside);

	if cc.side ~= sss then
		return error_return('play_hand : card not my hand');
	end
		
	if cc == nil then
		return error_return('play_hand : source nil : index=' 
		.. (index or 'nil'));
	end

	if cc.table ~= T_HAND then
		return error_return('play_hand : card not in hand');
	end

	if cc.cost > side_my.resource then
		return error_return('play_hand : resource not enough');
	end

	-- (53) DMT 'b 1201 2301' -> hand_ability = true (for client to input)
	-- when cast on hand:  if #atl == 1 AND trigger_skill ~= nil
    -- return play_ability(...) : e.g. play_ability(1201, {2301}, pside, sss);
	-- DMT card:
	-- hand_ability = true
	-- trigger_skill: check card is on T_HAND=1 damage, card on T_ALLY=2 damage
	-- target_list is the same for both on T_HAND or on T_ALLY
	-- target_list = {side=2, table_list={T_HERO, T_ALLY} }
	if cc.ctype == ALLY then
		if true ~= cc.haste then -- add by kelton
			set_first_ready(cc); -- order is important (b4 action_move)
		end
		eff_list2 = action_move(cc, pside, side_my[T_HAND], side_my[T_ALLY]);
		if eff_list2==nil then
			return error_return('play_hand : ally : action_move');
		end
		table_append(eff_list, eff_list2);

		-- XXX now atl is nil!
		eff_list2 = action_cast_target(cc, pside, atl);
		table_append(eff_list, eff_list2);


		eff_list2 = action_resource(-cc.cost, pside, sss);
		table_append(eff_list, eff_list2);

		update_solo_target(pside, sss, ALLY, cc.id);

		return eff_list;
	elseif cc.ctype>=50 and cc.ctype<=59 then -- support

		if cc.ctype == WEAPON or cc.ctype == ARMOR then
			local supp_list = side_my[T_SUPPORT] or {};
			for i = #supp_list, 1 , -1 do
				local sc = supp_list[i];
				if cc.ctype == sc.ctype then
					eff_list2 = action_grave(sc, pside);
					table_append(eff_list, eff_list2);
					-- assume only one weapon/armor in the same time
				end

			end
		end


		eff_list2 = action_move(cc, pside, side_my[T_HAND], side_my[T_SUPPORT]);
		if eff_list2==nil then
			return error_return('play_hand : support : action_move');
		end
		table_append(eff_list, eff_list2);

		eff_list2 = action_resource(-cc.cost, pside, sss);
		table_append(eff_list, eff_list2);

		update_solo_target(pside, sss, SUPPORT, cc.id);

		return eff_list;
	elseif cc.ctype == ABILITY then
		return play_ability(index, atl, pside, sss);
	elseif cc.ctype == ATTACH then
		local tc = index_card(atl[1], pside); 
		if tc==nil then
			return error_return('play_hand : attach : target nil');
			--[[	
			-- if attach card target is my hero, atl[1] == nil is available
			local target_list = cc.target_list;
			if target_list == nil or #target_list ~= 1 then
				return error_return('play_hand : attach : target_list nil');
			end
			local table_list = target_list[1].table_list;
			local side = target_list[1].side;
			if side == nil or (side ~= 1 and side ~= 2) or table_list == nil 
			or #table_list ~= 1 or table_list[1] ~= T_HERO then
				return error_return('play_hand : attach : target nil');
			end

			-- overwrite atl
			if side == 1 then 
				atl[1] = 1101;
			end
			if side == 2 then 
				atl[1] = 2101;
			end

			tc = index_card(atl[1], pside); 
			if tc==nil then
				return error_return('play_hand : attach : target nil');
			end
			]]--
		end

		-----
		-- check target
		local temp_atl = {};

		for k=1, #(atl or {}) do -- FIXME   atl may be nil
			local v = atl[k];
			local list;
			list = list_ability_target(index, pside, sss, temp_atl, k);
			if nil == table_find(list, v) then
				return nil, 'play_hand : attach : invali target';
			end
			temp_atl[#temp_atl] = v;
		end

		eff_list2 = action_attach(tc, pside, cc);
		if eff_list2==nil then
			return error_return('play_hand : attach : action_attach');
		end
		table_append(eff_list, eff_list2);
		eff_list2 = action_resource(-cc.cost, pside, sss);
		table_append(eff_list, eff_list2);

		update_solo_target(pside, sss, ATTACH, cc.id);

		return eff_list;
	end

	return nil, 'BUGBUG play_hand : index=' .. (index or 'nil');
end

-- assume this is attack @see play_attack
function play_validate_attack(index, atl, pside, sss) -- {
	local flag, reason;
	local src_card, target_card;
	src_card = index_card(index, pside);
	target_card = index_card(atl[1], pside); -- TODO check atl=nil or #0

	flag,reason = check_playable(src_card, pside, sss, false);
	if flag == false then
		return flag, 'play_validate_attack:src:' .. (reason or 'nil_reason');
	end

	flag, reason = check_target(target_card, pside, sss, false);
	if flag == false then
		return flag, 'play_validate_attack:target:' .. (reason or 'nil_reason');
	end

	-- finally it is ok
	return true;
end

-- assume this is ability @see play_hand, play_ability
function play_validate_ability(index, atl, pside, sss) -- {
	local cc = index_card(index, pside);
	if (cc==nil) then
		print('BUGBUG play_validate_ability cc=nil  index=', index);
		return false, 'cc_nil';
	end

	local flag, reason;
	flag, reason = check_playable(cc, pside, sss, true);
	if flag == false then
		return flag, reason;
	end
	
	local tb = index_table_num(index);
	if tb == nil then return false, 'tb_nil'; end
	if tb==T_HAND then
		if cc.cost > pside[sss].resource then
			return false, 'resource_not_enough:validate';
		end
		-- check duplicate: for SUPPORT and ARTIFACT @see check_playable
		if (cc.ctype==SUPPORT or cc.ctype==ARTIFACT) then
			if check_support_duplicate(pside, sss, cc.id) then
				return false, 'support_duplicate:validate';
			end
		end
		-- weapon / armor is replacable, duplicate = replace by new one
	end

	local trigger = cc.trigger_target_validate;
	local ret, err;
	if trigger==nil then
		return true;
	end

	-- trigger is non-nil
	ret = trigger(cc, pside, nil, 0);
	if ret == false then
		return ret, 'trigger_target_validate ' 
			.. index .. ' : ' .. 0;
	end

	local target_list = index_card_list(atl, pside);
	for i=1, #target_list do
		ret = trigger(cc, pside, target_list[i], 0);
		if ret == false then
			return ret, 'trigger_target_validate ' 
			.. index .. ' : ' .. 1 .. ' : ' ..  atl[i];
		end
	end

	return true; -- finally ok
end -- }

-- index is the src card,  atl[] is the target index list
function play_validate(index, atl, pside, sss, ability) -- {
	local flag, reason;
	local cc  = index_card(index, pside);
	if cc == nil then
		return false, 'play_validate:index_nil';
	end
	flag,reason = check_playable(cc, pside, sss, ability);
	if flag == false then
		return flag, 'play_validate:' .. (reason or 'nil_reason');
	end
	if ability then
		return play_validate_ability(index, atl, pside, sss);
	else
		return play_validate_attack(index, atl, pside, sss);
	end
end -- }


-- clist = {1000, 2201}  means using 1000 to attack 2201
-- when ability = nil or false, it is normal attack
-- when ability = true, it means using ability of 1000 to target 2201
function play(index, atl, pside, s, ability) -- {
	local eff_list = {};
	local eff;
	local tb;
	local ret, err;

	tb = index_table_num(index);
	if tb == nil then
		return nil, 'tb=nil';
	end

	ret, err = play_validate(index, atl, pside, s, ability);
	-- it may return nil
	if ret == false then
		return nil, err;
	end

	-- let the src card (cc) show up
	eff = eff_card(index, index_card(index, pside).id);

	if tb==T_HAND then
		eff_list, err = play_hand(index, atl, pside, s);
	else
		if true==ability then
			eff_list, err = play_ability(index, atl, pside, s);
		else
			eff_list, err = play_attack(index, atl, pside, s);
		end
	end
	-- early exit for error case
	if eff_list == nil then
		return nil, err;
	end

	table.insert(eff_list, 1, eff);
	return eff_list, err;
end -- play() }


long_cmd_map = {
	['ab'] = 'b'
,	['at'] = 't'
,	['sac'] = 's'
,	['next'] = 'n'
};

-- return true|false, reason  (reason only exist for false case)
function play_cmd_validate(cmd, pside, sss, phase)
	-- TODO 
	local cmd_list;
	local cc;
	cmd_list = split_num(cmd);
	if #cmd_list == 0 then
		return false, 'empty_cmd';
	end
	cmd_list[1] = long_cmd_map[cmd_list[1]] or cmd_list[1];
	-- cmd_list[1] should be either b, t, s, n
	if cmd_list[1] == 'n' then
		return true;
	end

	local index = cmd_list[2] or 0; -- should be after 'n'

	if cmd_list[1] == 's' then
		if phase ~= PHASE_SACRIFICE then
			return false, 'not_sacrifice_phase';
		end
		if index == 0 then return true; end -- 0 means no sacrifice
		cc = index_card(index, pside);
		if cc==nil then
			return false, 'sacrifice_cc_nil';
		end
		if cc.side ~= sss then 
			return false, 'sacrifice_not_my_side';
		end
		if cc.table ~= T_HAND then 
			return false, 'sacrifice_not_hand';
		end
		return true;
	end

	if cmd_list[1]=='t' or cmd_list[1]=='b' then
		local ability = (cmd_list[1]=='b');
		local atl = shallow_clone(cmd_list);  -- no need to shallow_clone ?
		table.remove(atl, 1); -- remove cmd
		table.remove(atl, 1); -- remove index
		return play_validate(index, atl, pside, sss, ability);
	end

	return false, 'unknown_cmd';
end

function play_cmd_validate_global(cmd)
	return play_cmd_validate(cmd, g_logic_table, g_current_side, g_phase);
end

-- return eff_list, sss, phase, err
function play_cmd(cmd, pside, sss, phase) -- {
	local cmd_list;
	local eff_list = nil;
	local err = nil;
	local atl = {}; -- or nil ???
	local ability = false;

	cmd_list = split_num(cmd);
	if #cmd_list==0 then
		return nil, sss, phase, 'empty_cmd';
	end
	cmd_list[1] = long_cmd_map[cmd_list[1]] or cmd_list[1];
	if cmd_list[1]=='n' then
		eff_list, sss, phase, err = action_next(pside, sss);
		return eff_list, sss, phase, err;
	end
	if cmd_list[1]=='s' then
		eff_list, phase, err = action_sacrifice(cmd_list[2] or 0, pside, sss, phase);
		return eff_list, sss, phase, err;
	end
	if cmd_list[1]=='b' or cmd_list[1]=='t' then
		if phase == PHASE_SACRIFICE then
			return nil, sss, phase, 'need_sacrifice';
		end
		ability = (cmd_list[1]=='b');
		atl = shallow_clone(cmd_list);  -- no need to shallow_clone ?
		table.remove(atl, 1); -- remove cmd
		table.remove(atl, 1); -- remove index
		-- if #atl==0 then 
		-- 	atl = nil;
		-- end
		eff_list, err = play(cmd_list[2], atl, pside, sss, ability);
		gate_hero_hide(pside, g_gate);
		return eff_list, sss, phase, err;
	end


	-- this is buggy
	return nil, sss, phase, ' unknown_cmd_BUG';
end -- play_cmd() }

function play_cmd_local(cmd, pside, sss, phase) -- {
	local eff_list;
	local err;

	eff_list, g_current_side, g_phase, err 
	= play_cmd(cmd, g_logic_table, g_current_side, g_phase);

	if err ~= nil then
		print('ERROR play_cmd_local err=', err);
	end
	if eff_list ~= nil then -- TODO check hero die here, esp: draw
		
		-- local current_side = g_current_side;
		-- XXX something may wrong
		-- print('DEBUG play_cmd_clobal:g_current_side=', g_current_side);

		local eff;
		local hh1 = g_logic_table[3-g_current_side][T_HERO][1];
		local hh2 = g_logic_table[g_current_side][T_HERO][1];
		if hh1.hp <= 0 and hh2.hp > 0 then
			eff = eff_win(g_current_side);
			eff_list[#eff_list + 1] = eff;
		end
		
		if hh1.hp > 0 and hh2.hp <= 0 then
			eff = eff_win(3-g_current_side);
			eff_list[#eff_list + 1] = eff;
		end
		
		if hh1.hp <= 0 and hh2.hp <= 0 then
			eff = eff_win(9);
			eff_list[#eff_list + 1] = eff;
		end
			
	end

	return eff_list, g_current_side, g_phase, err;
end

-- consider: minotaur(58) killed, deathbone(41)
function check_gate_win(gate_hero_hp1, gate_hero_hp2)
	if g_gate == nil then
		return {};
	end

	local eff;
	local eff_list = {};

	-- print('hero hp1=' .. gate_hero_hp1 .. ' hp2=' .. gate_hero_hp2);
	-- print('g_round=' .. g_round .. ' g_gate_max=' .. g_gate_max);
	-- print('#g_logic_table[2][T_ALLY]=' .. #g_logic_table[2][T_ALLY]);
	-- print('#g_logic_table[2][T_HAND]=' .. #g_logic_table[2][T_HAND]);

	-- player lose
	if gate_hero_hp2 < gate_hero_hp1 then
		print('check_gate_win:hero_hp_reduce' .. gate_hero_hp1 .. ' ' .. gate_hero_hp2);
		eff = eff_win(2);
		eff_list[#eff_list + 1] = eff;
		return eff_list;
	end

	-- player win
	if g_round > g_gate_max and #g_logic_table[2][T_ALLY] == 0 then
		local no_more_ally = true;
		for i=1, #g_logic_table[2][T_HAND] do
			local v = g_logic_table[2][T_HAND][i];
			if v.ctype == ALLY then
				no_more_ally = false;
				break;
			end
		end
		if no_more_ally == true then
			eff = eff_win(1);
			eff_list[#eff_list + 1] = eff;
		end
	end

	return eff_list;
end

-- this is for C to call lua
function play_cmd_global(cmd)
	local eff_list;
	local err;
	local win = 0;  -- 0 means nobody win, win=1 means side1 win

	local gate_hero_hp1 = g_logic_table[1][T_HERO][1].hp;

	eff_list, g_current_side, g_phase, err 
	= play_cmd(cmd, g_logic_table, g_current_side, g_phase);
	if err ~= nil then
		print('ERROR play_cmd_global err=', err);
	end
	if eff_list ~= nil then -- TODO check hero die here, esp: draw
		
		-- local current_side = g_current_side;
		-- XXX something may wrong
		-- print('DEBUG play_cmd_clobal:g_current_side=', g_current_side);

		local eff;
		local hh1 = g_logic_table[3-g_current_side][T_HERO][1];
		local hh2 = g_logic_table[g_current_side][T_HERO][1];
		if hh1.hp <= 0 and hh2.hp > 0 then
			eff = eff_win(g_current_side);
			eff_list[#eff_list + 1] = eff;
		end
		
		if hh1.hp > 0 and hh2.hp <= 0 then
			eff = eff_win(3-g_current_side);
			eff_list[#eff_list + 1] = eff;
		end
		
		if hh1.hp <= 0 and hh2.hp <= 0 then
			eff = eff_win(9);
			eff_list[#eff_list + 1] = eff;
		end

		-- gate win verify
		local gate_hero_hp2 = g_logic_table[1][T_HERO][1].hp;
		local eff_list2 = check_gate_win(gate_hero_hp1, gate_hero_hp2);
		table_append(eff_list, eff_list2);
		
		
		-- print_eff_list(eff_list);
		for i=1, #eff_list do 
			if 'win' == eff_list[i][1] then
				win = eff_list[i]['side'];
				-- print('DEBUG play_cmd_global win = ' .. win);
				break;  -- peter: count first win 
			end
		end
	end
	-- note: 
	return win, g_current_side, g_phase, err;
end


--------- ACTION END    


--------- AI START

-- assume c1, c2 are cards
function cost_comparator(c1, c2)
	if c1 == nil then 
		return true;
	end
	if c2 == nil then
		return false;
	end
	-- note: if we use c1.cost <= c2.cost, it will have invalid order function
	if c1.cost < c2.cost then
		return true;
	end
	if c1.cost == c2.cost then
		if c1.id < c2.id then
			return true;
		end
	end
	return false;
end

-- return true if the card(cc) is in the same job as hero(hh)
function same_job(hh, cc)
	-- e.g. cc.job = 4|8 (mage | priest) = 12
	--      hh.job = 4
	-- bit32.band(12, 4) = 4  :  this is > 0
	-- peter: lua in cocos-2dx does not support bit32
	-- if cc.job and cc.job==hh.job then
	-- get the job without camp
	local h_job = band(hh.job, (HUMAN-1));
	local c_job = band(cc.job, (HUMAN-1));
    if band(h_job, c_job) > 0 then
		return true
	else
		return false
	end
end

-- return true if the card(cc) is the same camp(human/shadow) as hero(hh)
-- for general camp card: return false!!!  (for ai)
function same_camp(hh, cc)
	-- cc.job overloaded camp + job
	if cc.camp and cc.camp==hh.camp then
		return true;
	else
		return false;
	end
end


function fit_hero(hh, cc)
    -- neutral card, always true
    if cc.job==nil or cc.job==0 then
        return true;
    end
    if band(hh.job, cc.job) > 0 then
        return true;
    else
        return false;
    end
end


function fit_hero_id(hid, cid)
    local hh, cc;
    hh = hero_list[hid];
    cc = g_card_list[cid];
    if hh==nil then
        print('BUGBUG fit_hero_id invalid hid=', hid);
        return false;
    end
    if cc==nil then
        print('BUGBUG fit_hero_id invalid cid=', cid);
        return false;
    end
    return fit_hero(hh, cc);
end


-- @see play_spec and cmd_spec
function play_to_cmd(ppp)
	local str = '';
	if ppp.ability then
		str = 'b '; -- peter: was ab
	else
		str = 't '; -- peter: was at
	end

	if ppp.index==nil then
		print('BUGBUG play_to_cmd  index=nil');
		return 'n';  -- was next
	end
	str = str .. ppp.index;
	for i=1, #(ppp.atl or {}) do
		str = str .. ' ' .. ppp.atl[i];
	end
	return str;
end

-- note: this will execute the play,
-- ai_sac() execute the actual sacrifice,  
-- weight_sac() return min sac card
-- 
-- ai_play() execute the actual play and return next phase, ssss
-- weight_all_play() return the max weighted play and its weight
function get_ai_play(pside, sss, phase) -- {
	local list;
	local max_play, max;
	local eff_list;

	if phase ~= PHASE_PLAY then
		print('ERROR get_ai_play phase_invalid ', phase);
		return 'n';
		-- return nil, sss, phase;
	end


	list = list_all_play(pside, sss);
	max_play, max = weight_all_play(list, pside, sss);
	-- ignore zero-negative play : which is bad, e.g. retreat my ally
	if nil == max_play or max <= 0 then
		-- print('DEBUG No play available, next!');
		return 'n';  -- eff_list, sss, phase; -- peter: was next
	end
	-- print_ai_play_detail(max_play, max, pside, sss);

	return play_to_cmd(max_play);
end -- get_ai_play }

function get_ai_solo_hand(pside, sss, phase)
	
	if g_solo_type ~= 1 then
		return nil;
	end

	-- not solo ai
	if sss ~= 2 then
		return nil;
	end

	local play_list = {};
	local play_one;
	local cc;
	local index;
	local hand=pside[sss][T_HAND];
	if #hand == 0 then
		return nil;
	end

	for i=1, #hand do
		cc = hand[i];
		play_list = {};
		local card_list = {cc};
		if cc.solo_ai == 1 then
			add_ability_play(play_list, card_list, pside, sss);
			if #play_list > 0 then
				break;
			end
		end
	end
	-- print("DEBUG get_ai_solo_hand:#play_list=", #play_list);

	if #play_list == 0 then
		return nil;
	end

	local max_play;
	local max;
	max_play, max = weight_all_play(play_list, pside, sss);

	-- peter: should not check max <= 0, must execute max_play, 
	-- even weight is negative
	-- if nil == max_play or max <= 0 then
	-- 	return nil;
	-- end

	return max_play;
end


function get_ai_solo_play(pside, sss, phase) -- {
	local list;
	local max_play, max;
	local eff_list;

	if phase ~= PHASE_PLAY then
		print('ERROR get_ai_play phase_invalid ', phase);
		return 'n';
		-- return nil, sss, phase;
	end

	-- check hand play first
	max_play = get_ai_solo_hand(pside, sss, phase);
	if max_play ~= nil then
		print('DEBUG get_ai_solo_hand');
		return play_to_cmd(max_play);
	end


	-- no more hand play, use default ai play
	list = list_all_play(pside, sss);
	max_play, max = weight_all_play(list, pside, sss);
	-- ignore zero-negative play : which is bad, e.g. retreat my ally
	if nil == max_play or max <= 0 then
		-- print('DEBUG No play available, next!');
		return 'n';  -- eff_list, sss, phase; -- peter: was next
	end
	-- print_ai_play_detail(max_play, max, pside, sss);

	return play_to_cmd(max_play);
end -- get_ai_solo_play }

-- check whether the attach_list in card cc contains the att card in id_list
-- id_list = { [67]=1, [1080]=1 }
function check_in_attach(cc, id_list)
	for __, at in ipairs(cc.attach_list or {}) do
		if id_list[at.id] == 1 then
			return true;
		end
	end
	return false;
end

-- check whether a card is in table include attachment
-- tb is the table, e.g. tb = pside[1][T_ALLY]
-- @return true means yes, the card is there,  false means no, the card missing
function check_in_table(tb, id)
	for _, v in ipairs(tb or {})  do
		if v.id == id then
			return true;
		end
		for __, at in ipairs(v.attach_list or {}) do
			if at.id == id then
				return true;
			end
		end
	end
	return false;
end

function check_ctype_in_table(tb, ctype)
	for _, v in ipairs(tb or {})  do
		if v.ctype == ctype then
			return true;
		end
		for __, at in ipairs(v.attach_list or {}) do
			if at.ctype == ctype then
				return true;
			end
		end
	end
	-- it does not exist in all tables above
	return false;
end

-- check whether a card is in side:  SUPPORT, ALLY, ALLY attach, HERO attach: 
-- side is only single side, not pside,  id is the card id
-- @return true means yes, the card is there,  false means no, the card missing
function check_in_side(side, id)
	-- check support
	if check_in_table(side[T_SUPPORT], id) then
		return true;
	end
	if check_in_table(side[T_HERO], id) then
		return true;
	end
	if check_in_table(side[T_ALLY], id) then
		return true;
	end

	-- it does not exist in all tables above
	return false;
end

-- check whether a card is in side:  SUPPORT, ALLY, ALLY attach, HERO attach: 
-- side is only single side, not pside,  id is the card id
-- @return true means yes, the card is there,  false means no, the card missing
-- id_list is like this format:
-- local draw_list = { [70]=1, [76]=1, [193]=1, [155]=1, [157]=1 };
function check_in_table_list(tb, id_list)
	-- check support
	-- check unit inside table with attach list
	for _, v in ipairs(tb or {})  do
		if id_list[v.id] == 1 then
			return true;
		end
		for __, at in ipairs(v.attach_list or {}) do
			if id_list[at.id] == 1 then
				return true;
			end
		end
	end
	return false;	
end

-- check whether the card id_list is in one side, include 
-- T_HERO, T_ALLY, T_SUPPORT  (not check in hand and grave)
function check_in_side_list(side, id_list)
	if check_in_table_list(side[T_HERO], id_list) then
		return true;
	end
	if check_in_table_list(side[T_ALLY], id_list) then
		return true;
	end
	if check_in_table_list(side[T_SUPPORT], id_list) then
		return true;
	end
	-- after all tables check, return false
	return false;
end

-- ai for sacrifice card
-- return the index to card for sacrifice
function weight_sac(side, s)  -- { start 
	local index = 0; -- the card index to sacrifice
	local hand;  -- shallow clone of my hand, will sort
	local next_res;  -- next_res is the res after this sacrifice
	local myside = side[s];  -- simply alias
	local opposide = side[3-s];
	local myhero = myside[T_HERO][1];

	hand = myside[T_HAND] ;
	if #hand == 0 then -- note: no nil check, just break it!
		-- empty hand, simply no sac
		return 0;
	end
	-- peter: do not sort!!!! 
	-- table.sort(hand, cost_comparator);

	-- core logic:
	-- each card is assign a weight, where weight = 100 * vcost
	-- card.weight = (next_res - math.abs(next_res - card.cost) ) * 100
	-- means:  card with cost = next_res  is the most valuable (best),
	-- while the weight will be lower if card.cost is further from next_res
	-- we keep those card which is the highest cost that we can play and
	-- sac card with too less or too much cost further from the next_res
	-- note1:  if total_cost <= myside.res (next_res - 1) then do not sacrifice!!
	-- note2:  card.weight is a side effect that assign to the card object
	--         make sure we do not use it elsewhere!
	-- some magical logic like:
	-- duplicate card weight-1000
	-- if a card is the only ally on hand, weight += 500 
	-- if the oppo side is a warrior or hunter, smashing blow + 500
	-- some card is unconditionally +weight 50 (or 150), e.g. fireball, 
	-- inner strength, puwen(for priest?)
	-- some card +weight according to number of ally in oppo side, 
	-- e.g. lightning +50 when #oppo.ally >= 1
	-- arcane burst +60 when #oppo.ally >= 3
	-- nova +120  when #oppo.ally - #my.ally >= 2
	-- for warrior:  ally+20, war banner +50
	-- for mage:     ability card + 20 ?
	-- unique control card:  if we hv only 1 control card 
	-- e.g. crippling blow, frozen grip, spider web(?),  weight+200 ?
	-- enemy resource >= 5 must keep control card
	-- lose-win critical card:  e.g. tidal wave, nova, beserker edge
	-- if we have only 2 or less cards and one of them are lose-win critical
	-- keep this card +weight 
	-- TODO same job card
	--
	-- core logic: sacrifice the card with the lowest weight
	-- if there are two cards with same 


	local dup_id = {};
	local total_cost = 0;
	local total_ally = 0;
	local str;
	next_res = myside.resource_max + 1;
	for i,v in ipairs(hand or {}) do -- i=1, #hand do
		-- local v = hand[i];
		v.weight = 100 * (10 - math.abs(next_res - v.cost));
		if dup_id[v.id] == 1 then  -- check duplicate card
			v.weight = v.weight - 1000;
		end
		total_cost = total_cost + v.cost;
		dup_id[v.id] = 1; 
		-- print(' index=' .. v:index() ,  '  id=' .. v.id , '  weight_sac:' .. v.weight);
	end

	if total_cost <= myside.resource_max then
		return 0;  -- no sac
	end

	-- magic rule start here

	-- weapon,armor(+40) > ally(+30) > ability(+20) > support(+10) > attach (+0)
	local SAC_WEIGHT_EQUIPMENT 	= 40;
	local SAC_WEIGHT_ALLY 		= 30;
	local SAC_WEIGHT_ABILITY	= 20;
	local SAC_WEIGHT_SUPPORT	= 10;

	local SAC_WEIGHT_JOB		= 28; -- this is tricky, 
	local SAC_WEIGHT_CAMP		= 14; -- little bonus over neutral card

	local SAC_WEIGHT_UNIQUE_ALLY 	= 150;
	local SAC_WEIGHT_UNIQUE_ABILITY = 150;
	local SAC_WEIGHT_DRAW	= 600; -- better weight for draw card
	-- 70=blood frency(warrior),   76=Tome(mage), 193=wizents staff(priest)
	-- 155=bazaar (general, low level ai), 157=bad santa(general) : <=3 cards
	-- for unique ally or unique ability + 150
	-- if it is 1 to 99, it is index to unique card (for 0 or 999)
	-- e.g. hand[unique_ally] = card to unique ally (if unique_ally is not 0 or 999)
	local unique_ally = 0;   
	local unique_ability = 0;

	for i, v in ipairs( hand or {}) do
		-- weapon or armor + 40 (TODO unique?)
		if v.ctype == WEAPON or v.ctype == ARMOR then
			v.weight = v.weight + SAC_WEIGHT_EQUIPMENT;  
			-- TODO unique weapon, unique armor
		end

		-- ally +30 
		if v.ctype==ALLY then
			v.weight = v.weight + SAC_WEIGHT_ALLY; 
			if unique_ally == 0 then
				unique_ally = i;
			else
				unique_ally = 999; -- not unique
			end
		end

		-- ability +20
		if v.ctype==ABILITY then
			v.weight = v.weight + SAC_WEIGHT_ABILITY;
			if unique_ability == 0 then
				unique_ability = i;
			else
				unique_ability = 999; -- not unique
			end
		end

		-- TODO unique support, on table support

		-- support +10
		if v.ctype==SUPPORT then
			v.weight = v.weight + SAC_WEIGHT_SUPPORT;
		end

		-- same job + 20 
		if true == same_job(myhero, v) then
			v.weight = v.weight + SAC_WEIGHT_JOB;
		end
		-- same camp + 10 
		if true == same_camp(myhero, v) then
			v.weight = v.weight + SAC_WEIGHT_CAMP;
		end

		-- TODO separate check on DRAW_FIX_LIST and DRAW_ONCE_LIST
		-- it is draw card, and it is not yet in my side
		if DRAW_LIST[v.id]==1 then 
			if true ~= check_in_side_list(myside, DRAW_FIX_LIST) 
			and v.weight > 0 then
				v.weight = v.weight + SAC_WEIGHT_DRAW;
			else
				-- either on hand or in T_SUPPORT, T_ALLY, T_HERO attach
				v.weight = v.weight - 1000; -- very likely drop
			end
		end

	end
	-- unique case
	if unique_ally ~= 0 and unique_ally ~= 999 then
		local v = hand[unique_ally];
		v.weight = v.weight + SAC_WEIGHT_UNIQUE_ALLY;
	end
	if unique_ability ~= 0 and unique_ability ~= 999 then
		local v = hand[unique_ability];
		v.weight = v.weight + SAC_WEIGHT_UNIQUE_ABILITY;
	end
	

	local lowest_weight = 9999;
	local lowest_card = nil;
	for i=1, #hand do 
		local v = hand[i];
		if v.weight < lowest_weight then
			lowest_card = v;
			lowest_weight = v.weight;
		end
		-- str = string.format('weight_sac  weight=%-3d    cost:%1d  %3d,%-20s', v.weight, v.cost, v.id, v.name);
		-- print('DEBUG ' .. str);
	end

	-- no lowest weight
	if lowest_card == nil then
		print('BUGBUG weight_sac lowest_card=nil');
		return 0;
	end

	index = cindex(lowest_card);

	return index;
end -- } end weight_sac


function get_ai_sac(pside, sss, phase) -- {
	if phase ~= PHASE_SACRIFICE then
		print('Not sacrifice phase');
		return nil;
	end

	local index;
	local eff_list;
	index = weight_sac(pside, g_current_side);
	-- print('DEBUG AI sacrifice index=', index);
	return 's ' .. index; -- peter: was 'sac'
end -- get_ai_sac }

function get_ai_solo_sac(pside, sss, phase) -- {
	if phase ~= PHASE_SACRIFICE then
		print('Not sacrifice phase');
		return nil;
	end

	if g_solo_type == 0 then
		print('BUGBUG get_ai_solo_sac:not_solo_mode');
		return nil;
	end

	if sss ~= 2 then
		print('BUGBUG get_ai_solo_sac:current_side_not_ai');
		return nil;
	end

	local index = 0;
	local hand = pside[g_current_side][T_HAND];
	if #hand == 0 then
		index = 0;
		return 's ' .. index;
	end

	--[[
	local ret = check_solo_list();
	if ret ~= 0 then
		print('BUGBUG get_ai_solo_sac:check_solo_list_bug ', ret);
		index = 0;
		return 's ' .. index;
	end
	]]

	-- loop g_solo_list, get first type=0 card index
	for i=1, #hand do
		--[[
		local t = g_solo_list[i];
		if t == 0 then
			index = cindex(hand[i]);
			break;
		end
		]]
		local cc = hand[i];
		if cc == nil then
			print('BUGBUG get_ai_solo_sac:cc_nil ', i);
			index = 0;
			return 's ' .. index;
		end
		-- NOTE: cc.solo_ai may == nil
		if cc.solo_ai == 0 then
			index = cindex(hand[i]);
			break;
		end
	end
		
	-- print('DEBUG AI solo sacrifice index=', index);
	return 's ' .. index; -- peter: was 'sac'
end -- get_ai_solo_sac }


-- return a string for cmd details
function cmd_detail(cmd, pside)
	local str = '';
	local cmd_list = split_num(cmd);
	local cc;
	local target_list;
	local cmd_act;
	local index = 0;
	if cmd_list[1] == 'next' or cmd_list[1] == 'n' then
		return 'n';  -- next without index, so we do early exit
	end
	cmd_act = cmd_list[1];
	index = tonumber(cmd_list[2]);
	if (cmd_list[1] == 'sac' or cmd_list[1] == 's') and index==0 then
		return 's 0';  
	end
	cc = index_card(index, pside);
	table.remove(cmd_list, 1); -- remove cmd_act
	table.remove(cmd_list, 1); -- remove index
	target_list = index_card_list(cmd_list, pside);

	if nil == cc then
		print('BUGBUG cmd_detail cmd_act, index ', cmd_act, index, cmd);
	end

	str = cmd .. ' ' .. string.sub(cc.name, 1, 8) .. '(' ..  cc.id .. ')';
	for i=1, #target_list do 
		cc = target_list[i];
		str = str .. ' ' ..  string.sub(cc.name, 1, 8) .. '(' ..  cc.id .. ')';
	end
	return str;
end

-- return eff_list, sss, phase TODO add err
function ai(pside, sss, phase)
	local cmd;
	if phase == PHASE_SACRIFICE then
		cmd = get_ai_sac(pside, sss, phase);
	else
		assert(phase==PHASE_PLAY); -- XXX SUSPECT bug in luajit assert
		cmd = get_ai_play(pside, sss, phase);
	end

	print('DEBUG ai cmd : ' .. cmd_detail(cmd, pside));
	return play_cmd(cmd, pside, sss, phase);
end

function ai_global()
	local eff_list;
	eff_list, g_current_side, g_phase = ai(g_logic_table, g_current_side, g_phase);
	return 0;
end;

-- use for solo ai game
function ai_solo_cmd_global()  
	local cmd;
	if g_phase == PHASE_SACRIFICE then
		cmd = get_ai_solo_sac(g_logic_table, g_current_side, g_phase);
	else
		cmd = get_ai_solo_play(g_logic_table, g_current_side, g_phase);
	end
	return cmd;
end

-- return the ai cmd  @see cmd_spec
function ai_cmd_global()  -- here is what the luajit bug happen! (2.0.2)
	local cmd;

	-- solo game and caller is ai
	-- if g_solo_list ~= nil and g_current_side == 2 then
	if g_solo_type == 1 and g_current_side == 2 then
		return ai_solo_cmd_global();
	end

	if g_phase == PHASE_SACRIFICE then
		cmd = get_ai_sac(g_logic_table, g_current_side, g_phase);
	else
		--assert(g_phase==PHASE_PLAY); -- XXX SUSPECT bug in luajit
		cmd = get_ai_play(g_logic_table, g_current_side, g_phase);
	end

	return cmd;
end

function get_all_play()
	local out_buffer = '';
	local play_list = {};
	if g_phase == PHASE_SACRIFICE then
		local hand = g_logic_table[g_current_side][T_HAND];	
		for i=1, #hand do
			play_list[#play_list+1] = 's ' .. cindex(hand[i]) .. ';';
		end
		if #hand == 0 then
			play_list[#play_list+1] = 's 0;';
		end
	else
		-- {index=index, atl=atl, ability=ability, id=id};
		local cmd_list = list_all_play(g_logic_table, g_current_side);
		for i=1, #cmd_list do
			play_list[#play_list+1] = play_to_cmd(cmd_list[i]) .. ';';
		end
	end

	if #play_list == 0 then
		play_list[1] = 'n;';
	end

	for i=1, #play_list do
		out_buffer = out_buffer .. play_list[i];	
	end
	print('get_all_play:out_buffer=' .. out_buffer);
	return out_buffer;
end

-- calculate the AI play for the current turn
-- return src_index, atl, ability(true/false)
-- if src_index == 0 then skip this round
-- 
-- action priority:
-- if #my.[effective]ally  >= #opp.[effective]ally then
--    hand > ally first
--    damage ability
-- else -- 
--    control ability > damage ability
-- 1. play direct damage ability
-- 2. ability active only if:  ability.cost <= enemy.cost
-- ally_ability
-- support_ability: with shadow energy, do not use
-- e.g. plate armor


--------- AI END



-- PRINT START handy print functions, with some utilities

-- one level simple table to str, generic now, not specific to card
function table_str(t)
	local str = '{';

	for k, v in pairs(t) do
		if type(v)=='function' then
			break;
		end
		-- print ('k=' .. k .. '  v=' .. v);
		if type(v)=='table' then 
			v = table_str(v);
		end
		if type(v)=='boolean' then
			if v==true then
				v = 'true';
			else 
				v = 'false';
			end
		end
		str = str .. k .. '=' .. v .. ',' ; 
	end
	str = str .. '}';
	return str;
end

function print_hero(hh, num, tabstop, desc) 
	local space = 0;
	local str ;
	tabstop = tabstop or 0;
	space = 4 * tabstop;
	num = num or 0;
	str = 'HERO:';
	-- status [none]=ready  X=exhausted,  v=ability_used
	if hh.ready==-1 then
		str = 'x' .. str;
		space = space - 1;
	elseif hh.ready==0 then
		str = 'X' .. str;
		space = space - 1;
	elseif hh.ready==1 then
		str = 'v' .. str;
		space = space - 1;
	end
	-- order is important, after status
	for i=1,space do
		str = ' ' .. str;
	end

	str = str .. '[' .. num .. ']Hero:';
	str = str .. string.format('%-30s HP:%2d/%-2d  power:%-1d  energy:%2d/%-2d',
		hh.name .. '(' .. job_map[hh.job % 256] .. ')', 
		hh.hp, hh.hp_max, hh.power, hh.energy, hh.skill_cost_energy);

	-- add status:  Am=ambush, Df=defender, Hi=hidden, St=stealth, Di=disabled, Nt=no_attack, Nb=no_ability
	if hh.side then  -- non-nil means in game (not by cccard)
		if hh.ambush then str = str .. ' Am'; end
		if hh.defender then str = str .. ' Df'; end
		if hh.hidden then str = str .. ' Hi'; end
		if hh.stealth then str = str .. ' St'; end
		if hh.disable then str = str .. ' Di'; end
		if hh.no_attack then str = str .. ' Nt'; end
		if hh.no_defend then str = str .. ' Nd'; end
		if hh.no_ability then str = str .. ' Nb'; end
	end

	print(str);

	for k, ac in ipairs(hh.attach_list or {}) do
		print_card(ac, 'A', tabstop+1);
	end

	if desc==true then
		space = '';
		for i=1,(tabstop*4) do
			space = ' ' .. space;
		end
		print(space .. '-- ' .. hh.skill_desc);
	end
end

function print_card_list(clist, offset)
	if offset==nil then
		offset = 0;
	end
	-- use ipairs, because clist.name is the table name
	-- print(clist.name .. ':');  -- if clist.name ~= nil 
	for k,v in ipairs(clist) do
		print_card(v, k+offset, 1);
	end
end

function print_deck(deck) 
	print ('Deck:');
	print_card_list(deck);
end

function print_hand(hand) 
	print ('Hand:');
	print_card_list(hand);
end

function print_ally(ally) 
	print ('Ally:');
	print_card_list(ally);
end

function print_support(support)
	print ('Support:');
	print_card_list(support);
end

function print_grave(grave)
	print ('Grave:');
	print_card_list(grave);
end

function print_side(s) 
	print_hero(s[T_HERO][1]);
	-- print_deck(s[T_DECK]);
	print_hand(s[T_HAND]);
	print_ally(s[T_ALLY]);
	print_support(s[T_SUPPORT]);
	print_grave(s[T_GRAVE]);
end

function print_both_side(ss) 
	print ('^^^^^  UP(1) SIDE ^^^^^');
	print_side(ss[1]);
	print ('');
	print ('vvvvv  DOWN(2) SIDE vvvvv');
	print_side(ss[2]);
	print ('---------- ---------- ----------'); 
	return 1,2;
end

function print_status(my_side)
	if (my_side==nil or my_side==0) then my_side = g_current_side; end
	local str;
	local side_my = g_logic_table[g_current_side];
	local side_your = g_logic_table[3-g_current_side];
	str = 'PLAY';
	if g_phase == PHASE_SACRIFICE then str = 'SACRIFICE'; end

	print ('\n' .. str .. ' current_side=' .. side_my.id .. ' resource=' 
	.. side_my.resource  .. '/' .. side_my.resource_max,   
	'  opp_res_max=' .. side_your.resource_max
	.. '  my_side(C)=' .. my_side);
end

function chinese_len(str)
	local slen = string.len(str);
	local i = 1;
	local v;
	local count = 0;

	while i <= slen do
		v = string.byte(str, i);

		if v >= 224 then -- 224 = 128 + 64 + 32 == > 1110 0000
			count = count + 1;
			i = i + 2;
		end

		i = i + 1;
	end

	return count;
end

function print_card(c, num, tabstop, desc)
	local ctype_name;
	local str = '';
	local space;
	if (c==nil) then
		print('nil card');
		return;
	end

	if (c.ctype==10) then
		print_hero(c, num, tabstop);
		return;
	end

	tabstop = tabstop or 0;
	space = 4 * tabstop;

	-- status character
	if c.ready==-1 then
		str = 'x' .. str;
		space = space - 1;
	elseif c.ready == 0 then
		str = 'X' .. str;  -- means not ready
		space = space - 1;
	elseif c.ready==1 then
		str = 'v' .. str;  -- mean half ready, ability is used
		space = space - 1;
	end

	-- order is important, after status
	for i=1, space do
		str = ' ' .. str ;
	end

	if num == nil then
		num = '?';
	end
	str = str .. '[' .. num .. ']';

	ctype_name = fun_ctype_map(c.ctype);
	local clen = chinese_len(c.name);
	str = str .. string.format('%-' .. (20+clen) .. 's(%3d)%-8s cost:%-2dpower:%-3dHP/max:%2d/%-2d',
		string.sub(c.name, 1, 20), c.id, ctype_name, 
		c.cost, c.power, c.hp, c.hp_max);
	if c.timer ~= nil then -- show timer count down
		str = str .. ' t' .. c.timer;
	end

	-- add status:  Am=ambush, DF=defender, Hi=hidden, St=stealth, Di=disabled, Nt=no_attack, Nb=no_ability
	if c.side then  -- non-nil means in game (not by cccard)
		if c.ambush then str = str .. ' Am'; end
		if c.defender then str = str .. ' Df'; end
		if c.hidden then str = str .. ' Hi'; end
		if c.stealth then str = str .. ' St'; end
		if c.disable then str = str .. ' Di'; end
		if c.no_attack then str = str .. ' Nt'; end
		if c.no_defend then str = str .. ' Nd'; end
		if c.no_ability then str = str .. ' Nb'; end
		if (nil ~= c.solo_ai) then str = str .. ' SA' .. c.solo_ai; end
	end


	print(str);
	-- print(str .. c.name .. '(' .. c.id .. ',' .. ctype_name .. ')',
	-- 	'cost:' .. c.cost,
	-- 	'power:' .. c.power .. '  HP/max:' .. c.hp .. '/' .. c.hp_max);
	if c.attach_list ~= nil then
		for k, ac in ipairs(c.attach_list) do
			-- print_card(ac, 'A' .. k, tabstop+1);
			print_card(ac, 'A' .. cindex(ac), tabstop+1);
		end
	end
	if desc==true then
		space = '';
		for i=1,(tabstop*4) do
			space = ' ' .. space;
		end
		print(space .. '-- ' .. c.skill_desc);
	end
end

function print_index_table(tb)
	print(table_name_map[tb.name] .. ':');
	for k, cc in ipairs(tb) do
		print_card(cc, cindex(cc), 1);
	end
end

function print_index_both(pside)
	print('^^^^^ UP(1) SIDE ^^^^^ VER=' .. LOGIC_VERSION .. ' SEED=' .. g_seed);	
	print_hero(pside[1][T_HERO][1], cindex(pside[1][T_HERO][1]));

	print_index_table(pside[1][T_HAND]);
	print_index_table(pside[1][T_ALLY]);
	print_index_table(pside[1][T_SUPPORT]);
	print_index_table(pside[1][T_GRAVE]);

	print('');

	print('^^^^^ DOWN(2) SIDE ^^^^^');	
	print_hero(pside[2][T_HERO][1], cindex(pside[2][T_HERO][1]));

	print_index_table(pside[2][T_HAND]);
	print_index_table(pside[2][T_ALLY]);
	print_index_table(pside[2][T_SUPPORT]);
	print_index_table(pside[2][T_GRAVE]);

end


function print_1d(list, tag)
	local str ;
	str = tag or '';
--	for i=1, #list do 
--		local v = list[i];
--		str = str .. v .. ', ';
--	end
	str = str .. table.concat(list, ', ');
	print('1D: ' ..  str);
end

-- print 2 dimension numeric list
function print_2d(list)
	local str = '' ;
	for i=1, #list do 
--		str = '';
--		for j=1, #list[i] do
--			local v = list[i][j];
--			str = str .. v .. ', ';
--		end
		str = table.concat(list[i], ', ');
		print('[' .. i ..  ']', str);
	end
end

function print_index_list(clist, pside)
	local str = '';
	for k, v in ipairs(clist) do
		str = str .. ', ' .. v;
	end
	print('index: ' .. str);

	-- print details
	for k, v in ipairs(clist) do
		local cc = index_card(v, pside);
		print_card(cc, v, 0);
	end
end

function print_eff(eff, tabstop)
	tabstop = tabstop or 1;
	local str = 'EFF ';
	for i=1, tabstop do
		str = '    ' .. str;
	end
	
	if eff[1]=='win' then
		print('DEBUG :  WIN side=', eff.side);
	end
	for k, v in pairs(eff) do 
		if type(k) == 'number' then
			-- do nothing
		else
			str = str .. k .. '=' ;
		end

		if type(v) == 'table' then
			str = str .. table_str(v) .. ', ';
		else
			if v==nil then v = '_nil_'; end
			str = str .. v .. ', ';
		end
	end
	str = str .. '|';

	print(str);
end

function print_eff_list(eff_list) 
	if nil == eff_list then
		print('ERROR print eff_list=nil');
		return ;
	end

	print ('effect list: #eff_list=' .. #eff_list);
	if eff_list == nil then
		return
	end
    for k,v in pairs(eff_list) do
		print_eff(v, 1);
	end
end

-- show index of my side: hero, ally and support
function print_ability_card(pside, s)
	local oneside = pside[s];
	local index;

	if nil ~= oneside[T_HERO][1].trigger_skill then
		index = card_index(s, T_HERO, 1);
		print_hero(oneside[T_HERO][1], index, 0, true);
	end

	for k, v in ipairs(oneside[T_ALLY] or {}) do
		if nil ~= v.trigger_skill then
			index = card_index(s, T_ALLY, k);
			print_card(v, index, 0, true);
		end
	end

	for k, v in ipairs(oneside[T_SUPPORT] or {}) do
		if nil ~= v.trigger_skill then
			index = card_index(s, T_SUPPORT, k);
			print_card(v, index, 0, true);
		end
	end
end

function print_target_list(target_list, id, tabstop) 
	local valid;
	local str ;
	local opt;
	local space = '';
	if target_list == nil then
		print('No target list');
		return ;
	end
	if id == nil then 
		id = '?';
	end
	if tabstop == nil then 
		tabstop = 0;
	end
	for i=1, tabstop*4 do
		space = space .. ' ';
	end
	-- str = table_str(target_list);
	valid = check_target_list(target_list); -- assign without use!
	for k, v in ipairs(target_list) do
		str = '';
		opt = '';
		if v.optional == true then 
			opt = 'optional';
		end
		for kk, vv in ipairs(v.table_list) do
			str = str .. vv .. ','
		end
	
		print(space .. 'Target ' .. k .. ' : side=' .. v.side, str, opt);
	end
end


-- TODO using list_ability_target() to replace this logic!!
function print_target_side(target, oneside)
	print('Side: ' .. oneside.name .. '(' .. oneside.id .. ')' );
	local tb;
	local offset;
	for j=1, #target.table_list do -- {
		tb = target.table_list[j];
		if tb == T_HERO then
			local hh = oneside[T_HERO][1];
			print_hero(hh, hh:index(), 1);
		elseif tb==T_WEAPON or tb==T_ARMOR then
			local ct ;
			if tb==T_WEAPON then
				ct = WEAPON;
			end
			if tb==T_ARMOR then
				ct = ARMOR;
			end
			print(table_name_map[tb] .. ':');
			for k, v in ipairs(oneside[T_SUPPORT] or {}) do 
				if v.ctype==ct then -- ct=51 or ct=52
					-- hard code support table, not tb, as tb=T_WEAPON
					offset = v:index(); -- card_index(oneside.id, T_SUPPORT, k);
					print_card(v, offset, 1);
				end
			end
		else 
			offset = card_index(oneside.id, tb, 0);
			print(tb .. ':');
			print_card_list(oneside[tb], offset);
		end
	end -- } for j=1 end
end

function print_target(target, pside, s)
	local myside;
	local opposide;
	print('--- print_target');
	myside = pside[s];
	opposide = pside[3-s];
	-- my side first
	if target.side==1 or target.side==3 then
		print_target_side(target, myside);
	end
	if target.side==2 or target.side==3 then
		print_target_side(target, opposide);
	end
end


function print_actual_target(at)
	print('at: side=' .. at.side .. '  table=' .. at.table .. '  pos=' .. at.pos);
end

function print_actual_target_list(actual_target_list)
	for k, at in ipairs(actual_target_list) do
		print_actual_target(at);
	end
end

-- print and input
function print_input_target(index, optional)
	local tinput;
	local str;
	str = '*** Select Target ' .. index;
	if true==optional then
		str = str .. '(optional)';
	end
	print(str);
	tinput = io.read();
	tinput = tonumber(tinput);
	return tinput;
end

function print_input(tag)
	local tinput;
	local str;
	str = '*** Select input ' .. (tag or '');
	print(str);
	tinput = io.read();
	tinput = tonumber(tinput);
	return tinput;
end

-- @see create_ai_play()
function print_ai_play(move, tag)
	tag = tag or '';
	local str;
	str = tag ;

	if move.weight then
		str = str ..  string.format('w:%-5d  id=%-5d  ', move.weight, 
		move.id);
	end

	str = str .. play_to_cmd(move);
	--[[
	if move.ability == true then
		str = str .. 'b '; -- peter: was 'ab'
	else
		str = str .. 't '; -- peter: was 'at'
	end
	str = str .. move.index .. ' ';
	for i=1, #move.atl do
		str = str .. move.atl[i] .. ' ';
	end
	]]--
	print(str);
end

function print_ai_play_detail(ppp, max, pside, sss)
	print('AI: weight=' .. max);
	local str = '';
	str = str .. ppp.index .. ':';
	if ppp.ability == true then
		str = str .. 'ability';
	else
		str = str .. 'attack';
	end
	print_card(index_card(ppp.index, pside), str);
	print('Target(s):');
	local target_list = index_card_list(ppp.atl, pside);
	for i=1, #target_list do
		local target = target_list[i];
		print_card(target, cindex(target));
	end
end



-- input actual target list, 
-- return null if cc == nil (or error case, e.g. non-optional target missing)
-- cc.target_list==nil or {}
-- for ability only (TODO may merge with attack case later)
function input_atl(cc, side, s)
	local num;
	local err;

	num, err = total_target(cindex(cc), side, s);
	if nil == num then
		print('ERROR input_atl total_target = nil, ', err);
		return nil, err;
	end

	if num == 0 then
		return {};
	end

	local flag = true;
	local index;
	local clist;
	local atl = {};
	index = cc:index();

	print('Total target = ' .. #(cc.target_list or {}));
	for k, t in ipairs(cc.target_list or {}) do
		local tindex;
		-- for attack: list_attack_target
		clist = list_ability_target(index, side, s, atl, k);
		print_index_list(clist, side);
		tindex = print_input_target(k, t.optional); -- *** Select Target
		if nil == table_find(clist, tindex) then
			flag = t.optional;
			break;
		end
		atl[#atl + 1] = tindex;
		print('');
	end

	if flag ~= true then
		return nil; 
	end

	return atl;
end

function get_hero_hp(side)
	local hh = g_logic_table[side][T_HERO][1];
	if hh == nil then
		return -3;
	end
	return hh.hp;
end

function get_hero_id(side)
	local hh = g_logic_table[side][T_HERO][1];
	local oppo_hh = g_logic_table[3-side][T_HERO][1];
	local hh_id = -3;
	local oppo_id = -3;
	if nil ~= hh then
		hh_id = hh.id;
	end
	if nil ~= oppo_hh then
		oppo_id = oppo_hh.id;
	end
	return hh_id, oppo_id;
end

--[[
-- old logic, remove later
function get_chapter_target()
	local hero_hp = get_hero_hp(1);

	local oppo_ally_num = #g_logic_table[2][T_ALLY];
	local oppo_support_num = #g_logic_table[2][T_SUPPORT];

	return hero_hp, g_round, g_chapter_up_ally, g_chapter_up_support, g_chapter_up_ability, oppo_ally_num, oppo_support_num, g_chapter_down_ability, g_chapter_up_card_id1, g_chapter_up_card_count1, g_chapter_up_card_id2, g_chapter_up_card_count2, g_chapter_up_card_id3, g_chapter_up_card_count3, g_chapter_down_card_id1, g_chapter_down_card_count1, g_chapter_down_card_id2, g_chapter_down_card_count2, g_chapter_down_card_id3, g_chapter_down_card_count3;
end
]]--

-- -----------------------------------
-- GAME TARGET
-- -----------------------------------
-- CHAPTER_TARGET_MY_HERO_HP                   1
-- CHAPTER_TARGET_ROUND                        2
-- CHAPTER_TARGET_WIN                          3
-- CHAPTER_TARGET_MY_HAND_ALLY                 4
-- CHAPTER_TARGET_MY_HAND_SUPPORT              5
-- -----------------------------------
-- CHAPTER_TARGET_MY_HAND_ABILITY              6
-- CHAPTER_TARGET_MY_CARD                      7
-- CHAPTER_TARGET_OPPO_ALLY                    8
-- CHAPTER_TARGET_OPPO_SUPPORT                 9
-- CHAPTER_TARGET_OPPO_HAND_ABILITY            10
-- -----------------------------------
-- CHAPTER_TARGET_OPPO_CARD                    11
-- CHAPTER_TARGET_MY_GRAVE                     12
-- CHAPTER_TARGET_OPPO_GRAVE                   13
-- CHAPTER_TARGET_MY_GRAVE_ALLY					14 // end game my grave ally counter
-- CHAPTER_TARGET_OPPO_GRAVE_ALLY				15 // end game oppo grave ally counter
-- -----------------------------------
-- CHAPTER_TARGET_MY_CARD_GRAVE                16 // end game my grave p1 card counter
-- CHAPTER_TARGET_OPPO_CARD_GRAVE              17 // end game oppo grave p1 card counter
-- CHAPTER_TARGET_MY_HERO
-- CHAPTER_TARGET_OPPO_HERO
function get_solo_target(target, p1, p2) --{

	-- CHAPTER_TARGET_MY_HERO_HP                   1
	if target == 1 then
		local my_hero_hp = get_hero_hp(1);
		return my_hero_hp;
	end

	-- CHAPTER_TARGET_ROUND                        2
	if target == 2 then
		return g_round;
	end

	-- CHAPTER_TARGET_WIN                          3
	if target == 3 then
		-- XXX not work
		return 0;
	end

	-- CHAPTER_TARGET_MY_HAND_ALLY                 4
	if target == 4 then
		local num = g_logic_table[1].num_hand_ally or 0;
		return num;
	end

	-- CHAPTER_TARGET_MY_HAND_SUPPORT              5
	if target == 5 then
		local num = g_logic_table[1].num_hand_support or 0;
		return num;
	end

	-- CHAPTER_TARGET_MY_HAND_ABILITY              6
	if target == 6 then
		local num = g_logic_table[1].num_hand_ability or 0;
		return num;
	end

	-- CHAPTER_TARGET_MY_CARD                      7
	if target == 7 then
		g_logic_table[1].use_card_table = g_logic_table[1].use_card_table or {};
		local num = g_logic_table[1].use_card_table[p1] or 0;
		return num;
	end

	-- CHAPTER_TARGET_OPPO_ALLY                    8
	if target == 8 then
		local num = #g_logic_table[2][T_ALLY];
		return num;
	end

	-- CHAPTER_TARGET_OPPO_SUPPORT                 9
	if target == 9 then
		local num = #g_logic_table[2][T_SUPPORT];
		return num;
	end

	-- CHAPTER_TARGET_OPPO_HAND_ABILITY            10
	if target == 10 then
		local num = g_logic_table[2].num_hand_ability or 0;
		return num;
	end

	-- CHAPTER_TARGET_OPPO_CARD                    11
	if target == 11 then
		local num = g_logic_table[2].use_card_table[p1] or 0;
		return num;
	end

	-- CHAPTER_TARGET_MY_GRAVE	                    12
	if target == 12 then
		local num = #g_logic_table[1][T_GRAVE];
		return num;
	end

	-- CHAPTER_TARGET_OPPO_GRAVE	                13
	if target == 13 then
		local num = #g_logic_table[2][T_GRAVE];
		return num;
	end

	-- CHAPTER_TARGET_MY_GRAVE_ALLY					14 // end game my grave ally counter
	if target == 14 then
		local num = 0;
		for i=1, #g_logic_table[1][T_GRAVE] do 
			local cc = g_logic_table[1][T_GRAVE][i];
			if cc.ctype == ALLY then
				num = num+1;
			end
		end
		return num;
	end

	-- CHAPTER_TARGET_OPPO_GRAVE_ALLY				15 // end game oppo grave ally counter
	if target == 15 then
		local num = 0;
		for i=1, #g_logic_table[2][T_GRAVE] do 
			local cc = g_logic_table[2][T_GRAVE][i];
			if cc.ctype == ALLY then
				num = num+1;
			end
		end
		return num;
	end

	-- CHAPTER_TARGET_MY_CARD_GRAVE                16 // end game my grave p1 card counter
	if target == 16 then
		local num = 0;
		for i=1, #g_logic_table[1][T_GRAVE] do 
			local cc = g_logic_table[1][T_GRAVE][i];
			if cc.id == p1 then
				num = num+1;
			end
		end
		return num;
	end

	-- CHAPTER_TARGET_OPPO_CARD_GRAVE              17 // end game oppo grave p1 card counter
	if target == 17 then
		local num = 0;
		for i=1, #g_logic_table[2][T_GRAVE] do 
			local cc = g_logic_table[2][T_GRAVE][i];
			if cc.id == p1 then
				num = num+1;
			end
		end
		return num;
	end
		
	-- CHAPTER_TARGET_MY_HERO
	if target == 18 then
		local num = 0;
		if g_logic_table[1][T_HERO][1].id == p1 then
			num = 1;
		end
		return num;
	end

	-- CHAPTER_TARGET_OPPO_HERO
	if target == 19 then
		local num = 0;
		if g_logic_table[2][T_HERO][1].id == p1 then
			num = 1;
		end
		return num;
	end

	return 0;
end --}


----- PRINT END


function cheat_ccd(input_list) 
	if #input_list < 2 then 
		print('ERROR: ccd index');
		return;
	end
	local index = tonumber(input_list[2]);
	local cc = index_card(index, g_logic_table);
	if nil == cc then
		print('ERROR ccd card = nil : ', index);
		return;
	end
	print(table_str(cc));
end

function cheat_cccard(input_list)
	local id = tonumber(input_list[2]);
	if id ~= nil then
		print_card(g_card_list[id], id, 0, true); -- desc=true 
		return;
	end
	local total = 0;
	local sorted_list = {};
	local str = '';
	for k, v in pairs(g_card_list) do
		total = total + 1;
		sorted_list[total] = v;
	end
	table.sort(sorted_list, card_compare);
	for k, v in ipairs(sorted_list) do
		str = str .. v.id .. ', '
		print_card(v, v.id, 0);
	end
	str = 'Valid card list: ' .. total .. '\n' .. str;
	print(str);
end

---------- DECK START ----------

-- standard card deck is 40 cards
deck_boris = {   -- id = 1
		-- 5 id a row
	22, 22, 23, 23, 26, -- dirk x 2, sandra x 2, puwen x 3
	26, 26, 27, 27, 28, -- birgitte x 2, kurt x 1, 
	30, 30, 30, 61, 61, -- blake x 3, valiant x 2, 
	63, 63, 64, 64, 65, -- warbanner x 2, smashing x 2, enrage x 1, train x 1
	66, 67, 67, 69, 69, -- train x 1, crippling x 2, shieldbash x 2, 
	70, 70,	131, 132, 133, -- blood x 2, c_o_night, retreat, armor, 
	134, 135, 151, 151, 154, -- special, campfire, rain x 2, extrasharp x 2
	154, 155, 155, 188, 188, -- bazaar x 2, rustylongsword x 2
};
deck_nishaven = { -- id = 5 or 6   nishaven or frostmire
		-- 5 id a row
	22, 22, 23, 23, 26, -- dirk x 2, sandra x 2, puwen x 3
	26, 26, 27, 27, 28, -- birgitte x 2, kurt x 1, 
	30, 30, 30, 71, 71, -- blake x 3, fireball x 2, 
	72, 72, 73, 74, 74, -- freezing(72)x2, poison(73), lightning(74)x2
	75, 76, 76, 79, 79, -- flames(75), tome(76)x2, arcane(79)x2
	80, 80, 131, 132, 133, -- webs(80)x2, c_o_night(131), retreat, armor
	134, 135, 151, 151, 154, -- special, campfire, rain(151)x2, extra(154)x2
	154, 155, 155, 172, 172, --bazaar(155)x2, dome(172)x2
};

deck_zhanna = { -- id = 7 or 8(zhanna)
		-- 5 id a row
	22, 22, 23, 23, 26, -- dirk x 2, sandra x 2, puwen x 3
	26, 26, 27, 27, 28, -- birgitte x 2, kurt x 1, 
	30, 30, 30, 91, 91, -- blake x 3, healingtouch(91) x 2
	92, 92, 94, 94, 95, -- innerstr(92)x2, icestorm(94)x2, focus prayer x 2
	95, 96, 97, 97, 99,	-- resurrection(96), holyshield(97)x2, smite(99)x2
	99, 100, 131, 132, 133, -- book of curses(100), c_o_night, retreat, armor
	134, 135, 151, 151, 154, -- special, campfire, rain(151)x2, extra(154)x2
	154, 155, 155, 173, 173, --bazaar(155)x2, plate armor(172)x2
};

deck_majiya = {  -- id = 15  (majiya)
		-- 5 id a row
	41, 41, 42, 42, 43, -- deathbone x 2,  keldor x 2, gargoyle(43) x2
	43, 44, 44, 45, 47, -- brutalis(44)x2, plasma(45), firesnake(47)x2
	47, 48, 48, 71, 71, -- belladonna(48)x2, fireball(71)x2
	72, 72, 73, 74, 74, -- freezing(72)x2, poison(73), lightning(74)
	75, 76, 76, 79, 79, -- flames(75), tome(76)x2, arcane(79)x2
	80, 80, 142, 143, 143, -- webs(80)x2, monsters(142), shadowspawn(143) x2
	144, 144, 151, 151, 154, -- shriek(144)x2, rain(151)x2, extra(154)x2
	154, 155, 155, 172, 172, --bazaar(155)x2, dome(172)x2
};

deck_ahero = {
	25, 25, 25, 25, 71	-- 25=kristoffer(haste), 71=fireball
	, 71, 99, 154 			-- 99=smite,  154=extra sharp
};


-- given a hero id, return the standard deck id list
-- input: hero_id  (e.g. 1=boris,  15=majiya)  1-20
-- output:  list of card id, e.g. {41, 41, 42, 42, ...}
-- note: not include the hero card
function get_standard_deck(hero_id)
	if hero_id == 1 or hero_id == 2 then
		return deck_boris;  -- human warrior: boris, amber
	end
	if hero_id == 6 or hero_id == 5 then
		return deck_nishaven;  -- human mage, eladwen frostmire, nishaven
	end
	if hero_id == 8 then
		return deck_zhanna; -- priest female +3 hp
	end
	if hero_id == 15 then
		return deck_majiya;  -- firemage
	end

	if hero_id == 19 then
		return deck_ahero; -- ahero is a testing human warrior
	end

	if hero_id == 20 then
		return deck_ahero; -- ahero is a testing human warrior
	end

	print('BUGBUG get_standard_deck hero_id not found : ', hero_id);
	return {}
end


function get_standard_deck_array(hero_id)
	local deck;
	deck = get_standard_deck(hero_id);
	if deck==nil or #deck<=0 then
		return nil;
	end

	local array = {}; -- 400  number array
	for i=1,400 do 
		array[i] = 0;
	end
	for i=1, #deck do
		local id = deck[i];
		if id <= 0 or id > 400 then
			print('BUGBUG get_standard_deck_array outbound ', id);
			break;
		end
		if array[id] >= 9 then
			print('BUGBUG get_standard_deck_array #card > 9: ', id);
			-- no add
		else
			array[id] = array[id] + 1;
		end
	end

	-- add hero:
	array[hero_id] = 1; -- hard code

	local array_str = '';
	for i=1, 400 do
		array_str = array_str .. array[i];
	end
	return array_str;
end


function shuffle_deck(deck)
	local max = #deck;
	local b;
	for a=1, max do
		b = math.random(a, max);
		deck[a], deck[b] = deck[b], deck[a]; -- swap
	end
	--[[
	local total_swap = #deck * 99;
	local a, b;
	-- if true then return end;
	for i=1, total_swap do
		a = math.random(1, max);
		b = math.random(1, max);
		deck[a], deck[b] = deck[b], deck[a]; -- swap
	end
	]]--
end


---------- DECK END ----------


-------- TEST START  test and debug, use t[n] to execute

test_map = 
{
	[1] = 	function ()
				local out = split_num('aa 11bb cc55 dd 33');
				local str = '';
				for i=1, #out do
					str = str .. ' , ' .. out[i];
				end
				print (str);
				return 0;
			end,
	[2] =	function ()
				local out = split_num('aa 45 cc');
				print (out[1], out[2]+5, out[3]);
				return 0;
			end,

	[3] = 	function ()
				local eff ;
				eff = eff_power_offset(1301, 1);	
				print_eff(eff, 1);
			end,
	[4] =	function ()
				local str = 't4';
				local s ;
				s = string.gsub(str, '(%a+)', '%1 ');
				print(s);
			end,

	[5] = 	function ()
				local deck = { 22, 33, name='deck'};
				print(#deck);
				print('magic = ' .. r_damage_map['magic']);
			end,
	[6] = 	function (pside) 
				local cc;
				local index;
				index = 1101; cc = index_card(index, pside); -- hero up side
				print_card(cc, index);

				index = 1203; cc = index_card(index, pside); -- hand 3
				print_card(cc, index);

				index = 13021; cc = index_card(index, pside); -- ally 2
				print_card(cc, index);

				-- oppo side
				cc = index_card(2302, pside); -- oppo ally 1
				print_card(cc, 2302);

				index = card_index(1, T_HAND, 1); 
				print('index of 1, hand, 1 = ' .. index); -- expect 1201
				index = card_index(2, T_HERO, 1); -- ignore p (pos)
				print('index of 1, hero, 1 = ' .. index); -- expect 2101
				index = card_index(1, T_ALLY, 2);  
				print('index of 1, ally, 2 = ' .. index); -- expect 1302

			end,

	[7] = 	function(pside)
				local tb;
				print('tb = ' , index_table(2201));
				print('tb = ' , index_table(nil));
				print('tb = ' , index_table(2));
				print('tb = ' , index_table(2900));
			end,
	[8] = function (pside)
				local ret;
				ret = check_action(index_card(2201, pside), pside, 1);
				print('2201 : ret = ' .. ret);

				ret = check_action(index_card(1102, pside), pside, 1);
				print('1102 : ret = ' .. ret);
				
				ret = check_action(index_card(1103, pside), pside, 1);
				print('1103 : ret = ' .. ret);

				ret = check_action(index_card(1201, pside), pside, 1);
				print('1201 : ret = ' .. ret);
			end,
	[9] = function (pside, sss) -- test9 aisac read-only
		local index = weight_sac(pside, sss);
		print('AI sacrifice index=' .. index);
		print_card( index_card(index, pside), index, 1);
		end,

	[10] = function (pside)
		local s, t, p, attpos;
		print('hero table  = ' , index_table_num(2101));
		s, t, p, attpos = index_stp(22013);
		print('s,t,p,ap = ', s, t, p, attpos);
		s, t, p = index_stp(22013);
		print('s,t,p = ', s, t, p);

	end,
		
	[11] = function(pside, sss, input_list)
		local list;
		local index;
		index = tonumber(input_list[3]) or 2202;
		list = list_valid_target_list(index, pside, sss);
		print('Valid list index,# : ', index, #list);
		print_2d(list);
	end,
	
	[12] = function(pside, sss, input_list) -- start test12 {
		print('test12:  list_all_play()');
		local list;
		list = list_all_play(pside, sss);
		print('----- All PLAY: ', #list);
		for i=1, #list do 
			print_ai_play(list[i], 'MOVE ' .. i .. ':  ');
		end

	end, -- end test12 }

	[13] = function(pside, sss, input_list) -- start test13 {
		-- test ai_weight_attack
		print('test13 : ai_weight_attack');
		local index = (input_list[3]) or 2302;  -- puwen
		local list;
		local src_card = index_card(index, pside);

		if src_card == nil then
			print('card is nil index=', index);
			return;
		end

		list = list_attack_target(index, pside, sss);
		print('src card:');
		print_card(src_card, index);
		local max = 0; 
		local max_index = 0;
		for i=1, #list do
			local atl = index_card_list({ list[i] }, pside);
			local weight;
			local cc;
			local kill = 0;
			local die = 0;
			local str;
			weight, kill, die = ai_weight_attack(src_card, atl, pside, sss );
			if weight > max then
				max, max_index = weight, i;
			end
			if kill == true then kill = 'kill'; end
			if die == true then die = 'die'; end
			kill = kill or '----';
			die = die or '---';
			cc = atl[1]; -- index_card(atl[1], pside);
			str = string.format('TAR[%d] Weight:%4d %4s/%3s  cost:%-2dpower:%-3d(%3d)%-20s', 
				list[i], weight, kill, die, cc.cost, cc.power, cc.id, cc.name);
			print(str);
		end 
		if max <= 0 then
			print('No play > 0');
			return;
		end
		print_card(src_card, 'SRC' .. index);
		print('Best play:  weight = ' .. max);
		local atl = { list[max_index] }; -- attack is different
		print_index_list(atl, pside);
	end, -- end test13 }

	[14] = function(pside, sss, input_list) -- start test14 {
		print('test14 : ai_weight_ability_damage');
		local index = (input_list[3]) or 2202;
		local list;
		local src_card = index_card(index, pside);

		if src_card == nil then
			print('card is nil index=', index);
			return;
		end

		list = list_valid_target_list(index, pside, sss);

		print_card(src_card, index);
		local max = 0; 
		local max_index = 0;
		for i=1, #list do
			local atl = index_card_list(list[i], pside);
			local weight, kill, dam;
			local str = 'TAR(' .. #atl .. ') ' ;
			weight, kill, dam 
			= ai_weight_ability_damage(src_card, atl, pside, sss, true);
			if weight > max then
				max, max_index = weight, i;
			end
			for j=1, #list[i] do str = str .. list[i][j] .. ' ' ; end
			str = string.format('%s Weight:%4d %4s/%3s ' ,
				str, weight, kill, dam);
			print(str);
		end
		-- ok, we are going to print the best play
		if max <= 0 then
			print('No play > 0 quit');
			return;
		end
		local atl = list[max_index];
		print_card(src_card, 'SRC' .. index);
		print('Best play:  weight = ' .. max);
		print_index_list(atl, pside);
	end, -- end test14 }
			
	[15] = function(pside, sss, input_list) -- start test15 {
		print('test15: ai_weight_control');
		local index = (input_list[3]) or 2203;
		local list;
		local src_card = index_card(index, pside);

		if src_card == nil then
			print('card is nil index=', index);
			return;
		end

		list = list_valid_target_list(index, pside, sss);

		print_card(src_card, index);
		local max = 0; 
		local max_index = 0;
		for i=1, #list do
			local atl = index_card_list(list[i], pside);
			local cc = atl[1];
			local weight, round, power;
			local str = '';
			if cc == nil then
				print('Invalid target cc=nil #atl=' .. #atl);
				break;
			end
			weight, round, power 
			= ai_weight_control(src_card, atl, pside, sss, true) -- 
			if weight > max then
				max, max_index = weight, i;
			end
			str = string.format('[%d] Weight:%4d round:%d  cost:%-2dpower:%-3d(%3d)%-20s',
				list[i][1], weight, round,
				cc.cost, cc.power, cc.id, cc.name);
			print(str);
		end
		-- ok, we are going to print the best play
		if max <= 0 then
			print('No play > 0 quit');
			return;
		end
		print_card(src_card, 'SRC' .. index);
		print('Best play:  weight = ' .. max);
		local atl = list[max_index];
		print_index_list(atl, pside);
	end, -- end test15 }

	[16] = function(pside, sss, input_list) -- start test16 {
		print('test16: weight_all_play');
		local list;
		local max_play, max;
		list = list_all_play(pside, sss);
		max_play, max = weight_all_play(list, pside, sss);

		if nil == max_play then
			print('No play available');
			return ;
		end

		for i=1, #list do
			print_ai_play(list[i]);
		end

		print('\nMax play : (weight=' .. max .. ')');
		print_ai_play_detail(max_play, max, pside, sss);
	end, -- end test16 }

	[17] = function(pside, sss, input_list)  -- {  start test17
		local str;
		str = get_standard_deck_array(1);
		print('standard deck array: hero=1');
		print(str);

		str = get_standard_deck_array(8); -- priest
		print('standard deck array: hero=8');
		print(str);

		-- non-exist hero
		str = get_standard_deck_array(88); -- 88 no such hero
		print('standard deck array: hero=88');
		print(str);
	end, -- end test17 }

	-- test for game_chapter and game_solo_plus
	[18] = function(pside, sss, input_list)  -- {  start test18
		local str;
		local array;
		local ret;
		local cmd;
		str = '150715182701001 15 1 3 234 -1 3397 501 502 ';
		print(str);
		-- array = split_num(str);
		array = split(str);
		str = join_str(array, 4, 6);
		print('join(4,6) = [' .. str .. ']');

		-- TODO more test case for bad format replay
		-- str = '150716152939001 15 1 2 38137 1 3397 547 2 10 1 0 1 x solo12 1 1000000000000000000002200321030000000000000000000000000000002022112022000000000000000000000000000000000000000000000000000000000000111110000000000000002002200000000000000000000000000000000200000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000 6 1 22 23 26 30 30 6 1 0 1 15 99 10000 1 n ;2 s 1201;3 n;4 s 2201;5 n;';
		-- 9 was 6

		-- str = '150717101705001 15 1 3 68293 1 3438 5286 11 18 1 1 8 p9 1-1 13 1 30 30 26 26 22 22 67 67 64 64 135 135 13 8 91 91 22 91 96 96 96 96 95 95 95 95  6 11 0 2 10 5 001000000000 1 s 1201;2 n ;3 s 2201;4 n;5 s 1201;6 b 1201;7 n ;8 s 2201;9 b 2201;10 n;11 s 1201;12 b 1201;13 t 1301 2301;14 n ;15 s 2201;16 n;17 s 1201;18 t 1301 2101;';

		-- str = '150717112816001 15 1 2 83673 1 3438 547 1 10 1 0 1 x solo11 9 1 22 25 26 22 26 26 26 26 6 1 22 23 26 30 30 7 1 10000 0 15 1 99 0 1 n ;';

		str = '150717161333001 15 2 0 59953 1 3448 1074 17 23 1 5 8 masha 1-7 16 1 67 67 64 64 23 26 135 26 135 22 135 22 23 30 30  18 8 94 94 94 29 31 36 22 39 29 22 94 22 22 26 26 26 26  7 1 11100010011111111 1 10 8 5 0 1 s 1204;2 n ;3 s 2204;4 n;5 s 1204;6 b 1204;7 n ;8 s 2204;9 b 2205;10 n;11 s 1204;12 b 1202 2301;13 n ;14 s 2204;15 n;16 fold 1;';

		ret , cmd = game_init(str);

		print('ret=' , ret , ' cmd=[' , cmd , ']');

	end, -- end test18 }

	-- test normal solo and room game
	[19] = function(pside, sss, input_list)  -- {  start test19
		local str;
		local array;
		local ret;
		local cmd;

		str = '150717113137001 1 2 0 93718 2 3448 547 1 10 1 0 0 x AI男战初级 1 1000000000000000000002200321030000000000000000000000000000002022112022000000000000000000000000000000000000000000000000000000000000111110000000000000002002200000000000000000000000000000000200000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000 1 1000000000000000000002200321030000000000000000000000000000002022112022000000000000000000000000000000000000000000000000000000000000111110000000000000002002200000000000000000000000000000000200000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000 7 0 0 0 0 0 0 0 1 s 2201;2 b 2201;3 n;4 n ;5 s 2205;6 b 2203;7 n;8 n ;9 s 2205;10 b 2201;11 n;12 n ;13 s 2203;14 t 2302 1101;15 b 2202;16 b 2203;17 n;18 n ;19 s 2204;20 t 2303 1101;21 t 2302 1101;22 t 2304 1101;23 b 2202;24 b 2202;25 t 2301 1101;26 n;27 n ;28 s 2202;29 t 2303 1101;30 t 2302 1101;31 t 2304 1101;32 b 2202;33 t 2101 1101;34 t 2301 1101;35 n;36 n ;37 s 2203;38 t 2303 1101;39 t 2302 1101;';

		ret , cmd = game_init(str);

		print('ret=' , ret , ' cmd=[' , cmd , ']');

	end, -- end test19 }

	-- for gate
	[20] = function(pside, sss, input_list)  -- {  start test20
		local str;
		local array;
		local ret;
		local cmd;

		str = '150717113417001 7 2 0 72726 2 3448 547 1 10 1 0 1 x gate1 1 1000000000000000000002200321030000000000000000000000000000002022112022000000000000000000000000000000000000000000000000000000000000111110000000000000002002200000000000000000000000000000000200000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000 6 6 51 6 51 6 51 7 0 0 0 0 0 0 0 1 n ;2 s 0;3 n;4 n ;5 s 0;6 n;7 n ;8 s 0;9 b 2201;10 b 2201;11 b 2201;12 n;13 n ;14 s 0;15 n;16 n ;17 s 0;18 n;19 n ;20 s 0;21 n;22 n ;23 s 0;24 n;25 fold 1;';

		ret , cmd = game_init(str);

		print('ret=' , ret , ' cmd=[' , cmd , ']');

	end, -- end test20 }

	-- for robot game
	[21] = function(pside, sss, input_list)  -- {  start test21
		local str;
		local array;
		local ret;
		local cmd;

		-- g_logic_table = nil;

		str = '150720105946001 11 2 0 94412 2 3448 547 1 10 10 0 1 x robot1 1 1000000000000000000002200321030000000000000000000000000000002022112022000000000000000000000000000000000000000000000000000000000000111110000000000000002002200000000000000000000000000000000200000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000 19 1 22 26 26 26 26 26 22 26 26 22 26 26 22 26 26 26 26 22 7 0 0 0 10 0 5 99 1 s 2202;2 n;3 n ;4 s 2202;5 b 2201;6 n;7 n ;8 s 2202;9 t 2301 1101;10 b 2201;11 n;12 n ;13 s 2203;14 t 2301 1101;15 t 2302 1101;16 b 2201;17 b 2201;18 n;19 n ;20 s 0;21 t 2301 1101;22 t 2302 1101;';

		ret , cmd = game_init(str);

		print('ret=' , ret , ' cmd=[' , cmd , ']');

	end, -- end test21 }
};


----------------- INIT START -----------------

--[[
pside_test = {
	side_up = {
		-- hero, deck, hand, ally, support, grave, 
		hero = {1}
		,deck = {}
		,hand = {}
		,ally = {}
		,support = {}
		,grave = {}
	}
	,	
	side_down = {
		...
	}
}

]]--

-- this is for testing
function logic_init_test(pside_test) -- {
-- init all structures on both side
g_round = 1; -- TODO need to ++ when next
g_current_side = 1;   -- side, up first 
g_phase = PHASE_SACRIFICE; -- init as 
g_logic_table = {};   -- 1=up,  2=down

g_logic_table[1] = {
	[T_HERO] 	= {side=1, name=T_HERO},
	[T_HAND] 	= {side=1, name=T_HAND}, -- card on hand, init 6 cards from deck
	[T_ALLY] 	= {side=1, name=T_ALLY},  -- ally in play
	[T_SUPPORT] = {side=1, name=T_SUPPORT},
	[T_GRAVE] 	= {side=1, name=T_GRAVE},
	[T_DECK] 	= {side=1, name=T_DECK},  -- list of card deck
	id = 1,
	name = "UP",
	resource = 0,
	resource_max = 99,
	energy_max = 25,
}
-- do not clone from side[1]
g_logic_table[2] = {
	[T_HERO] 	= {side=2, name=T_HERO},
	[T_HAND] 	= {side=2, name=T_HAND}, -- card on hand, init 6 cards from deck
	[T_ALLY] 	= {side=2, name=T_ALLY},  -- ally in play
	[T_SUPPORT] = {side=2, name=T_SUPPORT},
	[T_GRAVE] 	= {side=2, name=T_GRAVE},
	[T_DECK] 	= {side=2, name=T_DECK},  -- list of card deck
	id = 2,
	name = "DOWN",
	resource = 0,
	resource_max = 99,
	energy_max = 30,
}


if pside_test == nil then

-- 22=dirk(no_defend),  23=sandra(resource-1), 26=puwen(2power 3hp)
-- 27=birgitte(protector)  28=kurt whitehelm(ability-1)  30=blake(3power 1hp)
-- 63=war banner(+1 power)  64=smashing blow(break weapon/armor)
-- 65=enrage(hero +10hp)   66=warrior training(train protector, attach?)
-- 67=cripping blow(0 power, attach),  69=shield bash(ally 3 damage)
-- 70=blood frenzy(+1 drawcard -1hp),  131=cover of night(stealth)
-- mage:  71=fireball(4 damage), 74=Lightning Strike (3x2 damage)
-- shadow:  41=deathbone(die 2 damage)
-- 1 =  boris skullcrusher (warrior)
-- 5 =  Nishaven (mage)
card_init_hero(g_logic_table[1], 15);  -- 1=warrior boris, 6=ice

-- card_init_table(table, clist, g_logic_table)  
card_init_table(g_logic_table[1][T_DECK], {131, 22, 23, 86, 87}, g_logic_table);
-- card_init_table(g_logic_table[1][T_DECK], get_standard_deck(1), g_logic_table);
--card_init_table(g_logic_table[1][T_HAND], {94, 95, 97, 99, 100, 22, 26, 73, 91, 67, 100, 25, 51, 78, 77, 68, 158, 153, 151, 32}, g_logic_table);
card_init_table(g_logic_table[1][T_HAND], {94, 149, 149, 149, 143, 35}, g_logic_table);
-- card_init_table(g_logic_table[1][T_ALLY], {25, 51, 25, 25, 25, 25, 49}, g_logic_table);
card_init_table(g_logic_table[1][T_ALLY], {22, 22, 22, 22, 22}, g_logic_table);
-- card_init_table(g_logic_table[1][T_ALLY], {30}, g_logic_table); -- TEMP DEBUG
card_init_table(g_logic_table[1][T_SUPPORT], {136}, g_logic_table);
card_init_table(g_logic_table[1][T_GRAVE], {22, 26, 71}, g_logic_table);

card_init_hero(g_logic_table[2], 11);  -- 11=ter adun, 5=nishaven, 6=ice 
card_init_table(g_logic_table[2][T_DECK], {22, 23, 24, 25, 26}, g_logic_table);
-- card_init_table(g_logic_table[2][T_DECK], get_standard_deck(15), g_logic_table);
-- kurt(28), lightning(74), webs(80), smashingblow(64), retreat(132)
-- warbanner(63), valiantdefender(61), crippling blow(67)
--card_init_table(g_logic_table[2][T_HAND], {96, 101, 63, 28, 74, 80, 64, 144, 91, 132, 92, 154, 157, 59, 38, 152, 132, 33}, g_logic_table); 
card_init_table(g_logic_table[2][T_HAND], {23, 149, 149, 40, 81, 95, }, g_logic_table); 
-- order is important, if Aldon is added before Dirk, +1 attack is not effective
card_init_table(g_logic_table[2][T_ALLY], {21, 22, 24, 28, 32, 49 }, g_logic_table); 
card_init_table(g_logic_table[2][T_SUPPORT], {145}, g_logic_table); 
card_init_table(g_logic_table[2][T_GRAVE], {71}, g_logic_table);

	-- attachment test:
	-- 1302=armor sandworm SIDE UP ally, 2
	-- 2302=dirk SIDE DOWN ally 2
	
	-- open this if you need attach at start!
	--[[
	card = index_card(1301, g_logic_table);
	if card ~= nil then
		local att_card;
		att_card = clone(g_card_list[66]);
		att_card.home = 2;
		att_card.src = g_logic_table[2][T_HERO][1];
		card_attach(card, att_card); -- cannot use action_attach

		att_card = clone(g_card_list[88]);
		att_card.home = 2;
		att_card.src = g_logic_table[2][T_HERO][1];
		card_attach(card, att_card); -- cannot use action_attach

		att_card = clone(g_card_list[67]);
		att_card.home = 2;
		att_card.src = g_logic_table[2][T_HERO][1];
		card_attach(card, att_card); -- cannot use action_attach

		att_card = clone(g_card_list[80]);
		att_card.home = 2;
		att_card.src = g_logic_table[2][T_HERO][1];
		card_attach(card, att_card); -- cannot use action_attach

		att_card = clone(g_card_list[92]);
		att_card.home = 2;
		att_card.src = g_logic_table[2][T_HERO][1];
		card_attach(card, att_card); -- cannot use action_attach

		att_card = clone(g_card_list[133]);
		att_card.home = 2;
		att_card.src = g_logic_table[2][T_HERO][1];
		card_attach(card, att_card); -- cannot use action_attach

		att_card = clone(g_card_list[1073]);
		att_card.home = 2;
		att_card.src = g_logic_table[2][T_HERO][1];
		card_attach(card, att_card); -- cannot use action_attach

		att_card = clone(g_card_list[1021]);
		att_card.home = 2;
		att_card.src = g_logic_table[2][T_HERO][1];
		card_attach(card, att_card); -- cannot use action_attach

		att_card = clone(g_card_list[1072]);
		att_card.home = 2;
		att_card.src = g_logic_table[2][T_HERO][1];
		card_attach(card, att_card); -- cannot use action_attach
	end
	]]--

else -- logic_init_test(pside_test) 
	print('logic_init_test:use pside_test!!!');	
	local s_up;
	local s_down;
	s_up = pside_test.side_up;
	s_down = pside_test.side_down;

	if s_up == nil or s_down == nil then
		print('BUGBUG logic_init_test pside_test bug');
		return;
	end

	-- up side --
	card_init_hero(g_logic_table[1], s_up.hero[1]);
	card_init_table(g_logic_table[2][T_DECK], s_up.deck, g_logic_table);
	card_init_table(g_logic_table[1][T_HAND], s_up.hand, g_logic_table);
	card_init_table(g_logic_table[1][T_ALLY], s_up.ally, g_logic_table);
	card_init_table(g_logic_table[1][T_SUPPORT], s_up.support, g_logic_table);
	card_init_table(g_logic_table[1][T_GRAVE], s_up.grave, g_logic_table);


	-- down side -- 
	card_init_hero(g_logic_table[2], s_down.hero[1]);  
	card_init_table(g_logic_table[2][T_DECK], s_down.deck, g_logic_table);
	card_init_table(g_logic_table[2][T_HAND], s_down.hand, g_logic_table); 
	card_init_table(g_logic_table[2][T_ALLY], s_down.ally, g_logic_table); 
	card_init_table(g_logic_table[2][T_SUPPORT], s_down.support, g_logic_table); 
	card_init_table(g_logic_table[2][T_GRAVE], s_down.grave, g_logic_table);


end

	-- hard code more energy:
	g_logic_table[1][T_HERO][1].energy = 20;
	g_logic_table[2][T_HERO][1].energy = 20;

	-- setup resource max
	g_logic_table[g_current_side].resource = 
		g_logic_table[g_current_side].resource_max;

	-- set ready
	for i=T_HERO, T_SUPPORT do
		card_all_ready(g_logic_table[g_current_side][i]);
	end

end -- logic_init_test }



-- this is for testing
function gate_init_test(pside_test) -- {
	-- init all structures on both side
	g_round = 1; -- TODO need to ++ when next
	g_current_side = 1;   -- side, up first 
	g_phase = PHASE_SACRIFICE; -- init as 
	g_logic_table = {};   -- 1=up,  2=down

	g_logic_table[1] = {
		[T_HERO] 	= {side=1, name=T_HERO},
		[T_HAND] 	= {side=1, name=T_HAND}, -- card on hand, init 6 cards from deck
		[T_ALLY] 	= {side=1, name=T_ALLY},  -- ally in play
		[T_SUPPORT] = {side=1, name=T_SUPPORT},
		[T_GRAVE] 	= {side=1, name=T_GRAVE},
		[T_DECK] 	= {side=1, name=T_DECK},  -- list of card deck
		id = 1,
		name = "UP",
		resource = 0,
		resource_max = 99,
	}
	-- do not clone from side[1]
	g_logic_table[2] = {
		[T_HERO] 	= {side=2, name=T_HERO},
		[T_HAND] 	= {side=2, name=T_HAND}, -- card on hand, init 6 cards from deck
		[T_ALLY] 	= {side=2, name=T_ALLY},  -- ally in play
		[T_SUPPORT] = {side=2, name=T_SUPPORT},
		[T_GRAVE] 	= {side=2, name=T_GRAVE},
		[T_DECK] 	= {side=2, name=T_DECK},  -- list of card deck
		id = 2,
		name = "DOWN",
		resource = 0,
		resource_max = 0,
	}



	-- 22=dirk(no_defend),  23=sandra(resource-1), 26=puwen(2power 3hp)
	-- 27=birgitte(protector)  28=kurt whitehelm(ability-1)  30=blake(3power 1hp)
	-- 63=war banner(+1 power)  64=smashing blow(break weapon/armor)
	-- 65=enrage(hero +10hp)   66=warrior training(train protector, attach?)
	-- 67=cripping blow(0 power, attach),  69=shield bash(ally 3 damage)
	-- 70=blood frenzy(+1 drawcard -1hp),  131=cover of night(stealth)
	-- mage:  71=fireball(4 damage), 74=Lightning Strike (3x2 damage)
	-- shadow:  41=deathbone(die 2 damage)
	-- 1 =  boris skullcrusher (warrior)
	-- 5 =  Nishaven (mage)
	card_init_hero(g_logic_table[1], 2);  -- 1=warrior boris, 6=ice

	-- card_init_table(table, clist, g_logic_table)  
	card_init_table(g_logic_table[1][T_DECK], {27, 30, 36, 63}, g_logic_table);
	-- card_init_table(g_logic_table[1][T_DECK], get_standard_deck(1), g_logic_table);
	--card_init_table(g_logic_table[1][T_HAND], {94, 95, 97, 99, 100, 22, 26, 73, 91, 67, 100, 25, 51, 78, 77, 68, 158, 153, 151, 32}, g_logic_table);
	card_init_table(g_logic_table[1][T_HAND], {23,24, 25}, g_logic_table);
	-- card_init_table(g_logic_table[1][T_ALLY], {25, 51, 25, 25, 25, 25, 49}, g_logic_table);
	card_init_table(g_logic_table[1][T_ALLY], {}, g_logic_table);
	-- card_init_table(g_logic_table[1][T_ALLY], {30}, g_logic_table); -- TEMP DEBUG
	card_init_table(g_logic_table[1][T_SUPPORT], {}, g_logic_table);
	card_init_table(g_logic_table[1][T_GRAVE], {}, g_logic_table);

	card_init_hero(g_logic_table[2], 1);  -- 11=ter adun, 5=nishaven, 6=ice 

	g_gate = {
		{}
	,	{23, 51, 51}
	,	{}
	,	{24}
	,	{}
	,	{25, 25, 25}
	}


	g_logic_table[2].resource = 99;
	g_logic_table[2].resource_max = 99;

end -- gate_init_test }

function solo_init_test() -- {
	print('into solo_init_test()');
	g_logic_table = {};   -- 1=up,  2=down

--	local deck1_array = "0000100000000000000002200321030000000000111100000000000000000000000000221212002200000000000000000000000000000000000000000000000000141110000000000000002002200000000000000002000000000400000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000";
	local deck1_array = '1 131 64 26 22 30 22 131 30 64 67 22 26 67';
	-- '2 26 26 155 32 132 132 132 26';
	local game_flag = 0;
	local ai_max_ally = 0;
	local hp2 = 6;
	local hp1 = 10;
	local energy1 = 6;
	local deck2_array = '8 91 26 26 22 135 91';
	-- '8 91 91  91 191 91 22 135 28 95 36 26 22 95 95 26 ';
	-- "1 26 27 26 27 26 27 26 21 160 160 160 22 32";
	local type_list = '001011';
	-- '0010100110000000';
	-- "0101010100011";
	local seed = 2;

	solo_init_array(deck1_array, deck2_array, game_flag, ai_max_ally, hp2, hp1, energy1, type_list, seed);


end -- solo_init_test }


-- TODO need seed as parameter
function logic_init(hero1, hero2, deck1, deck2, seed, start_side, no_shuffle1, no_shuffle2) -- {
	g_round = 1; -- TODO need to ++ when next
	g_current_side = 1;   -- side, down first  // do not random this!
	g_phase = PHASE_SACRIFICE; -- init as 
	g_logic_table = {};   -- 1=up,  2=down

	-- print('WARN not warning: logic_init g_c_side,start_side=', g_current_side, start_side);

	deck1 = clone(deck1);
	deck2 = clone(deck2);
	local hand1 = {};
	local hand2 = {};

	seed = tonumber(seed) or 100;
	g_seed = seed;
	g_current_side = 2-(seed % 2); -- peter: use seed generate start_side;
	math.randomseed(seed);  

	if true ~= no_shuffle1 then 
		shuffle_deck(deck1);
	end 
	if true ~= no_shuffle2 then
		shuffle_deck(deck2);
	end 

	-- give 6 hand cards on both side
	for i=1, 6 do
		hand1[i] = deck1[1];
		table.remove(deck1, 1);
		hand2[i] = deck2[1];
		table.remove(deck2, 1);
	end


	g_logic_table[1] = {
		[T_HERO] 	= {side=1, name=T_HERO},
		[T_HAND] 	= {side=1, name=T_HAND}, -- card on hand, init 6 cards from deck
		[T_ALLY] 	= {side=1, name=T_ALLY},  -- ally in play
		[T_SUPPORT] = {side=1, name=T_SUPPORT},
		[T_GRAVE] 	= {side=1, name=T_GRAVE},
		[T_DECK] 	= {side=1, name=T_DECK},  -- list of card deck
		id = 1,
		name = "UP",
		resource = 0,
		resource_max = 0,
		energy_max = 99,
		ally_max = 99,
		
		-- write down play hand card count
		num_hand_ally 		= 0,
		num_hand_support 	= 0,
		num_hand_ability 	= 0,
		use_card_table		= {},
	}
	-- do not clone from side[1]
	g_logic_table[2] = {
		[T_HERO] 	= {side=2, name=T_HERO},
		[T_HAND] 	= {side=2, name=T_HAND}, -- card on hand, init 6 cards from deck
		[T_ALLY] 	= {side=2, name=T_ALLY},  -- ally in play
		[T_SUPPORT] = {side=2, name=T_SUPPORT},
		[T_GRAVE] 	= {side=2, name=T_GRAVE},
		[T_DECK] 	= {side=2, name=T_DECK},  -- list of card deck
		id = 2,
		name = "DOWN",
		resource = 0,
		resource_max = 0,
		energy_max = 99,
		ally_max = 99,
		
		-- write down play hand card count
		num_hand_ally 		= 0,
		num_hand_support 	= 0,
		num_hand_ability 	= 0,
		use_card_table		= {},
	}


	card_init_hero(g_logic_table[1], hero1); 
	card_init_hero(g_logic_table[2], hero2); 

	card_init_table(g_logic_table[1][T_DECK], deck1, g_logic_table);
	card_init_table(g_logic_table[2][T_DECK], deck2, g_logic_table);

	card_init_table(g_logic_table[1][T_HAND], hand1, g_logic_table);
	card_init_table(g_logic_table[2][T_HAND], hand2, g_logic_table);

	-- order is important, after card_init

	-- setup resource max
	g_logic_table[g_current_side].resource = g_logic_table[g_current_side].resource_max;

	-- set ready
	for i=T_HERO, T_SUPPORT do
		card_all_ready(g_logic_table[g_current_side][i]);
	end

end -- logic_init }

-- card array (400) to list, hero
-- input: array is a string length=400
-- output: list, hero_id  
-- list = { card_id1, card_id2, ... }
function card_array_list(array) -- {
    local list = {};
    local len = string.len(array);
    local hh = nil;
    for i=1, len do
        local c = array:byte(i) - 48; -- 48 = '0'
        if (c > 9) then  -- TODO it may be 0 to 4 (max)
            print('BUGBUG card_array_list c > 9 ', array:byte(i));
            c = 9;
        end
        -- c <= 0 will auto stop
        for j=1, c do
            if hh == nil then
                hh = i;
                break;
            end
            list[ #list + 1] = i;
        end

    end
    return list, hh;
end -- card_array_list  }

function clean_global()
	g_gate = nil;
	-- g_solo_list = nil;
	g_solo_type = 0;
	-- g_solo_max_ally = 0;
	-- g_solo_max_energy = 99;
end

-- input: '1 22 32 26'     (string list)
-- output: {22, 32, 26}, 1
-- where hero=1,    deck={22, 32, 26}
function card_list_split(list)
	local deck_table;
	local hero_id;

	deck_table = split_num(list);
	hero_id = deck_table[1] ;
	table.remove(deck_table, 1);

	return deck_table, hero_id;
end

-- return deck, hero, deck_type
-- deck_type: 0 for card400, 1 for card list, negative for bug
function deck_format(array)
	local deck = {};
	local hero_id = 0;
	local deck_type = -1; -- 0 for card400, 1 for card list
	if string.len(array) == 400 then
		print('DEBUG deck is 400');
		deck_type = 0;
		deck, hero_id = card_array_list(array);
	else
		print('DEBUG deck is list');
		deck_type = 1;
		deck, hero_id = card_list_split(array);
	end
	return deck, hero_id, deck_type;
end


-- deck1_array
function logic_init_array(deck1_array, deck2_array, seed, start_side, hp1, hp2, energy1, energy2) -- {
	-- g_gate = nil;
	clean_global();
	local deck1, deck2;
	local hero1, hero2;

	hp1 = hp1 or 0;
	hp2 = hp2 or 0;
	energy1 = energy1 or 0;
	energy2 = energy2 or 0;

	-- old logic
	-- deck1, hero1 = card_array_list(deck1_array);
	-- deck2, hero2 = card_array_list(deck2_array);

	-- new logic
	deck1, hero1 = deck_format(deck1_array);
	deck2, hero2 = deck_format(deck2_array);

	if hero1 < 1 or hero1 > 20 then
		print('ERROR logic_init_array invalid hero1 ', hero1);
		return -1; 
	end
	if hero2 < 1 or hero2 > 20 then
		print('ERROR logic_init_array invalid hero1 ', hero2);
		return -2; 
	end
	-- TODO check deck1, deck2 in g_card_list, return error if not found
	logic_init(hero1, hero2, deck1, deck2, seed, start_side);

	if hp1 > 0 then
		local hp = g_logic_table[1][T_HERO][1].hp;
		-- print('hp=' .. hp);
		local hp_offset = hp1 - hp;
		-- print('hp_offset=' .. hp_offset);
		g_logic_table[1][T_HERO][1]:change_base_hp(hp_offset);
	end

	if hp2 > 0 then
		local hp = g_logic_table[2][T_HERO][1].hp;
		-- print('hp=' .. hp);
		local hp_offset = hp2 - hp;
		-- print('hp_offset=' .. hp_offset);
		g_logic_table[2][T_HERO][1]:change_base_hp(hp_offset);
	end
	
	if energy1 > 0 then
		g_logic_table[1].energy_max = energy1;
	end
	
	if energy2 > 0 then
		g_logic_table[2].energy_max = energy2;
	end

	return 0;
end -- logic_init_array }

function gate_init_array(deck1_array, gate_array, seed) -- {
	clean_global();
	local deck1, deck2;
	local hero1, hero2;
	deck1, hero1 = card_array_list(deck1_array);
	if hero1 < 1 or hero1 > 20 then
		print('ERROR gate_init_array invalid hero1 ', hero1);
		return -1; 
	end
	hero2 = 20; -- set hero 20 is gate hero
	deck2 = {};


	g_gate = {};
	g_gate_max = 0;
	if type(gate_array) == 'string' then
		local str_table = {};
		str_table = split(gate_array);
		for i=1, #str_table, 2 do
			local r = str_table[i];
			local c = str_table[i+1];
			-- print('r=' .. r .. ' c=' .. c);
			g_gate[r] = g_gate[r] or {};
			table.insert(g_gate[r], c);
			if r > g_gate_max then
				g_gate_max = r;
			end
		end

	else 
		-- print('type table');
		g_gate = gate_array;
	end
	
	-- print('#gate_list=' .. table.getn(g_gate));

	-- TODO check deck1, deck2 in g_card_list, return error if not found
	logic_init(hero1, hero2, deck1, deck2, seed, 1);
	g_logic_table[2][T_HERO][1].trigger_skill = nil;
	g_logic_table[2][T_HERO][1].skill_desc = '';
	g_logic_table[2][T_HERO][1].target_list = nil;
	-- oppo hero cannot be damaged
	g_logic_table[2][T_HERO][1].trigger_calculate_defend = function(pside, src, power, dtype) 
		-- print('gate_init_array:trigger_calculate_defend');
		return 0; 
	end
	-- oppo hero cannot be target
	g_logic_table[2][T_HERO][1].hidden = true;


	g_logic_table[1][T_HERO][1].trigger_skill = nil;
	g_logic_table[1][T_HERO][1].skill_desc = '';
	g_logic_table[1][T_HERO][1].target_list = nil;
	g_logic_table[1][T_HERO][1].cost = 0;
	g_logic_table[1][T_HERO][1].hidden = true;

	g_current_side = 1;
	g_logic_table[2].resource = 99;
	g_logic_table[2].resource_max = 99;

	return 0;
end


-- deck1_array
function tower_init_array(deck1_array, deck2_array, seed, start_side, reset_hp, reset_res, reset_energy) -- {
	-- g_gate = nil;
	clean_global();
	local deck1, deck2;
	local hero1, hero2;
	deck1, hero1 = card_array_list(deck1_array);
	deck2, hero2 = card_array_list(deck2_array);
	if hero1 < 1 or hero1 > 20 then
		print('ERROR logic_init_array invalid hero1 ', hero1);
		return -1; 
	end
	if hero2 < 1 or hero2 > 20 then
		print('ERROR logic_init_array invalid hero1 ', hero2);
		return -2; 
	end
	-- TODO check deck1, deck2 in g_card_list, return error if not found
	logic_init(hero1, hero2, deck1, deck2, seed, start_side);

	g_logic_table[1].resource = reset_res;
	g_logic_table[1].resource_max = reset_res;
	-- g_logic_table[1][T_HERO][1].hp = reset_hp;
	-- g_logic_table[1][T_HERO][1].hp_max = reset_hp;
	local hp = g_logic_table[1][T_HERO][1].hp;
	print('hp=' .. hp);
	local hp_offset = reset_hp - hp;
	print('hp_offset=' .. hp_offset);
	-- g_logic_table[1][T_HERO][1]:change_hp(hp_offset);
	g_logic_table[1][T_HERO][1]:change_base_hp(hp_offset);
	g_logic_table[1][T_HERO][1].energy = reset_energy;
	
	return 0;
end

function solo_init_type(pside, solo_type, type_list)

	-- g_solo_type == 0, no type_list input
	if solo_type == 0 then
		return 0;
	end

	local hand = pside[2][T_HAND];
	local deck = pside[2][T_DECK];
	local type_len = string.len(type_list);
	-- length of custom AI type_list must be the same as #hand + #deck
	-- print('DEBUG #hand=' .. #hand .. ' #deck=' .. #deck .. ' type_len=' .. type_len);
	if #hand + #deck ~= type_len then
		print('BUGBUG solo_init_type:type_len_mismatch ', type_len, #hand+#deck);
		return -15;
	end

	local type_int = {};
    for i=1, type_len do
        local c = type_list:byte(i) - 48; -- 48 = '0'
        if c < 0 or c > 9 then
			print('BUGBUG solo_init_type:type_error ', string.sub(type_list, i, i), i);
			return -25;
        end
		type_int[i] = c;
    end

	local count = 0;
	for i=1, #hand do
		count = count + 1;
		local cc = hand[i];
		cc.solo_ai = type_int[count];
	end
	
	for i=1, #deck do
		count = count + 1;
		local cc = deck[i];
		cc.solo_ai = type_int[count];
	end

	-- for debug, remove later
	--[[
	local str = 'type_int:';
	for i=1, #type_int do
		str = str .. type_int[i];
	end
	print(str);
	]]--


	return 0;
end

-- for solo init ai deck and type_list
-- deck1_array is char400 array, deck2_list is card_id list: 23 26 35 ...
-- type_list: 100001001000000000 , len = #deck2_list, 0 for sacrifice, 1 for use
-- TODO write a solo ai logic, always sacrifice first type=0 card in hand, and use first type=1 and useful card in hand
-- NOTE: game_flag =  flag_shuffle_deck1 		* 1000 
--					+ flag_shuffle_deck2 		* 100 
--					+ flag_teach 				* 10 
--					+ flag_solo_type			* 1
function solo_init_array(deck1_array, deck2_array, game_flag, ai_max_ally, hp2, hp1, energy1, type_list, seed) -- {
	clean_global();
	local ret;
	local deck1, deck2;
	local hero1, hero2;
	local hand2;
	local no_shuffle1 = false;
	local no_shuffle2 = false;

	
	-- NOTE: game_flag = flag_teach * 10 + g_solo_type
	local flag_shuffle_deck1 = math.floor((game_flag % 10000) / 1000);
	local flag_shuffle_deck2 = math.floor((game_flag % 1000) / 100);
	-- useless now
	local flag_teach = math.floor((game_flag % 100) / 10);
	g_solo_type = game_flag % 10;



	print('DEBUG solo_init_array:game_flag=' .. game_flag .. ' ai_max_ally=' .. ai_max_ally
	.. ' hp2=' .. hp2 .. ' hp1=' .. hp1 .. ' energy1=' .. energy1
	.. ' seed=' .. seed .. ' flag_shuffle_deck1=' .. flag_shuffle_deck1
	.. ' flag_shuffle_deck2=' .. flag_shuffle_deck2 .. ' flag_teach=' .. flag_teach
	.. ' g_solo_type=' .. g_solo_type
	);

	-- process deck1
	if string.len(deck1_array) == 400 then
		print('DEBUG deck1 is 400');
		deck1, hero1 = card_array_list(deck1_array);
	else
		print('DEBUG deck1 is list');
		no_shuffle1 = true;
		deck1, hero1 = card_list_split(deck1_array);
	end

	-- process deck2
	if string.len(deck2_array) == 400 then
		print('DEBUG deck2 is 400');
		deck2, hero2 = card_array_list(deck2_array);
	else 
		print('DEBUG deck2 is list');
		no_shuffle2 = true; -- no shuffle deck
		deck2, hero2 = card_list_split(deck2_array);
	end

	if flag_shuffle_deck1 == 1 then
		no_shuffle1 = false;
	end

	-- g_solo_type == 0
	if flag_shuffle_deck2 == 1 then
		no_shuffle2 = false;
	end

--	print('DEBUG no_shuffle1=' , no_shuffle1 , ' no_shuffle2=' , no_shuffle2);
	logic_init(hero1, hero2, deck1, deck2, seed, 1, no_shuffle1, no_shuffle2);

	-- global setup first
	g_logic_table[2].ally_max = ai_max_ally;

	-- update hp
	if hp1 > 0 then
		local hp = g_logic_table[1][T_HERO][1].hp;
		-- print('hp=' .. hp);
		local hp_offset = hp1 - hp;
		-- print('hp_offset=' .. hp_offset);
		g_logic_table[1][T_HERO][1]:change_base_hp(hp_offset);
	end

	if hp2 > 0 then
		local hp = g_logic_table[2][T_HERO][1].hp;
		-- print('hp=' .. hp);
		local hp_offset = hp2 - hp;
		-- print('hp_offset=' .. hp_offset);
		g_logic_table[2][T_HERO][1]:change_base_hp(hp_offset);
	end
	
	if energy1 > 0 then
		-- g_solo_max_energy = energy1;
		g_logic_table[1].energy_max = energy1;
	end

	ret = solo_init_type(g_logic_table, g_solo_type, type_list);
	if ret ~= 0 then
		print('BUGBUG solo_init_array:init_type_fail');
		return -25;
	end

	return 0;
end --}


-- useless, merge to logic_init_array(), remove later
function robot_init_array(deck1_array, deck2_list, seed) -- {
	clean_global();
	local deck1, deck2;
	local hero1, hero2;
	-- local hand2;

	--[[
	-- old logic
	deck1, hero1 = card_array_list(deck1_array);
	if hero1 < 1 or hero1 > 20 then
		print('ERROR robot_init_array invalid hero1 ', hero1);
		return -1; 
	end
	deck2, hero2 = card_list_split(deck2_list);
	]]--

	-- new logic
	deck1, hero1 = deck_format(deck1_array);
	deck2, hero2 = deck_format(deck2_list);

	--[[
	deck2 = {};
	local str_table = {};
	local card_len = 0;
	str_table = split(deck2_list);
	for i=1, #str_table, 1 do
		local card_id = tonumber(str_table[i]);
		if card_id <= 0 or card_id > 400 then
			print('BUGBUG robot_init_array:invalid_card_id ', card_id);
			return -5;
		end
		deck2[i] = card_id;
		card_len = card_len + 1;
	end
	print('DEBUG robot:card_len=', card_len);
	]]--

	logic_init(hero1, hero2, deck1, deck2, seed, 1);
	return 0;
end --}


-- @see nio.cpp : game_param_string()
-- lua : type_flag, max_ally, max_hp, myhero_hp, energy1, type_list
-- C: solo_type, solo_max_ally, solo_max_hp, solo_myhero_hp, solo_myhero_energy, solo_type_list);
-- note: type_list is string, others are number
-- game_flag, type_list, ai_max_ally, hp1, hp2, energy1, energy2
function game_param_split(array, index)
	if index+7 > #array then
		print('ERROR game_param_split:index_out_bound:', index, #array);
		return -1;
	end

	return tonumber(array[index+1]), array[index+2], tonumber(array[index+3])
	, tonumber(array[index+4]), tonumber(array[index+5]), tonumber(array[index+6])
	, tonumber(array[index+7]);
end

function array_tonum(array, start, stop)
	if start > #array or stop > #array then
		print('ERROR array_tonum:start_stop_out_bound ', start, stop, #array);
		return -2, 'bug_array_tonum';
	end
	local num;
	for i=start,stop do
		num = tonumber(array[i]);  -- '11' will convert to 11
		if num ~= nil then 
			array[i] = num;
		end
	end
	return 0, '';
end

-- field_count = 19
-- gameid, game_type, winner, star
-- seed, start_side, ver, eid1, eid2
-- lv1 ,lv2, icon1, icon2, alias1
-- alias2, deck1, deck2, param, cmd
-- NOTE: deck1, deck2, param are in nscan format
function game_init(str)
	local ret = -1;
	local tmp;
	local array;
	-- local str_array;
	local gameid, game_type, winner, star;
	local seed, start_side, ver, eid1, eid2;
	local lv1 ,lv2, icon1, icon2, alias1;
	local alias2, deck1, deck2, param, cmd;
	local index_deck1, index_deck2, index_param, index_cmd;
	-- this is for game param, give it default value for pvp and normal game
	-- local type_flag, max_ally, max_hp, myhero_hp, myhero_energy, type_list;

	-- param = count(7), game_flag, type_list, ai_max_ally, hp1, hp2, energy1, energy2
	local game_flag, type_list, ai_max_ally, hp1, hp2, energy1, energy2;

	game_flag, type_list, ai_max_ally, hp1, hp2, energy1, energy2
	= 0, '0', 99, 0, 0, 99, 99;

	array = csplit(str, ' ');

	-- print('#array  == ', #array);
	if #array < 25 then -- minimum size, include 1 deck400 1 deck400 6 param1 ...
		print('ERROR game_init:not_enough_param ', #array);	
		return -5, 'array_size';
	end

	array_tonum(array, 1, 13);

	gameid 		= array[1];
	game_type 	= array[2];
	winner 		= array[3];
	star 		= array[4];
	seed 		= array[5];
	start_side 	= array[6];
	ver 		= array[7];
	eid1 		= array[8];
	eid2 		= array[9];
	lv1 		= array[10];
	lv2 		= array[11];
	icon1 		= array[12];
	icon2 		= array[13];
	alias1 		= array[14];
	alias2 		= array[15];

	tmp = string.format('gameid=%d  game_type=%d   eid1=%d  eid2=%d  alias1=%s  alias2=%s'
	, gameid, game_type, eid1, eid2, alias1, alias2);
	print('DEBUG game_init: ', tmp);

	if ver ~= tonumber(LOGIC_VERSION) then
		print('ERROR game_init:LOGIC_VERSION_mismatch ', ver, LOGIC_VERSION);
		return -6, 'ver';
	end
	-- print(str);

	-- deck1 is in nscan format
	-- deck1 :  3 22 26 28       count=3, index=1
	--      [16]^ ^[17]  ^[19]
	index_deck1 = 16;
	index_deck2,  deck1 = nscan(array, index_deck1);
	if index_deck2 < 0 then
		print('ERROR game_init:deck1 ', index_deck1, str);
		return -15, 'deck1';
	end
	print('------ index_deck2 : ' , index_deck2, array[index_deck2]);

	index_param,  deck2 = nscan(array, index_deck2);
	if index_param < 0 then
		print('ERROR game_init:deck2 ', index_deck2, str);
		return -25, 'deck2';
	end


	index_cmd, 	  param = nscan(array, index_param);
	if index_cmd < 0 or index_cmd > #array then
		print('ERROR game_init:param ', index_param, str);
		return -35, 'param';
	end


	cmd = join_str(array, index_cmd, #array);

	-- print('deck1=' .. deck1);
	-- print('deck2=' .. deck2);
	-- print('param=' .. param);
	-- print('cmd=' .. cmd);

	-- ok, we have done the general setup
	-- after this, do game_type specific logic

	game_flag, type_list, ai_max_ally, hp1, hp2, energy1, energy2
	= game_param_split(array, index_param);
	if game_flag < 0 then
		print('ERROR game_init:game_param_split ', str);
		return -45, 'param_split';
	end

	str = string.format('game_init(): game_flag=%d, type_list=[%s], ai_max_ally=%d, hp1=%d, hp2=%d, energy1=%d, energy2=%d', game_flag, type_list, ai_max_ally, hp1, hp2, energy1, energy2);
	print(str);

	-- 14=GAME_SOLO_PLUS, 15=GAME_CHAPTER, 
	if game_type == 15 or game_type == 14 then
		ret = solo_init_array(deck1, deck2
		, game_flag, ai_max_ally, hp2, hp1, energy1, type_list, seed);

	end

	-- GAME_GATE
	if game_type == 7 then
		ret = gate_init_array(deck1, deck2, seed);
	end

	-- GAME_SOLO		1
	-- GAME_ROOM		3
	-- GAME_CHALLENGE	4
	-- GAME_SOLO_GOLD	8
	-- GAME_VS_GOLD		9
	-- GAME_VS_CRYSTAL	10
	-- GAME_SOLO_FREE	11
	-- GAME_VS_FREE		12
	if game_type == 1 or game_type == 3 or game_type == 4 
	or game_type == 8 or game_type == 9 or game_type == 10 
	or game_type == 11 or game_type == 12 then
		ret = logic_init_array(deck1, deck2, seed, start_side, hp1, hp2, energy1, energy2);
	end

	if g_logic_table == nil then
		return -3, 'nil_logic_table';
	end

	if ret < 0 then
		str = string.format('init_array %d', game_type or 0);
		return ret, str;
	end
	-- TODO handle other game_type

	g_logic_table[1].eid = eid1;
	g_logic_table[2].eid = eid2;
	g_logic_table[1].alias = alias1;
	g_logic_table[2].alias = alias2;
	g_logic_table[1].lv = lv1;
	g_logic_table[2].lv = lv2;
	g_logic_table[1].icon = icon1;
	g_logic_table[2].icon = icon2;

	return ret, cmd; -- normal case


end

-- keep this for test case
function init_test_card_set(pside, sss) -- start {
	card_init_hero(pside[1], 1);  -- 1=warrior boris

	-- card_init_table(table, clist, g_logic_table)  
	card_init_table(pside[1][T_DECK], {22, 23, 26, 27, 30, 36}, pside);
	card_init_table(pside[1][T_HAND], {22, 26}, pside);
	card_init_table(pside[1][T_ALLY], {42, 36, 26, 41, 22}, pside);
	-- card_init_table(pside[1][T_ALLY], {30}, pside); -- TEMP DEBUG
	card_init_table(pside[1][T_SUPPORT], {63}, pside);
	card_init_table(pside[1][T_GRAVE], {79, 23, 26, 73}, pside);

	card_init_hero(pside[2], 5);  -- 5 = mage, nishaven
	card_init_table(pside[2][T_DECK], {63, 36, 30, 27, 26, 23, 22}, pside);
	-- kurt(28), lightning(74), webs(80), smashingblow(64), retreat(132)
	-- warbanner(63), valiantdefender(61), crippling blow(67)
	card_init_table(pside[2][T_HAND], {28, 74, 80, 64, 132, 63, 61, 67}, pside); 
	-- order is important, if Aldon is added before Dirk, +1 attack is not effective
	card_init_table(pside[2][T_ALLY], {22, 26, 36}, pside); 
	--card_init_table(pside[2][T_ALLY], {22, 26, 42}, pside);  -- TEMP DEBUG
	card_init_table(pside[2][T_SUPPORT], {188}, pside); 
	card_init_table(pside[2][T_GRAVE], {61,}, pside);

	-- attachment test:
	-- 1302=armor sandworm SIDE UP ally, 2
	-- att_card.home = 1;  --  avoid action_grave() .home is nil

	local cc;
	local attcc;
	cc = index_card(1302, pside);  
	if cc ~= nil then
		attcc = clone(g_card_list[67]); -- crippling blow
		attcc.home = 1; -- avoid grave bug!
		-- card_set_pos(attcc, cc.side, cc.table, cc.pos, 1);
		card_attach(cc, attcc);
	end

	-- hard code more energy:
	pside[1][T_HERO][1].energy = 10;
	pside[2][T_HERO][1].energy = 10;

	pside[1].resource_max = 10;
	pside[2].resource_max = 10;

	-- setup resource max
	g_logic_table[g_current_side].resource = g_logic_table[g_current_side].resource_max;

	-- set ready
	for i=T_HERO, T_SUPPORT do
		card_all_ready(g_logic_table[g_current_side][i]);
	end
end --  init_test_card_set }





function hero_main() -- start {
local str;
local err;

-- use standard card
-- init_test_card_set(g_logic_table, g_current_side);
if local_init_test then
	logic_init_test();
	-- gate_init_test();
	-- solo_init_test();
else
	logic_init(15, 6, get_standard_deck(15), get_standard_deck(6), 4999);
end

local total_card = 0;
str = 'valid card list: ';
for k, v in pairs(g_card_list) do
	str = str .. k .. ', '
	total_card = total_card + 1;
end
print(str);
print('total_card = ', total_card);


print ('----- GAME START -----');

-- round 1
print_both_side(g_logic_table);

-- print_card(g_card_list[71]);

local help_str = 'Help/Usage:\n'
	.. 'help = help\n' 
	.. 'p = print both side\n'
	.. 'pi = print index both side\n'
	.. 'q = quit\n'
	.. 'n = next side;   s [index] = sacrifice [index];\n'
	.. 't [index] [target_index];    b [index] [target_index1] [target_index2] ...\n'
	.. '----- ai test:\n'
	.. 'saclist = list all sacrifice weight and best sacrific\n'
	.. 'ailist = list all AI weight and the best move\n'
	.. 'ais = execute the AI sacrific\n'
	.. 'aip = execute AI one play\n'
	.. 'ai = execute ais, aip etc until g_current_side change (next)\n'
	.. '----- non-standard mode:\n'
	.. 'hand 1 = h 1 = cast card 1 on hand\n' 
	.. 'ability = ab [src_index] [target_index_list] = ability, src.trigger_skill \n'
	.. 'attack = at [src_index] [target_index] = execute attack [new index]\n'
	.. 'ally 1 2 = choose ally 1 to attack oppo ally 2; ally 1 0 attack oppo hero\n'
	.. 'sac 1 = s 1 = sacrifice card 1 on hand to become resource\n'
	.. '----- cheat code cc -----\n'
	.. 'ccd [index] print the detail info of a card by index'
	.. 'ccadd [hand|ally|grave] card_id = add a card(id) to hand or ally or grave\n'
	.. 'ccremove [hand|ally|grave] pos = remove a card at pos from hand or ally\n'
	.. 'ccmove from_table to_table pos = move a card from a table at pos to another table\n'
	.. 'ccres [num] = set resource_max to [num] for current side\n'
	.. 'ccenergy [num] = set hero energy to [num] for current side\n'
	.. 'ccdeck = show the deck\n'
	.. 'cccard = show the card list\n'
	.. '---------- ---------- ----------\n';

print(help_str);


------  LOGIC START
local run_flag = true;
while run_flag == true do
	local side_my = g_logic_table[g_current_side];
	local side_your = g_logic_table[3 - g_current_side];
	local input;
	local input_list;
	str = 'PLAY';
	if g_phase == PHASE_SACRIFICE then str = 'SACRIFICE'; end
	print ('\n' .. str .. ' side = ' .. side_my.name 
	.. '(' .. side_my.id .. ')  resource = ' 
	.. side_my.resource  .. '/' .. side_my.resource_max,   
	'  opp_resource_max=' .. side_your.resource_max);
	input = io.read()
	input_list = split_num(input); 

	repeat -- implement continue, inner while {

	if #input_list==0 or input_list[1]=='help' then
		print(help_str);
		break;
	end

	-- peter: use play_cmd_global as if in nio.c / cli.cpp
	if input_list[1]=='at' or input_list[1]=='ab' 
	or input_list[1]=='sac' or input_list[1]=='next' 
	or input_list[1]=='t' or input_list[1]=='b' 
	or input_list[1]=='s' or input_list[1]=='n' 
	then
		print('--- use play_cmd + play_cmd_validate_global ---');
		local flag, reason;
		flag, reason = play_cmd_validate_global(input);
		if flag == false then
			print('ERROR play_cmd_validate_global: ', reason);
			break;
		end
		local eff_list;
		eff_list, g_current_side, g_phase, err 
		= play_cmd_local(input, g_logic_table, g_current_side, g_phase);
		-- = play_cmd(input, g_logic_table, g_current_side, g_phase);
		if err ~= nil then
			print('ERROR play_cmd err=', err);
		end
		print_eff_list(eff_list);
		break;
	end

	if input_list[1]=='test' or input_list[1]=='t' then
		local index = input_list[2];
		if index ~= nil and test_map[index] ~= nil then
			local fun;
			print('test ' .. index .. ':');
			fun = test_map[index];
			fun(g_logic_table, g_current_side, input_list);
		else
			print ('ERROR: invalid test case num');
		end
		break;
	end

	if input_list[1]=='next' or input_list[1]=='n' then
		local eff_list;
		-- set both side ready
		eff_list, g_current_side, g_phase = action_next(g_logic_table, g_current_side);

		print_eff_list(eff_list);
		print('DONE : next side');
		break;
	end

	if input_list[1]=='print' or input_list[1]=='p' then
		
		if #input_list == 1 then 
			print_both_side(g_logic_table);
		else 
			local id = input_list[2];
			local cc ;
			cc  = g_card_list[id];
			if cc == nil then 
				print('ERROR cannot print card : ' .. id);
				break;
			end
			print_card(cc);
			print_target_list(cc.target_list, id, 0);
		end
		break;
	end

	if input_list[1]=='pi' or input_list[1]=='index' then
		print_index_both(g_logic_table);
		break;
	end

	if input_list[1]=='q' then
		run_flag = false;
		break;
	end

	-- debug function:

	-- ccd print detail card information (use print table)
	if input_list[1] == 'ccd' then -- { ccd start
		if #input_list < 2 then 
			print('ERROR: ccd index');
			break;
		end
		local index = tonumber(input_list[2]);
		local cc = index_card(index, g_logic_table);
		if nil == cc then
			print('ERROR ccd card = nil: ', index);
			break;
		end
		str = table_str(cc);
		print(str);
		break;
	end -- ccd }

	-- ccmove from_table to_table index
	-- ccmove hand ally 1 = move card 1 of hand to ally
	if input_list[1] == 'ccmove' then  -- { ccmove start
		local a = nil;
		if #input_list < 4 then 
			print('ERROR: ccmove from_table to_table pos(from)');
			break;
		end
		local from_table_name = r_table_name_map[input_list[2]];
		local to_table_name = r_table_name_map[input_list[3]];
		local index = tonumber(input_list[4]);

		local from_table = side_my[from_table_name];
		local to_table = side_my[to_table_name];

		if from_table == nil then 
			print('ERROR from_table not found: ', input_list[2], from_table_name);
			break;
		end

		if to_table == nil then 
			print('ERROR to_table not found: ', input_list[3], to_table_name);
			break;
		end

		if index < 1 or index > #from_table then
			print('ERROR ccmove out of bound #from_table=', #from_table);
			break;
		end
		local cc = from_table[index];
		if cc == nil then 
			print('ERROR ccmove cannot find card from_table');
			break;
		end
		print('Target card:');
		print_card(cc, cc.pos, 1);

		action_remove(cc, g_logic_table, from_table);
		-- side_your, side_my, nil, cc, from_table);
		action_add(cc, g_logic_table, to_table);
		-- side_your, side_my, nil, cc, to_table);
		print('DONE ccmove success');
		break;
	end -- } ccmove end


	-- ccadd table card_id
	if input_list[1] == 'ccadd' then  -- { ccadd start
		if #input_list < 3 then 
			print('ERROR: ccadd table card_id');
			break;
		end
		local table_name = r_table_name_map[input_list[2]];
		local card_id = tonumber(input_list[3]);
		local cc = g_card_list[card_id];
		local tb = side_my[table_name];
		if card_id == nil or cc == nil then
			print('ERROR invalid card_id : ' .. input_list[3]);
			break;
		end
		if tb == nil then
			print('ERROR invalid table : ', input_list[2]);
			break;
		end
		cc = clone(cc);  -- must be cloned!
		action_add(cc, g_logic_table, tb);
		-- side_your, side_my, nil, cc, tb);
		print('Added card:');
		print_card(cc, cc.pos, 1);
		print('DONE ccadd success');
		break;
	end -- } ccadd end

	-- ccremove table pos
	if input_list[1] == 'ccremove' then 
		if #input_list < 3 then 
			print('ERROR: ccremove table pos');
			break;
		end
		local table_name = r_table_name_map[input_list[2]];
		local tb = side_my[table_name];
		if tb == nil then
			print('ERROR invalid table : ', input_list[2]);
			break;
		end
		local pos = tonumber(input_list[3]);
		if pos == nil or pos < 1 or pos > #tb then
			print('ERROR invalid pos : ' .. input_list[3]);
			break;
		end
		local cc = tb[pos];
		if cc == nil then
			print('ERROR invalid card: ' .. input_list[3]);
			break;
		end
		print('Target card:');
		print_card(cc, cc.pos, 1);

		action_remove(cc, g_logic_table, tb);
		-- side_your, side_my, nil, cc, tb);
		print('DONE ccremove success');
		break;
	end


	if input_list[1] == 'ccres' then 
		if #input_list < 2 or type(input_list[2])~='number' then 
			print('ERROR: ccres [num] = set resource_max to [num]');
			break;
		end
		local res = input_list[2];
		side_my.resource_max = res;
		side_my.resource = res;
		print('DONE ccres success');
		break;
	end 

	if input_list[1] == 'ccenergy' then 
		if #input_list < 2 or type(input_list[2])~='number' then 
			print('ERROR: ccenergy [num] = set hero energy to [num]');
			break;
		end
		local res = input_list[2];
		side_my[T_HERO][1].energy = res;
		print('DONE ccenergy success');
		break;
	end 

	if input_list[1] == 'ccdeck' then 
		print('^^^^^ UP SIDE ^^^^^');
		print_deck(g_logic_table[1][T_DECK]);
		print('\nvvvvv DOWN SIDE vvvvv');
		print_deck(g_logic_table[2][T_DECK]);
		break;
	end

	if input_list[1] == 'cccard' then
		local id = tonumber(input_list[2]);
		if id ~= nil then
			print_card(g_card_list[id], id, 0, true); -- desc=true 
			break;
		end
		local total = 0;
		local sorted_list = {};
		str = 'valid card list: ' ;
		str = str .. #g_card_list .. '  ';
		for k, v in pairs(g_card_list) do
			str = str .. k .. ', '
			total = total + 1;
			sorted_list[total] = v;
		end
		table.sort(sorted_list, card_compare);
		print(str);
		for k, v in ipairs(sorted_list) do
			print_card(v, v.id, 0);
		end
		print('Total = ' .. total);
		sorted_list = nil;

		break;
	end

	-- show the available target_list for ability
	-- usage: tar index
	if input_list[1] == 'tar' then
		if #input_list < 2 then
			print('tar [index] - list all valid targets for card index');
			break;
		end
		local list;
		local index;
		local cc;
		index = input_list[2];
		cc = index_card(index, g_logic_table);
		if cc == nil then
			print('ERROR: tar index_card');
			break;
		end
		print_card(cc, 'SRC:' .. index);
		print('ATTACK: playable=', check_playable(cc, g_logic_table, g_current_side, false));
		list = list_attack_target(index, g_logic_table, g_current_side);
		print_index_list(list, g_logic_table);
		print('ABILITY: playable=', check_playable(cc, g_logic_table, g_current_side, true));
		-- old target list:
		-- list = list_valid_target_list(index, g_logic_table, g_current_side);
		-- print_2d(list);
		list = list_ability_target(index, g_logic_table, g_current_side, {}, 1);
		print_index_list(list, g_logic_table);

		-- also print the _all_ attack index
		list = list_attack_index(g_logic_table, g_current_side);
		str = '' ;
		for _,c in pairs(list or {'none'}) do
			str = str .. ' ' .. c;
		end
		print('TEST: list_attack_index: ' .. #list .. ' ' .. str);
		break;
	end

	if input_list[1] == 'sac' or input_list[1]=='s' then  -- { start sac
		local pos = nil;
		local cc = nil;
		local target_pos = nil;
		local eff_list = {};
		pos = tonumber(input_list[2]);
		if pos==nil or pos < 0 then
			print('ERROR: sac [num | index] invalid number, sac 0 to skip');
			break;
		end 

		-- re-use pos as cindex
		if pos > 0 and pos < 99 then
			cc = side_my[T_HAND][pos];
			if cc == nil then
				print('ERROR sac invalid card pos=', pos);
				break;
			end
			pos = cc:index();
		end
		if pos > 0 and index_card(pos, g_logic_table)==nil then
			print('ERROR sac invalid card index=', pos);
			break;
		end
		eff_list, g_phase = action_sacrifice(pos, g_logic_table, 
			g_current_side, g_phase);
		print_eff_list(eff_list);
		if eff_list == nil then
			print('ERROR cannot sacrifice');
			break;
		end
		print('DONE sac success');
		break;
	end -- } end sac

	-- ref: test16  t16  weight_all_play()
	if input_list[1] == 'aip' or input_list[1] == 'aiplay' then -- {
		if g_phase ~= PHASE_PLAY then
			print('Not play phase');
			break;
		end
		local eff_list;
		local cmd;

		if g_solo_type == 1 and g_current_side == 2 then
			cmd = get_ai_solo_play(g_logic_table, g_current_side, g_phase);
		else
			cmd = get_ai_play(g_logic_table, g_current_side, g_phase);
		end

		print('aiplay cmd: ' .. cmd);

		eff_list, g_current_side, g_phase,err = 
			play_cmd(cmd, g_logic_table, g_current_side, g_phase);

		if eff_list == nil then
			print('BUGBUG ai_play return nil, err: ', err);
			break;
		end
		print_eff_list(eff_list);
		print('DONE aiplay');
		break;
	end -- aiplay end }

	-- ref: test9  t9    ai_sac read-only
	if input_list[1] == 'ais' or input_list[1] == 'aisac' then -- aisac {
		local index;
		local eff_list;
		local cmd;
		if g_phase ~= PHASE_SACRIFICE then
			print('Not sacrifice phase');
			break;
		end

		if g_solo_type == 1 and g_current_side == 2 then
			cmd = get_ai_solo_sac(g_logic_table, g_current_side, g_phase);
		else
			cmd = get_ai_sac(g_logic_table, g_current_side, g_phase);
		end
		print('ai sacrifice: ' .. cmd);

		eff_list, g_current_side, g_phase,err = 
			play_cmd(cmd, g_logic_table, g_current_side, g_phase);

		if nil == eff_list then
			print('BUGBUG ai_sac return nil err:', err);
			break;
		end
		print_eff_list(eff_list);
		break;
	end -- aisac }

	if input_list[1] == 'ailist' then -- ailist {
		local list;
		local max_play, max;
		print('ailist : weight_all_play');
		list = list_all_play(g_logic_table, g_current_side);
		max_play, max = weight_all_play(list, g_logic_table, g_current_side);

		if nil == max_play then
			print('No play available');
			break ;
		end

		for i=1, #list do
			print_ai_play(list[i]);
		end

		print('\nMax play : (weight=' .. max .. ')  cmd : ', play_to_cmd(max_play));
		print_ai_play_detail(max_play, max, g_logic_table, g_current_side);
		break;
	end -- ailist }

	if input_list[1] == 'saclist' then -- saclist {
		local list;
		local min_index;
		print('saclist : weight_sac');
		min_index = weight_sac(g_logic_table, g_current_side);
		for k,v in ipairs( g_logic_table[g_current_side][T_HAND] or {}) do
			str = string.format('weight:%-5d  id=%-5d  s %d'
				, v.weight, v.id, v:index());
			print(str);
		end
		if 0 == min_index then
			print('No sac available');
			break ;
		end

		print('--- best sacrific : ' .. min_index);
		print_card( index_card(min_index, g_logic_table), min_index, 0 );
		break;
	end -- ailist }

	-- integrate aisac(ais) and aiplay(aip)
	-- aia only execute 1 step (1 command), using ai()
	if input_list[1] == 'aia' then -- aia {
		local eff_list;
		eff_list, g_current_side, g_phase = ai(g_logic_table, g_current_side, g_phase);
		if nil == eff_list then
			print('BUGBUG ai eff_list = nil');
			break;
		end
		print_eff_list(eff_list);
		print('DONE aia');
		break;
	end -- aia }

	-- ai will do all command until g_current_side change
	if input_list[1] == 'ai' then -- ai {
		local cmd;
		local old_side = g_current_side;
		while old_side == g_current_side do
			cmd = ai_cmd_global();
			print('AI cmd: ' .. cmd);
			play_cmd_global(cmd);
		end

		break;
	end -- ai }

	-- print one ai move
	if input_list[1] == 'aione' then -- aione {
		local cmd;
		cmd = ai_cmd_global();
		print('AI cmd: ' .. cmd);
		get_all_play();
		break;
	end -- aione }

	-- print solo target
	if input_list[1] == 'target' then -- target {
		local target = tonumber(input_list[2]);
		if target == nil then
			print('get_solo_target target_nil');
			break;
		end
		local p1 = tonumber(input_list[3]) or 1;
		local p2 = tonumber(input_list[4]) or 1;
		local num;
		num = get_solo_target(target, p1, p2);
		print('get_solo_target num: ' .. num);
		break;
	end -- target }


	if g_phase == PHASE_SACRIFICE then
		print('No action is allowed in sacrifice phase');
		break;
	end

	-- usage: hand 1  = cast first card on hand to ally or support
	if input_list[1]=='hand' or input_list[1]=='h' then -- start hand {
		local pos = input_list[2];  
		local cc = nil;
		local eff_list = {};
		local eff_list2 = {};
		if pos==nil or pos <1 or pos > #side_my[T_HAND] then
			print ('ERROR out of range #hand='.. #side_my[T_HAND]);
			break;
		end

		-- need to check ctype first,  
		-- valid range is 20 - 50
		-- 20 = ally : hand to ally
		-- 30 = attach:  TODO
		-- 40 = ability 
		-- 50 = support:  hand to support

		cc = side_my[T_HAND][pos];

		-- TODO implement attach
		if cc.ctype<20 or cc.ctype>59 then
			print ('ERROR: cannot cast hand card id=' .. cc.id 
			.. ' ctype=' .. cc.ctype);
			break;
		end

		print('Target hand card :');
		print_card(cc, pos, 1);
		if cc.cost > side_my.resource then
			print('ERROR: not enough resource');
			break;
		end

		if cc.ctype==ALLY then -- ally
			if true ~= cc.haste then -- add by kelton
				set_first_ready(cc); -- order is important (b4 action_move)
			end
			eff_list = action_move(cc, g_logic_table, side_my[T_HAND], side_my[T_ALLY]);
			-- side_your, side_my, nil, cc, side_my[T_HAND], side_my[T_ALLY]);
			-- update resource
			side_my.resource = side_my.resource - cc.cost;
			eff_list[#eff_list + 1] = eff_resource_offset(-cc.cost, side_my.id);
		elseif cc.ctype>=50 and cc.ctype<=59 then -- support
			eff_list = action_move(cc, g_logic_table, side_my[T_HAND], side_my[T_SUPPORT]);
			-- side_your, side_my, nil, cc, side_my.hand, side_my[T_SUPPORT]);
			-- update resource
			side_my.resource = side_my.resource - cc.cost;
			eff_list[#eff_list + 1] = eff_resource_offset(-cc.cost, side_my.id);
		elseif cc.ctype==ATTACH then -- attach  e.g. 133 Reinforced Armor
			-- logic: ask which ally or hero to attach
			local at = 0;
			local t;
			print_target_list(cc.target_list, cc.id, 1);
			t = cc.target_list[1]; -- assume only 1 target

			print_target(t, g_logic_table, g_current_side);

			local tinput = print_input_target(1, false);
			local tc;
			tc = index_card(tinput, g_logic_table);
			if tc == nil then
				print('ERROR: index to card is nil');
				break;
			end
			-- TODO check duplicated attachment!!!
			-- TODO check ac(attach card) compatible with t(target)
			-- note: tc is the target card,  cc is the attach card
			-- TODO eff_list return from card_remove()
			eff_list = action_attach(tc, g_logic_table, cc); -- TODO use action_attach()
			if eff_list == nil or #eff_list==0 then  -- normal: one record
				print('ERROR: attach fail');
				break;
			end
			-- TODO reduce resource, effect etc
			side_my.resource = side_my.resource - cc.cost;
			eff_list[#eff_list + 1] = eff_resource_offset(-cc.cost, g_current_side);
			print_eff_list(eff_list);
			print('DONE attach success');
			break;

		elseif cc.ctype == ABILITY then -- { ability, e.g. Shield Bash(69) 
			local atl = {};
			local target_list = cc.target_list;

			atl = input_atl(cc, g_logic_table, g_current_side);
			if nil == atl then
				print('ERROR: invalid target');
				break;
			end
			
			print('#actual_target_list : ' .. #atl);
			for k, v in ipairs(atl or {}) do
				local tc = index_card(v, g_logic_table);
				print_card(tc, tc.pos, 0);
			end

			-- double check: pls keep this for debug
			local flag = check_actual_target_list(atl, target_list, side_my.id);
			if flag == false then
				print('CANCEL: Input invalid');
				break;
			end

			-- cc is the source card (ability card)
			-- core logic to trigger the skill
			eff_list = cc:trigger_skill(g_logic_table, atl); -- note : self

			-- reduce the resource (must before grave)
			side_my.resource = side_my.resource - cc.cost;
			eff_list[#eff_list + 1] = eff_resource_offset(-cc.cost, side_my.id);

			-- move the card to grave
			eff_list2 = action_grave(cc, g_logic_table);
			table_append(eff_list, eff_list2);

			print_eff_list(eff_list);
			print('DONE cast ability card = ' .. cc.name 
				.. '(' .. cc.id .. ')');
			break;
		-- } -- end of elseif ctype==40
		else -- error case : invalid ctype
			print ('BUGBUG: cannot cast id:' .. cc.id .. ' ctype:' .. cc.ctype);
			break;
		end

		print_eff_list(eff_list);
		print('DONE: hand card cast success  id=' .. cc.id);
		break;
	end --  end hand  }


	-- TODO allyability(aa) which trigger the ability of card in ally table
	if input_list[1] == 'ally' then  -- start {
		local pos = nil;
		local cc = nil;
		local target_pos = nil;
		local target_card;
		local eff_list;
		pos = tonumber(input_list[2]);
		if pos==nil or pos<0 or pos>#side_my[T_ALLY] then
			print ('ERROR out of range #ally=' .. #side_my[T_ALLY]);
			break;
		end
		-- pos=0 means hero attack
		if pos==0 then
			cc = side_my[T_HERO][1];
			print('DEBUG hero attack');
		else 
			-- normal ally
			cc = side_my[T_ALLY][pos];
		end

		if cc.power == 0 then 
			print('ERROR power=0, cannot attack');
			break;
		end
		target_pos = tonumber(input_list[3]);
		if target_pos == nil then
			print('*** Input opponent ally or 0=hero:');
			print_hero(side_your[T_HERO][1]);
			print_ally(side_your[T_ALLY]);
			target_pos = tonumber(io.read());
		end
		if target_pos==nil or target_pos<0 or target_pos>#side_your[T_ALLY] then
			print ('ERROR target out of range #oppo[T_ALLY]=' .. #side_your[T_ALLY]);
			break;
		end
		if target_pos == 0 then -- 0 means hero
			target_card = side_your[T_HERO][1];
		else 
			target_card = side_your[T_ALLY][target_pos];
		end

		-- check src (disable, rain etc)
		-- TODO rain delay not affect hero (cc.ctype==HERO)
		if false==check_playable(cc, g_logic_table, g_current_side, false) then
			print('ERROR: cannot attack (rain or disable)');
			break;
		end

		-- check_target
		if false==check_target( target_card, g_logic_table, g_current_side, false ) then
			print('ERROR: cannot attack target (protect/hidden/stealth)');
			break;
		end

		if false == check_ready_attack(cc) then
			print('ERROR: attacker is exhausted (already attacked)');
			break;
		end

		print('Attack by:');
		print_card(cc, pos, 1);
		print('Attack Target:');
		print_card(target_card, target_pos, 1);

		eff_list = action_attack(target_card, g_logic_table, cc, g_current_side);
		-- side_my, side_your, cc, target_card);
		print_eff_list(eff_list);


		print('DONE attack, src and target new status:');
		print_card(cc, cc.pos, 1);
		print_card(target_card, target_pos, 1);
		break;
	end -- end ally }

	-- new hand
	if input_list[1]=='hh' then
		local index;
		local flag = true; -- must init as true (for 0 #target_list)
		local clist;
		local atl = {};
		local cc = nil;
		clist = list_hand_index(g_logic_table, g_current_side);
		print_index_list(clist, g_logic_table);
		index = print_input('hand card');
		cc = index_card(index, g_logic_table);
		if index == nil or cc == nil or nil==table_find(clist, index) then
			print('ERROR invalid source card');
			break;
		end

		print('Total target = ' .. #(cc.target_list or {}));
		for k, t in ipairs(cc.target_list or {}) do
			local tindex ;
			clist = list_hand_target(index, g_logic_table, g_current_side, atl, k);
			print_index_list(clist, g_logic_table);
			tindex = print_input_target(k, t.optional);
			if nil == table_find(clist, tindex) then
				flag = t.optional;
				if flag ~= true then
					print('ERROR invalid target');
				end
				break;
			end
			atl [#atl + 1] = tindex;
			print('');
		end

		-- error case, early exit
		if flag ~= true then
			break;
		end
		local eff_list;
		eff_list,err = play(index, atl, g_logic_table, g_current_side, true);
		print_eff_list(eff_list);
		if nil == eff_list then 
			err = err or 'nil_err';
			print('ERROR hand not execute err=', err);
			break;
		end
		print('DONE hand hh');
		break;
	end

	-- at = attack using new index rule
	if input_list[1]=='attack' or input_list[1]=='at' then -- {
		local index;
		local tindex; -- target index (atl[1])
		local cc;
		local atl;
		local clist;

		if #input_list==1 then
			clist = list_attack_index(g_logic_table, g_current_side);
			print_index_list(clist, g_logic_table);
			index = print_input();
			if nil == table_find(clist, index) then
				print('ERROR invalid source');
				break;
			end

			clist = list_attack_target(index, g_logic_table, g_current_side);
			print_index_list(clist, g_logic_table);
			tindex = print_input_target(1, false);
			if nil == table_find(clist, tindex) then
				print('ERROR invalid target');
				break;
			end
			atl = { tindex };

		elseif #input_list==3 then
			-- assume #input_list == 3
			index = input_list[2];
			atl = { input_list[3] };
		else
			print('ERROR at only accept 1 or 3 param');
			break;
		end

		local eff_list;
		-- now: index and atl are ready
		eff_list, err = play(index, atl, g_logic_table, g_current_side, false);
		print_eff_list(eff_list);
		if nil == eff_list or #eff_list==0 then
			print('ERROR play_attack err = ', err);
		end
		print('DONE at');
		break;
	end  -- end at attack }

	-- display list of card with ability (trigger_skill ~= nil) 
	-- in hero,ally,support, using new index rule
	if input_list[1]=='ability' or input_list[1]=='ab' then -- start ab {
		local index; -- src index
		local cc;  -- the selected card
		local atl;
		local clist;
		if #input_list>=2 then
			index = input_list[2];
			cc = index_card(index, g_logic_table);
			atl = clone(input_list);
			table.remove(atl, 1); -- remove the 'ab' command
			table.remove(atl, 1); -- remove the cc index 
			print('#atl = ', #atl); 
		else -- #input_list=1, only 'ab' command below
			local flag = true;
			clist = list_ability_index(g_logic_table, g_current_side); -- LOGIC
			print_index_list(clist, g_logic_table); -- UI
			-- print_ability_card(side, g_current_side);
			index = print_input('ability card:');
			if nil == table_find(clist, index) then
				print('ERROR src card not in valid ability list');
				break;
			end
			cc = index_card(index, g_logic_table);
			if cc==nil then
				print('ERROR invalid src card 111');
				break;
			end

			atl = input_atl(cc, g_logic_table, g_current_side);
		end

		-- assume: cc non-nil, index = src card, atl non-nil
		if cc==nil then 
			print('ERROR invalid src 222');
			break;
		end
		if atl == nil then
			print('ERROR invalid target');
			break;
		end

		-- core logic
		local eff_list;
		eff_list,err = play(index, atl, g_logic_table, g_current_side, true);
		if nil == eff_list or #eff_list == 0 then
			err = err or 'nil_err';
			print('ERROR ability not execute err=', err);
			break;
		end
		print_eff_list(eff_list);
		print('DONE ability');
		break;
	end --  end ab }


	print ('??? Unknown command:' );
	
	until true; -- inner while loop }
end;  -- first outter while loop

end -- end of hero_main function }


-- in LuaTest.lua (UI)  
-- g_ui = true; -- non nil
-- require hero.lua

-- print( 'LOGIC_VERSION = ' .. LOGIC_VERSION );

if local_ui == nil then -- it will trigger warning in luacheck.sh, ignore!!
	hero_main();
end

