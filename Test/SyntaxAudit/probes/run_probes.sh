#!/usr/bin/env bash
# 特性探针回归: 覆盖主套件(cases/)之外的语言特性角落
set -u
cd "$(dirname "$0")"
KRTC=/home/airs_td/桌面/KairoteLang/Re.KrtC/build/KrtC
PASS=0; FAIL=0
for f in p*.krt; do
  n=$(basename "$f" .krt)
  want=$(head -1 "${n}.want")
  timeout 60 "$KRTC" "$f" output "${n}.bin" >/dev/null 2>&1
  if [[ $? -ne 0 ]]; then echo "[CF] $n"; FAIL=$((FAIL+1)); continue; fi
  got=$(timeout 5 "./${n}.bin" 2>/dev/null); rc=$?
  rm -f "${n}.bin"
  if [[ $rc -ne 0 ]]; then echo "[CR] $n want=$want"; FAIL=$((FAIL+1));
  elif [[ "$got" == "$want" ]]; then PASS=$((PASS+1)); echo "[OK] $n"
  else echo "[WO] $n got=$got want=$want"; FAIL=$((FAIL+1)); fi
done
echo "===> probes PASS=$PASS FAIL=$FAIL"
