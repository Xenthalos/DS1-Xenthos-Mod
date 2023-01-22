#include "GameData.h"
#include "SfxManStructFunctions.h"
#include "Rollback.h"

void copy_FXManager(FXManager* to, FXManager* from, bool to_game)
{
    copy_SFXEntryList(to->SFXEntryList, from->SFXEntryList, to_game, to, from);
}

FXManager* init_FXManager()
{
    FXManager* local_FXManager = (FXManager*)malloc_(sizeof(FXManager));

    local_FXManager->SFXEntryList = init_SFXEntryList();

    return local_FXManager;
}

void free_FXManager(FXManager* to)
{
    free_SFXEntryList(to->SFXEntryList);
    free(to);
}

static const size_t max_preallocated_SFXEntries = 256;

uint64_t* HeapPtr = (uint64_t*)(0x0141B67450 + 8);

void copy_SFXEntryList(SFXEntry* to, SFXEntry* from, bool to_game, FXManager* to_parent, FXManager* from_parent)
{
    if (!to_game)
    {
        size_t to_index = 0;
        while (from)
        {
            if (to_index >= max_preallocated_SFXEntries)
            {
                ConsoleWrite("Unable to recursivly copy SFXEntry from the game. Out of space.");
                break;
            }
            copy_SFXEntry(to, from, to_game);

            //if we reach the tail here, we're at the last valid entry. The next ptr should be null anyway, but just to be safe
            if (from == from_parent->SFXEntryList_tail)
            {
                to->next = NULL;
                break;
            }

            if (from->next != NULL)
            {
                to->next = (SFXEntry*)((uint64_t)(to)+sizeof(SFXEntry));
            }
            else
            {
                to->next = NULL;
            }

            from = from->next;
            to = (SFXEntry*)((uint64_t)(to)+sizeof(SFXEntry));
            to_index += 1;
        }
    }
    else
    {
        //pre-process the list. If the target has too few or too many entries, need to correct that
        SFXEntry* from_pre = from;
        size_t from_len = 0;
        while (from_pre)
        {
            from_len++;
            from_pre = from_pre->next;
        }

        SFXEntry* to_pre = to;
        size_t to_len = 0;
        while (to_pre)
        {
            to_len++;
            to_pre = to_pre->next;
        }

        //handle if the game's list is too long, and we need to free it's extra slots
        if (from_len < to_len)
        {
            ConsoleWrite("Game SFXEntryList too long. Free");
            for (from_len; from_len < to_len; from_len++)
            {
                SFXEntry* entry_to_free = to_parent->SFXEntryList;
                to_parent->SFXEntryList = to_parent->SFXEntryList->next;
                smallObject_internal_dealloc(*HeapPtr, entry_to_free, sizeof(SFXEntry), 0x10);
            }
        }
        //handle if the game's list isn't long enough, and we need to alloc more slots
        else if (from_len > to_len)
        {
            ConsoleWrite("Game SFXEntryList too short. Alloc");
            for (to_len; to_len < from_len; to_len++)
            {
                SFXEntry* newEntry = (SFXEntry*)smallObject_internal_malloc(*HeapPtr, sizeof(SFXEntry), 0x10);
                newEntry->field0x48_head = NULL;
                newEntry->field0x48_tail = NULL;
                newEntry->field0xe0 = NULL;
                newEntry->field0xf0 = NULL;
                newEntry->next = to_parent->SFXEntryList;

                to_parent->SFXEntryList = newEntry;
            }
        }

        to = to_parent->SFXEntryList;
        while (from)
        {
            copy_SFXEntry(to, from, to_game);
            to->parent = to_parent;

            // last valid entry failsafe
            if (to == to_parent->SFXEntryList_tail && !(to->next == NULL && from->next == NULL))
            {
                ConsoleWrite("Warning: early abort at last valid entry. Some SFX lost");
                to->next = NULL;
                break;
            }

            from = from->next;
            to = to->next;
        }
    }
}

void copy_SFXEntry(SFXEntry* to, SFXEntry* from, bool to_game)
{
    to->vtable = 0x14151c278;
    to->field0x8 = NULL;
    memcpy(to->data_0, from->data_0, sizeof(to->data_0));
    to->parent = NULL;
    to->unk1 = NULL;
    to->unk2 = NULL; //TODO
    if (from->field0x48_head != NULL)
    {
        if (to->field0x48_head == NULL)
        {
            if (to_game)
            {
                to->field0x48_head = (FXEntry_Substruct*)smallObject_internal_malloc(*HeapPtr, sizeof(FXEntry_Substruct), 8);
                to->field0x48_head->parent = to;
            }
            else
            {
                to->field0x48_head = init_FXEntry_Substruct();
            }
        }
        copy_FXEntry_Substruct(to->field0x48_head, from->field0x48_head, to_game);
        FXEntry_Substruct* tail = to->field0x48_head;
        while (tail->next != NULL)
        {
            tail = tail->next;
        }
        to->field0x48_tail = tail;
    }
    else if (from->field0x48_head == NULL && to->field0x48_head != NULL)
    {
        if (to_game)
        {
            smallObject_internal_dealloc(*HeapPtr, to->field0x48_head, sizeof(FXEntry_Substruct), 8);
        }
        else
        {
            free_FXEntry_Substruct(to->field0x48_head);
        }
        to->field0x48_head = NULL;
        to->field0x48_tail = NULL;
    }

    to->unk4 = NULL;
    memcpy(to->data_1, from->data_1, sizeof(to->data_1));
    to->field0xe0 = NULL; //TODO
    to->data_2 = from->data_2;
    to->field0xf0 = NULL; //TODO
    to->data_3 = from->data_3;
}

SFXEntry* init_SFXEntryList()
{
    //this is a linked list, so pre-allocate a max of 256 for the classes
    SFXEntry* local_SFXEntry = (SFXEntry*)malloc_(sizeof(SFXEntry)*max_preallocated_SFXEntries);

    //field0x48 must be dynamically alloc'd, since it can be null

    return local_SFXEntry;
}

void free_SFXEntryList(SFXEntry* to)
{
    SFXEntry* head = to;
    for (size_t i = 0; i < max_preallocated_SFXEntries; i++)
    {
        free_FXEntry_Substruct(to->field0x48_head);
        head = (SFXEntry*)((uint64_t)(head)+sizeof(SFXEntry));
    }
    free(to);
}

void copy_FXEntry_Substruct(FXEntry_Substruct* to, FXEntry_Substruct* from, bool to_game)
{
    memcpy(to->data_0, from->data_0, sizeof(to->data_0));
    to->self_substruct2 = (uint64_t)to + 0xe0;
    memcpy(to->data_1, from->data_1, sizeof(to->data_1));
    to->unk1 = 0; //TODO
    memcpy(to->data_2, from->data_2, sizeof(to->data_2));
    to->unk2 = NULL;
    to->next = NULL;
    to->unk4 = 0; //TODO
    to->unk5 = 0; //TODO
    memset(to->padding_0, 0, sizeof(to->padding_0));
    //leave parent ptr alone
    to->unk6 = NULL;
    to->vtable = 0x141519DA0;
    to->unk7 = 0; //TODO
    to->unk8 = NULL;
    to->unk9 = NULL;
    to->unk10 = NULL;
    memcpy(to->data_3, from->data_3, sizeof(to->data_3));
}

FXEntry_Substruct* init_FXEntry_Substruct()
{
    FXEntry_Substruct* local_class_14150b808_field0x48 = (FXEntry_Substruct*)malloc_(sizeof(FXEntry_Substruct));

    return local_class_14150b808_field0x48;
}

void free_FXEntry_Substruct(FXEntry_Substruct* to)
{
    free(to);
}
