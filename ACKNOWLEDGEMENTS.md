# 致謝與致敬

A6400 Extra-Menu 建立在相機研究社群累積的成果之上。從取得研究入口，到理解按鍵、畫面與檔案系統，每一步都受惠於前人願意公開的探索。

特別感謝 **[ivica00](https://github.com/ivica00/sony-a6400-research)**。你的 A6400 研究與程式碼，為這個專案提供了重要的起點，也讓我們有機會沿着既有成果，繼續嘗試相機上的自製觸控選單。

特別感謝 **[DevCrashDay](https://github.com/DevCrashDay/a6400)**。你的 homebrew 平台與「The Ritual」濾鏡方向，讓我們看見把拍攝後處理留在相機裡的可能，也啟發了這個專案希望實現的操作方式：直接在機身選單開關與選擇效果。

也感謝 **[ma1co 與 Sony-PMCA-RE 的貢獻者](https://github.com/ma1co/Sony-PMCA-RE)**，提供了本次研究使用的服務連線工具；感謝 **G'MIC** 與 **[YahiaAngelo/Film-Luts](https://github.com/YahiaAngelo/Film-Luts)** 整理與分享的 `.cube` 膠片 LUT 參考資源（因上游授權與著作權邊界考量，本公開版不隨附第三方 `.cube` 資產）；以及所有願意記錄測試、分享線索、指出錯誤的研究者與使用者。

特別感謝 **ASFC-CYJ** 提供 Sony A6100（ILCE-6100）與 Windows 10 實機測試環境，並協助定位串口長指令掉字元與回執遺失重傳問題，促成 `--safe-serial` 安全傳輸機制（`100 B / 20 ms` 分片寫入、`echo` + `wc -l` 冪等防重複寫入與 `__B64_L__` 行數預檢）與 A6100 部署流程的完整實證。

我們希望把自己補上的選單、控制邏輯與測試整理清楚，讓下一位研究者少走一些彎路。A6400 的故事仍然可以由使用它的人繼續寫下去。

向原創者致敬，也尊重每一份成果的授權。來源與保留聲明列於 [THIRD_PARTY.md](THIRD_PARTY.md)；尚未取得再散布許可的上游濾鏡實作，沒有收入這次公開版。
