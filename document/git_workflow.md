# Git 常用命令

## 查看状态
```bash
git status
```

## 查看改动
```bash
git diff
```

## 查看指定文件改动
```bash
git diff -- main/quiz_app.c
```

## 切到主分支
```bash
git checkout main
```

## 拉取主分支最新代码
```bash
git pull origin main
```

## 新建功能分支
```bash
git checkout -b submit_upgrade
```

## 切换到已有分支
```bash
git checkout submit_upgrade
```

## 添加文件到提交区
```bash
git add main/quiz_app.c
git add document/ui_cn_en_mapping.md
```

## 添加全部改动
```bash
git add .
```

## 提交代码
```bash
git commit -m "fix: restore utf-8 chinese strings"
```

## 第一次推送分支
```bash
git push -u origin submit_upgrade
```

## 后续推送
```bash
git push
```

## 同步 main 到当前分支
```bash
git checkout main
git pull origin main
git checkout submit_upgrade
git merge main
```

## 处理冲突后继续提交
```bash
git add <文件>
git commit
```

## 如果使用 rebase，冲突后继续
```bash
git add <文件>
git rebase --continue
```

## 查看提交历史
```bash
git log --oneline --graph --decorate -20
```

## 撤销工作区未提交修改
```bash
git restore main/quiz_app.c
```

## 取消暂存
```bash
git restore --staged main/quiz_app.c
```

## 删除本地分支
```bash
git branch -d submit_upgrade
```

## 当前项目特别注意
- 提交前先看 `git diff`，确认没有把中文改成问号。
- 冲突处理后要确认文件里没有 `<<<<<<<`、`=======`、`>>>>>>>`。
- `quiz_app.c` 的当前逻辑要保留单题提交，不要被旧版本覆盖。
- Markdown 文档统一使用 UTF-8。
