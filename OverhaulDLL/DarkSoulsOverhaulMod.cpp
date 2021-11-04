/*
    DARK SOULS OVERHAUL

    Contributors to this file:
        Metal-Crow  -  C++
        Sean Pesce  -  C++

*/

#include "DllMain.h"
#include "Files.h"
#include "AntiCheat.h"
#include "BloodborneRallySystem.h"
#include "Challenge/GravelordPhantoms.h"
#include "Menu/SavedCharacters.h"
#include "XInputUtil.h"
#include "LadderFix.h"
#include "Updates.h"
#include "DurabilityBars.h"
#include "MultiConsume.h"
#include "MultiTribute.h"
#include "CrashHandler.h"

#include "D3dx9math.h"


/*
    Called from DllMain when the plugin DLL is first loaded into memory (PROCESS_ATTACH case).
    NOTE: This function runs in the same thread as DllMain, so code implemented here will halt
    game loading. Code in this function should be limited to tasks that absolutely MUST be
    executed before the game loads. For less delicate startup tasks, use on_process_attach_async()
    or initialize_plugin().
*/
void on_process_attach()
{
    LPSTR options = GetCommandLineA();

    //enable multiphatom if we have this arg given, or it's in the ini
    if (strstr(options, "-phantom-break-on") || (int)GetPrivateProfileInt(_DS1_OVERHAUL_PREFS_SECTION_, _DS1_OVERHAUL_PREF_ENABLE_MULTIPHANTOM_, 0, _DS1_OVERHAUL_SETTINGS_FILE_) != 0)
    {
        Mod::enable_multiphantom = true;
        DWORD OldProtect;
        //this is the address of the main window's name
        VirtualProtect((LPVOID)0x01168630, 50, PAGE_READWRITE, &OldProtect);
        swprintf((wchar_t*)0x01168630, 28, L"DARK SOULS - PHANTOM BREAK");
    }
    //disable multiphatom if we have this alternative arg given, or it's in the ini
    else if (strstr(options, "-phantom-break-off") || (int)GetPrivateProfileInt(_DS1_OVERHAUL_PREFS_SECTION_, _DS1_OVERHAUL_PREF_ENABLE_MULTIPHANTOM_, 0, _DS1_OVERHAUL_SETTINGS_FILE_) == 0)
    {
        Mod::enable_multiphantom = false;
    }
    //disable multiphatom by default
    else
    {
        Mod::enable_multiphantom = false;
    }

    //make sure we set the appid to spacewars, just in case
    FILE* fp = fopen("./steam_appid.txt", "w");
    fprintf(fp, "480");
    fclose(fp);

    Mod::startup_messages.push_back(DS1_OVERHAUL_TXT_INTRO);
    Mod::startup_messages.push_back("");

    set_crash_handlers();

    // Load startup preferences from settings file
    Mod::get_startup_preferences();

    // Initialize file I/O monitoring data structs
    Files::init_io_monitors();

    // Check if game version is supported
    if (!game_version_is_supported)
    {
        // Placeholder handling of wrong game version

        if (Game::get_game_version() == 139) { // @TODO: Fix get_game_version() to work on different builds of DARKSOULS.exe
            // Debug build
            Mod::startup_messages.push_back(Mod::output_prefix + "WARNING: Debug game version detected. Disabling features...");
            Game::run_debug_tasks();
            return;
        } else {
            Mod::startup_messages.push_back(Mod::output_prefix + "WARNING: Unsupported game version detected.");
            MessageBox(NULL, std::string("Invalid game version detected. Change to supported game version, or disable the Dark Souls Overhaul Mod.").c_str(),
                       "ERROR: Dark Souls Overhaul Mod - Wrong game version", NULL);
            exit(0);
        }
    }

    // Apply increased memory limit patch
    Game::set_memory_limit(Game::memory_limit);

    // Inject code to capture starting addresses of all Param files (removes need for AoB scans)
    Params::patch();

    Files::apply_function_intercepts();

    // Check for existence of non-default game files
    Files::check_custom_archive_file_path();
    Files::check_custom_save_file_path();
    Files::check_custom_game_config_file_path();

    if (Mod::enable_multiphantom) {
        Game::increase_phantom_limit1();
        Game::set_game_version(DS1_OVERHAUL_GAME_VER_NUM);
    }
    else {
        Game::set_game_version(DS1_VERSION_OVERHAUL_CHEATS);
    }
}

static uint32_t bow_cam_change_return;
static uint32_t ChrAimCam_addr = 0;
void __declspec(naked) __stdcall bow_cam_change_injection()
{
    __asm {
        //save the address so we can edit it as needed
        mov ChrAimCam_addr, esi

        //original code
        mov    eax, esi
        mov    esp, ebp
        pop    ebp
        ret
    }
}
/*
    Called from DllMain when the plugin DLL is first loaded into memory (PROCESS_ATTACH case).
    This function runs in a separate thread from DllMain, so code implemented here does NOT
    pause game loading. Code that must be executed before the game loads should be implemented
    in on_process_attach() instead of this function.
*/
DWORD WINAPI on_process_attach_async(LPVOID lpParam)
{
    if (!game_version_is_supported) {
        return 0;
    }

    // Start anti-cheat
    AntiCheat::start();

    // Inject custom strings for saved characters menu
    Menu::Saves::init_custom_strings();

    // Allow modded effectIDs
    Game::unrestrict_network_synced_effectids();

    Game::increase_gui_hpbar_max();

    if (LadderFix::enable_pref) {
        LadderFix::apply();
    }

    // Initialize XInput hook
    XInput::initialize();

    if ((int)GetPrivateProfileInt(_DS1_OVERHAUL_CHALLENGE_SECTION_, _DS1_OVERHAUL_PREF_CM_GL_PHANTOMS_,
                                  Challenge::GravelordPhantoms::active, _DS1_OVERHAUL_SETTINGS_FILE_) != 0) {
        // Enable auto-spawning Gravelord Phantoms challenge mod
        Challenge::GravelordPhantoms::enable();
    }

    //Allow us to have bow aiming changes
    uint8_t *write_address = (uint8_t*)(0xf18857);
    set_mem_protection(write_address, 5, MEM_PROTECT_RWX);
    inject_jmp_5b(write_address, &bow_cam_change_return, 0, &bow_cam_change_injection);

    return 0;
}

// Called from DllMain when the plugin DLL is unloaded (PROCESS_DETACH case)
void on_process_detach()
{
    // Exit tasks should be performed here

    // Cancel any unfinished tasks
    Mod::deferred_tasks_complete = true;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////// Exported functions ///////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////
/*
    The functions below are exported from this DLL and imported
    by the DirectX9 overlay when loading this DLL as a plugin.

    These functions are called during important events throughout
    the execution of the program, allowing the developer(s) to run
    code that takes advantage of these processes (such as rendering
    DirectX elements and reading player input).
*/

/*
    Called exactly once after DirectX9 overlay is initialized.
*/
__declspec(dllexport) void __stdcall initialize_plugin()
{
    // Set overlay info strings
    if (Mod::legacy_mode) {
#ifndef DS1_OVERHAUL_QOL_PREVIEW
        set_text_feed_title("[Dark Souls Overhaul Mod (Legacy Mode)]");
#else
        set_text_feed_title("[Dark Souls Overhaul Mod (QoL Preview)]");
#endif // DS1_OVERHAUL_QOL_PREVIEW
    } else {
        set_text_feed_title("[Dark Souls Overhaul Mod]");
    }
    print("-------------DARK SOULS OVERHAUL TEST BUILD-------------", 15000, false, SP_D3D9O_TEXT_COLOR_ORANGE);

    // Print startup messages
    for (std::string msg : Mod::startup_messages)
        print_console(msg.c_str());

    if (!game_version_is_supported) {
        return;
    }

    // Load user preferences & keybinds from settings file
    Mod::get_user_preferences();
    Mod::get_user_keybinds();

    // Register console commands
    Mod::register_console_commands();

    // Initialize pointers
    Game::init_pointers();

    // Apply secondary phantom limit patch
    if (Mod::enable_multiphantom) {
        Game::increase_phantom_limit2();
    }

    if (Mod::disable_low_fps_disconnect)
    {
        // Disable "Framerate insufficient for online play" error
        Game::enable_low_fps_disconnect(false, Mod::output_prefix);
    }

    std::string tmp;
    if ((int)GetPrivateProfileInt(_DS1_OVERHAUL_PREFS_SECTION_, _DS1_OVERHAUL_PREF_AUTO_MOTD_, 0, _DS1_OVERHAUL_SETTINGS_FILE_) != 0)
    {
        extern int cc_overhaul_motd(std::vector<std::string> args, std::string *output);
        cc_overhaul_motd({""}, &tmp);
        print_console(tmp);
        tmp.clear();
    }
    if ((int)GetPrivateProfileInt(_DS1_OVERHAUL_PREFS_SECTION_, _DS1_OVERHAUL_PREF_AUTO_UPDATE_CHECK_, 0, _DS1_OVERHAUL_SETTINGS_FILE_) != 0)
    {
        extern int cc_overhaul_update(std::vector<std::string> args, std::string *output);
        cc_overhaul_update({""}, &tmp);
        print_console(tmp);
        tmp.clear();
    }

    // Start thread for deferred tasks
    if (!CreateThread(NULL, 0, deferred_tasks, NULL, 0, NULL))
    {
        // Error creating new thread
    }

    Mod::initialized = true; // Should be the last statement in this function
}


typedef struct
{
    float NearClip;
    float FovMax;
    float FovMin;
    float ZoomSpeedScale;
    float ZoomInOrigin_X;
    float ZoomInOrigin_Y;
    float ZoomInOrigin_Z;
    float ZoomInOrigin_W;
    float ZoomOutOrigin_X;
    float ZoomOutOrigin_Y;
    float ZoomOutOrigin_Z;
    float ZoomOutOrigin_W;
    float VerticalLookLimit_Low;
    float VerticalLookLimit_High;
    float CamRotateSpeedMin_X;
    float CamRotateSpeedMin_Y;
    float CamRotateSpeedMax_X;
    float CamRotateSpeedMax_Y;
    float CamRotateSpeedHiMin_X;
    float CamRotateSpeedHiMin_Y;
    float CamRotateSpeedHiMax_X;
    float CamRotateSpeedHiMax_Y;
} CameraAimStruct;

void Set_ChrAimCam_From_CameraAimStruct(uint32_t ChrAimCam, CameraAimStruct CamData)
{
    if (ChrAimCam == 0)
    {
        return;
    }

    *(float*)(ChrAimCam + 0x58) = CamData.NearClip;
    *(float*)(ChrAimCam + 0x134) = CamData.FovMax;
    *(float*)(ChrAimCam + 0x130) = CamData.FovMin;
    *(float*)(ChrAimCam + 0x140) = CamData.ZoomSpeedScale;
    *(float*)(ChrAimCam + 0xB0) = CamData.ZoomInOrigin_X;
    *(float*)(ChrAimCam + 0xB4) = CamData.ZoomInOrigin_Y;
    *(float*)(ChrAimCam + 0xB8) = CamData.ZoomInOrigin_Z;
    *(float*)(ChrAimCam + 0xBC) = CamData.ZoomInOrigin_W;
    *(float*)(ChrAimCam + 0xC0) = CamData.ZoomOutOrigin_X;
    *(float*)(ChrAimCam + 0xC4) = CamData.ZoomOutOrigin_Y;
    *(float*)(ChrAimCam + 0xC8) = CamData.ZoomOutOrigin_Z;
    *(float*)(ChrAimCam + 0xCC) = CamData.ZoomOutOrigin_W;
    *(float*)(ChrAimCam + 0xF0) = CamData.VerticalLookLimit_Low;
    *(float*)(ChrAimCam + 0xF4) = CamData.VerticalLookLimit_High;
    *(float*)(ChrAimCam + 0x104) = CamData.CamRotateSpeedMin_X;
    *(float*)(ChrAimCam + 0x108) = CamData.CamRotateSpeedMin_Y;
    *(float*)(ChrAimCam + 0x10C) = CamData.CamRotateSpeedMax_X;
    *(float*)(ChrAimCam + 0x110) = CamData.CamRotateSpeedMax_Y;
    *(float*)(ChrAimCam + 0x11C) = CamData.CamRotateSpeedHiMin_X;
    *(float*)(ChrAimCam + 0x120) = CamData.CamRotateSpeedHiMin_Y;
    *(float*)(ChrAimCam + 0x124) = CamData.CamRotateSpeedHiMax_X;
    *(float*)(ChrAimCam + 0x128) = CamData.CamRotateSpeedHiMax_Y;
}

/*
    Continuously called from the main thread loop in the overlay DLL.
*/
__declspec(dllexport) void __stdcall main_loop()
{
    // Use this function for code that must be called rerpeatedly, such as checking flags or waiting for values to change

    if (Mod::initialized)
    {
        // Check for pending save file index changes
        if (Files::save_file_index_pending_set_next) {
            if (Files::saves_menu_is_open()) {
                Files::set_save_file_next();
            }
            Files::save_file_index_pending_set_next = false;
        }
        if (Files::save_file_index_pending_set_prev) {
            if (Files::saves_menu_is_open()) {
                Files::set_save_file_prev();
            }
            Files::save_file_index_pending_set_prev = false;
        }

        // Update multiplayer node graph HUD element display status
        Hud::set_show_node_graph(Hud::get_show_node_graph());

        // Update multiplayer node count
        Game::node_count = Game::get_node_count();

        // If cheats are enabled, make sure saveing and multiplayer are disabled
        if (Mod::cheats) {
            if (*(bool*)Game::saves_enabled.resolve())
                // Cheats are on, but saving is enabled; disable saving
                Game::saves_enabled.write(false);

            if (Game::get_game_version() != DS1_VERSION_OVERHAUL_CHEATS)
                // Change to network for players that have cheats enabled
                Game::set_game_version(DS1_VERSION_OVERHAUL_CHEATS);

            // @TODO: Check that multiplayer is disabled? (first figure out how to disable it)
        }

        // Check if the player is stuck at the bonfire, and if so, automatically apply the bonfire input fix
        Game::check_bonfire_input_bug();

        // Check that a character is loaded
        static uint32_t* pc_status = NULL;
        pc_status = reinterpret_cast<uint32_t*>(Game::player_char_status.resolve());
        if (pc_status && (*pc_status != DS1_PLAYER_STATUS_LOADING))
        {
            DurabilityBars::update_data();
        }
        else
        {
            // No character loaded
            DurabilityBars::render_data.display = false;

            MultiConsume::skip_next_multiconsume_dialog = 0;
            MultiConsume::force_consume_next_item = 0;
            MultiConsume::last_multiconsume_item = 0;
            MultiConsume::last_consumption_quantity = 0;
            MultiConsume::item_quantity = 0;
            MultiConsume::capture_next_dlg_result = 0;
            MultiConsume::consumed_from_inventory = 0;

            MultiTribute::change_next_dlg_to_number_picker = 0;
            MultiTribute::tmp_last_talk = 0;
            MultiTribute::adjust_tribute_dec = 0;
            MultiTribute::sunlight_medal_count = 0;
            MultiTribute::dragon_scale_count = 0;
            MultiTribute::souvenir_count = 0;
            MultiTribute::eye_of_death_count = 0;
        }

        // Check if the player is invading, or if other players are invading
        Game::check_invaders_present_in_world();

        // Store current world area for ladder fix
        LadderFix::world_area = Game::get_online_area_id();

        // Update IGT string
        if (Mod::hud_play_time_pref)
        {
            Game::update_play_time_str();
        }

        // Check if in-game text input has focus (character creation screen)
        Menu::Dlg::text_input_active = ((!console_is_open()) && Menu::Dlg::flags() && Menu::Dlg::flag(0x94) == 7);

		// Check if the character is loading, and apply actions that need to be _reapplied_ after every loading screen
		/*int char_status;
		Game::player_char_status.read(&char_status);
		if (char_status == DS1_PLAYER_STATUS_LOADING) {
			while (char_status == DS1_PLAYER_STATUS_LOADING) {
				Game::player_char_status.read(&char_status);
				Sleep(50);
			}
			Game::on_reloaded();
		}*/

        int char_status;
        Game::player_char_status.read(&char_status);
        bool use_default_camera = true;

        //hunt through the currently applied speffects
        //this is a linked list
        SpPointer speffect_cur = SpPointer(Game::world_char_base, { 0x28, 0x10, 0x4, 0x28 });
        uint32_t* speffect_cur_addr = NULL;
        while ((speffect_cur_addr = (uint32_t*)speffect_cur.resolve()) != NULL)
        {
            uint32_t speffect_id = *speffect_cur_addr;

            // Check if the player character has the special force-cooperation speffect applied, and force the invasion type
            // This is automatically reset back to -3 (SESSION_FORCEJOIN) on invasion, so no need to worry about resetting it
            if (speffect_id == 9513)
            {
                SpPointer invasionTypeSPP = SpPointer((void *)0x13784A0, { 0xBE4 });
                if (invasionTypeSPP.resolve() != NULL)
                {
                    *(uint32_t*)invasionTypeSPP.resolve() = -1; //SESSION_WHITE
                }
            }

            //over the shoulder + wide fov
            if (speffect_id == 9425 && char_status != DS1_PLAYER_STATUS_LOADING)
            {
                CameraAimStruct cam;
                cam.NearClip = 0.200000003f;
                cam.FovMax = 0.6108653545f;
                cam.FovMin = 0.0872665f;
                cam.ZoomSpeedScale = 0.04500000179f;
                cam.ZoomInOrigin_X = 0.2599999905f;
                cam.ZoomInOrigin_Y = 1.529999971f;
                cam.ZoomInOrigin_Z = 0.0f;
                cam.ZoomInOrigin_W = 0.6000000238f;
                cam.ZoomOutOrigin_X = 0.2599999905f;
                cam.ZoomOutOrigin_Y = 1.529999971f;
                cam.ZoomOutOrigin_Z = 0.0f;
                cam.ZoomOutOrigin_W = 0.6000000238f;
                cam.VerticalLookLimit_Low = 60.0f;
                cam.VerticalLookLimit_High = -50.0f;
                cam.CamRotateSpeedMin_X = 0.6108653545f;
                cam.CamRotateSpeedMin_Y = 0.6108653545f;
                cam.CamRotateSpeedMax_X = 1.658063054f;
                cam.CamRotateSpeedMax_Y = 1.658063054f;
                cam.CamRotateSpeedHiMin_X = 0.6108653545f;
                cam.CamRotateSpeedHiMin_Y = 0.6108653545f;
                cam.CamRotateSpeedHiMax_X = 1.658062577f;
                cam.CamRotateSpeedHiMax_Y = 1.658062577f;
                Set_ChrAimCam_From_CameraAimStruct(ChrAimCam_addr, cam);
                use_default_camera = false;
            }

            //over the shoulder + slight XY offset + wide fov + low zoom
            if (speffect_id == 9426 && char_status != DS1_PLAYER_STATUS_LOADING)
            {
                CameraAimStruct cam;
                cam.NearClip = 0.200000003f;
                cam.FovMax = 0.6108653545f;
                cam.FovMin = 0.2268927991f;
                cam.ZoomSpeedScale = 0.04500000179f;
                cam.ZoomInOrigin_X = 0.330f;
                cam.ZoomInOrigin_Y = 1.570f;
                cam.ZoomInOrigin_Z = 0.000f;
                cam.ZoomInOrigin_W = 0.600f;
                cam.ZoomOutOrigin_X = 0.380f;
                cam.ZoomOutOrigin_Y = 1.570f;
                cam.ZoomOutOrigin_Z = 0.000f;
                cam.ZoomOutOrigin_W = 0.820f;
                cam.VerticalLookLimit_Low = 60.0f;
                cam.VerticalLookLimit_High = -50.0f;
                cam.CamRotateSpeedMin_X = 0.6108653545f;
                cam.CamRotateSpeedMin_Y = 0.6108653545f;
                cam.CamRotateSpeedMax_X = 1.658063054f;
                cam.CamRotateSpeedMax_Y = 1.658063054f;
                cam.CamRotateSpeedHiMin_X = 0.6108653545f;
                cam.CamRotateSpeedHiMin_Y = 0.6108653545f;
                cam.CamRotateSpeedHiMax_X = 1.658062577f;
                cam.CamRotateSpeedHiMax_Y = 1.658062577f;
                Set_ChrAimCam_From_CameraAimStruct(ChrAimCam_addr, cam);
                use_default_camera = false;
            }

            //lower side angle + wide fov + low zoom
            if (speffect_id == 9427 && char_status != DS1_PLAYER_STATUS_LOADING)
            {
                CameraAimStruct cam;
                cam.NearClip = 0.200000003f;
                cam.FovMax = 0.6108653545f;
                cam.FovMin = 0.2617993951f;
                cam.ZoomSpeedScale = 0.04500000179f;
                cam.ZoomInOrigin_X = 0.310f;
                cam.ZoomInOrigin_Y = 1.370f;
                cam.ZoomInOrigin_Z = 0.000f;
                cam.ZoomInOrigin_W = 0.600f;
                cam.ZoomOutOrigin_X = 0.380f;
                cam.ZoomOutOrigin_Y = 1.370f;
                cam.ZoomOutOrigin_Z = 0.000f;
                cam.ZoomOutOrigin_W = 1.000f;
                cam.VerticalLookLimit_Low = 60.0f;
                cam.VerticalLookLimit_High = -50.0f;
                cam.CamRotateSpeedMin_X = 0.6108653545f;
                cam.CamRotateSpeedMin_Y = 0.6108653545f;
                cam.CamRotateSpeedMax_X = 1.658063054f;
                cam.CamRotateSpeedMax_Y = 1.658063054f;
                cam.CamRotateSpeedHiMin_X = 0.6108653545f;
                cam.CamRotateSpeedHiMin_Y = 0.6108653545f;
                cam.CamRotateSpeedHiMax_X = 1.658062577f;
                cam.CamRotateSpeedHiMax_Y = 1.658062577f;
                Set_ChrAimCam_From_CameraAimStruct(ChrAimCam_addr, cam);
                use_default_camera = false;
            }

            uint32_t next_speffect_addr = *(uint32_t*)((uint32_t)speffect_cur_addr+8);
            speffect_cur = SpPointer((void*)(next_speffect_addr+0x28));
        }

        //default aim and camera
        if (use_default_camera && char_status != DS1_PLAYER_STATUS_LOADING)
        {
            CameraAimStruct cam;
            cam.NearClip = 0.200000003f;
            cam.FovMax = 0.7850000262f;
            cam.FovMin = 0.0700000003f;
            cam.ZoomSpeedScale = 0.04500000179f;
            cam.ZoomInOrigin_X = 0.25f;
            cam.ZoomInOrigin_Y = 1.379999995f;
            cam.ZoomInOrigin_Z = 0.5f;
            cam.ZoomInOrigin_W = 0.0f;
            cam.ZoomOutOrigin_X = 0.25f;
            cam.ZoomOutOrigin_Y = 1.379999995f;
            cam.ZoomOutOrigin_Z = 0.3899999857f;
            cam.ZoomOutOrigin_W = 0.0f;
            cam.VerticalLookLimit_Low = 60.0f;
            cam.VerticalLookLimit_High = -65.0f;
            cam.CamRotateSpeedMin_X = 0.6108653545f;
            cam.CamRotateSpeedMin_Y = 0.6108653545f;
            cam.CamRotateSpeedMax_X = 1.658063054f;
            cam.CamRotateSpeedMax_Y = 1.658063054f;
            cam.CamRotateSpeedHiMin_X = 0.6108653545f;
            cam.CamRotateSpeedHiMin_Y = 0.6108653545f;
            cam.CamRotateSpeedHiMax_X = 1.658062577f;
            cam.CamRotateSpeedHiMax_Y = 1.658062577f;
            Set_ChrAimCam_From_CameraAimStruct(ChrAimCam_addr, cam);
        }
    }
}

/*
    Called from initialize_plugin() after the DLL is loaded and initialized. This function is for
    tasks  that can only be  executed AFTER certain events have  occurred, but don't need to be
    executed BEFORE any specific time or event.
*/
DWORD WINAPI deferred_tasks(LPVOID lpParam)
{
    if (!game_version_is_supported) {
        return 0;
    }

    // Sleep time (in milliseconds) between loop iterations
    const int wait_time = 500;

    // Wait for event: Main menu loaded
    //SpPointer main_menu_logo(Game::ds1_base, { 0x6C });
    //while ((!Mod::deferred_tasks_complete) && (!main_menu_logo.resolve() || !*reinterpret_cast<uint8_t*>(main_menu_logo.resolve()))) {
    //    Sleep(wait_time);
    //}
    //if (Mod::deferred_tasks_complete) return 0;


    // Wait for event: Saved characters menu opened
    while ((!Mod::deferred_tasks_complete) && !Files::saves_menu_is_open() && !Params::all_loaded()) {
        Sleep(wait_time);
    }
    if (Mod::deferred_tasks_complete) return 0;

    // Obtain menu strings from FSB file
    Menu::Dlg::menu_fsb();
    

    // Wait for event: all game parameter files loaded
    while ((!Mod::deferred_tasks_complete) && !Params::all_loaded()) {
        Sleep(wait_time);
    }
    if (Mod::deferred_tasks_complete) return 0;

    // Perform tasks that rely on parameter files being loaded
    Game::on_all_params_loaded();

    // Wait for event: first character loaded in this instance of the game
    int char_status = DS1_PLAYER_STATUS_LOADING;
    while ((!Mod::deferred_tasks_complete) && char_status == DS1_PLAYER_STATUS_LOADING) {
        Game::player_char_status.read(&char_status);
        Sleep(wait_time);
    }
    if (Mod::deferred_tasks_complete) return 0;

    // Perform tasks that rely on a character being loaded
    Game::on_first_character_loaded();

    ////////// Implement additional wait conditions here //////////

    // Finished deferred tasks
    Mod::deferred_tasks_complete = true;
    return 0;
}

/*
    Called every frame inside of a BeginScene()/EndScene() pair.

        Microsoft documentation for IDirect3DDevice9 Interface:
        https://msdn.microsoft.com/en-us/library/windows/desktop/bb174336(v=vs.85).aspx

*/
#if 0
__declspec(dllexport) void __stdcall draw_overlay(std::string *text_feed_info_header)
{
    /*
        Allows drawing of additional overlay elements and live text feed info header elements.
        An appropriate stateblock for overlay drawing is applied before this function is called.

        NOTE: Use the _d3d9_dev macro to obtain the game's IDirect3DDevice9 for drawing new elements.

        WARNING: This function is called from inside the DirectX9 Device Present() function. Keep
        code in this function as optimized as possible to avoid lowering FPS.
    */

    if (Mod::show_node_count)
    {
        // Show node count in text feed info header
        if (Game::node_count > -1)
            text_feed_info_header->append("[Nodes: ").append(std::to_string(Game::node_count)).append("]  ");
        else
            text_feed_info_header->append("[Nodes: --]  ");
    }

    if (Mod::hud_play_time_pref)
    {
        // Show IGT in text feed info header
        text_feed_info_header->append(Game::play_time_str.c_str());
    }
}
#endif

/*
    Called every frame from the Present() member function of either the D3D9 device or SwapChain (depends on the game's rendering method; Dark Souls uses Device).
    If called from IDirect3DDevice9::Present (rather than IDirect3DSwapChain9::Present), dwFlags is always zero.

        Microsoft documentation for IDirect3DDevice9::Present():
        https://msdn.microsoft.com/en-us/library/windows/desktop/bb174423(v=vs.85).aspx

*/
#if 0
__declspec(dllexport) void __stdcall present(const RECT *pSourceRect, const RECT *pDestRect, HWND hDestWindowOverride, const RGNDATA *pDirtyRegion, DWORD dwFlags)
{
    /*
        NOTE: Use the _d3d9_dev macro to obtain the game's IDirect3DDevice9 for drawing new elements.

        WARNING: This function is called from inside the DirectX9 Device Present() function. Keep
        code in this function as optimized as possible to avoid performance issues.
    */

    // Draw weapon durability meter HUD elements
    if (DurabilityBars::enable_pref && DurabilityBars::render_data.display)
    {
        _d3d9_dev->GetDisplayMode(0, &DurabilityBars::render_data.display_mode);

        _d3d9_dev->Clear(2, DurabilityBars::render_data.backgrounds, D3DCLEAR_TARGET, D3DXCOLOR(0.0f, 0.0f, 0.0f, 1.0f), 0, 0); // Black
        _d3d9_dev->Clear(2, DurabilityBars::render_data.bars, D3DCLEAR_TARGET, D3DXCOLOR(0x00563433), 0, 0); // Red
    }
}
#endif

/*
    Called from the EndScene() member function of the D3D9 device.
    Note: This function isn't necessarily called exactly once per frame (could be more, less, or not at all).

        Microsoft documentation for IDirect3DDevice9::EndScene():
        https://msdn.microsoft.com/en-us/library/windows/desktop/bb174375(v=vs.85).aspx

*/
#if 0
__declspec(dllexport) void __stdcall end_scene()
{
    /*
        NOTE: Use the _d3d9_dev macro to obtain the game's IDirect3DDevice9 for drawing new elements.

        WARNING: This function is called from inside the DirectX9 Device EndScene() function. Keep
        code in this function as optimized as possible to avoid performance issues.
    */
}
#endif

/*
    Called every time GetRawInputData() is called with a non-null pData argument.

    Use this function to read player input, if needed.

        Microsoft documentation for GetRawInputData:
        https://msdn.microsoft.com/en-us/library/windows/desktop/ms645596(v=vs.85).aspx

    @return     True if the given input message should be cancelled before being sent to
                the game, thereby disabling game input. Otherwise, returns false.
*/
#if 0
__declspec(dllexport) bool __stdcall get_raw_input_data(RAWINPUT *pData, PUINT pcbSize)
{
    // Mouse cursor position
    static POINT cursor_position; // Static variable; saved between calls

    // Determine input device type
    switch (pData->header.dwType)
    {
        case RIM_TYPEMOUSE:

            // Handle mouse input

            // Mouse button press
            switch (pData->data.mouse.usButtonFlags)
            {
                case RI_MOUSE_LEFT_BUTTON_DOWN:

                    break;

                case RI_MOUSE_LEFT_BUTTON_UP:

                    break;

                case RI_MOUSE_RIGHT_BUTTON_DOWN:

                    break;

                case RI_MOUSE_RIGHT_BUTTON_UP:

                    break;

                case RI_MOUSE_MIDDLE_BUTTON_DOWN:

                    break;

                case RI_MOUSE_MIDDLE_BUTTON_UP:

                    break;

                case RI_MOUSE_WHEEL:
                    if (pData->data.mouse.usButtonData == 120) {
                        // Scrolling up
                    } else if (pData->data.mouse.usButtonData == 65416) {
                        // Scrolling down
                    }
                    break;

                    // Additional mouse buttons
                case RI_MOUSE_BUTTON_4_DOWN:
                case RI_MOUSE_BUTTON_4_UP:
                case RI_MOUSE_BUTTON_5_DOWN:
                case RI_MOUSE_BUTTON_5_UP:
                    break;
            } // switch (pData->data.mouse.usButtonFlags)

            // Mouse movement
            switch (pData->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)
            {
                case MOUSE_MOVE_ABSOLUTE:
                    // Mouse movement data is based on absolute position
                    cursor_position.x = pData->data.mouse.lLastX;
                    cursor_position.y = pData->data.mouse.lLastY;
                    break;
                case MOUSE_MOVE_RELATIVE:
                    // Mouse movement data is relative (based on last known position)
                    cursor_position.x += pData->data.mouse.lLastX;
                    cursor_position.y += pData->data.mouse.lLastY;

                    // Keep cursor on-screen
                    RECT window_rect;
                    if (!GetClientRect(_game_window, &window_rect)) {
                        // Handle error
                        break;
                    }
                    if (cursor_position.x < 0) { // Too far left
                        cursor_position.x = 0;
                    } else if (cursor_position.x > window_rect.right) { // Too far right
                        cursor_position.x = window_rect.right;
                    }
                    if (cursor_position.y < 0) {  // Too far up
                        cursor_position.y = 0;
                    } else if (cursor_position.y > window_rect.bottom) { // Too far down
                        cursor_position.x = window_rect.bottom;
                    }
                    break;
            }

            if (!Mod::mouse_input || (Mod::console_lock_camera && console_is_open())) {
                // Mouse input is disabled; return true to ignore this input
                return true;
            }

            break; // case RIM_TYPEMOUSE

        case RIM_TYPEKEYBOARD:

            // Handle keyboard input

            // If in-game text input field has focus, ignore keyboard inputs
            if (Menu::Dlg::text_input_active)
            {
                return false; // Allow input to reach the game
            }

            // Determine type of keyboard input
            switch (pData->data.keyboard.Message)
            {
                case WM_KEYDOWN: // A nonsystem key is a key that is pressed when the ALT key is not pressed

                    // Determine which key was pressed
                    switch (pData->data.keyboard.VKey)
                    {
                        // Handle key press

                        default:
                            break;
                    }

                case WM_SYSKEYDOWN: // A system key is a key that is pressed when the ALT key is also down

                    // Determine which syskey was pressed
                    switch (pData->data.keyboard.VKey)
                    {
                        // Handle key press (when ALT is down)

                        default:
                            break;
                    }
                    break; // case  WM_KEYDOWN || WM_SYSKEYDOWN

                case WM_KEYUP:

                    // Determine which key was released
                    switch (pData->data.keyboard.VKey)
                    {
                        // Handle key release (key is no longer being pressed)

                        default:
                            break;
                    }
                    break; // case WM_KEYUP
            } // switch (pData->data.keyboard.Message)
            break; // case RIM_TYPEKEYBOARD

        case RIM_TYPEHID:

            // Handle input from some other type of hardware input device

            break; // case RIM_TYPEHID
    } // switch (pData->header.dwType)

    // False = don't disable game input (allow this input to reach the game)
    return false;
}
#endif
