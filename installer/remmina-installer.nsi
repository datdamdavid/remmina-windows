; Remmina Windows NSIS Installer Script
; Modern UI 2 based installer

!include "MUI2.nsh"
!include "FileFunc.nsh"

Unicode True

; General Configuration
Name "Remmina Remote Desktop Client"
OutFile "..\dist\Remmina-Setup-v1.4.43-x64.exe"
InstallDir "$PROGRAMFILES64\Remmina"
InstallDirRegKey HKLM "Software\Remmina" "InstallDir"
RequestExecutionLevel admin

; Interface Configuration
!define MUI_ICON "..\src\remmina.ico"
!define MUI_UNICON "..\src\remmina.ico"
!define MUI_ABORTWARNING

; Welcome page
!insertmacro MUI_PAGE_WELCOME

; License / Info page (optional, skip or use directory)
!insertmacro MUI_PAGE_DIRECTORY

; Components page
!insertmacro MUI_PAGE_COMPONENTS

; Instfiles page
!insertmacro MUI_PAGE_INSTFILES

; Finish page
!define MUI_FINISHPAGE_RUN "$INSTDIR\remmina.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Launch Remmina"
!insertmacro MUI_PAGE_FINISH

; Uninstaller pages
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; Languages
!insertmacro MUI_LANGUAGE "English"

; Sections
Section "Remmina Core (Required)" SecCore
    SectionIn RO
    SetOutPath "$INSTDIR"
    
    ; Copy all runtime files, excluding any temporary .old files
    File /r /x "*.old" "..\dist\remmina-win64\*.*"
    
    ; Write registry keys for install dir
    WriteRegStr HKLM "Software\Remmina" "InstallDir" "$INSTDIR"
    
    ; Write Uninstaller
    WriteUninstaller "$INSTDIR\Uninstall.exe"
    
    ; Add to Add/Remove Programs
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Remmina" "DisplayName" "Remmina Remote Desktop Client"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Remmina" "DisplayIcon" "$INSTDIR\remmina.exe,0"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Remmina" "DisplayVersion" "1.4.43"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Remmina" "Publisher" "Remmina Community"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Remmina" "URLInfoAbout" "https://github.com/datdamdavid/remmina-windows"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Remmina" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Remmina" "NoModify" 1
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Remmina" "NoRepair" 1
SectionEnd

Section "Start Menu Shortcut" SecStartMenu
    CreateDirectory "$SMPROGRAMS\Remmina"
    CreateShortcut "$SMPROGRAMS\Remmina\Remmina.lnk" "$INSTDIR\remmina.exe" "" "$INSTDIR\remmina.exe" 0
    CreateShortcut "$SMPROGRAMS\Remmina\Uninstall.lnk" "$INSTDIR\Uninstall.exe" "" "$INSTDIR\Uninstall.exe" 0
SectionEnd

Section "Desktop Shortcut" SecDesktop
    CreateShortcut "$DESKTOP\Remmina.lnk" "$INSTDIR\remmina.exe" "" "$INSTDIR\remmina.exe" 0
SectionEnd

Section "Associate .remmina profile files" SecAssoc
    WriteRegStr HKCR ".remmina" "" "Remmina.ConnectionProfile"
    WriteRegStr HKCR "Remmina.ConnectionProfile" "" "Remmina Connection Profile"
    WriteRegStr HKCR "Remmina.ConnectionProfile\DefaultIcon" "" "$INSTDIR\remmina.exe,0"
    WriteRegStr HKCR "Remmina.ConnectionProfile\shell\open\command" "" '"$INSTDIR\remmina.exe" -c "%1"'
SectionEnd

; Descriptions
LangString DESC_SecCore ${LANG_ENGLISH} "Core Remmina binaries, GTK3 runtime, and FreeRDP plugin."
LangString DESC_SecStartMenu ${LANG_ENGLISH} "Add shortcuts to the Windows Start Menu."
LangString DESC_SecDesktop ${LANG_ENGLISH} "Add a shortcut on the Desktop."
LangString DESC_SecAssoc ${LANG_ENGLISH} "Associate .remmina profile files to automatically open in Remmina."

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecCore} $(DESC_SecCore)
  !insertmacro MUI_DESCRIPTION_TEXT ${SecStartMenu} $(DESC_SecStartMenu)
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} $(DESC_SecDesktop)
  !insertmacro MUI_DESCRIPTION_TEXT ${SecAssoc} $(DESC_SecAssoc)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; Uninstaller Section
Section "Uninstall"
    ; Remove shortcuts
    Delete "$DESKTOP\Remmina.lnk"
    Delete "$SMPROGRAMS\Remmina\Remmina.lnk"
    Delete "$SMPROGRAMS\Remmina\Uninstall.lnk"
    RMDir "$SMPROGRAMS\Remmina"
    
    ; Remove file association
    DeleteRegKey HKCR ".remmina"
    DeleteRegKey HKCR "Remmina.ConnectionProfile"
    
    ; Remove Add/Remove Programs keys
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Remmina"
    DeleteRegKey HKLM "Software\Remmina"
    
    ; Remove installed files
    RMDir /r "$INSTDIR"
SectionEnd
