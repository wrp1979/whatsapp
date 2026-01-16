!include "MUI2.nsh"

!ifndef PRODUCT_NAME
  !define PRODUCT_NAME "WhatSie"
!endif
!ifndef PRODUCT_EXE
  !define PRODUCT_EXE "whatsie.exe"
!endif
!ifndef PRODUCT_VERSION
  !define PRODUCT_VERSION "0.0.0.0"
!endif
!ifndef PRODUCT_PUBLISHER
  !define PRODUCT_PUBLISHER "WhatSie"
!endif
!ifndef SOURCE_DIR
  !define SOURCE_DIR "."
!endif
!ifndef OUT_FILE
  !define OUT_FILE "WhatSie-setup.exe"
!endif

Name "${PRODUCT_NAME}"
OutFile "${OUT_FILE}"
InstallDir "$LOCALAPPDATA\\${PRODUCT_NAME}"
InstallDirRegKey HKCU "Software\\${PRODUCT_NAME}" "InstallDir"
RequestExecutionLevel user

VIProductVersion "${PRODUCT_VERSION}"
VIAddVersionKey /LANG=1033 "ProductName" "${PRODUCT_NAME}"
VIAddVersionKey /LANG=1033 "ProductVersion" "${PRODUCT_VERSION}"
VIAddVersionKey /LANG=1033 "CompanyName" "${PRODUCT_PUBLISHER}"
VIAddVersionKey /LANG=1033 "FileDescription" "${PRODUCT_NAME} Installer"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "English"

Section "Install"
  SetOutPath "$INSTDIR"
  File /r "${SOURCE_DIR}\\*.*"
  WriteRegStr HKCU "Software\\${PRODUCT_NAME}" "InstallDir" "$INSTDIR"
  CreateDirectory "$SMPROGRAMS\\${PRODUCT_NAME}"
  CreateShortcut "$SMPROGRAMS\\${PRODUCT_NAME}\\${PRODUCT_NAME}.lnk" "$INSTDIR\\${PRODUCT_EXE}"
  CreateShortcut "$DESKTOP\\${PRODUCT_NAME}.lnk" "$INSTDIR\\${PRODUCT_EXE}"
  WriteUninstaller "$INSTDIR\\Uninstall.exe"
SectionEnd

Section "Uninstall"
  Delete "$DESKTOP\\${PRODUCT_NAME}.lnk"
  Delete "$SMPROGRAMS\\${PRODUCT_NAME}\\${PRODUCT_NAME}.lnk"
  RMDir "$SMPROGRAMS\\${PRODUCT_NAME}"
  DeleteRegKey HKCU "Software\\${PRODUCT_NAME}"
  RMDir /r "$INSTDIR"
SectionEnd
