#!/usr/bin/env python3
"""比对两份 family_probe.jsx 导出的 JSON（AE 26 基准 vs AE 27 待测）。

用法：
    python compare_probe.py probe_26.json probe_27.json [--verbose]

退出码 0 表示每个效果暴露的属性名完全一致。
"""
import argparse
import json
import sys


def load(path):
    with open(path, encoding="utf-8") as handle:
        data = json.load(handle)
    app_version = data.get("app", "?")
    effects = {item["label"]: item.get("names", []) for item in data["items"]}
    return app_version, effects


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", help="AE 26 的探测结果")
    parser.add_argument("candidate", help="AE 27 的探测结果")
    parser.add_argument("--verbose", action="store_true", help="列出全部差异（默认只列前 5 条）")
    args = parser.parse_args(argv)

    ref_version, reference = load(args.reference)
    cand_version, candidate = load(args.candidate)

    print(f"基准: AE {ref_version}   待测: AE {cand_version}\n")

    failures = 0
    total_names = 0
    for label, ref_names in reference.items():
        if label not in candidate:
            print(f"{label:30s} 待测结果里缺少这个效果")
            failures += 1
            continue

        cand_names = candidate[label]
        total_names += max(len(ref_names), len(cand_names))

        differences = [(i, a, b)
                       for i, (a, b) in enumerate(zip(ref_names, cand_names))
                       if a != b]
        differences += [(i, ref_names[i] if i < len(ref_names) else "<missing>", "<extra>")
                        for i in range(min(len(ref_names), len(cand_names)),
                                       max(len(ref_names), len(cand_names)))]

        status = "OK" if not differences and len(ref_names) == len(cand_names) else "DIFF"
        if status == "DIFF":
            failures += 1
        print(f"{label:30s} {len(ref_names):5d} 个名字   {status}   （差异 {len(differences)} 条）")

        shown = differences if args.verbose else differences[:5]
        for index, before, after in shown:
            print(f"    [{index:4d}] 26={before!r}  27={after!r}")

    print(f"\n共 {len(reference)} 个效果、{total_names} 个名字，{failures} 个效果有差异")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
