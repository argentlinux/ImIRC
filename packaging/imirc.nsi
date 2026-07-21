; ImIRC NSIS installer (built by scripts/package-windows.sh)
; Expects defines: IMIRC_VERSION, IMIRC_STAGE (staging dir with imirc.exe + fonts)

!ifndef IMIRC_VERSION
	!define IMIRC_VERSION "0.2.0"
!endif
!ifndef IMIRC_STAGE
	!error "IMIRC_STAGE must be defined (path to staged portable tree)"
!endif

Name "ImIRC ${IMIRC_VERSION}"
!ifndef IMIRC_OUTFILE
	!define IMIRC_OUTFILE "ImIRC-${IMIRC_VERSION}-windows-x64-setup.exe"
!endif
OutFile "${IMIRC_OUTFILE}"
InstallDir "$PROGRAMFILES64\ImIRC"
InstallDirRegKey HKLM "Software\ImIRC" "InstallDir"
RequestExecutionLevel admin
Unicode true
SetCompressor /SOLID lzma

!include "MUI2.nsh"

!define MUI_ABORTWARNING
!define MUI_ICON "${NSISDIR}\Contrib\Graphics\Icons\modern-install.ico"
!define MUI_UNICON "${NSISDIR}\Contrib\Graphics\Icons\modern-uninstall.ico"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${IMIRC_STAGE}/LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Section "ImIRC" SecApp
	SectionIn RO
	SetOutPath "$INSTDIR"
	File "${IMIRC_STAGE}/imirc.exe"
	File "${IMIRC_STAGE}/LICENSE"
	File /nonfatal "${IMIRC_STAGE}/README.md"
	SetOutPath "$INSTDIR\fonts"
	File /r "${IMIRC_STAGE}/fonts/*.*"

	WriteRegStr HKLM "Software\ImIRC" "InstallDir" "$INSTDIR"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ImIRC" \
		"DisplayName" "ImIRC ${IMIRC_VERSION}"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ImIRC" \
		"UninstallString" "$\"$INSTDIR\Uninstall.exe$\""
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ImIRC" \
		"DisplayVersion" "${IMIRC_VERSION}"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ImIRC" \
		"Publisher" "ImIRC"
	WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ImIRC" \
		"NoModify" 1
	WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ImIRC" \
		"NoRepair" 1

	WriteUninstaller "$INSTDIR\Uninstall.exe"

	CreateDirectory "$SMPROGRAMS\ImIRC"
	CreateShortCut "$SMPROGRAMS\ImIRC\ImIRC.lnk" "$INSTDIR\imirc.exe"
	CreateShortCut "$SMPROGRAMS\ImIRC\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
	CreateShortCut "$DESKTOP\ImIRC.lnk" "$INSTDIR\imirc.exe"
SectionEnd

Section "Uninstall"
	Delete "$DESKTOP\ImIRC.lnk"
	Delete "$SMPROGRAMS\ImIRC\ImIRC.lnk"
	Delete "$SMPROGRAMS\ImIRC\Uninstall.lnk"
	RMDir "$SMPROGRAMS\ImIRC"

	Delete "$INSTDIR\Uninstall.exe"
	RMDir /r "$INSTDIR"

	DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ImIRC"
	DeleteRegKey HKLM "Software\ImIRC"
SectionEnd
