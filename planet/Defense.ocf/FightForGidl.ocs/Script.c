/*
	Fight for Gidl
	Authors: Sven2
	
	Defend the statue against waves of enemies
*/

static g_goal, g_object_fade, g_statue, g_doorleft, g_doorright;
static g_wave; // index of current wave
static g_spawned_enemies;
static g_relaunchs; // array of relaunch counts
static g_scores; // array of player scores
static g_ai; // derived from AI; contains changes for this scenario
static g_homebases; // item management / buy menus for each player
static const ENEMY = 10; // player number of enemy

static const MAX_RELAUNCH = 10;

//======================================================================
/* Initialization */

func Initialize()
{
	// Init door dummies
	g_doorleft.dummy_target = g_doorleft->CreateObject(DoorDummy, -6, 6);
	g_doorright.dummy_target = g_doorright->CreateObject(DoorDummy, +6, 6);
	// Wealth shown at all time
	GUI_Controller->ShowWealth();
	// static variable init
	g_homebases = [];
	InitWaveData();
}

func InitializePlayer(int plr, int iX, int iY, object pBase, int iTeam)
{
	if (GetPlayerType(plr) != C4PT_User) return;
	//DoWealth(plr, 10000);
	if (!g_statue) { EliminatePlayer(plr); return; } // no post-elimination join
	if (!g_relaunchs)
	{
		g_relaunchs = [];
		g_scores = [];
		Scoreboard->Init([{key = "relaunchs", title = Rule_Restart, sorted = true, desc = true, default = "", priority = 75},
	                    {key = "score", title = Nugget, sorted = true, desc = true, default = "0", priority = 100}]);
	}
	for (var stonedoor in FindObjects(Find_ID(StoneDoor), Find_Owner(NO_OWNER))) stonedoor->SetOwner(plr);
	g_relaunchs[plr] = MAX_RELAUNCH;
	g_scores[plr] = 0;
	Scoreboard->NewPlayerEntry(plr);
	Scoreboard->SetPlayerData(plr, "relaunchs", g_relaunchs[plr]);
	Scoreboard->SetPlayerData(plr, "score", g_scores[plr]);
	//SetFoW(false,plr); - need FoW for lights
	CreateObject(Homebase, 0,0, plr);
	JoinPlayer(plr);
	if (!g_wave) StartGame();
	return;
}

func RemovePlayer(int plr)
{
	if (g_homebases[plr]) g_homebases[plr]->RemoveObject();
	Scoreboard->SetPlayerData(plr, "relaunchs", Icon_Cancel);
	return;
}

private func TransferInventory(object from, object to)
{
	// Drop some items that cannot be transferred (such as connected pipes and dynamite igniters)
	var i = from->ContentsCount(), contents;
	while (i--)
		if (contents = from->Contents(i))
			if (contents->~IsDroppedOnDeath(from))
				contents->Exit();
	return to->GrabContents(from);
}

func JoinPlayer(plr, prev_clonk)
{
	var spawn_idx = Random(2);
	if (prev_clonk && g_statue) spawn_idx = (prev_clonk->GetX() > g_statue->GetX());
	var x=[494,763][spawn_idx],y = 360;
	var clonk = GetCrew(plr);
	if (clonk)
	{
		clonk->SetPosition(x,y-10);
	}
	else
	{
		clonk = CreateObjectAbove(Clonk, x,y, plr);
		clonk->MakeCrewMember(plr);
	}
	SetCursor(plr, clonk);
	clonk->DoEnergy(1000);
	clonk->MakeInvincibleToFriendlyFire();
	// contents
	clonk.MaxContentsCount = CustomAI.Clonk_MaxContentsCount;
	clonk.MaxContentsCountVal = 1;
	if (prev_clonk) TransferInventory(prev_clonk, clonk);
	if (!clonk->ContentsCount())
	{
		clonk->CreateContents(Bow);
		var arrow = CreateObjectAbove(Arrow);
		clonk->Collect(arrow);
		arrow->SetInfiniteStackCount();
	}
	//clonk->CreateContents(Musket);
	//clonk->Collect(CreateObjectAbove(LeadShot));
	clonk->~CrewSelection(); // force update HUD
	return;
}

func StartGame()
{
	// Init objects to defend
	var obj;
	for (obj in [g_statue, g_doorleft, g_doorright]) if (obj)
	{
		obj->SetCategory(C4D_Living);
		obj->SetAlive(true);
		obj.MaxEnergy = 800000;
		obj->DoEnergy(obj.MaxEnergy/1000);
		obj->AddEnergyBar();
		obj.FxNoPlayerDamageDamage = Scenario.Object_NoPlayerDamage;
		AddEffect("NoPlayerDamage", obj, 500, 0, obj);
	}
	if (g_statue)
	{
		g_statue.Death = Scenario.Statue_Death;
	}
	// Launch first wave!
	g_wave = 1;
	ScheduleCall(nil, Scenario.LaunchWave, 50, 1, g_wave);
	return true;
}

func Object_NoPlayerDamage(object target, fx, dmg, cause, cause_player)
{
	// players can't damage statue or doors
	if (GetPlayerType(cause_player) == C4PT_User) return 0;
	return dmg;
}



//======================================================================
/* Enemy waves */

func LaunchWave(int wave)
{
	// * Schedules spawning of all enemies 
	// * Schedules call to LaunchWaveDone() after last enemy has been spawned
	var wave_data = ENEMY_WAVE_DATA[g_wave];
	g_spawned_enemies = [];
	if (wave_data)
	{
		var wave_spawn_time = 0;
		CustomMessage(Format("$MsgWave$: %s", wave, wave_data.Name));
		Sound("Ding");
		for (var enemy in ForceVal2Array(wave_data.Enemies)) if (enemy)
		{
			if (enemy.Delay)
				ScheduleCall(nil, Scenario.ScheduleLaunchEnemy, enemy.Delay, 1, enemy);
			else
				ScheduleLaunchEnemy(enemy);
			wave_spawn_time = Max(wave_spawn_time, enemy.Delay + enemy.Interval * enemy.Num);
		}
		ScheduleCall(nil, Scenario.LaunchWaveDone, wave_spawn_time+5, 1, wave);
		return true;
	}
	return false;
}

func ScheduleLaunchEnemy(proplist enemy)
{
	// Schedules spawning of enemy definition
	// Spawn on ground or in air?
	var xmin, xmax, y;
	if (enemy.Type && enemy.Type->~IsFlyingEnemy())
	{
		// Air spawn
		xmin = 0;
		xmax = 550;
		y = 5;
	}
	else
	{
		xmin = xmax = 0;
		y = 509;
	}
	// Spawn either only enemy or mirrored enemies on both sides
	var side = enemy.Side;
	if (!side) side = WAVE_SIDE_LEFT | WAVE_SIDE_RIGHT;
	if (side & WAVE_SIDE_LEFT)  ScheduleCall(nil, CustomAI.LaunchEnemy, Max(enemy.Interval,1), Max(enemy.Num,1), enemy, 10 + xmin, xmax - xmin, y);
	if (side & WAVE_SIDE_RIGHT) ScheduleCall(nil, CustomAI.LaunchEnemy, Max(enemy.Interval,1), Max(enemy.Num,1), enemy, 1190 - xmax, xmax - xmin, y);
	return true;
}

func LaunchWaveDone(int wave)
{
	// All enemies spawned! Now start timer to check whether they are all dead
	ScheduleCall(nil, Scenario.CheckWaveCleared, 20, 9999999, wave);
	return true;
}

func CheckWaveCleared(int wave)
{
	// Check timer to determine if enemy wave has been cleared.
	// Enemies nil themselves when they're dead. So clear out nils and we're done when the list is empty
	var nil_idx;
	while ( (nil_idx=GetIndexOf(g_spawned_enemies))>=0 )
	{
		var l = GetLength(g_spawned_enemies) - 1;
		if (nil_idx<l) g_spawned_enemies[nil_idx] = g_spawned_enemies[l];
		SetLength(g_spawned_enemies, l);
	}
	if (!GetLength(g_spawned_enemies))
	{
		// All enemies dead!
		ClearScheduleCall(nil, Scenario.CheckWaveCleared);
		OnWaveCleared(wave);
	}
}

func OnWaveCleared(int wave)
{
	CustomMessage(Format("$MsgWaveCleared$", wave));
	Sound("Ding");
	// Fade out corpses
	if (g_object_fade) 
		for (var obj in FindObjects(Find_Or(Find_And(Find_ID(Clonk), Find_Not(Find_OCF(OCF_Alive))), Find_ID(Catapult))))
			obj->AddEffect("IntFadeOut", obj, 100, 1, g_object_fade, Rule_ObjectFade);
	// Next wave!
	++g_wave;
	if (ENEMY_WAVE_DATA[g_wave])
		ScheduleCall(nil, Scenario.LaunchWave, 500, 1, g_wave);
	else
	{
		// There is no next wave? Game done D:
		ScheduleCall(nil, Scenario.OnAllWavesCleared, 50, 1);
	}
}

// Clonk death callback
func OnClonkDeath(clonk, killed_by)
{
	// Player died?
	if (!clonk) return;
	var plr = clonk->GetOwner();
	if (GetPlayerType(plr) == C4PT_User)
	{
		// Relaunch count
		if (!g_relaunchs[plr])
		{
			Log("$MsgOutOfRelaunchs$", GetTaggedPlayerName(plr));
			Scoreboard->SetPlayerData(plr, "relaunchs", Icon_Cancel);
			EliminatePlayer(plr);
			return false;
		}
		// Relaunch count
		--g_relaunchs[plr];
		Scoreboard->SetPlayerData(plr, "relaunchs", g_relaunchs[plr]);
		Log("$MsgRelaunch$", GetTaggedPlayerName(plr));
		JoinPlayer(plr, clonk);
		//var gui_arrow = FindObject(Find_ID(GUI_GoalArrow), Find_Owner(plr));
		//gui_arrow->SetAction("Show", GetCursor(plr));
	}
	else
	{
		// Enemy clonk death
		// Remove inventory
		var i = clonk->ContentsCount(), obj;
		while (i--) if (obj=clonk->Contents(i))
			if (!obj->~OnContainerDeath())
				obj->RemoveObject();
		// Clear enemies from list
		i = GetIndexOf(g_spawned_enemies, clonk);
		if (i>=0)
		{
			g_spawned_enemies[i] = nil;
			// Kill bounty
			if (killed_by>=0)
			{
				Scoreboard->SetPlayerData(killed_by, "score", ++g_scores[killed_by]);
				DoWealth(killed_by, clonk.Bounty);
			}
		}
	}
	return;
}



//======================================================================
/* Game end */

func OnAllWavesCleared()
{
	// Success!
	if (g_goal) g_goal.is_fulfilled = true;
	if (GetPlayerType(ENEMY) == C4PT_Script) EliminatePlayer(ENEMY);
	GainScenarioAchievement("Done");
	GameOver();
	return true;
}

func Statue_Death()
{
	// Fail :(
	// Elminiate all players
	var i=GetPlayerCount(C4PT_User);
	while (i--) EliminatePlayer(GetPlayerByIndex(i, C4PT_User));
	// Statue down :(
	CastObjects(Nugget, 5, 10);
	Explode(10);
	return true;
}


//======================================================================
/* Wave and enemy definitions */

static const CSKIN_Default = 0,
             CSKIN_Steampunk = 1,
             CSKIN_Alchemist = 2,
             CSKIN_Farmer = 3,
             CSKIN_Amazon = [CSKIN_Farmer, "farmerClonkAmazon"],
             CSKIN_Ogre = [CSKIN_Alchemist, "alchemistClonkOgre"];

static const WAVE_POS_LEFT = [10, 529];
static const WAVE_POS_RIGHT = [1190, 509];

static const WAVE_SIDE_LEFT = 1,
             WAVE_SIDE_RIGHT = 2;

static ENEMY_WAVE_DATA;

static const g_respawning_weapons = [Firestone, Rock];

// init ENEMY_WAVE_DATA - would like to make it const, but arrays in static const proplists don't work properly
func InitWaveData()
{
	// Define weapon types
	var bigsword   = { InvType=Sword,     Scale=2000, Material="LaserSword"};
	var ogresword  = { InvType=Sword,     Scale=1800, Material="OgreSword"};
	var bigclub    = { InvType=Club,      Scale=2000};
	var nukekeg    = { InvType=PowderKeg, Scale=1400, Material="NukePowderKeg", Strength=80};
	// Define different enemy types
	var newbie     = { Name="$EnemyNewbie$",    Inventory=Rock,        Energy=  1, Bounty=  1, Color=0xff8000ff,                                  };
	var flintstone = { Name="$EnemyFlintstone$",Inventory=Firestone,   Energy= 10, Bounty=  3, Color=0xff8080ff,                                  };
	var bowman     = { Name="$EnemyBow$",       Inventory=[Bow, Arrow],Energy= 10, Bounty=  5, Color=0xff00ff00, Skin=CSKIN_Farmer                };
	var amazon     = { Name="$EnemyAmazon$",    Inventory=Javelin,     Energy= 10, Bounty=  5,                   Skin=CSKIN_Amazon,    Backpack=0 };
	var suicide    = { Name="$EnemySuicide$",   Inventory=PowderKeg,   Energy= 20, Bounty= 15, Color=0xffff0000, Skin=CSKIN_Alchemist, Backpack=0,                         Speed=80, Siege=true };
	var runner     = { Name="$EnemyRunner$",    Inventory=Rock,        Energy=  1, Bounty= 10, Color=0xffff0000,                       Backpack=0,                         Speed=250           };
	var artillery  = { Name="$EnemyArtillery$", Inventory=Firestone,   Energy= 30, Bounty= 20, Color=0xffffff00, Skin=CSKIN_Steampunk,             Vehicle=Catapult };
	var swordman   = { Name="$EnemySwordman$",  Inventory=Sword,       Energy= 30, Bounty= 30, Color=0xff0000ff,                                  };
	var bigswordman= { Name="$EnemySwordman2$", Inventory=bigsword,    Energy= 60, Bounty= 60, Color=0xff00ffff, Skin=CSKIN_Steampunk, Backpack=0 };
	var ogre       = { Name="$EnemyOgre$",      Inventory=bigclub,     Energy= 90, Bounty=100, Color=0xff00ffff, Skin=CSKIN_Ogre,      Backpack=0, Scale=[1400,1200,1200], Speed=50 };
	var swordogre  = { Name="$EnemyOgre$",      Inventory=ogresword,   Energy= 90, Bounty=100, Color=0xff805000, Skin=CSKIN_Ogre,      Backpack=0, Scale=[1400,1200,1200], Speed=50 };
	var nukeogre   = { Name="$EnemyOgre$",      Inventory=nukekeg,     Energy=120, Bounty=100, Color=0xffff0000, Skin=CSKIN_Ogre,      Backpack=0, Scale=[1400,1200,1200], Speed=40, Siege=true };
	var chippie    = { Type=Chippie };
	var boomattack = { Type=Boomattack };
	//newbie = runner;
	//newbie = runner;

	// Define composition of waves
	ENEMY_WAVE_DATA = [nil,
			{ Name = "$WaveNewbies$", Enemies = [
			new newbie      {            Num= 1, Interval=10, Side = WAVE_SIDE_LEFT }
	]}, { Name = "$WaveBows$", Enemies = [
			new newbie      {            Num= 2, Interval=10 },
			new bowman      { Delay= 30, Num= 3, Interval=10, Side = WAVE_SIDE_RIGHT },
			new amazon      { Delay= 30, Num= 3, Interval=10, Side = WAVE_SIDE_LEFT }
	]}, { Name = "Explosive", Enemies = [
			new flintstone  {            Num=10, Interval=20 }
	]}, { Name = "Boomattack", Enemies = [
			new boomattack  {            Num=10, Interval=70 }
	]}, { Name = "Suicidal", Enemies = [
			new suicide     {            Num= 2, Interval= 5 },
			new flintstone  { Delay= 15, Num= 5, Interval= 5 },
			new suicide     { Delay= 50, Num= 1 }
	]}, { Name = "Swordsmen", Enemies = [
			new swordman    {            Num=10, Interval=20 },
			new bigswordman { Delay=210, Num= 1, Interval=10 }
	]}, { Name = "Oh Shrek!", Enemies = [
			new ogre        {            Num= 2, Interval=20 },
			new swordogre   { Delay= 40, Num= 1, Interval=20 },
			new bowman      { Delay= 60, Num= 3, Interval= 5 }
	]}, { Name = "Heavy artillery incoming", Enemies = [
			new artillery   {            Num= 1              },
			new ogre        {            Num= 2, Interval=20 },
			new flintstone  { Delay= 15, Num= 5, Interval= 5 },
			new swordogre   { Delay= 60, Num= 1, Interval=20 }
	]}, { Name = "Stop the big ones", Enemies = [
			new flintstone  {            Num=20, Interval=15 },
			new nukeogre    {            Num= 2, Interval=99 },
			new amazon      { Delay= 50, Num= 6, Interval=10 }
	]}, { Name = "Supreme Boomattack", Enemies = [
			new boomattack  {            Num=30, Interval=10 }
	]}, { Name = "Alien invasion", Enemies = [
			new bowman      { Delay=260, Num= 3, Interval= 5 },
			new chippie     {            Num=10, Interval=10 },
			new amazon      { Delay=250, Num= 6, Interval=10 }
	]}, { Name = "Two of each kind", Enemies = [
			new newbie      { Delay=  0                      },
			new ogre        { Delay=  0                      },
			new swordman    { Delay= 10                      },
			new amazon      { Delay= 12                      },
			new nukeogre    { Delay= 14                      },
			new boomattack  { Delay= 15                      },
			new swordogre   { Delay= 20                      },
			new artillery   { Delay= 21                      },
			new flintstone  { Delay= 22                      },
			new bigswordman { Delay= 40                      },
			new bowman      { Delay= 45                      },
			new suicide     { Delay= 30                      }
	]}, { Name = "Finale!", Enemies = [
			new artillery   {            Num= 2, Interval= 5 },
			new newbie      {            Num=10, Interval=15 },
			new swordman    { Delay=  3, Num= 5, Interval=30 },
			new nukeogre    { Delay= 60, Num= 2, Interval=50 },
			new bigswordman { Delay=103, Num= 3, Interval=30 },
			new amazon      { Delay= 10, Num= 5, Interval=10 },
			new boomattack  { Delay= 40, Num=20, Interval=20 },
			new flintstone  {            Num=20, Interval=10 },
			new bowman      { Delay=  8, Num= 5, Interval=40 },
			new suicide     { Delay= 25, Num=10, Interval=20 },
			new ogre        { Delay= 30, Num= 3, Interval=20 },
			new swordogre   { Delay=  4, Num= 4, Interval=99 }
	]} ];
	return true;
}
