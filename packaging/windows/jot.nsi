; jot.nsi - the Windows installer.
;
; Build it with packaging/windows/build-installer.sh, which stage-dir/out-file/
; version in; makensis itself is run on Linux in CI and in the release job, so
; this script stays the only description of what an installed jot looks like.
;
; What it installs is the staged tree itself - both directories, whole - so the
; installer and the zip carry the same bytes and nothing has to be assembled by
; hand:
;   <install>\bin\jot.exe    the static binary (no DLL of its own to drop)
;   <install>\share\jot      bundled colour schemes, Lua runtime, LSP payload
;   <install>\share\...      the licences for the third-party code it links
; That two-level layout is not cosmetic. The binary resolves its data next to
; itself (exe_dir/../share/jot - see src/base/exe_path.h), so flattening it here
; would leave a jot that runs with no themes and no bundled Lua.
;
; Everything is per-user: %LOCALAPPDATA%\Programs\jot, HKCU only, no UAC prompt,
; and the user's own config in %APPDATA%\jot is left alone by the uninstaller -
; it is their file, not ours.

Unicode true
SetCompressor /SOLID lzma
SetCompressorDictSize 32

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "WinMessages.nsh"
!include "StrFunc.nsh"
; StrFunc is opt-in per function, and the uninstaller carries its own copy of
; the one it uses: the un-prefixed variants exist only in the installer.
${Using:StrFunc} StrStr
${Using:StrFunc} UnStrRep

!ifndef STAGE_DIR
  !error "STAGE_DIR is not defined: build with packaging/windows/build-installer.sh"
!endif
!ifndef OUT_FILE
  !error "OUT_FILE is not defined: build with packaging/windows/build-installer.sh"
!endif
!ifndef JOT_VERSION
  !define JOT_VERSION "0.0.0"
!endif
!ifndef JOT_VERSION_NUM
  !define JOT_VERSION_NUM "0.0.0.0"
!endif
!ifndef ICON_FILE
  !define ICON_FILE "${__FILEDIR__}/jot.ico"
!endif

!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\jot"
!define APP_PATHS_KEY "Software\Microsoft\Windows\CurrentVersion\App Paths\jot.exe"

Name "jot ${JOT_VERSION}"
OutFile "${OUT_FILE}"
; Per-user install, no admin rights and no UAC. InstallDirRegKey keeps an
; upgrade in the directory the first install chose (and /D still wins).
InstallDir "$LOCALAPPDATA\Programs\jot"
InstallDirRegKey HKCU "Software\jot" "InstallDir"
RequestExecutionLevel user
ShowInstDetails show
ShowUninstDetails show
Icon "${ICON_FILE}"
UninstallIcon "${ICON_FILE}"

VIProductVersion "${JOT_VERSION_NUM}"
VIAddVersionKey "ProductName" "jot"
VIAddVersionKey "FileDescription" "jot editor installer"
VIAddVersionKey "FileVersion" "${JOT_VERSION}"
VIAddVersionKey "ProductVersion" "${JOT_VERSION}"
VIAddVersionKey "LegalCopyright" "MIT"

!define MUI_ABORTWARNING
!define MUI_ICON "${ICON_FILE}"
!define MUI_UNICON "${ICON_FILE}"
!define MUI_FINISHPAGE_RUN "$INSTDIR\bin\jot.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Run jot"
!define MUI_UNCONFIRMPAGE_TEXT_TOP "jot will be removed from this account. Your own settings and themes in %APPDATA%\jot, and any files you opened with it, are not touched."

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; Every shortcut below is the current user's ($SMPROGRAMS / $DESKTOP / $APPDATA),
; in both the installer and the uninstaller: top-level instructions are not
; shared between the two, so each callback says it.
Function .onInit
  SetShellVarContext current
FunctionEnd

Function un.onInit
  SetShellVarContext current

  ; $INSTDIR needs no restoring here, however the install was reached: the
  ; uninstaller is written by WriteUninstaller at the end of the install, with
  ; the directory that install actually used, /D included. Reading the path
  ; back out of the registry instead replaces a correct value with whatever
  ; that read returns, and every path in the uninstall section then misses.
FunctionEnd

; ---------------------------------------------------------------------------
; Sections
; ---------------------------------------------------------------------------

; Windows keeps a running executable's image locked, so replacing or deleting
; bin\jot.exe under a live jot fails partway through and leaves a half-installed
; tree. An open handle is a sharing violation on that one file, so a write-open
; is the test; the retry loop also covers the editor or scanner that holds it
; open for a moment. One macro, two functions: the uninstaller needs its own
; copy of everything it calls.
!macro CHECK_NOT_RUNNING un
Function ${un}CheckNotRunning
  IfFileExists "$INSTDIR\bin\jot.exe" 0 missing
ask:
  ClearErrors
  FileOpen $0 "$INSTDIR\bin\jot.exe" a
  IfErrors 0 close
  ; A silent run has nobody to answer the dialog, so it reports the one thing
  ; the caller can act on: a non-zero exit code and no half-installed tree.
  IfSilent busy
  MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION \
    "jot is running from $INSTDIR. Close it, then choose Retry." \
    IDRETRY ask
  Abort
busy:
  SetErrorLevel 2
  Abort
close:
  FileClose $0
missing:
FunctionEnd
!macroend

!insertmacro CHECK_NOT_RUNNING ""
!insertmacro CHECK_NOT_RUNNING "un."

Section "jot" SEC_MAIN
  SectionIn RO

  Call CheckNotRunning

  ; Replace the two trees wholesale rather than merging into them, the same way
  ; `cmake --install` does: an upgrade must not leave a runtime file this build
  ; no longer ships behind, where it would still be loaded. Both trees are the
  ; application's own; the user's config lives in %APPDATA%\jot and is never
  ; under here.
  RMDir /r "$INSTDIR\bin"
  RMDir /r "$INSTDIR\share"

  SetOutPath "$INSTDIR\bin"
  File /r "${STAGE_DIR}/bin/*"

  SetOutPath "$INSTDIR\share"
  File /r "${STAGE_DIR}/share/*"

  WriteUninstaller "$INSTDIR\Uninstall.exe"

  ; Where the installer put it: read back by InstallDirRegKey on the next run.
  WriteRegStr HKCU "Software\jot" "InstallDir" "$INSTDIR"

  ; Apps & features entry (per-user, so it lists under the current account).
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayName" "jot"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayVersion" "${JOT_VERSION}"
  WriteRegStr HKCU "${UNINST_KEY}" "Publisher" "jot"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\bin\jot.exe"
  WriteRegStr HKCU "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINST_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKCU "${UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoRepair" 1

  ; Makes `jot` resolve from the Run box and from `where jot`.
  WriteRegStr HKCU "${APP_PATHS_KEY}" "" "$INSTDIR\bin\jot.exe"
  WriteRegStr HKCU "${APP_PATHS_KEY}" "Path" "$INSTDIR\bin"

  CreateDirectory "$SMPROGRAMS\jot"
  CreateShortCut "$SMPROGRAMS\jot\jot.lnk" "$INSTDIR\bin\jot.exe" "" "$INSTDIR\bin\jot.exe" 0
  CreateShortCut "$SMPROGRAMS\jot\Uninstall jot.lnk" "$INSTDIR\Uninstall.exe"

  ; The user's config tree, created so the drop-in locations exist before the
  ; first launch: settings.conf, colors\<theme>.json and lua\ overrides all
  ; live here. Only the directories are made - jot writes settings.conf itself
  ; on first run, and seeding one would freeze this build's defaults into the
  ; user's config where a later default could never reach it.
  CreateDirectory "$APPDATA\jot\configs\colors"
  CreateDirectory "$APPDATA\jot\lua"
SectionEnd

Section "Add jot to PATH" SEC_PATH
  Call AddToPath
SectionEnd

Section /o "Desktop shortcut" SEC_DESKTOP
  CreateShortCut "$DESKTOP\jot.lnk" "$INSTDIR\bin\jot.exe" "" "$INSTDIR\bin\jot.exe" 0
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SEC_MAIN} "jot itself: the editor, the bundled colour schemes, the Lua runtime and the offline LSP payload."
  !insertmacro MUI_DESCRIPTION_TEXT ${SEC_PATH} "Lets 'jot' be typed from any prompt. New shells see it at once; already-open ones need a restart."
  !insertmacro MUI_DESCRIPTION_TEXT ${SEC_DESKTOP} "A shortcut on the desktop. A Start menu entry is always created."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; ---------------------------------------------------------------------------
; PATH helpers
; ---------------------------------------------------------------------------

; Appends $INSTDIR\bin to the per-user PATH (HKCU\Environment), once: an
; existing entry - a previous install, or the same directory written another
; way - is left as it is. The value stays a REG_EXPAND_SZ so entries such as
; %USERPROFILE%\bin keep working, and the broadcast is what makes Explorer and
; new prompts pick the change up without a sign-out.
Function AddToPath
  Push $0
  Push $1

  ReadRegStr $0 HKCU "Environment" "Path"
  ${StrStr} $1 $0 "$INSTDIR\bin"
  ${If} $1 == ""
    ${If} $0 == ""
      StrCpy $0 "$INSTDIR\bin"
    ${Else}
      ; Drop a trailing separator so the join never leaves a ';;'.
      StrCpy $1 $0 1 -1
      ${If} $1 == ";"
        StrCpy $0 $0 -1
      ${EndIf}
      StrCpy $0 "$0;$INSTDIR\bin"
    ${EndIf}
    WriteRegExpandStr HKCU "Environment" "Path" "$0"
    System::Call 'user32::SendMessageW(i ${HWND_BROADCAST}, i ${WM_SETTINGCHANGE}, i 0, w "Environment")'
  ${EndIf}

  Pop $1
  Pop $0
FunctionEnd

; The inverse: strips $INSTDIR\bin (every occurrence) out of the per-user PATH
; and puts the rest back. A trailing separator is appended first so an entry at
; either end of the list is found by the same replacement.
Function un.RemoveFromPath
  Push $0
  Push $1

  ReadRegStr $0 HKCU "Environment" "Path"
  ${If} $0 != ""
    StrCpy $0 "$0;"
    ${UnStrRep} $0 "$0" "$INSTDIR\bin;" ""
    StrCpy $1 $0 1 -1
    ${If} $1 == ";"
      StrCpy $0 $0 -1
    ${EndIf}
    ; An account that had no Path before the install gets no empty one back.
    ${If} $0 == ""
      DeleteRegValue HKCU "Environment" "Path"
    ${Else}
      WriteRegExpandStr HKCU "Environment" "Path" "$0"
    ${EndIf}
    System::Call 'user32::SendMessageW(i ${HWND_BROADCAST}, i ${WM_SETTINGCHANGE}, i 0, w "Environment")'
  ${EndIf}

  Pop $1
  Pop $0
FunctionEnd

; ---------------------------------------------------------------------------
; Uninstall
; ---------------------------------------------------------------------------

Section "Uninstall"
  Call un.CheckNotRunning
  Call un.RemoveFromPath

  Delete "$SMPROGRAMS\jot\jot.lnk"
  Delete "$SMPROGRAMS\jot\Uninstall jot.lnk"
  RMDir "$SMPROGRAMS\jot"
  Delete "$DESKTOP\jot.lnk"

  DeleteRegKey HKCU "${UNINST_KEY}"
  DeleteRegKey HKCU "${APP_PATHS_KEY}"
  DeleteRegKey HKCU "Software\jot"

  ; Only what the installer put here. %APPDATA%\jot (settings, themes, Lua
  ; overrides) stays, so a reinstall keeps the user's setup.
  RMDir /r "$INSTDIR\bin"
  RMDir /r "$INSTDIR\share"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
SectionEnd
