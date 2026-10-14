; -*- coding: utf-8 -*-
Unicode true
ManifestDPIAware true
RequestExecutionLevel user
SetCompressor /SOLID lzma
SetCompressorDictSize 32
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "Sections.nsh"
!include "FileFunc.nsh"
!include "x64.nsh"
!include "WinVer.nsh"
!include "${GENERATED_DIR}\payload.nsh"
!include "${GENERATED_DIR}\shell-cleanup.nsh"

Name "Trie FrameViewer"
OutFile "${OUTPUT_FILE}"
InstallDir "$LOCALAPPDATA\Programs\TrieFrameViewer"
InstallDirRegKey HKCU "Software\Trie\FrameViewer\Installation" "InstallLocation"
BrandingText "Trie FrameViewer ${APP_VERSION}"
VIProductVersion "${APP_VERSION}.0"
VIAddVersionKey /LANG=1033 "ProductName" "Trie FrameViewer"
VIAddVersionKey /LANG=1033 "FileDescription" "Trie FrameViewer Setup"
VIAddVersionKey /LANG=1033 "FileVersion" "${APP_VERSION}"
VIAddVersionKey /LANG=1033 "ProductVersion" "${APP_VERSION}"
VIAddVersionKey /LANG=1033 "LegalCopyright" "Trie FrameViewer contributors"

!define MUI_ICON "${APP_ICON}"
!define MUI_UNICON "${APP_ICON}"
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "Install Trie FrameViewer"
!define MUI_WELCOMEPAGE_TEXT "A compact player for animation reference and playblasts.$\r$\n$\r$\nThis installs Trie FrameViewer for your Windows account. Administrator access is not required."
!define MUI_FINISHPAGE_RUN "$INSTDIR\TrieFrameViewer.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Open Trie FrameViewer"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

!macro CheckAppClosed PREFIX
Function ${PREFIX}CheckAppClosed
  IfFileExists "$INSTDIR\TrieFrameViewer.exe" 0 done
retry:
  ClearErrors
  FileOpen $0 "$INSTDIR\TrieFrameViewer.exe" a
  ${IfNot} ${Errors}
    FileClose $0
    Goto done
  ${EndIf}
  IfSilent cancel
  MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "Please save your work and close Trie FrameViewer, then click Retry." IDRETRY retry
cancel:
  SetErrorLevel 2
  Abort
done:
FunctionEnd
!macroend
!insertmacro CheckAppClosed ""
!insertmacro CheckAppClosed "un."

Section "Trie FrameViewer (required)" SEC_APP
  SectionIn RO
  Call CheckAppClosed
  SetOutPath "$INSTDIR"
  !insertmacro InstallPayload
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "Software\Trie\FrameViewer\Installation" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\TrieFrameViewer" "DisplayName" "Trie FrameViewer"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\TrieFrameViewer" "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\TrieFrameViewer" "Publisher" "Trie"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\TrieFrameViewer" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\TrieFrameViewer" "DisplayIcon" '"$INSTDIR\TrieFrameViewer.exe",0'
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\TrieFrameViewer" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\TrieFrameViewer" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\TrieFrameViewer" "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\TrieFrameViewer" "NoRepair" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\TrieFrameViewer" "EstimatedSize" ${PAYLOAD_KIB}
SectionEnd

Section "Start menu shortcuts" SEC_START
  CreateDirectory "$SMPROGRAMS\Trie FrameViewer"
  CreateShortcut "$SMPROGRAMS\Trie FrameViewer\Trie FrameViewer.lnk" "$INSTDIR\TrieFrameViewer.exe"
  CreateShortcut "$SMPROGRAMS\Trie FrameViewer\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
SectionEnd

Section /o "Desktop shortcut" SEC_DESKTOP
  CreateShortcut "$DESKTOP\Trie FrameViewer.lnk" "$INSTDIR\TrieFrameViewer.exe"
SectionEnd

Section "Video right-click menu and Open with" SEC_CONTEXT
  ExecWait '"$INSTDIR\TrieFrameViewer.exe" --register-context-menu' $0
  ${If} $0 != 0
    IfSilent +2
    MessageBox MB_OK|MB_ICONEXCLAMATION "The app is installed, but Windows could not register the video menu. You can retry in the app's Settings."
  ${EndIf}
SectionEnd

Function .onInit
  SetRegView 64
  SetShellVarContext current
  ${IfNot} ${RunningX64}
    MessageBox MB_OK|MB_ICONSTOP "Trie FrameViewer requires 64-bit Windows 10 or later."
    Abort
  ${EndIf}
  ${IfNot} ${AtLeastWin10}
    MessageBox MB_OK|MB_ICONSTOP "Trie FrameViewer requires Windows 10 or later."
    Abort
  ${EndIf}
  ${GetParameters} $R0
  ClearErrors
  ${GetOptions} $R0 "/NOCONTEXTMENU" $R1
  ${IfNot} ${Errors}
    !insertmacro UnselectSection ${SEC_CONTEXT}
  ${EndIf}
  ClearErrors
  ${GetOptions} $R0 "/NOSHORTCUTS" $R1
  ${IfNot} ${Errors}
    !insertmacro UnselectSection ${SEC_START}
    !insertmacro UnselectSection ${SEC_DESKTOP}
  ${EndIf}
FunctionEnd

Function un.onInit
  SetRegView 64
  SetShellVarContext current
  Call un.CheckAppClosed
FunctionEnd

Function .onVerifyInstDir
  GetFullPathName $0 "$INSTDIR"
  System::Call 'shlwapi::PathIsRootW(w r0)i.r1'
  ${If} $1 != 0
  ${OrIf} $0 == $WINDIR
  ${OrIf} $0 == $SYSDIR
  ${OrIf} $0 == $PROFILE
  ${OrIf} $0 == $DESKTOP
  ${OrIf} $0 == $LOCALAPPDATA
  ${OrIf} $0 == $PROGRAMFILES64
  ${OrIf} $0 == $PROGRAMFILES32
    Abort
  ${EndIf}
FunctionEnd

Section "Uninstall"
  !insertmacro RemoveOwnedShellEntries
  ReadRegStr $0 HKCU "Software\Trie\FrameViewer\Installation" "InstallLocation"
  ${If} $0 == $INSTDIR
    Delete "$SMPROGRAMS\Trie FrameViewer\Trie FrameViewer.lnk"
    Delete "$SMPROGRAMS\Trie FrameViewer\Uninstall.lnk"
    RMDir "$SMPROGRAMS\Trie FrameViewer"
    Delete "$DESKTOP\Trie FrameViewer.lnk"
    DeleteRegKey HKCU "Software\Trie\FrameViewer\Installation"
    DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\TrieFrameViewer"
  ${EndIf}
  !insertmacro UninstallPayload
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
SectionEnd
