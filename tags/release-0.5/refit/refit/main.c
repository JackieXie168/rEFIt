/*
 * refit/main.c
 * Main code for the boot menu
 *
 * Copyright (c) 2006 Christoph Pfisterer
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the
 *    distribution.
 *
 *  * Neither the name of Christoph Pfisterer nor the names of the
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "lib.h"

// types

typedef struct {
    REFIT_MENU_ENTRY me;
    CHAR16           *LoaderPath;
    CHAR16           *VolName;
    EFI_DEVICE_PATH  *DevicePath;
    BOOLEAN          UseGraphicsMode;
    CHAR16           *LoadOptions;
} LOADER_ENTRY;

typedef struct {
    REFIT_MENU_ENTRY me;
    REFIT_VOLUME     *Volume;
} LEGACY_ENTRY;

// variables

#define MACOSX_LOADER_PATH L"\\System\\Library\\CoreServices\\boot.efi"

#define TAG_EXIT   (1)
#define TAG_RESET  (2)
#define TAG_ABOUT  (3)
#define TAG_LOADER (4)
#define TAG_TOOL   (5)
#define TAG_LEGACY (6)

static REFIT_MENU_ENTRY entry_reset   = { L"Restart Computer", TAG_RESET, 1, NULL, NULL, NULL };
static REFIT_MENU_ENTRY entry_about   = { L"About rEFIt", TAG_ABOUT, 1, NULL, NULL, NULL };
static REFIT_MENU_SCREEN main_menu    = { L"Main Menu", NULL, 0, NULL, 0, NULL, 20, L"Automatic boot" };

static REFIT_MENU_SCREEN about_menu   = { L"About", NULL, 0, NULL, 0, NULL, 0, NULL };

static REFIT_MENU_ENTRY submenu_exit_entry = { L"Return to Main Menu", TAG_RETURN, 0, NULL, NULL, NULL };



static void about_refit(void)
{
    if (about_menu.EntryCount == 0) {
        about_menu.TitleImage = BuiltinIcon(4);
        AddMenuInfoLine(&about_menu, L"rEFIt Version 0.5");
        AddMenuInfoLine(&about_menu, L"");
        AddMenuInfoLine(&about_menu, L"Copyright (c) 2006 Christoph Pfisterer");
        AddMenuInfoLine(&about_menu, L"Portions Copyright (c) Intel Corporation and others");
        AddMenuEntry(&about_menu, &submenu_exit_entry);
    }
    
    RunMenu(&about_menu, NULL);
}


static void start_loader(IN LOADER_ENTRY *Entry)
{
    EFI_STATUS              Status;
    EFI_HANDLE              ChildImageHandle;
    EFI_LOADED_IMAGE        *ChildLoadedImage;
    CHAR16                  ErrorInfo[256];
    CHAR16                  *FullLoadOptions = NULL;
    
    BeginExternalScreen(Entry->UseGraphicsMode ? 1 : 0, L"Booting OS");
    Print(L"Starting %s\n", Basename(Entry->LoaderPath));
    
    // load the image into memory
    Status = BS->LoadImage(FALSE, SelfImageHandle, Entry->DevicePath, NULL, 0, &ChildImageHandle);
    SPrint(ErrorInfo, 255, L"while loading %s on %s", Entry->LoaderPath, Entry->VolName);
    if (CheckError(Status, ErrorInfo))
        goto bailout;
    
    // set load options
    if (Entry->LoadOptions != NULL) {
        Status = BS->HandleProtocol(ChildImageHandle, &LoadedImageProtocol, (VOID **) &ChildLoadedImage);
        if (CheckError(Status, L"while getting a LoadedImageProtocol handle"))
            goto bailout_unload;
        
        FullLoadOptions = PoolPrint(L"%s %s ", Basename(Entry->LoaderPath), Entry->LoadOptions);
        // NOTE: That last space is also added by the EFI shell and seems to be significant
        //  when passing options to Apple's boot.efi... We also include the terminating null
        //  in the length for safety.
        ChildLoadedImage->LoadOptions = (VOID *)FullLoadOptions;
        ChildLoadedImage->LoadOptionsSize = (StrLen(FullLoadOptions) + 1) * sizeof(CHAR16);
        Print(L"Using load options '%s'\n", FullLoadOptions);
    }
    
    // turn control over to the image
    // TODO: re-enable the EFI watchdog timer!
    Status = BS->StartImage(ChildImageHandle, NULL, NULL);
    // control returns here when the child image calls Exit()
    CheckError(Status, L"returned from loader");
    
bailout_unload:
    // unload the image, we don't care if it works or not...
    Status = BS->UnloadImage(ChildImageHandle);
bailout:
    if (FullLoadOptions != NULL)
        FreePool(FullLoadOptions);
    FinishExternalScreen();
}

static void add_loader_entry(IN CHAR16 *LoaderPath, IN CHAR16 *LoaderTitle, IN REFIT_VOLUME *Volume)
{
    CHAR16          *FileName;
    CHAR16          IconFileName[256];
    UINTN           LoaderKind;
    LOADER_ENTRY    *Entry, *SubEntry;
    REFIT_MENU_SCREEN *SubScreen;
    
    FileName = Basename(LoaderPath);
    
    // prepare the menu entry
    Entry = AllocateZeroPool(sizeof(LOADER_ENTRY));
    Entry->me.Title        = PoolPrint(L"Boot %s from %s", (LoaderTitle != NULL) ? LoaderTitle : LoaderPath + 1, Volume->VolName);
    Entry->me.Tag          = TAG_LOADER;
    Entry->me.Row          = 0;
    Entry->me.BadgeImage   = Volume->VolBadgeImage;
    Entry->LoaderPath      = StrDuplicate(LoaderPath);
    Entry->VolName         = Volume->VolName;
    Entry->DevicePath      = FileDevicePath(Volume->DeviceHandle, Entry->LoaderPath);
    Entry->UseGraphicsMode = FALSE;
    
#ifndef TEXTONLY
    // locate a custom icon for the loader
    StrCpy(IconFileName, LoaderPath);
    ReplaceExtension(IconFileName, L".icns");
    if (FileExists(Volume->RootDir, IconFileName))
        Entry->me.Image = LoadIcns(Volume->RootDir, IconFileName, 128);
#endif  /* !TEXTONLY */
    
    // detect specific loaders
    LoaderKind = 0;
    if (StriCmp(LoaderPath, MACOSX_LOADER_PATH) == 0) {
        if (Entry->me.Image == NULL)
            Entry->me.Image = BuiltinIcon(0);  // os_mac
        Entry->UseGraphicsMode = TRUE;
        LoaderKind = 1;
    } else if (StriCmp(FileName, L"diags.efi") == 0) {
        if (Entry->me.Image == NULL)
            Entry->me.Image = BuiltinIcon(11); // os_hwtest
    } else if (StriCmp(FileName, L"e.efi") == 0 ||
               StriCmp(FileName, L"elilo.efi") == 0) {
        if (Entry->me.Image == NULL)
            Entry->me.Image = BuiltinIcon(1);  // os_linux
        LoaderKind = 2;
    } else if (StriCmp(FileName, L"Bootmgfw.efi") == 0) {
        if (Entry->me.Image == NULL)
            Entry->me.Image = BuiltinIcon(2);  // os_win
    } else if (StriCmp(FileName, L"xom.efi") == 0) {
        if (Entry->me.Image == NULL)
            Entry->me.Image = BuiltinIcon(2);  // os_win
        Entry->UseGraphicsMode = TRUE;
        LoaderKind = 3;
    }
    if (Entry->me.Image == NULL)
        Entry->me.Image = BuiltinIcon(3);  // os_unknown
    
    // create the submenu
    SubScreen = AllocateZeroPool(sizeof(REFIT_MENU_SCREEN));
    SubScreen->Title = PoolPrint(L"Boot Options for %s on %s", (LoaderTitle != NULL) ? LoaderTitle : FileName, Volume->VolName);
    SubScreen->TitleImage = Entry->me.Image;
    
    // default entry
    SubEntry = AllocateZeroPool(sizeof(LOADER_ENTRY));
    SubEntry->me.Title        = (LoaderKind == 1) ? L"Boot Mac OS X" : PoolPrint(L"Run %s", FileName);
    SubEntry->me.Tag          = TAG_LOADER;
    SubEntry->LoaderPath      = Entry->LoaderPath;
    SubEntry->VolName         = Entry->VolName;
    SubEntry->DevicePath      = Entry->DevicePath;
    SubEntry->UseGraphicsMode = Entry->UseGraphicsMode;
    AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
    
    // loader-specific submenu entries
    if (LoaderKind == 1) {          // entries for Mac OS X
        SubEntry = AllocateZeroPool(sizeof(LOADER_ENTRY));
        SubEntry->me.Title        = L"Boot Mac OS X in verbose mode";
        SubEntry->me.Tag          = TAG_LOADER;
        SubEntry->LoaderPath      = Entry->LoaderPath;
        SubEntry->VolName         = Entry->VolName;
        SubEntry->DevicePath      = Entry->DevicePath;
        SubEntry->UseGraphicsMode = FALSE;
        SubEntry->LoadOptions     = L"-v";
        AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
        
        SubEntry = AllocateZeroPool(sizeof(LOADER_ENTRY));
        SubEntry->me.Title        = L"Boot Mac OS X in single user mode";
        SubEntry->me.Tag          = TAG_LOADER;
        SubEntry->LoaderPath      = Entry->LoaderPath;
        SubEntry->VolName         = Entry->VolName;
        SubEntry->DevicePath      = Entry->DevicePath;
        SubEntry->UseGraphicsMode = FALSE;
        SubEntry->LoadOptions     = L"-v -s";
        AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
        
    } else if (LoaderKind == 2) {   // entries for elilo
        SubEntry = AllocateZeroPool(sizeof(LOADER_ENTRY));
        SubEntry->me.Title        = PoolPrint(L"Run %s in interactive mode", FileName);
        SubEntry->me.Tag          = TAG_LOADER;
        SubEntry->LoaderPath      = Entry->LoaderPath;
        SubEntry->VolName         = Entry->VolName;
        SubEntry->DevicePath      = Entry->DevicePath;
        SubEntry->UseGraphicsMode = Entry->UseGraphicsMode;
        SubEntry->LoadOptions     = L"-p";
        AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
        
        SubEntry = AllocateZeroPool(sizeof(LOADER_ENTRY));
        SubEntry->me.Title        = L"Boot Linux for a 17\" iMac or a 15\" MacBook Pro (*)";
        SubEntry->me.Tag          = TAG_LOADER;
        SubEntry->LoaderPath      = Entry->LoaderPath;
        SubEntry->VolName         = Entry->VolName;
        SubEntry->DevicePath      = Entry->DevicePath;
        SubEntry->UseGraphicsMode = TRUE;
        SubEntry->LoadOptions     = L"-d 0 i17";
        AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
        
        SubEntry = AllocateZeroPool(sizeof(LOADER_ENTRY));
        SubEntry->me.Title        = L"Boot Linux for a 20\" iMac (*)";
        SubEntry->me.Tag          = TAG_LOADER;
        SubEntry->LoaderPath      = Entry->LoaderPath;
        SubEntry->VolName         = Entry->VolName;
        SubEntry->DevicePath      = Entry->DevicePath;
        SubEntry->UseGraphicsMode = TRUE;
        SubEntry->LoadOptions     = L"-d 0 i20";
        AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
        
        SubEntry = AllocateZeroPool(sizeof(LOADER_ENTRY));
        SubEntry->me.Title        = L"Boot Linux for a Mac Mini (*)";
        SubEntry->me.Tag          = TAG_LOADER;
        SubEntry->LoaderPath      = Entry->LoaderPath;
        SubEntry->VolName         = Entry->VolName;
        SubEntry->DevicePath      = Entry->DevicePath;
        SubEntry->UseGraphicsMode = TRUE;
        SubEntry->LoadOptions     = L"-d 0 mini";
        AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
        
        AddMenuInfoLine(SubScreen, L"NOTE: This is an example. Entries");
        AddMenuInfoLine(SubScreen, L"marked with (*) may not work.");
        
    } else if (LoaderKind == 3) {   // entries for xom.efi
        // by default, skip the built-in selection and boot from hard disk only
        Entry->LoadOptions = L"-s -h";
        
        SubEntry = AllocateZeroPool(sizeof(LOADER_ENTRY));
        SubEntry->me.Title        = L"Boot Windows from Hard Disk";
        SubEntry->me.Tag          = TAG_LOADER;
        SubEntry->LoaderPath      = Entry->LoaderPath;
        SubEntry->VolName         = Entry->VolName;
        SubEntry->DevicePath      = Entry->DevicePath;
        SubEntry->UseGraphicsMode = Entry->UseGraphicsMode;
        SubEntry->LoadOptions     = L"-s -h";
        AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
        
        SubEntry = AllocateZeroPool(sizeof(LOADER_ENTRY));
        SubEntry->me.Title        = L"Boot Windows from CD-ROM";
        SubEntry->me.Tag          = TAG_LOADER;
        SubEntry->LoaderPath      = Entry->LoaderPath;
        SubEntry->VolName         = Entry->VolName;
        SubEntry->DevicePath      = Entry->DevicePath;
        SubEntry->UseGraphicsMode = Entry->UseGraphicsMode;
        SubEntry->LoadOptions     = L"-s -c";
        AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
        
        SubEntry = AllocateZeroPool(sizeof(LOADER_ENTRY));
        SubEntry->me.Title        = PoolPrint(L"Run %s in text mode", FileName);
        SubEntry->me.Tag          = TAG_LOADER;
        SubEntry->LoaderPath      = Entry->LoaderPath;
        SubEntry->VolName         = Entry->VolName;
        SubEntry->DevicePath      = Entry->DevicePath;
        SubEntry->UseGraphicsMode = FALSE;
        SubEntry->LoadOptions     = L"-v";
        AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
        
    }
    
    AddMenuEntry(SubScreen, &submenu_exit_entry);
    Entry->me.SubScreen = SubScreen;
    AddMenuEntry(&main_menu, (REFIT_MENU_ENTRY *)Entry);
}

static void free_loader_entry(IN LOADER_ENTRY *Entry)
{
    FreePool(Entry->me.Title);
    FreePool(Entry->LoaderPath);
    FreePool(Entry->VolName);
    FreePool(Entry->DevicePath);
}

static void loader_scan_dir(IN REFIT_VOLUME *Volume, IN CHAR16 *Path)
{
    EFI_STATUS              Status;
    REFIT_DIR_ITER          DirIter;
    EFI_FILE_INFO           *DirEntry;
    CHAR16                  FileName[256];
    
    // look through contents of the directory
    DirIterOpen(Volume->RootDir, Path, &DirIter);
    while (DirIterNext(&DirIter, 2, L"*.EFI", &DirEntry)) {
        if (StriCmp(DirEntry->FileName, L"TextMode.efi") == 0 ||
            StriCmp(DirEntry->FileName, L"ebounce.efi") == 0 ||
            StriCmp(DirEntry->FileName, L"GraphicsConsole.efi") == 0)
            continue;   // skip this
        
        if (Path)
            SPrint(FileName, 255, L"\\%s\\%s", Path, DirEntry->FileName);
        else
            SPrint(FileName, 255, L"\\%s", DirEntry->FileName);
        add_loader_entry(FileName, NULL, Volume);
    }
    Status = DirIterClose(&DirIter);
    if (Status != EFI_NOT_FOUND) {
        if (Path)
            SPrint(FileName, 255, L"while scanning the %s directory", Path);
        else
            StrCpy(FileName, L"while scanning the root directory");
        CheckError(Status, FileName);
    }
}

static void loader_scan(void)
{
    EFI_STATUS              Status;
    UINTN                   VolumeIndex;
    REFIT_VOLUME            *Volume;
    REFIT_DIR_ITER          EfiDirIter;
    EFI_FILE_INFO           *EfiDirEntry;
    CHAR16                  FileName[256];
    
    Print(L"Scanning for boot loaders...\n");
    
    for (VolumeIndex = 0; VolumeIndex < VolumesCount; VolumeIndex++) {
        Volume = Volumes[VolumeIndex];
        if (Volume->RootDir == NULL || Volume->VolName == NULL)
            continue;
        
        // check for Mac OS X boot loader
        StrCpy(FileName, MACOSX_LOADER_PATH);
        if (FileExists(Volume->RootDir, FileName)) {
            Print(L"  - Mac OS X boot file found\n");
            add_loader_entry(FileName, L"Mac OS X", Volume);
        }
        
        // check for XOM
        StrCpy(FileName, L"\\System\\Library\\CoreServices\\xom.efi");
        if (FileExists(Volume->RootDir, FileName)) {
            add_loader_entry(FileName, L"Windows XP (XoM)", Volume);
        }
        
        // check for Microsoft boot loader/menu
        StrCpy(FileName, L"\\EFI\\Microsoft\\Boot\\Bootmgfw.efi");
        if (FileExists(Volume->RootDir, FileName)) {
            Print(L"  - Microsoft boot menu found\n");
            add_loader_entry(FileName, L"Microsoft boot menu", Volume);
        }
        
        // scan the root directory for EFI executables
        loader_scan_dir(Volume, NULL);
        // scan the elilo directory (as used on gimli's first Live CD)
        loader_scan_dir(Volume, L"elilo");
        // scan the boot directory
        loader_scan_dir(Volume, L"boot");
        
        // scan subdirectories of the EFI directory (as per the standard)
        DirIterOpen(Volume->RootDir, L"EFI", &EfiDirIter);
        while (DirIterNext(&EfiDirIter, 1, NULL, &EfiDirEntry)) {
            if (StriCmp(EfiDirEntry->FileName, L"TOOLS") == 0 || EfiDirEntry->FileName[0] == '.')
                continue;   // skip this, doesn't contain boot loaders
            if (StriCmp(EfiDirEntry->FileName, L"REFIT") == 0 || StriCmp(EfiDirEntry->FileName, L"REFITL") == 0)
                continue;   // skip ourselves
            Print(L"  - Directory EFI\\%s found\n", EfiDirEntry->FileName);
            
            SPrint(FileName, 255, L"EFI\\%s", EfiDirEntry->FileName);
            loader_scan_dir(Volume, FileName);
        }
        Status = DirIterClose(&EfiDirIter);
        if (Status != EFI_NOT_FOUND)
            CheckError(Status, L"while scanning the EFI directory");
        
        // check for Apple hardware diagnostics
        StrCpy(FileName, L"\\System\\Library\\CoreServices\\.diagnostics\\diags.efi");
        if (FileExists(Volume->RootDir, FileName)) {
            Print(L"  - Apple Hardware Test found\n");
            add_loader_entry(FileName, L"Apple Hardware Test", Volume);
        }
    }
}


#define LEGACY_OPTION_HD_SIZE (0x4E)
#define LEGACY_OPTION_CD_SIZE (0x4E)

static UINT8 legacy_option_hd[LEGACY_OPTION_HD_SIZE] = {
    0x01, 0x00, 0x00, 0x00, 0x30, 0x00, 0x4D, 0x00,
    0x61, 0x00, 0x63, 0x00, 0x20, 0x00, 0x4F, 0x00,
    0x53, 0x00, 0x20, 0x00, 0x58, 0x00, 0x00, 0x00,
    0x01, 0x03, 0x18, 0x00, 0x0B, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xE0, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xF9, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0x04, 0x06, 0x14, 0x00, 0xEB, 0x85, 0x05, 0x2B,
    0xB8, 0xD8, 0xA9, 0x49, 0x8B, 0x8C, 0xE2, 0x1B,
    0x01, 0xAE, 0xF2, 0xB7, 0x7F, 0xFF, 0x04, 0x00,
    'H',  0x00, 'D',  0x00, 0x00, 0x00
};
static UINT8 legacy_option_cd[LEGACY_OPTION_CD_SIZE] = {
    0x01, 0x00, 0x00, 0x00, 0x30, 0x00, 0x4D, 0x00,
    0x61, 0x00, 0x63, 0x00, 0x20, 0x00, 0x4F, 0x00,
    0x53, 0x00, 0x20, 0x00, 0x58, 0x00, 0x00, 0x00,
    0x01, 0x03, 0x18, 0x00, 0x0B, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xE0, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xF9, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0x04, 0x06, 0x14, 0x00, 0xEB, 0x85, 0x05, 0x2B,
    0xB8, 0xD8, 0xA9, 0x49, 0x8B, 0x8C, 0xE2, 0x1B,
    0x01, 0xAE, 0xF2, 0xB7, 0x7F, 0xFF, 0x04, 0x00,
    'C',  0x00, 'D',  0x00, 0x00, 0x00
};
static EFI_GUID GlobalVarVendorId = EFI_GLOBAL_VARIABLE;

static void start_legacy(IN LEGACY_ENTRY *Entry)
{
    EFI_STATUS              Status;
    UINT8                   *BootOptionData;
    UINTN                   BootOptionSize;
    UINT16                  BootNextData;
    
    BeginExternalScreen(1, L"Initiating Legacy Boot");
    
    // decide which kind of media to use
    if (Entry->Volume->DiskKind == DISK_KIND_OPTICAL) {
        BootOptionData = legacy_option_cd;
        BootOptionSize = LEGACY_OPTION_CD_SIZE;
    } else {
        BootOptionData = legacy_option_hd;
        BootOptionSize = LEGACY_OPTION_HD_SIZE;
    }
    
    // set the boot option data
    Status = RT->SetVariable(L"BootC0DE", &GlobalVarVendorId,
                             EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
                             BootOptionSize, (VOID *) BootOptionData);
    if (CheckError(Status, L"while setting BootC0DE variable"))
        goto bailout;
    
    // arrange for it to be used on next boot only
    BootNextData = 0xC0DE;
    Status = RT->SetVariable(L"BootNext", &GlobalVarVendorId,
                             EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
                             sizeof(UINT16), (VOID *) &BootNextData);
    if (CheckError(Status, L"while setting BootNext variable"))
        goto bailout;
    
    // reboot system
    RT->ResetSystem(EfiResetWarm, EFI_SUCCESS, 0, NULL);
    CheckError(EFI_LOAD_ERROR, L"when trying to reboot");
    
bailout:
    FinishExternalScreen();
}

static void add_legacy_entry(IN CHAR16 *LoaderTitle, IN REFIT_VOLUME *Volume)
{
    LEGACY_ENTRY            *Entry, *SubEntry;
    REFIT_MENU_SCREEN       *SubScreen;
    CHAR16                  *VolDesc;
    
    if (LoaderTitle == NULL)
        LoaderTitle = L"Legacy OS";
    if (Volume->VolName != NULL)
        VolDesc = Volume->VolName;
    else
        VolDesc = (Volume->DiskKind == DISK_KIND_OPTICAL) ? L"CD" : L"HD";
    
    // prepare the menu entry
    Entry = AllocateZeroPool(sizeof(LEGACY_ENTRY));
    Entry->me.Title        = PoolPrint(L"Boot %s from %s", LoaderTitle, VolDesc);
    Entry->me.Tag          = TAG_LEGACY;
    Entry->me.Row          = 0;
    Entry->me.Image        = BuiltinIcon(12);  // os_legacy
    Entry->me.BadgeImage   = Volume->VolBadgeImage;
    Entry->Volume          = Volume;
    
    // create the submenu
    SubScreen = AllocateZeroPool(sizeof(REFIT_MENU_SCREEN));
    SubScreen->Title = PoolPrint(L"Boot Options for %s on %s", LoaderTitle, VolDesc);
    SubScreen->TitleImage = Entry->me.Image;
    
    // default entry
    SubEntry = AllocateZeroPool(sizeof(LEGACY_ENTRY));
    SubEntry->me.Title        = PoolPrint(L"Boot %s", LoaderTitle);
    SubEntry->me.Tag          = TAG_LEGACY;
    SubEntry->Volume          = Entry->Volume;
    AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
    
    AddMenuEntry(SubScreen, &submenu_exit_entry);
    Entry->me.SubScreen = SubScreen;
    AddMenuEntry(&main_menu, (REFIT_MENU_ENTRY *)Entry);
}

static void legacy_scan(void)
{
    UINTN                   VolumeIndex;
    REFIT_VOLUME            *Volume;
    
    Print(L"Scanning for legacy boot volumes...\n");
    
    for (VolumeIndex = 0; VolumeIndex < VolumesCount; VolumeIndex++) {
        Volume = Volumes[VolumeIndex];
        //Print(L" %d %s %d %s\n", VolumeIndex, Volume->VolName ? Volume->VolName : L"(no name)", Volume->DiskKind, Volume->IsLegacy ? L"L" : L"N");
        if (Volume->IsLegacy)
            add_legacy_entry(NULL, Volume);
    }
}


static void start_tool(IN LOADER_ENTRY *Entry)
{
    EFI_STATUS              Status;
    EFI_HANDLE              ChildImageHandle;
    CHAR16                  ErrorInfo[256];
    
    BeginExternalScreen(Entry->UseGraphicsMode ? 1 : 0, Entry->me.Title + 6);  // assumes "Start <title>"
    
    // load the image into memory
    Status = BS->LoadImage(FALSE, SelfImageHandle, Entry->DevicePath, NULL, 0, &ChildImageHandle);
    SPrint(ErrorInfo, 255, L"while loading %s", Entry->LoaderPath);
    if (CheckError(Status, ErrorInfo))
        goto bailout;
    
    // turn control over to the image
    Status = BS->StartImage(ChildImageHandle, NULL, NULL);
    // control returns here when the child image calls Exit()
    CheckError(Status, L"returned from tool");
    
bailout_unload:
    // unload the image, we don't care if it works or not...
    Status = BS->UnloadImage(ChildImageHandle);
bailout:
    FinishExternalScreen();
}

static void add_tool_entry(IN CHAR16 *LoaderPath, IN CHAR16 *LoaderTitle, REFIT_IMAGE *Image, BOOLEAN UseGraphicsMode)
{
    LOADER_ENTRY *Entry;
    
    Entry = AllocateZeroPool(sizeof(LOADER_ENTRY));
    
    Entry->me.Title = PoolPrint(L"Start %s", LoaderTitle);
    Entry->me.Tag = TAG_TOOL;
    Entry->me.Row = 1;
    Entry->me.Image = Image;
    Entry->LoaderPath = StrDuplicate(LoaderPath);
    Entry->DevicePath = FileDevicePath(SelfLoadedImage->DeviceHandle, Entry->LoaderPath);
    Entry->UseGraphicsMode = UseGraphicsMode;
    
    AddMenuEntry(&main_menu, (REFIT_MENU_ENTRY *)Entry);
}

static void free_tool_entry(IN LOADER_ENTRY *Entry)
{
    FreePool(Entry->me.Title);
    FreePool(Entry->LoaderPath);
    FreePool(Entry->DevicePath);
}

static void tool_scan(void)
{
    //EFI_STATUS              Status;
    CHAR16                  FileName[256];
    
    Print(L"Scanning for tools...\n");
    
    // look for the EFI shell
    SPrint(FileName, 255, L"%s\\apps\\shell.efi", SelfDirPath);
    if (FileExists(SelfRootDir, FileName)) {
        add_tool_entry(FileName, L"EFI Shell", BuiltinIcon(7), FALSE);
    } else {
        StrCpy(FileName, L"\\efi\\tools\\shell.efi");
        if (FileExists(SelfRootDir, FileName)) {
            add_tool_entry(FileName, L"EFI Shell", BuiltinIcon(7), FALSE);
        }
    }
}


EFI_STATUS
EFIAPI
RefitMain (IN EFI_HANDLE           ImageHandle,
           IN EFI_SYSTEM_TABLE     *SystemTable)
{
    EFI_STATUS Status;
    BOOLEAN mainLoopRunning = TRUE;
    REFIT_MENU_ENTRY *chosenEntry;
    UINTN MenuExit;
    UINTN i;
    
    InitializeLib(ImageHandle, SystemTable);
    InitScreen();
    Status = InitRefitLib(ImageHandle);
    if (EFI_ERROR(Status))
        return Status;
    
    BS->SetWatchdogTimer(0x0000, 0x0000, 0x0000, NULL);   // disable EFI watchdog timer
    
    // scan for loaders and tools, add them to the menu
    ScanVolumes();
    loader_scan();
    legacy_scan();
    tool_scan();
    
    // fixed other menu entries
    entry_about.Image = BuiltinIcon(4);
    AddMenuEntry(&main_menu, &entry_about);
    entry_reset.Image = BuiltinIcon(6);
    AddMenuEntry(&main_menu, &entry_reset);
    
    // wait for user ACK when there were errors
    FinishTextScreen(FALSE);
    
    while (mainLoopRunning) {
        MenuExit = RunMainMenu(&main_menu, &chosenEntry);
        
        if (MenuExit == MENU_EXIT_ESCAPE || chosenEntry->Tag == TAG_EXIT)
            break;
        
        switch (chosenEntry->Tag) {
            
            case TAG_RESET:    // Reboot
                TerminateScreen();
                RT->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, NULL);
                mainLoopRunning = FALSE;   // just in case we get this far
                break;
                
            case TAG_ABOUT:    // About rEFIt
                about_refit();
                break;
                
            case TAG_LOADER:   // Boot OS via .EFI loader
                start_loader((LOADER_ENTRY *)chosenEntry);
                break;
                
            case TAG_LEGACY:   // Boot legacy OS
                start_legacy((LEGACY_ENTRY *)chosenEntry);
                break;
                
            case TAG_TOOL:     // Start a EFI tool
                start_tool((LOADER_ENTRY *)chosenEntry);
                break;
                
        }
    }
    
    for (i = 0; i < main_menu.EntryCount; i++) {
        if (main_menu.Entries[i]->Tag == TAG_LOADER) {
            free_loader_entry((LOADER_ENTRY *)(main_menu.Entries[i]));
            FreePool(main_menu.Entries[i]);
        } else if (main_menu.Entries[i]->Tag == TAG_TOOL) {
            free_tool_entry((LOADER_ENTRY *)(main_menu.Entries[i]));
            FreePool(main_menu.Entries[i]);
        }
    }
    FreePool(main_menu.Entries);
    
    // clear screen completely
    TerminateScreen();
    return EFI_SUCCESS;
}