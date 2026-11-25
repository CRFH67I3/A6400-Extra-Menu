# Windows WSL2 全離線一鍵包 — 常見問題與排錯指南 (FAQ)

> 適用版本：`A6400-A6100-v4.3-SLEM-*-Public-Win-WSL-Lite.zip`（亦相容舊版 `v4.1` / `v4.2.1` WSL2 一鍵包，內建 `usbipd-win.msi` 與極輕量 Alpine Linux）。

---

## 零、【最高優先必看！】顯示 SUCCESS 卻沒有選單？→ 一定要打開相機「藍牙功能 = 開」！

> **⚠️ 實測 99%「刷完顯示 SUCCESS 但完全叫不出選單」全都是因為沒開藍牙！**

- **為什麼一定要開藍牙？**
  - Sony A6400 / A6100 原廠系統如果把「藍牙功能」關閉（或開啟了飛航模式），開機後系統完全不會觸發載入腳本，就算刷機顯示 `SUCCESS!` 也 **100% 不會有選單**！
- **立刻解決步驟（完全不用重刷，30 秒搞定）**：
  1. 相機進原廠 **`MENU` → 網路（地球圖示）→ `飛航模式` = `關`**。
  2. 相機進原廠 **`MENU` → 網路（地球圖示）→ `藍牙設定` → `藍牙功能` = `開`**（**不用配對手機**，只要把開關切到「開」即可！）。
  3. 相機進原廠 **`MENU` → 設定2 → `觸控操作` = `開`**（並確保沒有手擋到觀景窗眼感應器）。
  4. **拔掉 USB 線 → 關機 → 取出電池等 10 秒 → 裝回再開機**。
  5. 開機後靜置等 **10 秒** → **輕觸一下螢幕並放開** → 等 **2 秒** → **同時長按 `MENU + 放大鏡 (AEL)` 約 2 秒** 即可叫出選單！
- **若以上 5 步都確認無誤仍無選單**：
  - 開機等 15 秒 → 輕觸螢幕一次後，**不要關機、不要拔電池**，直接插上電腦 USB，雙擊資料夾內的 **`一鍵抓取相機Log診斷.bat`**，把產生的 `a6400-camera-log.txt` 發給作者即可秒定位。

---

## 一、雙擊 `.bat` 沒反應、閃退、或提示找不到 `.ps1`

### 1. 資料夾路徑含有括號 `(1)`、空格或特殊字元
- 若解壓後的資料夾名稱帶有 `(1)`（例如瀏覽器重複下載）或放在深層/特殊字元目錄，舊版 `.bat` 在 UAC 提權時可能被 `cmd.exe` 截斷路徑。
- **解法 A**：更新至最新版一鍵包（已改用 `[char]34` 直接提權 `powershell.exe -NoExit`），或把資料夾重新命名為簡單英文路徑（如 `D:\A6400`）。
- **解法 B（萬能手動啟動法）**：
  1. 右鍵點擊 Windows 開始功能表 → 開啟 **「終端機 (管理員)」** 或 **「PowerShell (管理員)」**。
  2. 輸入以下前綴（**最後留一個空格**）：
     ```powershell
     powershell -NoProfile -ExecutionPolicy Bypass -File 
     ```
  3. 打開解壓資料夾裡的 `tools`，把 **`wsl_deploy.ps1` 直接用滑鼠拖進 PowerShell 視窗**（會自動填好完整路徑）。
  4. 補打 ` -Action install` 後按 Enter。

---

## 二、匯入失敗：`HCS_E_SERVICE_NOT_AVAILABLE`（由於未安裝所需的特性）

錯誤範例：
```text
由于未安装所需的特性，无法启动操作。
错误代码: Wsl/Service/RegisterDistro/CreateVm/HCS/HCS_E_SERVICE_NOT_AVAILABLE
```

代表 Windows 的 WSL2 虛擬化底層尚未就緒，請依序檢查以下 4 項：

### 1. 必須點「重新啟動」，不能按「關機」再開機
Windows 10/11 預設開啟「快速啟動」，按「關機」再開機不會套用新的系統功能變更。啟用功能後務必點擊開始功能表的 **「重新啟動 / 重启」**。

### 2. 工控軟體／模擬器／優化工具把 Hypervisor 關閉了
裝過 **TIA Portal（西門子）**、部分虛擬機軟體或系統優化工具的電腦，常會將開機參數設為 `hypervisorlaunchtype off`。請在**管理員 PowerShell** 執行：
```powershell
bcdedit /set hypervisorlaunchtype auto
systeminfo | findstr /i "Hyper-V 虚拟化 Virtualization"
```
- 若顯示 `已在固件中启用虚拟化: 否` → 需進主機板 BIOS 開啟 `Intel VT-x` 或 `AMD SVM`。
- 若已為 `是` → 執行 `bcdedit /set hypervisorlaunchtype auto` 後 **重新啟動電腦** 即可。

### 3. 只開了「WSL」，漏開「虛擬機器平台 (`VirtualMachinePlatform`)」或被停用 `vmcompute`
在**管理員 PowerShell** 執行以下指令檢查與修復：
```powershell
dism /online /get-featureinfo /featurename:VirtualMachinePlatform | findstr /i "State 状态"
sc.exe config vmcompute start= demand
net start vmcompute
```
- 若 `net start vmcompute` 提示 **`[SC] OpenService 失败 1060: 指定的服务未安装`**，代表 `VirtualMachinePlatform` 根本尚未安裝，請執行下方第三節指令安裝。

### 4. Windows 為精簡版 / Ghost 版（已移除虛擬化元件）
若執行 `dism /online /enable-feature /featurename:VirtualMachinePlatform /all` 時直接報錯：
- **`功能名称 VirtualMachinePlatform 未知`** 或 **`0x800f080c` / `0x800f081f`（找不到源文件）**
- 代表該精簡版系統已刪除 WSL2 / Hyper-V 系統元件，無法在這套系統上運行 WSL2，建議改借另一台正常的 Windows 10/11 或 macOS 電腦刷機。

---

## 三、`DISM` 卡在「启用一个或多个功能」或 `OptionalFeatures` 卡在「正在寻找所需文件」

1. **先在黑視窗內按一下 `Enter` 鍵**：排除滑鼠誤點進入「控制台快速編輯凍結」。
2. **首次開啟 `VirtualMachinePlatform` 正常需等待 2～5 分鐘**：可打開工作管理員查看 `TiWorker.exe` / `Windows Modules Installer Worker` 是否有在吃 CPU 或磁碟；有在動代表正在展開系統元件，**切勿強制關閉**。
3. **超過 5 分鐘完全沒動（國內網路卡在連線 Windows Update）**：
   關閉卡住的視窗，重開**管理員 PowerShell** 改用 `/LimitAccess` 強制只讀本機檔案、不連微軟更新伺服器：
   ```powershell
   net stop wuauserv
   net stop bits
   dism.exe /online /enable-feature /featurename:VirtualMachinePlatform /all /LimitAccess /norestart
   dism.exe /online /enable-feature /featurename:Microsoft-Windows-Subsystem-Linux /all /LimitAccess /norestart
   net start wuauserv
   ```
   - 跑完顯示成功 → **重新啟動電腦一次** 再刷機。
   - 若秒報錯 `0x800f081f`（找不到源文件）→ 代表本機 WinSxS 離線包曾被系統清理工具刪掉，必須開啟能連微軟 Windows Update 的網路後再勾選 `OptionalFeatures`，或改用另一台電腦。

---

## 四、匯入失敗：`ERROR_ALREADY_EXISTS` / `ERROR_FILE_NOT_FOUND (ext4.vhdx)` / `WSL_E_DISTRO_NOT_FOUND`

錯誤範例：
- `Wsl/Service/RegisterDistro/ERROR_ALREADY_EXISTS`（具有所提供名稱的發佈已經存在）
- `Wsl/Service/CreateInstance/MountVhd/HCS/ERROR_FILE_NOT_FOUND`（找不到 `%LOCALAPPDATA%\A6400WSL\ext4.vhdx`）
- `Wsl/Service/WSL_E_DISTRO_NOT_FOUND`（剛匯入完成但系統找不到發佈）

**原因**：先前中斷安裝或手動刪除了 `AppData\Local\A6400WSL` 資料夾，導致 WSL 註冊表留下「沒有實體 `.vhdx` 磁碟」的幽靈實例，或 `wslservice` 尚未刷新註冊表。不需要另外選磁碟，照以下任一方式清除幽靈紀錄即可：
- **解法 A**：直接雙擊資料夾內的 **`一鍵清理WSL環境.bat`**（會自動清除所有 `A6400Deployer*` 註冊紀錄與殘留目錄），清完再雙擊 `一鍵開始安裝.bat`。
- **解法 B**：在**管理員 PowerShell** 執行以下兩行（請完整複製包含最前面的 `& ` 符號）：
  ```powershell
  & "C:\Program Files\WSL\wsl.exe" --shutdown
  & "C:\Program Files\WSL\wsl.exe" --unregister A6400Deployer
  ```
  （若曾有帶隨機碼的 `A6400Deployer_xxxx`，先用 `wsl -l -v` 查看後一併 `--unregister` 即可。）

---

## 五、刷完機或復原原廠後，Windows 連不上相機 / 讀卡機消失

1. **相機端未離開服務模式（`054C:0336`）**：
   刷機或回滾結束時，相機本身仍停留在 Sony 服務模式。必須 **拔掉 USB 線 → 關機 → 取出電池等 10 秒 → 裝回開機**，相機才會回到正常的大容量儲存讀卡機模式（回滾原廠後也務必拔電池 10 秒清空記憶體，避免舊版常駐程式殘留）。
2. **舊版腳本殘留 `usbipd` 持久佔用（`PersistedGuid`）**：
   若曾經使用較早版本並在中途切過 `054C:0CA8 → 054C:0336`，`usbipd` 可能仍綁著離線的 `054C:0CA8` 裝置。請在**管理員 PowerShell** 執行這一行立刻釋放所有佔用（**注意最前面必須保留 `& `，且不要複製任何多餘標題**）：
   ```powershell
   & "C:\Program Files\usbipd-win\usbipd.exe" unbind --all
   ```
   （若在新開的視窗中已套用系統 PATH，也可以直接執行 `usbipd unbind --all`。最新版 `tools\wsl_deploy.cmd` 已內建自動解除綁定與清除離線 `PersistedGuid`，正常刷完／回滾完會自動恢復。）

---

## 六、20 秒內未偵測到 Sony 相機 USB（`054c:0ca8 / 0d12 / 0336`）

請逐一確認以下 5 項：
1. 相機必須 **處於開機狀態**（螢幕亮起）。
2. 相機設定：`MENU` → 設定4 → **USB 連線 = 大容量儲存**（不能選自動或 MTP）。
3. 相機設定：`MENU` → 設定4 → **USB LUN 設定 = 多重**。
4. 必須使用 **可傳輸資料的 Micro-USB 線**（很多隨附充電線只能充電、沒有資料針腳），建議直插主機板後方 USB 孔，不要接無供電 Hub。
5. 插線開機後 **靜置 8 秒** 再執行或按確認。

---

## 七、刷機成功後怎麼喚出選單？不要用的 WSL 環境怎麼刪？

- **冷開機喚出選單**：
  1. 拔掉 USB 線 → 關機 → **取出電池等 10 秒** → 裝回再開機。
  2. 每次開機等待約 **5～10 秒**（剛刷完首次冷開機建議等 **10～15 秒**）→ **輕觸螢幕並鬆開一次** → 等 **1～2 秒** → **長按「MENU + 放大鏡」2 秒** 即可叫出膠片選單。
  3. 記憶卡需確保最上層已建好空資料夾 `/A6400/TEST/`。
- **清理佔用的微型 Linux**：
  刷機完成後，雙擊資料夾內的 **`一鍵清理WSL環境.bat`** 即可一鍵卸載 `A6400Deployer` 並釋放磁碟空間（完全不影響相機端已裝好的功能）。
