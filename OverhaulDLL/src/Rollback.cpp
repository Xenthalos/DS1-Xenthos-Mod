#include "Rollback.h"
#include "SP/memory.h"
#include "SP/memory/injection/asm/x64.h"
#include "DarkSoulsOverhaulMod.h"
#include "MainLoop.h"
#include "InputUtil.h"
#include "ModNetworking.h"

PlayerIns* Rollback::saved_playerins = NULL;
BulletMan* Rollback::saved_bulletman = NULL;
FXManager* Rollback::saved_sfxobjs = NULL;
DamageMan* Rollback::saved_damageman = NULL;
PadManipulator** Rollback::saved_PadManipulator = NULL;

GGPOSession* Rollback::ggpo = NULL;
GGPOPlayerHandle Rollback::ggpoHandles[GGPO_MAX_PLAYERS] = {};
bool Rollback::ggpoStarted = false;
GGPOREADY Rollback::ggpoReady = GGPOREADY::NotReady;

GGPOSessionCallbacks Rollback::ggpoCallbacks = {
    .begin_game = rollback_begin_game_callback,
    .save_game_state = rollback_save_game_state_callback,
    .load_game_state = rollback_load_game_state_callback,
    .log_game_state = rollback_log_game_state,
    .free_buffer = rollback_free_buffer,
    .advance_frame = rollback_advance_frame_callback,
    .on_event = rollback_on_event_callback,
};

bool Rollback::gsave = false;
bool Rollback::gload = false;
bool state_test(void* unused)
{
    if (Rollback::gsave)
    {
        auto player_o = Game::get_PlayerIns();
        PlayerIns* player = (PlayerIns*)player_o.value();
        copy_PlayerIns(Rollback::saved_playerins, player, false);
        copy_BulletMan(Rollback::saved_bulletman, *(BulletMan**)Game::bullet_man, false);
        //copy_FXManager(Rollback::saved_sfxobjs, (*(SfxMan**)Game::sfx_man)->FrpgFxManagerBase->base.fXManager, false);
        copy_DamageMan(Rollback::saved_damageman, *(DamageMan**)Game::damage_man, false);

        Rollback::gsave = false;
    }

    if (Rollback::gload)
    {
        auto player_o = Game::get_PlayerIns();
        PlayerIns* player = (PlayerIns*)player_o.value();
        copy_PlayerIns(player, Rollback::saved_playerins, true);
        copy_BulletMan(*(BulletMan**)Game::bullet_man, Rollback::saved_bulletman, true);
        //copy_FXManager((*(SfxMan**)Game::sfx_man)->FrpgFxManagerBase->base.fXManager, Rollback::saved_sfxobjs, true);
        copy_DamageMan(*(DamageMan**)Game::damage_man, Rollback::saved_damageman, true);

        Game::Step_GameSimulation();

        Rollback::gload = false;
    }

    return true;
}

bool Rollback::isave = false;
bool Rollback::iload = false;
static uint32_t inputSaveFrameI = 0;
static const size_t INPUT_ROLLBACK_LENGTH = 5 * 60; //5 seconds
bool input_test(void* unused)
{
    return true;
}

bool Rollback::networkToggle = false;
bool Rollback::networkTest = false;
bool network_toggle(void* unused)
{
    if (Rollback::networkToggle)
    {
        Rollback::networkTest = !Rollback::networkTest;
        ConsoleWrite("Netcode %d", Rollback::networkTest);
        Rollback::networkToggle = false;
    }
    return true;
}

bool Rollback::rollbackToggle = false;
bool Rollback::rollbackEnabled = false;
bool ggpo_toggle(void* unused)
{
    if (Rollback::rollbackToggle)
    {
        Rollback::rollbackEnabled = !Rollback::rollbackEnabled;
        ConsoleWrite("rollback %d", Rollback::rollbackEnabled);
        Rollback::rollbackToggle = false;
    }
    return true;
}

void PadDeviceSetKey(uint32_t* inputkeys, uint32_t keyI, bool state)
{
    if (state)
    {
        inputkeys[keyI >> 0x5] |= (1 << (keyI & 0x1f));
    }
    else
    {
        inputkeys[keyI >> 0x5] &= ~(1 << (keyI & 0x1f));
    }
}

//need to use this so PackRollbackInput can get the correct values post keybind processing, but will return failed when the game calls it
static bool OverrideVirtualMultiDeviceInjection = false;

void PackRollbackInput(RollbackInput* out, PlayerIns* player)
{
    void* padDevice = PadMan_GetPadDevice(0);

    OverrideVirtualMultiDeviceInjection = true;
    for (uint32_t i = 0; i < 256; i++)
    {
        PadDeviceSetKey(out->keys, i, PadDevice_GetInputI(padDevice, i));
    }
    for (uint32_t i = 0; i < 40; i++)
    {
        out->sticks[i] = PadDevice_GetStickInputI(padDevice, i);
    }
    OverrideVirtualMultiDeviceInjection = false;

    out->IsLockedOn = !(*(bool*)(((uint64_t)(&player->chrins.playerCtrl->chrCtrl)) + 0x21D));
    //out->curGesture
    out->curSelectedMagicId = get_currently_selected_magic_id(player);
    out->curUsingItemId = (player->chrins).curUsedItem.itemId;
    for (size_t i = 0; i < InventorySlots::END; i++)
    {
        out->equipment_array[i] = Game::get_equipped_inventory((uint64_t)player, (InventorySlots)i);
    }
}

void UnpackRollbackInput(RollbackInput* in, PlayerIns* player, uint32_t player_i)
{
    PadDevice* padDevice = (PadDevice*)PadMan_GetPadDevice(player_i);

    uint32_t* keys = padDevice->VirtMultiDevice->base.base.VirtInputData.keys.inputKeys;
    for (uint32_t i = 0; i < 256; i++)
    {
        bool val = (in->keys[i >> 0x5] >> (i & 0x1f)) & 1;
        PadDeviceSetKey(keys, i, val);
    }

    float* sticks = padDevice->VirtMultiDevice->base.base.VirtInputData.VirAnalogKeyInfo.analogSticksAndPad;
    for (uint32_t i = 0; i < 40; i++)
    {
        sticks[i] = in->sticks[i];
    }

    //only have to do the rest if this is a remote player, if this is the pc the game takes care of it
    if (player_i > 0)
    {
        //in->curGesture

        (player->chrins).curSelectedMagicId = in->curSelectedMagicId;
        PlayerIns_Update_curSelectedMagicId(player, in->curSelectedMagicId);
        //don't bother to emulate the spell changing, just force it manually
        player->playergamedata->equipGameData.equipMagicData->equippedMagicList[0].count = 999;
        player->playergamedata->equipGameData.equipMagicData->equippedMagicList[0].magic_id = in->curSelectedMagicId;

        if (in->curUsingItemId != -1)
        {
            (player->chrins).curUsedItem.itemId = in->curUsingItemId;
            (player->curUsedItem).itemId = in->curUsingItemId;
            (player->curUsedItem).amountUsed = 1;
            (player->chrins).curUsedItem.amountUsed = 1;
        }

        uint64_t itemList = *(uint64_t*)(((uint64_t)(&player->playergamedata->equipGameData.equippedInventory)) + 0x30);
        for (uint32_t i = 0; i < InventorySlots::END; i++)
        {
            //insert into EquipGameData
            ChrAsm_Set_Equipped_Items_FromNetwork(&player->playergamedata->equipGameData, i, in->equipment_array[i], -1, false);
            //inset into EquipInventoryData, and set the equippedItemIndexes
            player->playergamedata->equipGameData.equippedItemIndexes[i] = i;
            //category
            if (i >= InventorySlots::LeftHand1 && i <= InventorySlots::RightHand2)
            {
                *(uint32_t*)(itemList + 0x1C * i + 0) = 0;
            }
            else if (i >= InventorySlots::ArmorHead && i <= InventorySlots::ArmorLegs)
            {
                *(uint32_t*)(itemList + 0x1C * i + 0) = 0x10000000;
            }
            *(uint32_t*)(itemList + 0x1C * i + 4) = in->equipment_array[i];
            *(uint32_t*)(itemList + 0x1C * i + 8) = 1;
        }

        //forcably set the PlayerCtrl->chrctrl_parent.NotLockedOn flag if the player is locked on. Dark souls can't handle this for anyone but the host
        uint8_t* NotLockedOn = (uint8_t*)(((uint64_t)(&player->chrins.playerCtrl->chrCtrl)) + 0x21D);
        if (in->IsLockedOn)
        {
            *NotLockedOn = 0;
        }
        else
        {
            *NotLockedOn = 1;
        }
    }
}

void rollback_sync_inputs()
{
    RollbackInput inputs[GGPO_MAX_PLAYERS];
    int disconnect_flags; //TODO

    //get the inputs for this frame
    GGPOErrorCode res = ggpo_synchronize_input(Rollback::ggpo, inputs, sizeof(RollbackInput) * GGPO_MAX_PLAYERS, &disconnect_flags);
    if (!GGPO_SUCCEEDED(res))
    {
        FATALERROR("ggpo_synchronize_input call returned %d", res);
    }

    //load the input states into the game to be used this frame
    for (uint32_t i = 0; i < Rollback::ggpoCurrentPlayerCount; i++)
    {
        auto player_o = Game::get_connected_player(i);
        if (!player_o.has_value() || player_o.value() == NULL)
        {
            FATALERROR("Unable to get playerins in rollback_load_game_state_callback");
        }
        PlayerIns* player = (PlayerIns*)player_o.value();
        UnpackRollbackInput(&inputs[i], player, i);
    }
}

extern "C" {
    uint64_t rollback_game_frame_sync_inputs_return;
    void rollback_game_frame_sync_inputs_injection();
    void rollback_game_frame_sync_inputs_helper();
}

void rollback_game_frame_sync_inputs_helper()
{
    if (Rollback::rollbackEnabled && Rollback::ggpoStarted)
    {
        ggpo_idle(Rollback::ggpo, 0); //timeout isn't actually used by this function

        if (Rollback::ggpoReady == GGPOREADY::ReadyAwaitingFrameHead)
        {
            Rollback::ggpoReady = GGPOREADY::Ready;
        }

        if (Rollback::ggpoReady == GGPOREADY::Ready)
        {
            auto player_o = Game::get_PlayerIns();
            if (!player_o.has_value() || player_o.value() == NULL)
            {
                FATALERROR("Unable to get playerins for PC in rollback_game_frame_sync_inputs_helper");
            }
            PlayerIns* player = (PlayerIns*)player_o.value();

            //By this point, the game will have read the controller inputs and saved them. Read it now
            RollbackInput localInput{};
            PackRollbackInput(&localInput, player);

            //notify ggpo of the local player's inputs
            GGPOErrorCode result = ggpo_add_local_input(Rollback::ggpo, Rollback::ggpoHandles[0], &localInput, sizeof(RollbackInput));
            if (!GGPO_SUCCEEDED(result))
            {
                FATALERROR("Unable to ggpo_add_local_input. %d", result);
            }

            rollback_sync_inputs();
        }
    }

    if (Rollback::networkTest)
    {
        auto player_o = Game::get_PlayerIns();
        if (player_o.has_value() && player_o.value() != NULL)
        {
            PlayerIns* player = (PlayerIns*)player_o.value();

            Step_PadManipulator(player->chrins.padManipulator, FRAMETIME, player->chrins.playerCtrl);

            auto guest_o = Game::get_connected_player(1);
            if (guest_o.has_value() && guest_o.value() != NULL)
            {
                PlayerIns* guest = (PlayerIns*)guest_o.value();
                //send out our input
                RollbackInput localInput{};
                PackRollbackInput(&localInput, player);
                SteamNetworkingIdentity target;
                target.SetSteamID(guest->steamPlayerData->steamOnlineIDData->steam_id);
                ModNetworking::SteamNetMessages->SendMessageToUser(target, &localInput, sizeof(localInput), k_nSteamNetworkingSend_UnreliableNoNagle, 1);

                //read in and set the other player input
                SteamNetworkingMessage_t* new_message;
                int num_messages = ModNetworking::SteamNetMessages->ReceiveMessagesOnChannel(1, &new_message, 1);
                if (num_messages == 1)
                {
                    RollbackInput* remoteInput = (RollbackInput*)new_message->GetData();
                    UnpackRollbackInput(remoteInput, guest, 1);
                    new_message->Release();
                }
            }
        }
    }
}

extern "C" {
    uint64_t dsr_frame_finished_return;
    void dsr_frame_finished_injection();
    void dsr_frame_finished_helper();
}

void dsr_frame_finished_helper()
{
    if (Rollback::rollbackEnabled && Rollback::ggpoStarted)
    {
        //only start telling ggpo we're running once the players are synced
        if (Rollback::ggpoReady == GGPOREADY::Ready)
        {
            ggpo_advance_frame(Rollback::ggpo);

            if (Rollback::rollbackVisual)
            {
                float* visability = (float*)((uint64_t)Game::get_PlayerIns().value() + 0x328);
                if (*visability < 1.0f)
                {
                    *visability += 0.2f;
                }
            }
        }
    }
}

extern "C" {
    uint64_t MoveMapStep_SetPlayerLockOn_FromController_offset_return;
    void MoveMapStep_SetPlayerLockOn_FromController_offset_injection();
    bool* ggpoStarted_ptr;

    uint64_t VirtualMultiDevice_GetInputI_return;
    void VirtualMultiDevice_GetInputI_injection();
    bool VirtualMultiDevice_GetInputI_helper(uint32_t inputI);
}

//return false to abort the function, true to continue
bool VirtualMultiDevice_GetInputI_helper(uint32_t inputI)
{
    if (Rollback::rollbackEnabled && Rollback::ggpoStarted && !OverrideVirtualMultiDeviceInjection)
    {
        switch (inputI)
        {
        case PadButtons::Y:
        case PadButtons::B:
        case PadButtons::A:
        case PadButtons::X:
        case PadButtons::R1:
        case PadButtons::R2:
        case PadButtons::R3:
        case PadButtons::L1:
        case PadButtons::L2:
        case PadButtons::DpadUp:
        case PadButtons::DpadDown:
        case PadButtons::DpadLeft:
        case PadButtons::DpadRight:
            return false;
        default:
            return false;
        }
    }
    else
    {
        return true;
    }
}

extern "C" {
    uint64_t VirtualMultiDevice_GetStickInputI_return;
    void VirtualMultiDevice_GetStickInputI_injection();
    bool VirtualMultiDevice_GetStickInputI_helper();
}

//return false to abort the function, true to continue
bool VirtualMultiDevice_GetStickInputI_helper()
{
    if (Rollback::rollbackEnabled && Rollback::ggpoStarted && !OverrideVirtualMultiDeviceInjection)
    {
        return false;
    }
    else
    {
        return true;
    }
}

extern "C" {
    uint64_t PadMan_GetPadDevice_return;
    void PadMan_GetPadDevice_injection();
    uint32_t PadMan_GetPadDevice_helper(PlayerCtrl*);
}

uint32_t PadMan_GetPadDevice_helper(PlayerCtrl* playerctrl)
{
    uint64_t playerins = (uint64_t)playerctrl->chrCtrl.playerins_parent;
    uint32_t playerhandle = *(uint32_t*)(playerins + 8);

    if (playerhandle >= Game::PC_Handle && playerhandle < Game::PC_Handle + 4)
    {
        return playerhandle - Game::PC_Handle;
    }
    return 0;
}

void Rollback::start()
{
    ConsoleWrite("Rollback...");
    uint8_t* write_address;

    SetEnvironmentVariable("ggpo.log", "1");

    Rollback::NetcodeFix();

    //Synchronize input at the start of each frame
    //do this anytime after the game reads the inputs, but before MoveMapStep_Step_13
    write_address = (uint8_t*)(Rollback::rollback_game_frame_sync_inputs_offset + Game::ds1_base);
    sp::mem::code::x64::inject_jmp_14b(write_address, &rollback_game_frame_sync_inputs_return, 4, &rollback_game_frame_sync_inputs_injection);

    //Inform ggpo after a frame has been rendered
    write_address = (uint8_t*)(Rollback::MainUpdate_end_offset + Game::ds1_base);
    sp::mem::code::x64::inject_jmp_14b(write_address, &dsr_frame_finished_return, 0, &dsr_frame_finished_injection);

    //prevent the game from directly reading the controller and setting our lockon. We need to only use the RollbackInput
    write_address = (uint8_t*)(Rollback::MoveMapStep_SetPlayerLockOn_FromController_offset + Game::ds1_base);
    ggpoStarted_ptr = &Rollback::ggpoStarted;
    sp::mem::code::x64::inject_jmp_14b(write_address, &MoveMapStep_SetPlayerLockOn_FromController_offset_return, 2, &MoveMapStep_SetPlayerLockOn_FromController_offset_injection);

    //disable the VirtualMultiDevice_GetInputI method in most cases. It doesn't work for PadDevices > 0, so just force the fallback on inputs we care about
    write_address = (uint8_t*)(Game::ds1_base + Rollback::VirtualMultiDevice_GetInputI_offset);
    sp::mem::code::x64::inject_jmp_14b(write_address, &VirtualMultiDevice_GetInputI_return, 0, &VirtualMultiDevice_GetInputI_injection);

    //disable the VirtualMultiDevice_GetStickInputI method. Same as above, but it has no inputs we care about
    write_address = (uint8_t*)(Game::ds1_base + Rollback::VirtualMultiDevice_GetStickInputI_offset);
    sp::mem::code::x64::inject_jmp_14b(write_address, &VirtualMultiDevice_GetStickInputI_return, 0, &VirtualMultiDevice_GetStickInputI_injection);

    //patch the PadMan_GetPadDevice calls so we get the correct PadDevice for the player being checked
    write_address = (uint8_t*)0x14039690b;
    sp::mem::code::x64::inject_jmp_14b(write_address, &PadMan_GetPadDevice_return, 1, &PadMan_GetPadDevice_injection);

    //Testing rollback related stuff
    Rollback::saved_playerins = init_PlayerIns();
    Rollback::saved_bulletman = init_BulletMan();
    Rollback::saved_sfxobjs = init_FXManager();
    Rollback::saved_damageman = init_DamageMan();
    Rollback::saved_PadManipulator = (PadManipulator**)malloc_(sizeof(PadManipulator*) * INPUT_ROLLBACK_LENGTH);
    for (size_t i = 0; i < INPUT_ROLLBACK_LENGTH; i++)
    {
        Rollback::saved_PadManipulator[i] = (PadManipulator*)malloc(sizeof(PadManipulator));
    }
    //Testing save/restore with a hotkey
    MainLoop::setup_mainloop_callback(state_test, NULL, "state_test");
    MainLoop::setup_mainloop_callback(input_test, NULL, "input_test");
    MainLoop::setup_mainloop_callback(ggpo_toggle, NULL, "ggpo_toggle");
    MainLoop::setup_mainloop_callback(network_toggle, NULL, "network_toggle");
}

bool rollback_begin_game_callback(const char*)
{
    return true;
}

/*
* Notification from GGPO we should step foward exactly 1 frame
* during a rollback.
*/
bool rollback_advance_frame_callback(int)
{
    rollback_sync_inputs();

    //step next frame
    Game::Step_GameSimulation(true);
    ggpo_advance_frame(Rollback::ggpo);

    //ConsoleWrite("rollback_advance_frame_callback finished");
    return true;
}

/*
 * Makes our current state match the state passed in by GGPO.
 */
bool rollback_load_game_state_callback(unsigned char* buffer, int)
{
    RollbackState* state = (RollbackState*)buffer;

    for (uint32_t i = 0; i < Rollback::ggpoCurrentPlayerCount; i++)
    {
        auto player_o = Game::get_connected_player(i);
        if (!player_o.has_value() || player_o.value() == NULL)
        {
            FATALERROR("Unable to get playerins %d in rollback_load_game_state_callback", i);
        }
        PlayerIns* player = (PlayerIns*)player_o.value();

        copy_PlayerIns(player, state->playerins[i], true);
    }

    copy_BulletMan(*(BulletMan**)Game::bullet_man, state->bulletman, true);
    //TODO copy_FXManager
    copy_DamageMan(*(DamageMan**)Game::damage_man, state->damageman, true);

    if (Rollback::rollbackVisual)
    {
        *(float*)((uint64_t)Game::get_PlayerIns().value() + 0x328) = 0.0f;
    }
    //ConsoleWrite("rollback_load_game_state_callback finish");

    return true;
}

/*
 * Save the current state to a buffer and return it to GGPO via the
 * buffer and len parameters.
 */
bool rollback_save_game_state_callback(unsigned char** buffer, int* len, int* checksum, int)
{
    RollbackState* state = (RollbackState*)malloc(sizeof(RollbackState));
    if (state == NULL)
    {
        FATALERROR("Unable to get allocate state for rollback_save_game_state_callback");
    }

    for (uint32_t i = 0; i < Rollback::ggpoCurrentPlayerCount; i++)
    {
        auto player_o = Game::get_connected_player(i);
        if (!player_o.has_value() || player_o.value() == NULL)
        {
            FATALERROR("Unable to get playerins %d in rollback_save_game_state_callback", i);
        }
        PlayerIns* player = (PlayerIns*)player_o.value();

        state->playerins[i] = init_PlayerIns();
        copy_PlayerIns(state->playerins[i], player, false);
    }

    state->bulletman = init_BulletMan();
    copy_BulletMan(state->bulletman, *(BulletMan**)Game::bullet_man, false);
    //TODO copy_FXManager;
    state->damageman = init_DamageMan();
    copy_DamageMan(state->damageman, *(DamageMan**)Game::damage_man, false);

    *buffer = (unsigned char*)state;
    *len = sizeof(RollbackState);
    *checksum = 0;

    return true;
}

void rollback_free_buffer(void* buffer)
{
    RollbackState* state = (RollbackState*)buffer;

    for (size_t i = 0; i < Rollback::ggpoCurrentPlayerCount; i++)
    {
        free_PlayerIns(state->playerins[i]);
        state->playerins[i] = NULL;
    }
    //TODO free_FXManager
    free_BulletMan(state->bulletman);
    state->bulletman = NULL;
    free_DamageMan(state->damageman);
    state->damageman = NULL;

    free(state);
}

bool rollback_on_event_callback(GGPOEvent* info)
{
    switch (info->code)
    {
    case GGPO_EVENTCODE_CONNECTED_TO_PEER:
        ConsoleWrite("GGPO_EVENTCODE_CONNECTED_TO_PEER");
        break;
    case GGPO_EVENTCODE_SYNCHRONIZING_WITH_PEER:
        ConsoleWrite("GGPO_EVENTCODE_SYNCHRONIZING_WITH_PEER");
        break;
    case GGPO_EVENTCODE_SYNCHRONIZED_WITH_PEER:
        ConsoleWrite("GGPO_EVENTCODE_SYNCHRONIZED_WITH_PEER");
        break;
    case GGPO_EVENTCODE_RUNNING:
        ConsoleWrite("GGPO_EVENTCODE_RUNNING");
        Rollback::ggpoReady = GGPOREADY::ReadyAwaitingFrameHead;
        break;
    case GGPO_EVENTCODE_CONNECTION_INTERRUPTED:
        ConsoleWrite("GGPO_EVENTCODE_CONNECTION_INTERRUPTED");
        break;
    case GGPO_EVENTCODE_CONNECTION_RESUMED:
        ConsoleWrite("GGPO_EVENTCODE_CONNECTION_RESUMED");
        break;
    case GGPO_EVENTCODE_DISCONNECTED_FROM_PEER:
        ConsoleWrite("GGPO_EVENTCODE_DISCONNECTED_FROM_PEER");
        break;
    case GGPO_EVENTCODE_TIMESYNC:
        ConsoleWrite("GGPO_EVENTCODE_TIMESYNC");
        Sleep(1000 * info->u.timesync.frames_ahead / 60);
        break;
    }
    return true;
}

bool rollback_log_game_state(char* filename, unsigned char* buffer, int)
{
    return true;
}

void Rollback::rollback_end_session()
{
    if (Rollback::ggpoStarted)
    {
        Rollback::ggpoStarted = false;
        Rollback::ggpoReady = GGPOREADY::NotReady;
        GGPOErrorCode result = ggpo_close_session(Rollback::ggpo);
        Rollback::ggpo = NULL;
        if (!GGPO_SUCCEEDED(result))
        {
            FATALERROR("unable to close ggpo. %d", result);
        }
    }
}

bool rollback_await_init(void* steamMsgs)
{
    //Wait for all the players to be loaded in before we start ggpo
    if (!Game::playerchar_is_loaded())
    {
        return true;
    }

    for (uint32_t i = 0; i < Rollback::ggpoCurrentPlayerCount; i++)
    {
        auto player_o = Game::get_connected_player(i);
        if (!player_o.has_value() || player_o.value() == NULL)
        {
            return true;
        }
        PlayerIns* player = (PlayerIns*)player_o.value();
        //kinda hacky checks to make sure the chrs are fully loaded
        if (player->chrins.maxHp <= 1 || player->chrins.specialEffects == NULL || player->chrins.specialEffects->specialEffect_Info == NULL)
        {
            return true;
        }
    }

    //Start ggpo
    GGPOErrorCode result = ggpo_start_session(&Rollback::ggpo, &Rollback::ggpoCallbacks, (ISteamNetworkingMessages*)steamMsgs, "DSR_GGPO", Rollback::ggpoCurrentPlayerCount, sizeof(RollbackInput));
    if (!GGPO_SUCCEEDED(result))
    {
        FATALERROR("unable to start ggpo. %d", result);
    }

    for (uint32_t i = 0; i < Rollback::ggpoCurrentPlayerCount; i++)
    {
        GGPOPlayer ggpoplayer = {};
        ggpoplayer.size = sizeof(GGPOPlayer);
        if (i > 0)
        {
            auto player_o = Game::get_connected_player(i);
            if (!player_o.has_value() || player_o.value() == NULL)
            {
                FATALERROR("Unable to get playerins for guest in rollback_start_session");
            }
            PlayerIns* player = (PlayerIns*)player_o.value();
            uint64_t steamid = player->steamPlayerData->steamOnlineIDData->steam_id;

            ConsoleWrite("GGPO connecting to guest %llx", steamid);

            ggpoplayer.type = GGPO_PLAYERTYPE_REMOTE;
            ggpoplayer.u.remote.steamid.SetSteamID(steamid);
        }
        else
        {
            ggpoplayer.type = GGPO_PLAYERTYPE_LOCAL;
        }
        ggpoplayer.player_num = i + 1;
        result = ggpo_add_player(Rollback::ggpo, &ggpoplayer, &Rollback::ggpoHandles[i]);
        if (!GGPO_SUCCEEDED(result))
        {
            FATALERROR("unable to ggpo_add_player. %d", result);
        }
    }

    //ggpo_set_frame_delay(ggpo, Rollback::ggpoHandles[0], 1);

    ConsoleWrite("GGPO started");
    Rollback::ggpoStarted = true;

    return false;
}

void Rollback::rollback_start_session(ISteamNetworkingMessages* steamMsgs)
{
    if (Rollback::rollbackEnabled)
    {
        MainLoop::setup_mainloop_callback(rollback_await_init, steamMsgs, "rollback_await_init");
    }
}
