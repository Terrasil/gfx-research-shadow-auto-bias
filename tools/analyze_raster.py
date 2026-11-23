#!/usr/bin/env python3
"""Calibrate depth bias on the ramp and compare actual rasterized masks held out."""
import argparse
import csv
from collections import defaultdict
from pathlib import Path
from statistics import mean, median


def read(file):
    with file.open(newline="",encoding="utf-8") as inp:
        return list(csv.DictReader(inp))


def save(file,rows):
    with file.open("w",newline="",encoding="utf-8") as out:
        writer=csv.DictWriter(out,fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def rates(row):
    lit, shadow, contact = (int(row[k]) for k in ("lit","shadow","contact"))
    fp, fn, miss = (int(row[k]) for k in ("false_shadow","missed_shadow","missed_contact"))
    if not (0<=fp<=lit and 0<=miss<=min(fn,contact) and 0<=fn<=shadow and
            0<=contact<=shadow and lit+shadow==int(row["eligible"])-int(row["excluded"])):
        raise ValueError(f"Invalid visibility counts in case {row['case']}")
    return (fp/lit if lit else None, fn/shadow if shadow else None, miss/contact if contact else None)


def analyze(root):
    info=dict(line.split("=",1) for line in (root/"raster_manifest.txt").read_text().splitlines())
    if info.get("completed")!="1":
        raise ValueError("The raster study is incomplete")
    grouped, times = defaultdict(dict), defaultdict(dict)
    for row in read(root/"raster_timing.csv"):
        key = row["case"],row["method"],float(row["parameter"])
        sample,gpu = int(row["sample"]),int(row["gpu_ns"])
        if gpu<=0 or sample in times[key]:
            raise ValueError("Invalid elapsed query sample")
        times[key][sample]=gpu
    samples = set(range(int(info["samples"])))
    for row in read(root/"raster_quality.csv"):
        case,method,value = row["case"],row["method"],float(row["parameter"])
        arm = method,value
        if arm in grouped[case] or set(times[case,method,value])!=samples:
            raise ValueError(f"Incomplete or duplicate raster data for {case} / {arm}")
        row["rates"]=rates(row)
        row["gpu_ms"]=median(times[case,method,value].values())/1e6
        grouped[case][arm]=row
    expected = set(next(iter(grouped.values())))
    if len(grouped)!=int(info["planned"]) or len(expected)!=int(info["arms"]) or any(set(g)!=expected for g in grouped.values()):
        raise ValueError("Incomplete raster quality matrix")

    fits = defaultdict(list)
    for group in grouped.values():
        for (method,value),row in group.items():
            fp,fn,_=row["rates"]
            if row["split"]=="calibration" and fp is not None and fn is not None:
                fits[(row["pcf_radius"],method,value)].append((fp+fn)/2)
    fit_rows = [dict(pcf_radius=r,method=m,parameter=v,balanced_error=mean(es),cases=len(es))
                for (r,m,v),es in sorted(fits.items())]
    best = {}
    for row in sorted(fit_rows,key=lambda r:(r["balanced_error"],r["parameter"])):
        best.setdefault((row["pcf_radius"],row["method"]),row["parameter"])
    if not best:
        raise ValueError("Calibration did not see both classes")
    for row in fit_rows:
        row["selected"] = best[row["pcf_radius"],row["method"]]==row["parameter"]
    save(root/"raster_calibration.csv",fit_rows)

    held = []
    for group in grouped.values():
        for (method,value),row in group.items():
            if row["split"]!="held-out" or value!=best[row["pcf_radius"],method]:
                continue
            a,b,c=row["rates"]
            held.append({k:row[k] for k in ("case","scene","view","resolution","warp","elevation","pcf_radius","method","parameter",
                                          "lit","shadow","contact","excluded","gpu_ms")} |
                        {"false_shadow_rate":a,"missed_shadow_rate":b,"missed_contact_rate":c,
                         "balanced_error":(a+b)/2 if a is not None and b is not None else None})
    save(root/"raster_held_out.csv",held)
    print(f"{len(grouped)} raster cases. {len(held)} held-out rows at calibrated bias values.")


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results",type=Path)
    analyze(parser.parse_args().results)
