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

// images

#ifdef TEXTONLY

DUMMY_IMAGE(image_tool_about)
DUMMY_IMAGE(image_tool_exit)
DUMMY_IMAGE(image_tool_reset)

DUMMY_IMAGE(image_tool_shell)

DUMMY_IMAGE(image_os_mac)
DUMMY_IMAGE(image_os_linux)
DUMMY_IMAGE(image_os_win)
DUMMY_IMAGE(image_os_unknown)

#else

#include "image_tool_about.h"
#include "image_tool_exit.h"
#include "image_tool_reset.h"

#include "image_tool_shell.h"

#include "image_os_mac.h"
#include "image_os_linux.h"
#include "image_os_win.h"
#include "image_os_unknown.h"
#endif

REFIT_IMAGE *IcnsTest;

// types

typedef struct {
    REFIT_MENU_ENTRY me;
    CHAR16           *LoaderPath;
    CHAR16           *VolName;
    EFI_DEVICE_PATH  *DevicePath;
    BOOLEAN          UseGraphicsMode;
} LOADER_ENTRY;

// variables

#define TAG_EXIT   (1)
#define TAG_RESET  (2)
#define TAG_ABOUT  (3)
#define TAG_LOADER (4)
#define TAG_TOOL   (5)

static EFI_HANDLE SelfImageHandle;
static EFI_LOADED_IMAGE *SelfLoadedImage;

static REFIT_MENU_ENTRY entry_exit    = { L"Exit to built-in Boot Manager", TAG_EXIT, 1, &image_tool_exit };
static REFIT_MENU_ENTRY entry_reset   = { L"Restart Computer", TAG_RESET, 1, &image_tool_reset };
static REFIT_MENU_ENTRY entry_about   = { L"About rEFIt", TAG_ABOUT, 1, &image_tool_about };

static REFIT_MENU_SCREEN main_menu    = { L"rEFIt - Main Menu", 0, NULL, 20, L"Automatic boot" };

#define MACOSX_LOADER_PATH L"\\System\\Library\\CoreServices\\boot.efi"


static void about_refit(void)
{
    BeginTextScreen(L"rEFIt - About");
    Print(L"rEFIt Version 0.4\n\n");
    Print(L"Copyright (c) 2006 Christoph Pfisterer\n");
    Print(L"Portions Copyright (c) Intel Corporation and others\n");
    FinishTextScreen(TRUE);
}


static void start_loader(IN LOADER_ENTRY *Entry)
{
    EFI_STATUS              Status;
    EFI_HANDLE              ChildImageHandle;
    CHAR16                  ErrorInfo[256];
    
    BeginExternalScreen(Entry->UseGraphicsMode ? 1 : 0, L"rEFIt - Booting OS");
    
    // load the image into memory
    Status = BS->LoadImage(FALSE, SelfImageHandle, Entry->DevicePath, NULL, 0, &ChildImageHandle);
    SPrint(ErrorInfo, 255, L"while loading %s on %s", Entry->LoaderPath, Entry->VolName);
    if (CheckError(Status, ErrorInfo))
        goto bailout;
    
    // turn control over to the image
    // TODO: re-enable the EFI watchdog timer!
    BS->StartImage(ChildImageHandle, NULL, NULL);
    // control returns here when the child image calls Exit()
    
bailout:
    FinishExternalScreen();
}

static void add_loader_entry(IN CHAR16 *LoaderPath, IN CHAR16 *LoaderTitle, IN EFI_HANDLE DeviceHandle, IN CHAR16 *VolName)
{
    CHAR16 *FileName;
    UINTN i;
    LOADER_ENTRY *Entry;
    
    Entry = AllocatePool(sizeof(LOADER_ENTRY));
    
    FileName = LoaderPath;
    for (i = StrLen(LoaderPath) - 1; i >= 0; i--) {
        if (LoaderPath[i] == '\\') {
            FileName = LoaderPath + i + 1;
            break;
        }
    }
    
    if (LoaderTitle == NULL)
        LoaderTitle = LoaderPath + 1;
    Entry->me.Title = PoolPrint(L"Boot %s from %s", LoaderTitle, VolName);
    Entry->me.Tag = TAG_LOADER;
    Entry->me.Row = 0;
    Entry->me.Image = &image_os_unknown;
    Entry->LoaderPath = StrDuplicate(LoaderPath);
    Entry->VolName = StrDuplicate(VolName);
    Entry->DevicePath = FileDevicePath(DeviceHandle, Entry->LoaderPath);
    Entry->UseGraphicsMode = FALSE;
    
    if (StriCmp(LoaderPath, MACOSX_LOADER_PATH) == 0) {
        Entry->me.Image = IcnsTest; //&image_os_mac;
        Entry->UseGraphicsMode = TRUE;
    } else if (StriCmp(FileName, L"e.efi") == 0 ||
               StriCmp(FileName, L"elilo.efi") == 0) {
        Entry->me.Image = &image_os_linux;
    } else if (StriCmp(FileName, L"Bootmgfw.efi") == 0) {
        Entry->me.Image = &image_os_win;
    } else if (StriCmp(FileName, L"xom.efi") == 0) {
        Entry->me.Image = &image_os_win;
        Entry->UseGraphicsMode = TRUE;
    }
    
    MenuAddEntry(&main_menu, (REFIT_MENU_ENTRY *)Entry);
}

static void free_loader_entry(IN LOADER_ENTRY *Entry)
{
    FreePool(Entry->me.Title);
    FreePool(Entry->LoaderPath);
    FreePool(Entry->VolName);
    FreePool(Entry->DevicePath);
}

static void loader_scan_dir(IN EFI_FILE *RootDir, IN CHAR16 *Path, IN EFI_HANDLE DeviceHandle, IN CHAR16 *VolName)
{
    EFI_STATUS              Status;
    REFIT_DIR_ITER          DirIter;
    EFI_FILE_INFO           *DirEntry;
    CHAR16                  FileName[256];
    
    // look through contents of the directory
    DirIterOpen(RootDir, Path, &DirIter);
    while (DirIterNext(&DirIter, 2, L"*.EFI", &DirEntry)) {
        if (StriCmp(DirEntry->FileName, L"TextMode.efi") == 0 ||
            StriCmp(DirEntry->FileName, L"ebounce.efi") == 0 ||
            StriCmp(DirEntry->FileName, L"GraphicsConsole.efi") == 0)
            continue;   // skip this
        
        if (Path)
            SPrint(FileName, 255, L"\\%s\\%s", Path, DirEntry->FileName);
        else
            SPrint(FileName, 255, L"\\%s", DirEntry->FileName);
        add_loader_entry(FileName, NULL, DeviceHandle, VolName);
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
    UINTN                   HandleCount = 0;
    UINTN                   HandleIndex;
    EFI_HANDLE              *Handles;
    EFI_HANDLE              DeviceHandle;
    EFI_FILE                *RootDir;
    EFI_FILE_SYSTEM_INFO    *FileSystemInfoPtr;
    CHAR16                  *VolName;
    REFIT_DIR_ITER          EfiDirIter;
    EFI_FILE_INFO           *EfiDirEntry;
    CHAR16                  FileName[256];
    
    Print(L"Scanning for boot loaders...\n");
    
    // get all filesystem handles
    Status = LibLocateHandle(ByProtocol, &FileSystemProtocol, NULL, &HandleCount, &Handles);
    if (Status == EFI_NOT_FOUND)
        return;  // no filesystems. strange, but true...
    if (CheckError(Status, L"while listing all file systems"))
        return;
    // iterate over the filesystem handles
    for (HandleIndex = 0; HandleIndex < HandleCount; HandleIndex++) {
        DeviceHandle = Handles[HandleIndex];
        
        RootDir = LibOpenRoot(DeviceHandle);
        if (RootDir == NULL) {
            Print(L"Error: Can't open volume.\n");
            // TODO: signal that we had an error
            continue;
        }
        
        // get volume name
        FileSystemInfoPtr = LibFileSystemInfo(RootDir);
        if (FileSystemInfoPtr != NULL) {
            Print(L"  Volume %s\n", FileSystemInfoPtr->VolumeLabel);
            VolName = StrDuplicate(FileSystemInfoPtr->VolumeLabel);
            FreePool(FileSystemInfoPtr);
        } else {
            Print(L"  GetInfo failed\n");
            VolName = StrDuplicate(L"Unnamed Volume");
        }
        
        // check for Mac OS X boot loader
        StrCpy(FileName, MACOSX_LOADER_PATH);
        if (FileExists(RootDir, FileName)) {
            Print(L"  - Mac OS X boot file found\n");
            add_loader_entry(FileName, L"Mac OS X", DeviceHandle, VolName);
        }
        
        // check for XOM
        StrCpy(FileName, L"\\System\\Library\\CoreServices\\xom.efi");
        if (FileExists(RootDir, FileName)) {
            add_loader_entry(FileName, L"Windows XP (XoM)", DeviceHandle, VolName);
        }
        
        // check for Microsoft boot loader/menu
        StrCpy(FileName, L"\\EFI\\Microsoft\\Boot\\Bootmgfw.efi");
        if (FileExists(RootDir, FileName)) {
            Print(L"  - Microsoft boot menu found\n");
            add_loader_entry(FileName, L"Microsoft boot menu", DeviceHandle, VolName);
        }
        
        // scan the root directory for EFI executables
        loader_scan_dir(RootDir, NULL, DeviceHandle, VolName);
        // scan the elilo directory (as used on gimli's first Live CD)
        loader_scan_dir(RootDir, L"elilo", DeviceHandle, VolName);
        // scan the boot directory
        loader_scan_dir(RootDir, L"boot", DeviceHandle, VolName);
        
        // scan subdirectories of the EFI directory (as per the standard)
        DirIterOpen(RootDir, L"EFI", &EfiDirIter);
        while (DirIterNext(&EfiDirIter, 1, NULL, &EfiDirEntry)) {
            if (StriCmp(EfiDirEntry->FileName, L"TOOLS") == 0)
                continue;   // skip this, doesn't contain boot loaders
            if (StriCmp(EfiDirEntry->FileName, L"REFIT") == 0 || StriCmp(EfiDirEntry->FileName, L"REFITL") == 0)
                continue;   // skip ourselves
            Print(L"  - Directory EFI\\%s found\n", EfiDirEntry->FileName);
            
            SPrint(FileName, 255, L"EFI\\%s", EfiDirEntry->FileName);
            loader_scan_dir(RootDir, FileName, DeviceHandle, VolName);
        }
        Status = DirIterClose(&EfiDirIter);
        if (Status != EFI_NOT_FOUND)
            CheckError(Status, L"while scanning the EFI directory");
        
        RootDir->Close(RootDir);
        FreePool(VolName);
    }
    
    FreePool(Handles);
}


static void start_tool(IN LOADER_ENTRY *Entry)
{
    EFI_STATUS              Status;
    CHAR16                  ScreenTitle[256];
    EFI_HANDLE              ChildImageHandle;
    CHAR16                  ErrorInfo[256];
    
    SPrint(ScreenTitle, 255, L"rEFIt - %s", Entry->me.Title + 6);
    BeginExternalScreen(Entry->UseGraphicsMode ? 1 : 0, ScreenTitle);
    
    // load the image into memory
    Status = BS->LoadImage(FALSE, SelfImageHandle, Entry->DevicePath, NULL, 0, &ChildImageHandle);
    SPrint(ErrorInfo, 255, L"while loading %s", Entry->LoaderPath);
    if (CheckError(Status, ErrorInfo))
        goto bailout;
    
    // turn control over to the image
    BS->StartImage(ChildImageHandle, NULL, NULL);
    // control returns here when the child image calls Exit()
    
bailout:
    FinishExternalScreen();
}

static void add_tool_entry(IN CHAR16 *LoaderPath, IN CHAR16 *LoaderTitle, REFIT_IMAGE *Image, BOOLEAN UseGraphicsMode)
{
    LOADER_ENTRY *Entry;
    
    Entry = AllocatePool(sizeof(LOADER_ENTRY));
    
    Entry->me.Title = PoolPrint(L"Start %s", LoaderTitle);
    Entry->me.Tag = TAG_TOOL;
    Entry->me.Row = 1;
    Entry->me.Image = Image;
    Entry->LoaderPath = StrDuplicate(LoaderPath);
    Entry->VolName = NULL;
    Entry->DevicePath = FileDevicePath(SelfLoadedImage->DeviceHandle, Entry->LoaderPath);
    Entry->UseGraphicsMode = UseGraphicsMode;
    
    MenuAddEntry(&main_menu, (REFIT_MENU_ENTRY *)Entry);
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
    EFI_FILE                *RootDir;
    CHAR16                  *DevicePathAsString;
    CHAR16                  BaseDirectory[256];
    CHAR16                  FileName[256];
    UINTN                   i;
    
    Print(L"Scanning for tools...\n");
    
    // open volume for probing
    RootDir = LibOpenRoot(SelfLoadedImage->DeviceHandle);
    if (RootDir == NULL) {
        Print(L"Error: Can't open volume.\n");
        // TODO: signal that we had an error
        return;
    }
    
    // find the current directory
    DevicePathAsString = DevicePathToStr(SelfLoadedImage->FilePath);
    if (DevicePathAsString != NULL) {
        StrCpy(BaseDirectory, DevicePathAsString);
        FreePool(DevicePathAsString);
        for (i = StrLen(BaseDirectory); i > 0 && BaseDirectory[i] != '\\'; i--) ;
        BaseDirectory[i] = 0;
    } else
        BaseDirectory[0] = 0;
    
    // look for the EFI shell
    SPrint(FileName, 255, L"%s\\apps\\shell.efi", BaseDirectory);
    if (FileExists(RootDir, FileName)) {
        add_tool_entry(FileName, L"EFI Shell", &image_tool_shell, FALSE);
    }
    
    // done!
    RootDir->Close(RootDir);
}


EFI_STATUS
EFIAPI
RefitMain (IN EFI_HANDLE           ImageHandle,
           IN EFI_SYSTEM_TABLE     *SystemTable)
{
    EFI_STATUS Status;
    REFIT_MENU_ENTRY *chosenEntry;
    BOOLEAN mainLoopRunning = TRUE;
    UINTN MenuExit;
    UINTN i;
    
    InitializeLib(ImageHandle, SystemTable);
    BS->SetWatchdogTimer(0x0000, 0x0000, 0x0000, NULL);   // disable EFI watchdog timer
    InitScreen();
    
    SelfImageHandle = ImageHandle;
    Status = BS->HandleProtocol(SelfImageHandle, &LoadedImageProtocol, (VOID*)&SelfLoadedImage);
    if (CheckFatalError(Status, L"while getting a LoadedImageProtocol handle"))
        return EFI_LOAD_ERROR;
    
    
    IcnsTest = LoadIcns(LibOpenRoot(SelfLoadedImage->DeviceHandle), L"\\efi\\refit\\Internal.icns", 128);
    
    
    // scan for loaders and tools, add them to the menu
    loader_scan();
    tool_scan();
    
    // fixed other menu entries
    MenuAddEntry(&main_menu, &entry_about);
    MenuAddEntry(&main_menu, &entry_exit);
    MenuAddEntry(&main_menu, &entry_reset);
    
    // wait for user ACK when there were errors
    FinishTextScreen(FALSE);
    
    while (mainLoopRunning) {
        MenuExit = MenuRun(1, &main_menu, &chosenEntry);
        
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
                
            case TAG_TOOL:     // Start a EFI tool
                start_tool((LOADER_ENTRY *)chosenEntry);
                break;
                
        }
        
        main_menu.TimeoutSeconds = 0;   // no timeout on the second run
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