# UI 文案中文化候选清单（阶段 1）

扫描范围：
- `main/`、`components/` 下 `.c/.h/.cpp/.hpp` 运行时代码（用于后续实际替换）
- 说明：`build/`、`managed_components/`、文档文件不参与运行时 UI 替换，默认按 `❌禁止` 处理，不进入本次代码替换清单

判定规则：
- `✅` 系统 UI 固定文案：允许替换，允许绑定中文字体
- `❌` 题库显示区域/动态内容/非运行时代码：禁止替换，禁止绑定中文字体
- `❓` 不确定：按禁止处理

## 命中清单

文件：`main/quiz_app.c:850`  
命中：`"No test taken yet"`  
上下文：`quiz_show_toast("No test taken yet", 2000);`  
分类：`✅ 系统 UI（主页操作提示 toast）`

文件：`main/quiz_app.c:872`  
命中：`"Download"`  
上下文：`static const char *btn_texts[3] = {"Download", "Start Test", "View Results"};`  
分类：`✅ 系统 UI（主页按钮）`

文件：`main/quiz_app.c:872`  
命中：`"Start Test"`  
上下文：`static const char *btn_texts[3] = {"Download", "Start Test", "View Results"};`  
分类：`✅ 系统 UI（主页按钮）`

文件：`main/quiz_app.c:872`  
命中：`"View Results"`  
上下文：`static const char *btn_texts[3] = {"Download", "Start Test", "View Results"};`  
分类：`✅ 系统 UI（主页按钮）`

文件：`main/quiz_app.c:947`  
命中：`"Loading..."`  
上下文：`lv_label_set_text(s_question_label, "Loading...");`  
分类：`❌ 题库显示区域（题干 label，后续承载服务器下发题目）`

文件：`main/quiz_app.c:1049`  
命中：`"Submit"`  
上下文：`lv_label_set_text(submit_lbl, "Submit");`  
分类：`✅ 系统 UI（提交按钮）`

文件：`main/quiz_app.c:1108`  
命中：`"Result"`  
上下文：`lv_label_set_text(title, "Result");`  
分类：`✅ 系统 UI（结果页标题）`

文件：`main/quiz_app.c:1265`  
命中：`"All answers correct!"`  
上下文：`lv_label_set_text(all_good, "All answers correct!");`  
分类：`✅ 系统 UI（结果页固定提示）`

文件：`main/quiz_app.c:1295`  
命中：`"Retry"`  
上下文：`lv_label_set_text(retry_lbl, "Retry");`  
分类：`✅ 系统 UI（结果页按钮）`

文件：`main/quiz_app.c:1310`  
命中：`"Back"`  
上下文：`lv_label_set_text(back_lbl, "Back");`  
分类：`✅ 系统 UI（结果页按钮）`

文件：`main/quiz_app.c:1509`  
命中：`"Results uploaded. View results from home."`  
上下文：`quiz_show_toast("Results uploaded. View results from home.", 1800);`  
分类：`✅ 系统 UI（提交流程完成提示 toast）`

文件：`main/quiz_app.c:1573`  
命中：`"Select an option"`  
上下文：`quiz_show_toast("Select an option", 1600);`  
分类：`✅ 系统 UI（表单校验提示 toast）`

## 未命中（运行时代码中无完全匹配字符串字面量）

`"OK"`, `"DEL"`, `"Sign In"`, `"Enter account and password"`, `"account"`, `"Password"`, `"Score"`, `"Acc"`, `"Time"`, `"Correct"`, `"Wrong"`, `"Wrong list"`, `"Your"`, `"Ans"`

备注：
- `main/login_app.c` 中当前是 `"Account"`（首字母大写），不属于本轮 key `"account"` 的完全匹配，按规则不改。
- `Score/Acc/Time/Correct/Wrong/Your/Ans` 在代码里以组合字符串出现（如 `"Score: %d/%d"`、`"Q%d   Your:%c  Ans:%c"`），不属于本轮“完全匹配 key”。
