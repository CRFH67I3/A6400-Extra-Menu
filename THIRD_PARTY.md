# 第三方來源與授權範圍

## 1. 本專案新增內容

除下列第三方部分外，這次公開的自製選單、設定接駁、掃描／派工程式、測試及文件，依根目錄 `LICENSE` 的 PolyForm Noncommercial 1.0.0 提供。`NOTICE` 的 Required Notice 須按授權條款一併保留。

本專案不主張可以重新授權不屬於我們的程式碼，也不撤銷第三方已經給予的權利。

## 2. ivica00/sony-a6400-research：MIT

來源：https://github.com/ivica00/sony-a6400-research

本機研究基線 commit：`314c70d1d1e36af8875edb027d4845a759b7e384`。
以下元件對照到的最近來源 commit：`54e7bd78018fb58dbea8bc80a81cbcc1cc549ee0`。

| 本倉庫檔案 | 來源 | 處理方式 |
| --- | --- | --- |
| `camera/native_freeze.h` | `tools/native_camera/native_freeze.h` | 原樣保留，MIT |
| `camera/native_ui_load_guard.c` | `tools/native_camera/native_menu_touch_trace_guard.c` | 改編；來源部分保留 MIT，新修改依本專案 LICENSE |

MIT 完整原文與版權聲明保存在 [licenses/ivica00-MIT.txt](licenses/ivica00-MIT.txt)。原本依 MIT 取得的部分仍可按 MIT 使用，包括商用；本專案的非商業條款不會抹除這些既有權利。

## 3. DevCrashDay/a6400：未附授權，不隨本倉庫散布

來源：https://github.com/DevCrashDay/a6400

研究過的濾鏡分支 commit：`de73e850044da4e5872be1ee93e4dc71684f2851`。
2026 年 11 月 15 日核對：GitHub 倉庫授權欄位為空，授權 API 未提供 LICENSE，上述 commit 的檔案樹也未見 LICENSE／COPYING／NOTICE。

以下內容因此沒有收入公開版：

- 上游 `ritual_daemon.c` 或其濾鏡函式。
- 研究版本中的 `author_film.c`、`author_curves.h` 及衍生曲線資料。
- 連入這些實作的 worker 二進位檔、參考函式與驗證圖片。
- 上游開機 hook、原廠函式庫及套裝執行檔。

這是再散布許可尚未取得的處理方式，不代表上游程式不能被閱讀。不要把 GitHub 上可看到的原始碼視為已獲准重新授權。日後若取得明確許可，應先記錄許可範圍，再加入相應檔案與授權聲明。

選單保留 Portra 400、CineStill 800T、Fuji Provia、Kodak Gold、Tri-X 400 作為介面識別字；名稱不是濾鏡演算法的再散布許可。

## 4. Sony 元件及私人研究資料：不隨本倉庫散布

Sony 韌體映像、相機的 `.so`／`.elf`、原生 monitor、libc 副本、記憶體 dump、照片、音檔、LUT、連線設定與原始實機日誌均未收錄。程式內用於版本檢查的地址、短驗證值與 API 研究接駁不是整份韌體。

相機端編譯所需的本地輸入由研究者自行合法取得，不能因為建置腳本引用了檔名，就把檔案加入倉庫或 release。

## 5. 授權原文來源

- PolyForm 官方原文：https://github.com/polyformproject/polyform-licenses/blob/1.0.0/PolyForm-Noncommercial-1.0.0.md
- GitHub 授權說明：https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/licensing-a-repository

`LICENSE` 保留官方原文，不自行刪改其法律條款。
