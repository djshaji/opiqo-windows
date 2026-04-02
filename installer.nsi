; -----------------------------------------------------------------------------
; Opiqo LV2 Plugin Host — NSIS Installer Script
; Requires NSIS 3.x with Modern UI 2 (MUI2)
;
; Build:
;   makensis installer.nsi
;
; Expected layout (relative to this script):
;   bin\opiqo.exe
;   bin\*.dll
;   bin\lv2\<plugin>.lv2\...
; -----------------------------------------------------------------------------

!include "MUI2.nsh"
!include "LogicLib.nsh"

; -----------------------------------------------------------------------------
; General
; -----------------------------------------------------------------------------
Name          "Opiqo"
OutFile       "opiqo-setup.exe"
Unicode       True

InstallDir    "$PROGRAMFILES64\Opiqo"
InstallDirRegKey HKLM "Software\Opiqo" "InstallDir"
RequestExecutionLevel admin

; -----------------------------------------------------------------------------
; Version metadata (appears in the installer EXE properties)
; -----------------------------------------------------------------------------
VIProductVersion "1.0.0.0"
VIAddVersionKey  "ProductName"      "Opiqo"
VIAddVersionKey  "FileDescription"  "Opiqo LV2 Plugin Host Installer"
VIAddVersionKey  "FileVersion"      "1.0.0"
VIAddVersionKey  "ProductVersion"   "1.0.0"
VIAddVersionKey  "LegalCopyright"   "© 2026 Opiqo contributors"

; -----------------------------------------------------------------------------
; Modern UI — look & feel
; -----------------------------------------------------------------------------
!define MUI_ABORTWARNING
!define MUI_ICON   "${NSISDIR}\Contrib\Graphics\Icons\modern-install.ico"
!define MUI_UNICON "${NSISDIR}\Contrib\Graphics\Icons\modern-uninstall.ico"

!define MUI_WELCOMEFINISHPAGE_BITMAP \
    "${NSISDIR}\Contrib\Graphics\Wizard\win.bmp"
!define MUI_UNWELCOMEFINISHPAGE_BITMAP \
    "${NSISDIR}\Contrib\Graphics\Wizard\win.bmp"

!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_BITMAP \
    "${NSISDIR}\Contrib\Graphics\Header\nsis3-metro.bmp"
!define MUI_HEADERIMAGE_RIGHT

; Finish page — offer to launch the application
!define MUI_FINISHPAGE_RUN          "$INSTDIR\opiqo.exe"
!define MUI_FINISHPAGE_RUN_TEXT     "Launch Opiqo"

; Start Menu folder variable
Var StartMenuFolder

; -----------------------------------------------------------------------------
; Installer pages
; -----------------------------------------------------------------------------
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_STARTMENU Application $StartMenuFolder
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

; -----------------------------------------------------------------------------
; Uninstaller pages
; -----------------------------------------------------------------------------
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; -----------------------------------------------------------------------------
; Language
; -----------------------------------------------------------------------------
!insertmacro MUI_LANGUAGE "English"

; =============================================================================
; SECTIONS
; =============================================================================

; --- Core application (required) -----------------------------------------
Section "Opiqo (required)" SecCore

    SectionIn RO   ; cannot be deselected

    SetOutPath "$INSTDIR"

    ; Main executable
    File "bin\opiqo.exe"

    ; MinGW / GCC runtime DLLs
    File "bin\libgcc_s_seh-1.dll"
    File "bin\libstdc++-6.dll"
    File "bin\libwinpthread-1.dll"

    ; Audio & codec DLLs
    File "bin\libFLAC-12.dll"
    File "bin\libogg-0.dll"
    File "bin\libopus-0.dll"
    File "bin\libopusenc-0.dll"
    File "bin\libvorbis-0.dll"
    File "bin\libvorbisenc-2.dll"
    File "bin\libsndfile-1.dll"
    File "bin\libsndfile.dll"
    File "bin\libmp3lame-0.dll"

    ; LV2 host / utility DLLs
    File "bin\libdl.dll"
    File "bin\liblilv-0.dll"
    File "bin\libserd-0.dll"
    File "bin\libsord-0.dll"
    File "bin\libsratom-0.dll"
    File "bin\libzix-0.dll"

    ; LV2 command-line utilities
    File "bin\lv2apply.exe"
    File "bin\lv2info.exe"
    File "bin\lv2ls.exe"
    File "bin\serdi.exe"
    File "bin\sordi.exe"
    File "bin\sord_validate.exe"

    ; Bundled LV2 plugin bundles (preserves sub-directory structure)
    SetOutPath "$INSTDIR"
    File /r "bin\lv2"

    ; Registry: install location
    WriteRegStr HKLM "Software\Opiqo" "InstallDir" "$INSTDIR"

    ; Write uninstaller
    WriteUninstaller "$INSTDIR\uninstall.exe"

    ; Add / Remove Programs entry
    WriteRegStr   HKLM \
        "Software\Microsoft\Windows\CurrentVersion\Uninstall\Opiqo" \
        "DisplayName"      "Opiqo LV2 Plugin Host"
    WriteRegStr   HKLM \
        "Software\Microsoft\Windows\CurrentVersion\Uninstall\Opiqo" \
        "UninstallString"  '"$INSTDIR\uninstall.exe"'
    WriteRegStr   HKLM \
        "Software\Microsoft\Windows\CurrentVersion\Uninstall\Opiqo" \
        "InstallLocation"  "$INSTDIR"
    WriteRegStr   HKLM \
        "Software\Microsoft\Windows\CurrentVersion\Uninstall\Opiqo" \
        "DisplayVersion"   "1.0.0"
    WriteRegStr   HKLM \
        "Software\Microsoft\Windows\CurrentVersion\Uninstall\Opiqo" \
        "Publisher"        "Opiqo contributors"
    WriteRegDWORD HKLM \
        "Software\Microsoft\Windows\CurrentVersion\Uninstall\Opiqo" \
        "NoModify" 1
    WriteRegDWORD HKLM \
        "Software\Microsoft\Windows\CurrentVersion\Uninstall\Opiqo" \
        "NoRepair"  1

    ; Start Menu shortcuts
    !insertmacro MUI_STARTMENU_WRITE_BEGIN Application
        CreateDirectory "$SMPROGRAMS\$StartMenuFolder"
        CreateShortcut  "$SMPROGRAMS\$StartMenuFolder\Opiqo.lnk" \
                        "$INSTDIR\opiqo.exe"
        CreateShortcut  "$SMPROGRAMS\$StartMenuFolder\Uninstall Opiqo.lnk" \
                        "$INSTDIR\uninstall.exe"
    !insertmacro MUI_STARTMENU_WRITE_END

SectionEnd

; --- Optional desktop shortcut -------------------------------------------
Section "Desktop Shortcut" SecDesktop

    CreateShortcut "$DESKTOP\Opiqo.lnk" "$INSTDIR\opiqo.exe"

SectionEnd

; -----------------------------------------------------------------------------
; Section descriptions (shown on the Components page)
; -----------------------------------------------------------------------------
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecCore} \
        "Opiqo application, bundled LV2 plugins, and all required runtime libraries."
    !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} \
        "Add an Opiqo shortcut to the Desktop."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; =============================================================================
; UNINSTALLER
; =============================================================================

Section "Uninstall"

    ; Application and utility executables
    Delete "$INSTDIR\opiqo.exe"
    Delete "$INSTDIR\lv2apply.exe"
    Delete "$INSTDIR\lv2info.exe"
    Delete "$INSTDIR\lv2ls.exe"
    Delete "$INSTDIR\serdi.exe"
    Delete "$INSTDIR\sordi.exe"
    Delete "$INSTDIR\sord_validate.exe"

    ; Runtime DLLs
    Delete "$INSTDIR\libgcc_s_seh-1.dll"
    Delete "$INSTDIR\libstdc++-6.dll"
    Delete "$INSTDIR\libwinpthread-1.dll"
    Delete "$INSTDIR\libFLAC-12.dll"
    Delete "$INSTDIR\libogg-0.dll"
    Delete "$INSTDIR\libopus-0.dll"
    Delete "$INSTDIR\libopusenc-0.dll"
    Delete "$INSTDIR\libvorbis-0.dll"
    Delete "$INSTDIR\libvorbisenc-2.dll"
    Delete "$INSTDIR\libsndfile-1.dll"
    Delete "$INSTDIR\libsndfile.dll"
    Delete "$INSTDIR\libmp3lame-0.dll"
    Delete "$INSTDIR\libdl.dll"
    Delete "$INSTDIR\liblilv-0.dll"
    Delete "$INSTDIR\libserd-0.dll"
    Delete "$INSTDIR\libsord-0.dll"
    Delete "$INSTDIR\libsratom-0.dll"
    Delete "$INSTDIR\libzix-0.dll"

    ; Bundled LV2 plugins
    RMDir /r "$INSTDIR\lv2"

    ; Uninstaller itself
    Delete "$INSTDIR\uninstall.exe"

    ; Remove install directory (only if empty after the above)
    RMDir "$INSTDIR"

    ; Start Menu shortcuts
    !insertmacro MUI_STARTMENU_GETFOLDER Application $StartMenuFolder
    Delete "$SMPROGRAMS\$StartMenuFolder\Opiqo.lnk"
    Delete "$SMPROGRAMS\$StartMenuFolder\Uninstall Opiqo.lnk"
    RMDir  "$SMPROGRAMS\$StartMenuFolder"

    ; Desktop shortcut
    Delete "$DESKTOP\Opiqo.lnk"

    ; Registry cleanup
    DeleteRegKey HKLM \
        "Software\Microsoft\Windows\CurrentVersion\Uninstall\Opiqo"
    DeleteRegKey HKLM "Software\Opiqo"

SectionEnd
