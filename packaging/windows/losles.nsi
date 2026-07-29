# -*- coding: utf-8 -*-
#
# Build with APP_VERSION, APP_FILE_VERSION, PAYLOAD_DIR, OUTPUT_FILE, and
# INSTALLER_ICON supplied on the makensis command line.

!ifndef APP_VERSION
  !error "APP_VERSION must be defined"
!endif
!ifndef APP_FILE_VERSION
  !error "APP_FILE_VERSION must be defined"
!endif
!ifndef PAYLOAD_DIR
  !error "PAYLOAD_DIR must be defined"
!endif
!ifndef OUTPUT_FILE
  !error "OUTPUT_FILE must be defined"
!endif
!ifndef INSTALLER_ICON
  !error "INSTALLER_ICON must be defined"
!endif

!include "FileFunc.nsh"
!include "LogicLib.nsh"
!include "MUI2.nsh"
!include "WinVer.nsh"
!include "x64.nsh"

!define APP_NAME "losles"
!define APP_ID "io.github.develancer.losles"
!define APP_DESCRIPTION "Color-managed lossless photo viewer"
!define APP_PUBLISHER "Piotr T. Różański"
!define APP_URL "https://github.com/develancer/losles"
!define APP_EXE "bin\losles.exe"
!define APP_INSTALL_MARKER ".io.github.develancer.losles-install-root"
!define APP_PROGID "${APP_ID}.Image"
!define APP_REGISTRY_KEY "Software\${APP_ID}"
!define APP_CAPABILITIES_KEY "${APP_REGISTRY_KEY}\Capabilities"
!define APP_PATHS_KEY \
  "Software\Microsoft\Windows\CurrentVersion\App Paths\losles.exe"
!define APP_UNINSTALL_KEY \
  "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_ID}"

Unicode true
Name "${APP_NAME}"
OutFile "${OUTPUT_FILE}"
InstallDir "$LOCALAPPDATA\Programs\${APP_NAME}"
InstallDirRegKey HKCU "${APP_UNINSTALL_KEY}" "InstallLocation"
RequestExecutionLevel user
SetCompressor /SOLID lzma
SetCompressorDictSize 32
SetDatablockOptimize on
CRCCheck force
ShowInstDetails show
ShowUninstDetails show

VIProductVersion "${APP_FILE_VERSION}"
VIFileVersion "${APP_FILE_VERSION}"
VIAddVersionKey /LANG=1033 "ProductName" "${APP_NAME}"
VIAddVersionKey /LANG=1033 "ProductVersion" "${APP_VERSION}"
VIAddVersionKey /LANG=1033 "FileVersion" "${APP_VERSION}"
VIAddVersionKey /LANG=1033 "FileDescription" "${APP_DESCRIPTION} installer"
VIAddVersionKey /LANG=1033 "CompanyName" "${APP_PUBLISHER}"
VIAddVersionKey /LANG=1033 "LegalCopyright" \
  "Copyright © 2026 ${APP_PUBLISHER}"

!define MUI_ABORTWARNING
!define MUI_ICON "${INSTALLER_ICON}"
!define MUI_UNICON "${INSTALLER_ICON}"
!define MUI_FINISHPAGE_RUN "$INSTDIR\${APP_EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "Run ${APP_NAME}"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "COPYING.rtf"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Function .onInit
  SetRegView 64
  ${IfNot} ${AtLeastWin10}
    MessageBox MB_OK|MB_ICONSTOP \
      "${APP_NAME} requires Windows 10 or later."
    Abort
  ${EndIf}
  ${IfNot} ${RunningX64}
    MessageBox MB_OK|MB_ICONSTOP \
      "${APP_NAME} requires a 64-bit version of Windows."
    Abort
  ${EndIf}
FunctionEnd

Function un.onInit
  SetRegView 64
FunctionEnd

Section "Install ${APP_NAME}" SEC_APPLICATION
  SectionIn RO
  SetShellVarContext current
  SetOverwrite on
  SetOutPath "$INSTDIR"
  File /r "${PAYLOAD_DIR}\*"
  WriteUninstaller "$INSTDIR\uninstall.exe"
  FileOpen $0 "$INSTDIR\${APP_INSTALL_MARKER}" w
  FileWrite $0 "${APP_ID}$\r$\n"
  FileClose $0

  CreateShortcut "$SMPROGRAMS\${APP_NAME}.lnk" \
    "$INSTDIR\${APP_EXE}" "" "$INSTDIR\${APP_EXE}" 0

  WriteRegStr HKCU "${APP_PATHS_KEY}" "" "$INSTDIR\${APP_EXE}"
  WriteRegStr HKCU "${APP_PATHS_KEY}" "Path" "$INSTDIR\bin"

  WriteRegStr HKCU "Software\Classes\${APP_PROGID}" "" "Image file"
  WriteRegStr HKCU "Software\Classes\${APP_PROGID}" \
    "FriendlyTypeName" "Image file"
  WriteRegStr HKCU "Software\Classes\${APP_PROGID}\DefaultIcon" "" \
    '"$INSTDIR\${APP_EXE}",0'
  WriteRegStr HKCU \
    "Software\Classes\${APP_PROGID}\shell\open\command" "" \
    '"$INSTDIR\${APP_EXE}" "%1"'

  WriteRegStr HKCU \
    "Software\Classes\Applications\losles.exe\SupportedTypes" ".jpg" ""
  WriteRegStr HKCU \
    "Software\Classes\Applications\losles.exe\SupportedTypes" ".jpeg" ""
  WriteRegStr HKCU \
    "Software\Classes\Applications\losles.exe\SupportedTypes" ".jpe" ""
  WriteRegStr HKCU \
    "Software\Classes\Applications\losles.exe\SupportedTypes" ".png" ""
  WriteRegStr HKCU "Software\Classes\Applications\losles.exe" \
    "FriendlyAppName" "${APP_NAME}"
  WriteRegStr HKCU \
    "Software\Classes\Applications\losles.exe\DefaultIcon" "" \
    '"$INSTDIR\${APP_EXE}",0'
  WriteRegStr HKCU \
    "Software\Classes\Applications\losles.exe\shell\open\command" "" \
    '"$INSTDIR\${APP_EXE}" "%1"'

  WriteRegStr HKCU "${APP_CAPABILITIES_KEY}" \
    "ApplicationName" "${APP_NAME}"
  WriteRegStr HKCU "${APP_CAPABILITIES_KEY}" \
    "ApplicationDescription" "${APP_DESCRIPTION}"
  WriteRegStr HKCU "${APP_CAPABILITIES_KEY}" \
    "ApplicationIcon" "$INSTDIR\${APP_EXE},0"
  WriteRegStr HKCU "${APP_CAPABILITIES_KEY}\FileAssociations" \
    ".jpg" "${APP_PROGID}"
  WriteRegStr HKCU "${APP_CAPABILITIES_KEY}\FileAssociations" \
    ".jpeg" "${APP_PROGID}"
  WriteRegStr HKCU "${APP_CAPABILITIES_KEY}\FileAssociations" \
    ".jpe" "${APP_PROGID}"
  WriteRegStr HKCU "${APP_CAPABILITIES_KEY}\FileAssociations" \
    ".png" "${APP_PROGID}"
  WriteRegStr HKCU "Software\RegisteredApplications" \
    "${APP_NAME}" "${APP_CAPABILITIES_KEY}"

  WriteRegStr HKCU "${APP_UNINSTALL_KEY}" \
    "DisplayName" "${APP_NAME}"
  WriteRegStr HKCU "${APP_UNINSTALL_KEY}" \
    "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKCU "${APP_UNINSTALL_KEY}" \
    "DisplayIcon" "$INSTDIR\${APP_EXE},0"
  WriteRegStr HKCU "${APP_UNINSTALL_KEY}" \
    "Publisher" "${APP_PUBLISHER}"
  WriteRegStr HKCU "${APP_UNINSTALL_KEY}" \
    "URLInfoAbout" "${APP_URL}"
  WriteRegStr HKCU "${APP_UNINSTALL_KEY}" \
    "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${APP_UNINSTALL_KEY}" \
    "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKCU "${APP_UNINSTALL_KEY}" \
    "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
  WriteRegDWORD HKCU "${APP_UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${APP_UNINSTALL_KEY}" "NoRepair" 1

  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  WriteRegDWORD HKCU "${APP_UNINSTALL_KEY}" "EstimatedSize" $0

  System::Call "shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)"
SectionEnd

Section "Uninstall"
  SetShellVarContext current

  IfFileExists "$INSTDIR\${APP_INSTALL_MARKER}" uninstall_owned_directory
  MessageBox MB_OK|MB_ICONSTOP \
    "The losles installation marker is missing. Files were not removed." \
    /SD IDOK
  Abort

uninstall_owned_directory:
  Delete "$SMPROGRAMS\${APP_NAME}.lnk"

  DeleteRegKey HKCU "${APP_UNINSTALL_KEY}"
  DeleteRegKey HKCU "${APP_PATHS_KEY}"
  DeleteRegValue HKCU "Software\RegisteredApplications" "${APP_NAME}"
  DeleteRegKey HKCU "${APP_REGISTRY_KEY}"
  DeleteRegKey HKCU "Software\Classes\Applications\losles.exe"
  DeleteRegKey HKCU "Software\Classes\${APP_PROGID}"

  RMDir /r "$INSTDIR"

  System::Call "shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)"
SectionEnd
