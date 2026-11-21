param(
    [ValidateSet("install", "rollback", "diag", "cleanup")]
    [string]$Action = "install",
    [ValidateSet("", "tc", "sc")]
    [string]$Lang = ""
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8

$DistroName = "A6400Deployer"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Split-Path -Parent $ScriptDir
$OfflineDir = Join-Path $RootDir "offline"
$RootfsTar = Join-Path $OfflineDir "a6400-alpine-wsl.tar.gz"
$WslMsi = Join-Path $OfflineDir "wsl_update_x64.msi"
$UsbipdMsi = Join-Path $OfflineDir "usbipd-win.msi"
$InstallBase = Join-Path $env:LOCALAPPDATA "A6400WSL"

function Test-IsAdmin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $p = New-Object Security.Principal.WindowsPrincipal($id)
    return $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Find-UsbipdExe {
    $cmd = Get-Command usbipd.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $candidates = @(
        "C:\Program Files\usbipd-win\usbipd.exe",
        (Join-Path $env:ProgramFiles "usbipd-win\usbipd.exe")
    )
    foreach ($c in $candidates) {
        if ($c -and (Test-Path $c)) { return $c }
    }
    return $null
}

function Convert-ToWslPath([string]$WinPath) {
    $full = (Resolve-Path $WinPath).Path
    if ($full -match '^([A-Za-z]):\\(.*)$') {
        $drive = $Matches[1].ToLower()
        $rest = $Matches[2] -replace '\\', '/'
        return "/mnt/$drive/$rest"
    }
    throw "無法轉換 Windows 路徑至 WSL：$full"
}

function Get-WslDistros {
    $raw = & wsl.exe -l -q 2>$null
    if (-not $raw) { return @() }
    $list = @()
    foreach ($line in $raw) {
        $clean = ($line -replace "`0", "").Trim()
        if ($clean) { $list += $clean }
    }
    return $list
}

function Ensure-Wsl2Offline {
    Write-Host "[*] 檢查 Windows WSL2 環境..."
    $wslOk = $false
    try {
        & wsl.exe --status *> $null
        if ($LASTEXITCODE -eq 0) { $wslOk = $true }
    } catch {}

    if (-not $wslOk) {
        Write-Host "[*] 尚未啟用 WSL2，正在使用 Windows 本機離線功能開啟 WSL2 與虛擬機器平台（免聯網）..."
        $needReboot = $false
        foreach ($feat in @("Microsoft-Windows-Subsystem-Linux", "VirtualMachinePlatform")) {
            $proc = Start-Process -FilePath "dism.exe" -ArgumentList "/online /enable-feature /featurename:$feat /all /norestart" -Wait -PassThru -NoNewWindow
            if ($proc.ExitCode -eq 3010) {
                $needReboot = $true
            } elseif ($proc.ExitCode -ne 0) {
                Write-Host "[!] 啟用 Windows 功能 $feat 返回代碼 $($proc.ExitCode)，請確認 BIOS 已開啟虛擬化（VT-x / SVM）。" -ForegroundColor Yellow
            }
        }
        if (Test-Path $WslMsi) {
            Write-Host "[*] 正在離線安裝微軟官方 WSL2 核心套件 (wsl_update_x64.msi)..."
            $p2 = Start-Process -FilePath "msiexec.exe" -ArgumentList "/i `"$WslMsi`" /qn /norestart" -Wait -PassThru
            if ($p2.ExitCode -eq 3010) { $needReboot = $true }
        }
        if ($needReboot) {
            Write-Host ""
            Write-Host "========================================================" -ForegroundColor Green
            Write-Host "  [+] 已完成離線啟用 WSL2 與安裝微軟 WSL2 核心！" -ForegroundColor Green
            Write-Host "  [!] Windows 需要「重新開機一次」讓虛擬機器平台生效。" -ForegroundColor Yellow
            Write-Host "  請重新開機後，再次雙擊「一鍵開始安裝.bat」即可直接刷機！" -ForegroundColor Yellow
            Write-Host "========================================================" -ForegroundColor Green
            exit 0
        }
        try { & wsl.exe --set-default-version 2 *> $null } catch {}
    } else {
        Write-Host "[+] WSL2 已就緒"
    }
}

function Ensure-UsbipdOffline {
    $exe = Find-UsbipdExe
    if ($exe) {
        Write-Host "[+] usbipd-win 已安裝 ($exe)"
    } else {
        if (-not (Test-Path $UsbipdMsi)) {
            throw "找不到離線安裝檔：$UsbipdMsi"
        }
        Write-Host "[*] 正在離線安裝微軟簽章 usbipd-win (免聯網、免 Zadig)..."
        $p = Start-Process -FilePath "msiexec.exe" -ArgumentList "/i `"$UsbipdMsi`" /qn /norestart" -Wait -PassThru
        if ($p.ExitCode -ne 0 -and $p.ExitCode -ne 3010) {
            throw "usbipd-win 安裝失敗 (rc=$($p.ExitCode))"
        }
        Start-Sleep -Seconds 2
        $exe = Find-UsbipdExe
        if (-not $exe) {
            throw "usbipd-win 安裝後仍找不到 usbipd.exe，請重新開機後再試。"
        }
        Write-Host "[+] usbipd-win 離線安裝完成"
    }
    try {
        $svc = Get-Service -Name "usbipd" -ErrorAction SilentlyContinue
        if ($svc -and $svc.Status -ne "Running") {
            Start-Service -Name "usbipd" -ErrorAction SilentlyContinue
        }
    } catch {}
    return $exe
}

function Ensure-AlpineDistro {
    $distros = Get-WslDistros
    if ($distros -notcontains $DistroName) {
        if (-not (Test-Path $RootfsTar)) {
            throw "找不到極輕量 Alpine 系統映像：$RootfsTar"
        }
        Write-Host "[*] 正在匯入極輕量 Alpine Linux 部署環境 ($DistroName)，約需 3~5 秒..."
        New-Item -ItemType Directory -Force -Path $InstallBase | Out-Null
        & wsl.exe --import $DistroName $InstallBase $RootfsTar --version 2
        if ($LASTEXITCODE -ne 0) {
            if (Test-Path $WslMsi) {
                Write-Host "[*] 嘗試補裝離線 WSL2 核心套件後重試匯入..."
                Start-Process -FilePath "msiexec.exe" -ArgumentList "/i `"$WslMsi`" /qn /norestart" -Wait | Out-Null
                & wsl.exe --import $DistroName $InstallBase $RootfsTar --version 2
            }
            if ($LASTEXITCODE -ne 0) {
                throw "匯入 $DistroName 失敗。若電腦剛開啟 WSL2，請先重新開機一次並確認 BIOS 已開啟虛擬化（VT-x / SVM）。"
            }
        }
        Write-Host "[+] 極輕量 Linux 環境匯入完成"
    } else {
        Write-Host "[+] 偵測到極輕量 Linux 環境 ($DistroName) 已就緒"
    }

    # Boot lightweight WSL2 VM and verify USB/IP vhci_hcd kernel support
    & wsl.exe -d $DistroName -- /bin/sh -c "test -e /sys/devices/platform/vhci_hcd.0/status || modprobe vhci-hcd 2>/dev/null || true"
    if ($LASTEXITCODE -ne 0) {
        throw "無法啟動 $DistroName，請確認電腦已重開機並開啟 BIOS 虛擬化（VT-x / SVM）。"
    }
}

function Parse-SonyUsbipdDevices([string]$UsbipdExe) {
    $lines = & $UsbipdExe list 2>$null
    $found = @()
    foreach ($line in $lines) {
        if ($line -match '^\s*(\d+-\d+(?:\.\d+)*)\s+054c:(0ca8|0d12|0336)\s+(.*?)\s{2,}(Not shared|Shared(?:\s*\(forced\))?|Attached)\s*$') {
            $found += [PSCustomObject]@{
                BusId = $Matches[1]
                Pid   = $Matches[2].ToLower()
                Desc  = $Matches[3].Trim()
                State = $Matches[4].Trim()
            }
        }
    }
    return $found
}

function Attach-SonyDevicesOnce([string]$UsbipdExe) {
    $devs = Parse-SonyUsbipdDevices $UsbipdExe
    $attachedAny = $false
    foreach ($d in $devs) {
        if ($d.State -eq "Not shared") {
            & $UsbipdExe bind --force --busid $d.BusId *> $null
        }
        if ($d.State -ne "Attached") {
            & $UsbipdExe attach --wsl --busid $d.BusId *> $null
            if ($LASTEXITCODE -eq 0) {
                Write-Host "[+] 已透過 usbipd 將相機 (054c:$($d.Pid), bus $($d.BusId)) 透傳至 Linux"
                $attachedAny = $true
            }
        } else {
            $attachedAny = $true
        }
    }
    return ($devs.Count -gt 0 -and $attachedAny)
}

function Detach-SonyDevices([string]$UsbipdExe) {
    try {
        $devs = Parse-SonyUsbipdDevices $UsbipdExe
        foreach ($d in $devs) {
            if ($d.State -eq "Attached") {
                & $UsbipdExe detach --busid $d.BusId *> $null
            }
            if ($d.State -ne "Not shared") {
                & $UsbipdExe unbind --busid $d.BusId *> $null
            }
        }
    } catch {}
}

function Select-PackageDir([string]$LangChoice) {
    $tc = Join-Path $RootDir "local-boot-package-tc"
    $sc = Join-Path $RootDir "local-boot-package-sc"
    $single = Join-Path $RootDir "local-boot-package"

    if ($LangChoice -eq "sc" -and (Test-Path $sc)) { return $sc }
    if ($LangChoice -eq "tc" -and (Test-Path $tc)) { return $tc }

    if ((Test-Path $tc) -and (Test-Path $sc)) {
        Write-Host ""
        Write-Host "請選擇要安裝的介面語言版本（務必與相機系統語言一致）："
        Write-Host "  [1] 繁體中文版（相機系統語言請設為「繁體中文」）"
        Write-Host "  [2] 簡體中文版（相機系統語言請設為「简体中文」）"
        while ($true) {
            $ans = (Read-Host "請輸入 1 或 2（直接按 Enter 預設 1 繁體中文）").Trim().ToLower()
            if ($ans -in @("", "1", "tc")) {
                Write-Host "[*] 已選擇：繁體中文版"
                return $tc
            }
            if ($ans -in @("2", "sc")) {
                Write-Host "[*] 已選擇：簡體中文版"
                return $sc
            }
            Write-Host "輸入無效，請輸入 1 或 2。"
        }
    }
    if (Test-Path $tc) { return $tc }
    if (Test-Path $sc) { return $sc }
    if (Test-Path $single) { return $single }
    throw "找不到開機套件目錄 (local-boot-package-tc / local-boot-package-sc)"
}

if (-not (Test-IsAdmin)) {
    throw "請以系統管理員身分執行本腳本。"
}

if ($Action -eq "cleanup") {
    $usbipd = Find-UsbipdExe
    if ($usbipd) { Detach-SonyDevices $usbipd }
    $distros = Get-WslDistros
    if ($distros -contains $DistroName) {
        Write-Host "[*] 正在移除極輕量 Linux 部署環境 ($DistroName)..."
        & wsl.exe --unregister $DistroName
    }
    if (Test-Path $InstallBase) {
        Remove-Item -Recurse -Force $InstallBase -ErrorAction SilentlyContinue
    }
    Write-Host "[+] 已清理完畢，不佔用硬碟空間。"
    exit 0
}

Write-Host "========================================================"
Write-Host "  Sony A6400 / A6100 — WSL2 極輕量 Linux 全離線部署工具"
Write-Host "  特點：免 Zadig、免 libusb-win32、免疫 Win11 核心隔離、"
Write-Host "        全離線不需翻牆、刷完自動恢復正常讀卡機！"
Write-Host "  事前確認："
Write-Host "  1. 相機：USB連線 = 大容量儲存、USB LUN = 多重"
Write-Host "  2. 記憶卡最上層已建好空資料夾 /A6400/TEST/"
Write-Host "  3. 相機插線開機後靜置 8 秒再繼續"
Write-Host "========================================================"

Ensure-Wsl2Offline
$usbipdExe = Ensure-UsbipdOffline
Ensure-AlpineDistro

$wslArgs = @("/opt/a6400/pmca_deploy_v3_clite.py", "--pmca-dir", "/opt/pmca-re", "--safe-serial", "--yes")
if ($Action -eq "rollback") {
    $wslArgs += "--rollback"
} elseif ($Action -eq "diag") {
    $wslArgs += "--diag"
} else {
    $pkgWinDir = Select-PackageDir $Lang
    $pkgWslDir = Convert-ToWslPath $pkgWinDir
    $wslArgs += $pkgWslDir
    $customHook = Join-Path $RootDir "a6400-autoexec.so"
    if (Test-Path $customHook) {
        $wslArgs += @("--install-hook", (Convert-ToWslPath $customHook))
    } else {
        $wslArgs += @("--auto-hook-stub", "/opt/a6400/hook-stub.bin")
    }
}

# Keep WSL2 VM awake while choosing language and attaching Sony USB
$keepAlive = Start-Process -FilePath "wsl.exe" -ArgumentList "-d $DistroName -- sleep 600" -WindowStyle Hidden -PassThru

$watcher = $null
$rc = 1
try {
    Write-Host "[*] 正在偵測 Sony 相機 USB (054c:0ca8 / 0d12 / 0336) 並透傳至 Linux..."
    $waitEnd = (Get-Date).AddSeconds(20)
    $ready = $false
    while ((Get-Date) -lt $waitEnd) {
        if (Attach-SonyDevicesOnce $usbipdExe) {
            $ready = $true
            break
        }
        Start-Sleep -Milliseconds 800
    }
    if (-not $ready) {
        Write-Host "[!] 20 秒內未在 Windows 偵測到 Sony 相機 USB (054c:0ca8 / 0d12 / 0336)。" -ForegroundColor Yellow
        Write-Host "    請確認：相機已開機、USB 連線設為「大容量儲存」、使用可傳資料的 USB 線。" -ForegroundColor Yellow
        exit 1
    }

    Start-Sleep -Seconds 1

    # Background auto-attacher: automatically binds & attaches 054c:0336 within 0.7s when camera switches to Service Mode
    $watcher = Start-Job -ScriptBlock {
        param($exe)
        while ($true) {
            try {
                $lines = & $exe list 2>$null
                foreach ($line in $lines) {
                    if ($line -match '^\s*(\d+-\d+(?:\.\d+)*)\s+054c:(0ca8|0d12|0336)\s+.*?\s{2,}(Not shared|Shared(?:\s*\(forced\))?)\s*$') {
                        $bus = $Matches[1]
                        $st = $Matches[3]
                        if ($st -eq "Not shared") {
                            & $exe bind --force --busid $bus *> $null
                        }
                        & $exe attach --wsl --busid $bus *> $null
                    }
                }
            } catch {}
            Start-Sleep -Milliseconds 700
        }
    } -ArgumentList $usbipdExe

    Write-Host "[*] 已開始在極輕量 Linux 內執行部署（切換服務模式時會自動重新透傳，請勿拔線）..."
    & wsl.exe -d $DistroName -- python3 @wslArgs
    $rc = $LASTEXITCODE

    if ($rc -eq 1) {
        Write-Host "[*] 相機剛切換至服務模式 (054c:0336)，等待 Linux 接管後自動重試..."
        Start-Sleep -Seconds 3
        Attach-SonyDevicesOnce $usbipdExe | Out-Null
        Start-Sleep -Seconds 1
        & wsl.exe -d $DistroName -- python3 @wslArgs
        $rc = $LASTEXITCODE
    }
} finally {
    if ($watcher) {
        Stop-Job -Job $watcher -ErrorAction SilentlyContinue
        Remove-Job -Job $watcher -Force -ErrorAction SilentlyContinue
    }
    Write-Host "[*] 正在解除 USB 透傳，恢復 Windows 正常讀卡機..."
    Detach-SonyDevices $usbipdExe
    if ($keepAlive -and -not $keepAlive.HasExited) {
        Stop-Process -Id $keepAlive.Id -Force -ErrorAction SilentlyContinue
    }
}

if ($rc -eq 0) {
    Write-Host ""
    Write-Host "========================================================" -ForegroundColor Green
    Write-Host "  [+] 部署成功！相機已自動恢復 Windows 正常讀卡機模式。" -ForegroundColor Green
    Write-Host "  下一步：拔線 → 關機扣電池 10 秒 → 開機等 35~40 秒 →" -ForegroundColor Green
    Write-Host "          輕觸螢幕鬆開一次 → 等 2 秒 → 長按 MENU+放大鏡 2 秒！" -ForegroundColor Green
    Write-Host "========================================================" -ForegroundColor Green
}
exit $rc
