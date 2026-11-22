@echo off
chcp 65001 >nul
setlocal EnableExtensions EnableDelayedExpansion

set "WSL_UTF8=1"
set "ACTION=%~1"
if "!ACTION!"=="" set "ACTION=install"
set "LANG_ARG=%~2"

set "DISTRO=A6400Deployer"
set "SCRIPT_DIR=%~dp0"
if "!SCRIPT_DIR:~-1!"=="\" set "SCRIPT_DIR=!SCRIPT_DIR:~0,-1!"
for %%I in ("!SCRIPT_DIR!\..") do set "ROOT_DIR=%%~fI"
set "OFFLINE_DIR=!ROOT_DIR!\offline"
set "ROOTFS_TAR=!OFFLINE_DIR!\a6400-alpine-wsl.tar.gz"
set "WSL_FULL_MSI=!OFFLINE_DIR!\wsl.2.4.13.0.x64.msi"
set "WSL_WIN10_MSI=!OFFLINE_DIR!\wsl_update_x64.msi"
set "USBIPD_MSI=!OFFLINE_DIR!\usbipd-win.msi"
set "INSTALL_BASE=!LOCALAPPDATA!\A6400WSL"
set "WATCH_CMD=!SCRIPT_DIR!\usbipd_watch.cmd"
set "RUN_ID=%RANDOM%%RANDOM%"
set "WATCH_LOCK=!TEMP!\a6400_watch_!RUN_ID!.lock"
set "TMP_OUT=!TEMP!\a6400_out_!RUN_ID!.txt"
set "TMP_JSON=!TEMP!\a6400_state_!RUN_ID!.json"
set "TMP_GUIDS=!TEMP!\a6400_guids_!RUN_ID!.txt"
set "NEED_REBOOT=0"
set "WIN_BUILD=0"

reg query "HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion" /v CurrentBuildNumber >"!TMP_OUT!" 2>nul
for /f "tokens=3" %%B in ('findstr /i "CurrentBuildNumber" "!TMP_OUT!" 2^>nul') do set "WIN_BUILD=%%B"
del /f /q "!TMP_OUT!" >nul 2>&1

fltmc >nul 2>&1
if errorlevel 1 (
    echo [!] 請以系統管理員身分執行本腳本（右鍵點擊 .bat 選「以系統管理員身分執行」）。
    exit /b 1
)

if /i "!ACTION!"=="cleanup" goto :do_cleanup

echo ========================================================
echo   Sony A6400 / A6100 — WSL2 極輕量 Linux 一鍵部署（純CMD版）
echo   特點：免 PowerShell 延遲、啟動前全自動體檢補齊環境、
echo         國內純離線防 DISM 卡死、免 Zadig、刷完自動歸還讀卡機！
echo   事前確認：
echo   1. 相機：USB連線 = 大容量儲存、USB LUN = 多重
echo   2. 記憶卡最上層已建好空資料夾 /A6400/TEST/
echo   3. 相機插線開機後靜置 8 秒再繼續
echo ========================================================
echo.

echo [體檢 1/6] 管理員權限與系統版本...
echo   [+] 已取得管理員權限（Windows Build !WIN_BUILD!）

echo [體檢 2/6] 離線部署檔案完整性...
if not exist "!ROOTFS_TAR!" goto :err_missing_rootfs
if not exist "!USBIPD_MSI!" goto :err_missing_usbipd
if exist "!WSL_FULL_MSI!" goto :msg_full_offline
echo   [+] 離線輕量包完整（若系統缺 WSL2 本體，將自動由國內加速鏡像補齊）
goto :check_3

:msg_full_offline
echo   [+] 離線包完整（含 196 MB 微軟官方 WSL2 完整本體，可 100%% 斷網安裝）
goto :check_3

:err_missing_rootfs
echo   [X] 缺少 !ROOTFS_TAR!
echo       請務必把整個 ZIP「完整解壓縮」後再執行，勿在壓縮檔內直接雙擊！
exit /b 1

:err_missing_usbipd
echo   [X] 缺少 !USBIPD_MSI!，請重新完整解壓縮 ZIP。
exit /b 1

:check_3
echo [體檢 3/6] Hypervisor 虛擬化開機參數...
bcdedit /enum {current} >"!TMP_OUT!" 2>nul
findstr /i "hypervisorlaunchtype" "!TMP_OUT!" 2>nul | findstr /i "Off" >nul 2>&1
if not errorlevel 1 goto :fix_hypervisor
del /f /q "!TMP_OUT!" >nul 2>&1
echo   [+] Hypervisor 開機參數正常
goto :check_4

:fix_hypervisor
del /f /q "!TMP_OUT!" >nul 2>&1
echo   [!] 偵測到 hypervisorlaunchtype 被設為 Off（常見於工控軟體/優化工具），正在自動修復...
bcdedit /set hypervisorlaunchtype auto >nul 2>&1
set "NEED_REBOOT=1"
echo   [+] 已修正為 hypervisorlaunchtype auto（待會需重新啟動一次生效）

:check_4
echo [體檢 4/6] Windows 虛擬機器平台（VirtualMachinePlatform / vmcompute）...
sc query vmcompute >nul 2>&1
if not errorlevel 1 goto :vmcompute_exists

echo   [*] 偵測到尚未安裝 vmcompute 服務，正在使用純本機模式離線啟用（避免國內卡在微軟更新）...
net stop wuauserv >nul 2>&1
net stop bits >nul 2>&1
dism.exe /online /enable-feature /featurename:VirtualMachinePlatform /all /LimitAccess /norestart
set "D1=!errorlevel!"
if !D1! NEQ 0 if !D1! NEQ 3010 (
    echo   [!] 純本機模式返回代碼 !D1!，嘗試開啟 Windows Update 補完...
    net start wuauserv >nul 2>&1
    dism.exe /online /enable-feature /featurename:VirtualMachinePlatform /all /norestart
    set "D1=!errorlevel!"
)
dism.exe /online /enable-feature /featurename:Microsoft-Windows-Subsystem-Linux /all /LimitAccess /norestart >nul 2>&1
set "D2=!errorlevel!"
if !D2! NEQ 0 if !D2! NEQ 3010 (
    dism.exe /online /enable-feature /featurename:Microsoft-Windows-Subsystem-Linux /all /norestart >nul 2>&1
    set "D2=!errorlevel!"
)
net start wuauserv >nul 2>&1
if !D1! EQU 0 set "NEED_REBOOT=1"
if !D1! EQU 3010 set "NEED_REBOOT=1"
if !D2! EQU 3010 set "NEED_REBOOT=1"
if !D1! NEQ 0 if !D1! NEQ 3010 goto :err_dism_failed
echo   [+] Windows 虛擬機器平台已離線啟用（將於體檢結束後提示重開機一次）
goto :check_5

:err_dism_failed
echo   [X] 無法啟用 VirtualMachinePlatform（錯誤代碼 !D1!）。
echo       若錯誤為 0x800f080c / 0x800f081f，代表此系統為精簡版或已刪除此元件，
echo       建議改換另一台標準版 Windows 10/11 電腦刷機。
exit /b 1

:vmcompute_exists
sc config vmcompute start= demand >nul 2>&1
net start vmcompute >nul 2>&1
echo   [+] 虛擬機器平台（vmcompute）已安裝

:check_5
echo [體檢 5/6] 微軟官方 WSL2 引擎...
call :find_wsl
set "WSL_OK=0"
if !WIN_BUILD! GEQ 22000 goto :check_5_win11
if exist "!WSL_EXE!" (
    "!WSL_EXE!" --status <nul >nul 2>&1
    if not errorlevel 1 set "WSL_OK=1"
)
goto :check_5_eval

:check_5_win11
if exist "C:\Program Files\WSL\wsl.exe" set "WSL_OK=1"
if "!WSL_OK!"=="0" (
    dir /b "!LOCALAPPDATA!\Packages\MicrosoftCorporationII.WindowsSubsystemForLinux*" >nul 2>&1
    if not errorlevel 1 set "WSL_OK=1"
)

:check_5_eval
if "!WSL_OK!"=="1" goto :wsl_ok_done
echo   [*] 尚未安裝完整 WSL2 引擎，正在自動補裝...
call :ensure_full_wsl_msi
if errorlevel 1 goto :err_wsl_install
if !WIN_BUILD! LSS 22000 if exist "!WSL_WIN10_MSI!" (
    echo   [*] 同步安裝 Win10 WSL2 核心備援套件...
    msiexec.exe /i "!WSL_WIN10_MSI!" /qn /norestart
)
call :find_wsl
echo   [+] WSL2 引擎補裝完成（!WSL_EXE!）
goto :check_6

:err_wsl_install
echo   [X] 補裝 WSL2 引擎失敗。若電腦裝有 360/電腦管家，請先暫時退出安全軟體後重試！
exit /b 1

:wsl_ok_done
echo   [+] WSL2 引擎已就緒（!WSL_EXE!）

:check_6
echo [體檢 6/6] 微軟簽章 usbipd-win USB 透傳服務...
call :find_usbipd
if not "!USBIPD_EXE!"=="" goto :usbipd_ok_done
echo   [*] 尚未安裝 usbipd-win，正在離線安裝...
msiexec.exe /i "!USBIPD_MSI!" /qn /norestart
set "URC=!errorlevel!"
if !URC! NEQ 0 if !URC! NEQ 3010 (
    echo   [!] 靜默安裝被系統/殺毒軟體攔截（rc=!URC!），改以圖形視窗開啟安裝程式，請照畫面點「下一步」...
    msiexec.exe /i "!USBIPD_MSI!"
)
call :find_usbipd
if "!USBIPD_EXE!"=="" goto :err_usbipd_missing
echo   [+] usbipd-win 安裝完成
goto :usbipd_start_svc

:err_usbipd_missing
echo   [X] 仍找不到 usbipd.exe，請先暫時退出 360/殺毒軟體或重開機後再試。
exit /b 1

:usbipd_ok_done
echo   [+] usbipd-win 已就緒（!USBIPD_EXE!）

:usbipd_start_svc
net start usbipd >nul 2>&1

if "!NEED_REBOOT!"=="1" goto :prompt_reboot
goto :check_alpine

:prompt_reboot
echo.
echo ========================================================
echo   [+] 所有缺少的系統環境已全部自動補齊！
echo   [!] Windows 需要「重新啟動一次」讓虛擬機器平台生效。
echo       注意：請點開始功能表的「重新啟動」，不要按「關機」！
echo       重開機後再次雙擊「一鍵開始安裝.bat」即可直接秒刷！
echo ========================================================
exit /b 0

:check_alpine
echo.
echo [*] 正在檢查極輕量 Alpine Linux 環境（!DISTRO!）...
"!WSL_EXE!" --set-default-version 2 <nul >nul 2>&1
set "HAS_DISTRO=0"
"!WSL_EXE!" -l -q <nul >"!TMP_OUT!" 2>nul
for /f "usebackq delims=" %%D in ("!TMP_OUT!") do (
    if /i "%%D"=="!DISTRO!" set "HAS_DISTRO=1"
)
del /f /q "!TMP_OUT!" >nul 2>&1
if "!HAS_DISTRO!"=="1" goto :alpine_ready

echo [*] 正在匯入極輕量 Alpine Linux（!DISTRO!），約需 3~5 秒...
if not exist "!INSTALL_BASE!" mkdir "!INSTALL_BASE!"
"!WSL_EXE!" --import "!DISTRO!" "!INSTALL_BASE!" "!ROOTFS_TAR!" --version 2 <nul
if errorlevel 1 goto :err_import_failed
echo [+] 極輕量 Linux 環境匯入完成
goto :alpine_boot_check

:err_import_failed
echo.
echo ========================================================
echo   [X] 匯入 !DISTRO! 失敗！
echo   常見原因與解法：
echo   1. 剛啟用虛擬機器平台尚未點「重新啟動」-^> 請先重啟一次。
echo   2. 主機板 BIOS 虛擬化（Intel VT-x / AMD SVM）關閉 -^> 請進 BIOS 開啟。
echo   3. 360/電腦管家攔截虛擬化服務 -^> 請暫時退出安全軟體後重試。
echo ========================================================
exit /b 1

:alpine_ready
echo [+] 極輕量 Linux 環境（!DISTRO!）已就緒

:alpine_boot_check
"!WSL_EXE!" -d "!DISTRO!" -- /bin/sh -c "test -e /sys/devices/platform/vhci_hcd.0/status || modprobe vhci-hcd 2>/dev/null || true" <nul
if errorlevel 1 (
    echo [X] 無法啟動 !DISTRO!，請先點「重新啟動」電腦並確認 BIOS 已開啟 VT-x / SVM。
    exit /b 1
)

set "PKG_WSL="
set "HOOK_WSL="
if /i "!ACTION!"=="rollback" goto :start_usb_phase
if /i "!ACTION!"=="diag" goto :start_usb_phase

call :select_package
if errorlevel 1 exit /b 1
call :win_to_wsl "!PKG_WIN!" PKG_WSL
if "!PKG_WSL!"=="" (
    echo [X] 無法轉換開機包路徑至 WSL：!PKG_WIN!
    exit /b 1
)
if exist "!ROOT_DIR!\a6400-autoexec.so" (
    call :win_to_wsl "!ROOT_DIR!\a6400-autoexec.so" HOOK_WSL
)

:start_usb_phase
rem Keep WSL2 VM awake cleanly using marker file + BusyBox timeout (no PID hunting)
start "" /b "!WSL_EXE!" -d "!DISTRO!" -- timeout 600 /bin/sh -c "touch /tmp/.a6400_keepalive; while [ -f /tmp/.a6400_keepalive ]; do sleep 1; done" <nul >nul 2>&1
ping -n 2 127.0.0.1 >nul

echo.
echo [*] 正在偵測 Sony 相機 USB（054c:0ca8 / 0d12 / 0336）並透傳至 Linux...
set "ATTACHED=0"
for /l %%T in (1,1,20) do (
    if "!ATTACHED!"=="0" (
        call :attach_sony_once
        if "!ATTACH_HIT!"=="1" (
            set "ATTACHED=1"
        ) else (
            ping -n 2 127.0.0.1 >nul
        )
    )
)

if "!ATTACHED!"=="0" (
    echo [!] 20 秒內未在 Windows 偵測到 Sony 相機 USB（054c:0ca8 / 0d12 / 0336）。
    echo     請確認：相機已開機、USB 連線 =「大容量儲存」、使用可傳資料的 USB 線。
    set "DEPLOY_RC=1"
    goto :cleanup_session
)

ping -n 2 127.0.0.1 >nul
echo running>"!WATCH_LOCK!"
start "" /b cmd /c call "!WATCH_CMD!" "!USBIPD_EXE!" "!WATCH_LOCK!" >nul 2>&1

echo [*] 已開始在極輕量 Linux 內執行部署（切換服務模式時會自動重新透傳，請勿拔線）...
call :run_python_deployer
set "DEPLOY_RC=!errorlevel!"

if "!DEPLOY_RC!"=="1" (
    echo [*] 相機剛切換至服務模式（054c:0336），等待 Linux 接管後自動重試...
    ping -n 4 127.0.0.1 >nul
    call :attach_sony_once
    ping -n 2 127.0.0.1 >nul
    call :run_python_deployer
    set "DEPLOY_RC=!errorlevel!"
)

:cleanup_session
if exist "!WATCH_LOCK!" del /f /q "!WATCH_LOCK!" >nul 2>&1
ping -n 2 127.0.0.1 >nul
echo [*] 正在解除 USB 透傳與所有殘留綁定，恢復 Windows 正常讀卡機...
call :detach_all_sony
"!WSL_EXE!" -d "!DISTRO!" -- rm -f /tmp/.a6400_keepalive <nul >nul 2>&1
del /f /q "!TMP_OUT!" "!TMP_JSON!" "!TMP_GUIDS!" >nul 2>&1

if "!DEPLOY_RC!"=="0" (
    echo.
    echo ========================================================
    echo   [+] 部署成功！相機已自動解除透傳、恢復 Windows 正常讀卡機。
    echo   下一步：拔線 -^> 關機扣電池 10 秒 -^> 開機等 35~40 秒 -^>
    echo           輕觸螢幕鬆開一次 -^> 等 2 秒 -^> 長按 MENU+放大鏡 2 秒！
    echo ========================================================
)
exit /b !DEPLOY_RC!

:run_python_deployer
if /i "!ACTION!"=="rollback" (
    "!WSL_EXE!" -d "!DISTRO!" -- python3 -u /opt/a6400/pmca_deploy_v3_clite.py --pmca-dir /opt/pmca-re --safe-serial --yes --rollback
    exit /b !errorlevel!
)
if /i "!ACTION!"=="diag" (
    "!WSL_EXE!" -d "!DISTRO!" -- python3 -u /opt/a6400/pmca_deploy_v3_clite.py --pmca-dir /opt/pmca-re --safe-serial --yes --diag
    exit /b !errorlevel!
)
if not "!HOOK_WSL!"=="" (
    "!WSL_EXE!" -d "!DISTRO!" -- python3 -u /opt/a6400/pmca_deploy_v3_clite.py --pmca-dir /opt/pmca-re --safe-serial --yes "!PKG_WSL!" --install-hook "!HOOK_WSL!"
    exit /b !errorlevel!
)
"!WSL_EXE!" -d "!DISTRO!" -- python3 -u /opt/a6400/pmca_deploy_v3_clite.py --pmca-dir /opt/pmca-re --safe-serial --yes "!PKG_WSL!" --auto-hook-stub /opt/a6400/hook-stub.bin
exit /b !errorlevel!

:do_cleanup
echo [*] 正在清理 Sony USB 透傳與極輕量 Linux 環境（!DISTRO!）...
call :find_usbipd
call :find_wsl
if not "!USBIPD_EXE!"=="" call :detach_all_sony
if not "!WSL_EXE!"=="" (
    "!WSL_EXE!" --unregister "!DISTRO!" <nul >nul 2>&1
)
if exist "!INSTALL_BASE!" rmdir /s /q "!INSTALL_BASE!" >nul 2>&1
del /f /q "!TMP_OUT!" "!TMP_JSON!" "!TMP_GUIDS!" >nul 2>&1
echo [+] 已清理完畢，不佔用硬碟空間。
exit /b 0

:find_wsl
set "WSL_EXE="
if exist "C:\Program Files\WSL\wsl.exe" (
    set "WSL_EXE=C:\Program Files\WSL\wsl.exe"
    exit /b 0
)
if exist "%ProgramFiles%\WSL\wsl.exe" (
    set "WSL_EXE=%ProgramFiles%\WSL\wsl.exe"
    exit /b 0
)
if exist "C:\Windows\System32\wsl.exe" (
    set "WSL_EXE=C:\Windows\System32\wsl.exe"
    exit /b 0
)
exit /b 0

:find_usbipd
set "USBIPD_EXE="
if exist "C:\Program Files\usbipd-win\usbipd.exe" (
    set "USBIPD_EXE=C:\Program Files\usbipd-win\usbipd.exe"
    exit /b 0
)
if exist "%ProgramFiles%\usbipd-win\usbipd.exe" (
    set "USBIPD_EXE=%ProgramFiles%\usbipd-win\usbipd.exe"
    exit /b 0
)
where usbipd.exe >"!TMP_OUT!" 2>nul
for /f "usebackq delims=" %%P in ("!TMP_OUT!") do (
    if "!USBIPD_EXE!"=="" set "USBIPD_EXE=%%P"
)
del /f /q "!TMP_OUT!" >nul 2>&1
exit /b 0

:ensure_full_wsl_msi
if exist "!WSL_FULL_MSI!" goto :install_full_wsl_msi
echo   [*] 輕量版偵測到需下載微軟官方 WSL2 本體（約 196 MB，下載後會快取於 offline\）...
set "DL_OK=0"
for %%U in (
    "https://gh-proxy.com/https://github.com/microsoft/WSL/releases/download/2.4.13/wsl.2.4.13.0.x64.msi"
    "https://ghfast.top/https://github.com/microsoft/WSL/releases/download/2.4.13/wsl.2.4.13.0.x64.msi"
    "https://ghproxy.net/https://github.com/microsoft/WSL/releases/download/2.4.13/wsl.2.4.13.0.x64.msi"
    "https://github.com/microsoft/WSL/releases/download/2.4.13/wsl.2.4.13.0.x64.msi"
) do (
    if "!DL_OK!"=="0" (
        echo   [*] 嘗試鏡像：%%~U
        del /f /q "!WSL_FULL_MSI!.part" >nul 2>&1
        curl.exe -fL --connect-timeout 15 --max-time 600 -# -o "!WSL_FULL_MSI!.part" "%%~U"
        if not errorlevel 1 (
            for %%S in ("!WSL_FULL_MSI!.part") do (
                if %%~zS GTR 100000000 (
                    move /y "!WSL_FULL_MSI!.part" "!WSL_FULL_MSI!" >nul
                    set "DL_OK=1"
                )
            )
        )
        if "!DL_OK!"=="0" del /f /q "!WSL_FULL_MSI!.part" >nul 2>&1
    )
)
if "!DL_OK!"=="0" exit /b 1

:install_full_wsl_msi
echo   [*] 正在安裝微軟官方簽章 WSL2 本體（約 15~30 秒）...
msiexec.exe /i "!WSL_FULL_MSI!" /qn /norestart
set "MRC=!errorlevel!"
if !MRC! EQU 3010 set "NEED_REBOOT=1"
if !MRC! EQU 0 exit /b 0
if !MRC! EQU 3010 exit /b 0
echo   [!] 靜默安裝返回代碼 !MRC!（可能被 360/殺毒軟體攔截），改以圖形視窗開啟，請點「下一步」完成...
msiexec.exe /i "!WSL_FULL_MSI!"
if exist "C:\Program Files\WSL\wsl.exe" exit /b 0
exit /b 1

:select_package
set "PKG_WIN="
set "TC_DIR=!ROOT_DIR!\local-boot-package-tc"
set "SC_DIR=!ROOT_DIR!\local-boot-package-sc"
set "SINGLE_DIR=!ROOT_DIR!\local-boot-package"
if /i "!LANG_ARG!"=="sc" if exist "!SC_DIR!" (
    set "PKG_WIN=!SC_DIR!"
    exit /b 0
)
if /i "!LANG_ARG!"=="tc" if exist "!TC_DIR!" (
    set "PKG_WIN=!TC_DIR!"
    exit /b 0
)
if exist "!TC_DIR!" if exist "!SC_DIR!" goto :ask_lang_prompt
if exist "!TC_DIR!" (
    set "PKG_WIN=!TC_DIR!"
    exit /b 0
)
if exist "!SC_DIR!" (
    set "PKG_WIN=!SC_DIR!"
    exit /b 0
)
if exist "!SINGLE_DIR!" (
    set "PKG_WIN=!SINGLE_DIR!"
    exit /b 0
)
echo [X] 找不到開機套件目錄（local-boot-package-tc / local-boot-package-sc）
exit /b 1

:ask_lang_prompt
echo.
echo 請選擇要安裝的介面語言版本（務必與相機系統語言一致）：
echo   [1] 繁體中文版（相機系統語言請設為「繁體中文」）
echo   [2] 簡體中文版（相機系統語言請設為「简体中文」）
:ask_lang_loop
set "ANS="
set /p "ANS=請輸入 1 或 2（直接按 Enter 預設 1 繁體中文）: "
if "!ANS!"=="" set "ANS=1"
if /i "!ANS!"=="1" goto :pick_tc
if /i "!ANS!"=="tc" goto :pick_tc
if /i "!ANS!"=="2" goto :pick_sc
if /i "!ANS!"=="sc" goto :pick_sc
echo 輸入無效，請輸入 1 或 2。
goto :ask_lang_loop

:pick_tc
echo [*] 已選擇：繁體中文版
set "PKG_WIN=!TC_DIR!"
exit /b 0

:pick_sc
echo [*] 已選擇：簡體中文版
set "PKG_WIN=!SC_DIR!"
exit /b 0

:win_to_wsl
set "_WIN_IN=%~1"
set "_OUT_VAR=%~2"
set "!_OUT_VAR!="
del /f /q "!TMP_OUT!" >nul 2>&1
"!WSL_EXE!" -d "!DISTRO!" -- python3 -c "import sys; p=sys.argv[1].replace(chr(92),'/'); print('/mnt/'+p[0].lower()+p[2:] if len(p)>2 and p[1]==':' else '')" "!_WIN_IN!" <nul >"!TMP_OUT!" 2>nul
if exist "!TMP_OUT!" (
    for /f "usebackq delims=" %%W in ("!TMP_OUT!") do set "!_OUT_VAR!=%%W"
    del /f /q "!TMP_OUT!" >nul 2>&1
)
exit /b 0

:attach_sony_once
set "ATTACH_HIT=0"
"!USBIPD_EXE!" list >"!TMP_OUT!" 2>nul
for /f "usebackq tokens=1,2" %%A in ("!TMP_OUT!") do (
    set "_VP=%%B"
    set "_HIT=0"
    if /i "!_VP!"=="054c:0ca8" set "_HIT=1"
    if /i "!_VP!"=="054c:0d12" set "_HIT=1"
    if /i "!_VP!"=="054c:0336" set "_HIT=1"
    if "!_HIT!"=="1" (
        "!USBIPD_EXE!" bind --force --busid %%A >nul 2>&1
        "!USBIPD_EXE!" attach --wsl --busid %%A >nul 2>&1
        if not errorlevel 1 (
            echo [+] 已透過 usbipd 將相機（%%B, bus %%A）透傳至 Linux
            set "ATTACH_HIT=1"
        ) else (
            "!USBIPD_EXE!" list 2>nul | findstr /i "%%A" | findstr /i "Attached" >nul 2>&1 && set "ATTACH_HIT=1"
        )
    )
)
del /f /q "!TMP_OUT!" >nul 2>&1
exit /b 0

:detach_all_sony
"!USBIPD_EXE!" list >"!TMP_OUT!" 2>nul
for /f "usebackq tokens=1,2" %%A in ("!TMP_OUT!") do (
    set "_VP=%%B"
    set "_HIT=0"
    if /i "!_VP!"=="054c:0ca8" set "_HIT=1"
    if /i "!_VP!"=="054c:0d12" set "_HIT=1"
    if /i "!_VP!"=="054c:0336" set "_HIT=1"
    if "!_HIT!"=="1" (
        "!USBIPD_EXE!" detach --busid %%A >nul 2>&1
        "!USBIPD_EXE!" unbind --busid %%A >nul 2>&1
    )
)
del /f /q "!TMP_OUT!" >nul 2>&1
if "!WSL_EXE!"=="" exit /b 0
"!USBIPD_EXE!" state >"!TMP_JSON!" 2>nul
if not exist "!TMP_JSON!" exit /b 0
"!WSL_EXE!" -d "!DISTRO!" -- python3 -c "import sys,json,re; raw=sys.stdin.read().strip(); d=json.loads(raw) if raw else {}; items=d.get('Devices',d) if isinstance(d,dict) else d; [print(x['PersistedGuid']) for x in items if isinstance(x,dict) and x.get('PersistedGuid') and re.search(r'VID_054C&PID_(0CA8|0D12|0336)',str(x.get('InstanceId','')),re.I)]" <"!TMP_JSON!" >"!TMP_GUIDS!" 2>nul
if exist "!TMP_GUIDS!" (
    for /f "usebackq delims=" %%G in ("!TMP_GUIDS!") do (
        "!USBIPD_EXE!" unbind --guid %%G >nul 2>&1
    )
)
del /f /q "!TMP_JSON!" "!TMP_GUIDS!" >nul 2>&1
exit /b 0
