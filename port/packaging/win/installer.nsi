; BoincTasks++ Windows installer
;
; Built with NSIS, which cross-compiles from Linux (makensis), so the whole
; release - Linux packages and the Windows installer - comes off one machine.
;
;   makensis -DVERSION=0.9.0 -DSRCEXE=../../../release-windows/BoincTasksPP.exe \
;            -DOUTFILE=../../../release-windows/BoincTasksPP-0.9.0-setup.exe installer.nsi

Unicode true
!include "MUI2.nsh"
!include "x64.nsh"
!include "FileFunc.nsh"

!ifndef VERSION
  !define VERSION "0.9.0"
!endif
!ifndef SRCEXE
  !define SRCEXE "..\..\..\release-windows\BoincTasksPP.exe"
!endif
!ifndef OUTFILE
  !define OUTFILE "..\..\..\release-windows\BoincTasksPP-${VERSION}-setup.exe"
!endif

!define APPNAME    "BoincTasks++"
!define EXENAME    "BoincTasksPP.exe"
!define PUBLISHER  "Skillz"
!define WEBSITE    "https://github.com/lblanchardiii/boinctasks-pp"
!define REGKEY     "Software\Microsoft\Windows\CurrentVersion\Uninstall\BoincTasksPP"

Name "${APPNAME} ${VERSION}"
OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES64\${APPNAME}"
InstallDirRegKey HKLM "Software\${APPNAME}" "InstallDir"
RequestExecutionLevel admin      ; installing to Program Files needs elevation
SetCompressor /SOLID lzma

VIProductVersion "0.9.0.0"
VIAddVersionKey "ProductName"     "${APPNAME}"
VIAddVersionKey "FileDescription" "${APPNAME} installer"
VIAddVersionKey "FileVersion"     "${VERSION}"
VIAddVersionKey "ProductVersion"  "${VERSION}"
VIAddVersionKey "LegalCopyright"  "GPLv3. Based on BoincTasks by eFMer (Fred)."
VIAddVersionKey "CompanyName"     "${PUBLISHER}"

!define MUI_ABORTWARNING
!define MUI_ICON "boinctasks.ico"
!define MUI_UNICON "boinctasks.ico"

!insertmacro MUI_PAGE_LICENSE "license.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES

; offer to launch, but not elevated - the application has no business running
; as administrator, and a client added while elevated writes its settings to
; the wrong profile
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_FUNCTION LaunchAsUser
!define MUI_FINISHPAGE_RUN_TEXT "Run ${APPNAME} now"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Function LaunchAsUser
  ; ShellExecute via the shell drops the installer's elevated token
  ExecShell "" "$INSTDIR\${EXENAME}"
FunctionEnd

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "${APPNAME} is 64-bit only and this is a 32-bit Windows."
    Abort
  ${EndIf}
  SetRegView 64
FunctionEnd

Section "Install"
  SetOutPath "$INSTDIR"
  File "${SRCEXE}"
  File "license.txt"

  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "Software\${APPNAME}" "InstallDir" "$INSTDIR"

  ; Start menu, and Add/Remove Programs
  CreateDirectory "$SMPROGRAMS\${APPNAME}"
  CreateShortCut "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk" "$INSTDIR\${EXENAME}"
  CreateShortCut "$SMPROGRAMS\${APPNAME}\Uninstall ${APPNAME}.lnk" "$INSTDIR\Uninstall.exe"

  WriteRegStr   HKLM "${REGKEY}" "DisplayName"     "${APPNAME}"
  WriteRegStr   HKLM "${REGKEY}" "DisplayVersion"  "${VERSION}"
  WriteRegStr   HKLM "${REGKEY}" "Publisher"       "${PUBLISHER}"
  WriteRegStr   HKLM "${REGKEY}" "DisplayIcon"     "$INSTDIR\${EXENAME}"
  WriteRegStr   HKLM "${REGKEY}" "URLInfoAbout"    "${WEBSITE}"
  WriteRegStr   HKLM "${REGKEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr   HKLM "${REGKEY}" "InstallLocation" "$INSTDIR"
  WriteRegDWORD HKLM "${REGKEY}" "NoModify" 1
  WriteRegDWORD HKLM "${REGKEY}" "NoRepair" 1

  ; report an honest size in Add/Remove Programs
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKLM "${REGKEY}" "EstimatedSize" "$0"
SectionEnd

Section "Uninstall"
  SetRegView 64
  Delete "$INSTDIR\${EXENAME}"
  Delete "$INSTDIR\license.txt"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir  "$INSTDIR"

  Delete "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk"
  Delete "$SMPROGRAMS\${APPNAME}\Uninstall ${APPNAME}.lnk"
  RMDir  "$SMPROGRAMS\${APPNAME}"

  DeleteRegKey HKLM "${REGKEY}"
  DeleteRegKey HKLM "Software\${APPNAME}"

  ; Settings, computer list and history live in %APPDATA% and are deliberately
  ; left alone unless asked for - reinstalling should not cost somebody their
  ; list of forty computers.
  MessageBox MB_YESNO|MB_ICONQUESTION \
    "Also remove your settings, computer list and task history?$\n$\n\
     ($APPDATA\BoincTasksPP)" IDNO keep
    RMDir /r "$APPDATA\BoincTasksPP"
  keep:
SectionEnd
