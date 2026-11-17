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

## 3. Public Domain JPEG 編解碼與膠片演算法參考

- **`vendor/picojpeg.c` / `vendor/picojpeg.h`**：Public Domain（Rich Geldreich）。
- **`vendor/jpge.cpp` / `vendor/jpge.h`**：Public Domain 或 Apache 2.0（Richard Geldreich / Alex Evans）。
- **10 款膠片濾鏡 worker（`camera/Makefile.film`、`camera/author_film.*`、`camera/author_curves.h`、`camera/author_fast_jpeg.h`、`camera/film_jpeg.cpp`）**：本專案重寫之 ARM 定點 `3×3` 色彩矩陣＋17 點 3D LUT／曲線＋粒子／暈影 JPEG 濾鏡處理器，靈感來自 **[DevCrashDay/a6400](https://github.com/DevCrashDay/a6400)** 平台與 **G'MIC** / **[YahiaAngelo/Film-Luts](https://github.com/YahiaAngelo/Film-Luts)** 膠片色彩曲線參考，現依本倉庫授權一併公開。
- **Sony 原廠與其他研究機密**：Sony 韌體映像、`.so`／`.elf`、原廠 monitor、libc 副本、私人照片與音檔仍**不隨本倉庫散布**。

## 4. Sony 元件及私人研究資料：不隨本倉庫散布

Sony 韌體映像、相機的 `.so`／`.elf`、原生 monitor、libc 副本、記憶體 dump、私人研究照片、音檔、LUT、連線設定與原始實機日誌均未收錄。程式內用於版本檢查的地址、短驗證值與 API 研究接駁不是整份韌體。

首頁的 [實機展示照](docs/images/a6400-extra-menu-camera.jpg) 由使用者明確指定公開，已縮圖並移除 EXIF 等中繼資料；它是文件展示素材，與用於研究及測試的私人拍攝原片分開處理。

相機端編譯所需的本地輸入由研究者自行合法取得，不能因為建置腳本引用了檔名，就把檔案加入倉庫或 release。

## 5. 授權原文來源

- PolyForm 官方原文：https://github.com/polyformproject/polyform-licenses/blob/1.0.0/PolyForm-Noncommercial-1.0.0.md
- GitHub 授權說明：https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/licensing-a-repository

`LICENSE` 保留官方原文，不自行刪改其法律條款。
