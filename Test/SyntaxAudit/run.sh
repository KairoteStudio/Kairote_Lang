#!/usr/bin/env bash
# Re.KrtC 基础语法自动跑批
# 用法: bash run.sh [用例glob]
set -u
ROOT="/home/airs_td/桌面/KairoteLang"
KRTC="$ROOT/Re.KrtC/build/KrtC"
DIR="$ROOT/Test/SyntaxAudit/cases"
OUT="$ROOT/Test/SyntaxAudit/out"
mkdir -p "$OUT"

PASS=0; FAIL=0; CFAIL=0; RCRASH=0
RESULT_MD="$ROOT/Test/SyntaxAudit/RESULTS.md"
: > "$RESULT_MD"
echo "| 用例 | 特性 | 编译 | 运行 | 结果 | 备注 |" >> "$RESULT_MD"
echo "|---|---|---|---|---|---|" >> "$RESULT_MD"

PATTERN="${1:-*.krt}"

for f in $(ls "$DIR"/$PATTERN 2>/dev/null | sort); do
    name=$(basename "$f" .krt)
    # 多文件: M1_main.krt 代表 M1 组
    GROUP=$(echo "$name" | sed 's/_.*//')
    EXP="$DIR/${GROUP}_multi.exp"
    if [[ ! -f "$EXP" ]]; then EXP="$DIR/$name.exp"; fi

    DESC=$(head -c 200 "$f" | grep -oP '(?<=// ).*' | head -1)
    bin="$OUT/bin_$GROUP"

    # 跳过同组非主文件 (多文件组只编译一次)
    case "$name" in
        M1_lib) continue;;
        M1_main) SRC="$DIR/M1_main.krt $DIR/M1_lib.krt";;
        *) SRC="$f";;
    esac

    LOG="$OUT/$name.log"
    timeout 90 "$KRTC" $SRC output "$bin" > "$LOG" 2>&1
    CE=$?

    if [[ $CE -ne 0 ]]; then
        CFAIL=$((CFAIL+1))
        ERRMSG=$(grep -oP '(?<=error: ).*' "$LOG" | head -2 | tr '\n' ';' | cut -c1-80)
        echo "| $name | ${DESC:-} | ❌ | - | COMPILE_FAIL | \`${ERRMSG:-exit=$CE}\` |" >> "$RESULT_MD"
        echo "[CF] $name"
        continue
    fi

    # 多文件项目构建忽略 output 参数: 产物为 <输出名>.exe 落在运行器 cwd
    if [[ ! -x "$bin" && -x "./$(basename "$bin").exe" ]]; then
        mv "./$(basename "$bin").exe" "$bin"
    fi

    if [[ ! -x "$bin" ]]; then
        FAIL=$((FAIL+1))
        echo "| $name | ${DESC:-} | ✅ | - | NO_BINARY | 链接未产出可执行文件 |" >> "$RESULT_MD"
        continue
    fi

    RAWOUT=$(timeout 10 "$bin" 2>/dev/null)
    RC=$?
    ACTUAL=$(printf '%s' "$RAWOUT" | od -c | head -20)
    EXPECTED_RAW=$(cat "$EXP")

    # EXITCODE 类期望优先于崩溃闸门: 测退出码的用例本来就会非零退出
    if [[ "$EXPECTED_RAW" == EXITCODE:* ]]; then
        WANT=${EXPECTED_RAW#EXITCODE:}
        if [[ "$RC" == "$WANT" ]]; then
            PASS=$((PASS+1)); echo "| $name | ${DESC:-} | ✅ | ✅ | PASS | exit=$RC |" >> "$RESULT_MD"; echo "[OK] $name"
        else
            FAIL=$((FAIL+1)); echo "| $name | ${DESC:-} | ✅ | ✅ | WRONG_EXIT | want=$WANT got=$RC |" >> "$RESULT_MD"; echo "[WX] $name want=$WANT got=$RC"
        fi
        continue
    fi

    if [[ $RC -ne 0 ]]; then
        RCRASH=$((RCRASH+1))
        echo "| $name | ${DESC:-} | ✅ | 💥segv | RUNTIME_CRASH | 退出码$RC |" >> "$RESULT_MD"
        echo "[CR] $name"
        continue
    fi

    CLEAN=${EXPECTED_RAW//\"/}
    WANT=$(printf "%s" "$CLEAN" | od -c | head -20)
    if [[ "$ACTUAL" == "$WANT" ]]; then
        PASS=$((PASS+1))
        echo "| $name | ${DESC:-} | ✅ | ✅ | PASS | |" >> "$RESULT_MD"
        echo "[OK] $name"
    else
        FAIL=$((FAIL+1))
        AW=$(echo "$ACTUAL" | head -1)
        WW=$(echo "$WANT" | head -1)
        echo "| $name | ${DESC:-} | ✅ | ✅ | WRONG_OUTPUT | got:\`$AW\` want:\`$WW\` |" >> "$RESULT_MD"
        echo "[WO] $name got=[$AW] want=[$WW]"
    fi
done

echo "" >> "$RESULT_MD"
echo "**统计**: PASS=$PASS, 输出错误=$FAIL, 编译失败=$CFAIL, 运行崩溃=$RCRASH, 总计=$((PASS+FAIL+CFAIL+RCRASH))" >> "$RESULT_MD"
echo "===> PASS=$PASS FAIL(output)=$FAIL COMPILE_FAIL=$CFAIL CRASH=$RCRASH"
