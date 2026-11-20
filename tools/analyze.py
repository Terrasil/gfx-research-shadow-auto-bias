"""Verify counts and analyze matched shadow-map configurations."""
import argparse
import csv
import math
from collections import defaultdict
from pathlib import Path
from statistics import mean

METHODS = {
    "constant": 0.0001, "slope": 0.5, "receiver": 0.000002,
    "numeric": 2.0, "footprint": 2.0, "full": 2.0,
}
DATA = ("lit", "shadow", "false_shadow", "missed_shadow", "contact", "missed_contact")
METRICS = ("false_shadow_pct", "missed_shadow_pct", "missed_contact_pct", "balanced_error_pct")


def read(path):
    with path.open(newline="", encoding="utf-8") as source:
        rows = list(csv.DictReader(source))
    for row in rows:
        row["parameter"] = float(row["parameter"])
        for key in DATA:
            row[key] = int(row[key])
        if not all(0 <= row[n] <= row[d] for n, d in
                   [("false_shadow", "lit"), ("missed_shadow", "shadow"), ("missed_contact", "contact")]):
            raise ValueError(f"invalid counts for {row['case']}")
    if not rows:
        raise ValueError(f"empty measurement table: {path}")
    return rows


def verify(rows):
    by_configuration = defaultdict(dict)
    for row in rows:
        index = row["case"], row["method"], row["parameter"]
        ring = row["ring"]
        if ring in by_configuration[index]:
            raise ValueError(f"duplicate row for {index} {ring}")
        by_configuration[index][ring] = row
    for index, groups in by_configuration.items():
        if set(groups) != {"all", "inner", "middle", "outer"}:
            raise ValueError(f"incomplete strata for {index}")
        for column in DATA:
            subtotals = sum(groups[r][column] for r in ("inner", "middle", "outer"))
            if subtotals != groups["all"][column]:
                raise ValueError(f"invalid stratum subtotal for {index}")
    by_arm = {(row["case"], row["ring"], row["method"], row["parameter"]): row for row in rows}
    for row in rows:
        if row["method"] == "footprint":
            full = by_arm[row["case"], row["ring"], "full", row["parameter"]]
            if full["false_shadow"] > row["false_shadow"] or full["missed_shadow"] < row["missed_shadow"]:
                raise ValueError(f"non-monotonic positive mismatch term: {row['case']}")


def rates(rows):
    def macro(numerator, denominator):
        usable = [100 * row[numerator] / row[denominator] for row in rows if row[denominator] > 0]
        return mean(usable) if usable else math.nan
    fp = macro("false_shadow", "lit")
    fn = macro("missed_shadow", "shadow")
    contact = macro("missed_contact", "contact")
    return dict(zip(METRICS, (fp, fn, contact, 0.5 * (fp + fn))))


def write(path, rows):
    with path.open("w", newline="", encoding="utf-8") as target:
        writer = csv.DictWriter(target, fieldnames=list(rows[0]))
        writer.writeheader()
        for row in rows:
            writer.writerow({k: f"{v:.8g}" if isinstance(v, float) else v for k, v in row.items()})


def analyze(primary, extra=None):
    verify(primary)
    if extra is not None:
        verify(extra)
    ramp = [r for r in primary if r["scene"] == "ramp" and r["ring"] == "all"]
    calibration = {}
    candidates = []
    for method in METHODS:
        settings = sorted({r["parameter"] for r in ramp if r["method"] == method})
        for value in settings:
            counts = [r for r in ramp if (r["method"], r["parameter"]) == (method, value)]
            candidates.append({"method": method, "parameter": value, "cases": len(counts), **rates(counts)})
        eligible = [e for e in candidates if e["method"] == method]
        choice = min(eligible, key=lambda e: (e["balanced_error_pct"], e["missed_contact_pct"], e["parameter"]))
        calibration[method] = choice["parameter"]
    for row in candidates:
        row["selected"] = int(calibration[row["method"]] == row["parameter"])

    summary, sweeps = [], []
    groups = {"pilot": primary}
    if extra is not None:
        groups["production"] = extra
    for profile, all_rows in groups.items():
        heldout = [r for r in all_rows if r["scene"] != "ramp"]
        for scene in ("all", "hills", "terraces", "ridge"):
            subset = [r for r in heldout if scene == "all" or r["scene"] == scene]
            for ring in ("all", "inner", "middle", "outer"):
                for warp in ("all", *sorted({r["warp"] for r in subset}, key=int)):
                    rows = [r for r in subset if r["ring"] == ring and (warp == "all" or r["warp"] == warp)]
                    for arm in ("default", "calibrated"):
                        for method in METHODS:
                            # Calibration is provided for all methods, including an optional scale sensitivity.
                            param = METHODS[method] if arm == "default" else calibration[method]
                            selected = [r for r in rows if (r["method"], r["parameter"]) == (method, param)]
                            summary.append({"profile": profile, "scene": scene, "ring": ring, "warp": warp,
                                            "arm": arm, "method": method, "parameter": param,
                                            "cases": len(selected), **rates(selected)})
        grouped = defaultdict(list)
        for row in heldout:
            if row["ring"] == "all":
                grouped[(row["method"], row["parameter"])].append(row)
        for (method, param), rows in grouped.items():
            sweeps.append({"profile": profile, "method": method, "parameter": param,
                           "cases": len(rows), **rates(rows)})
    return summary, candidates, sweeps


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pilot", type=Path)
    parser.add_argument("--production", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    for name, rows in zip(("summary", "calibration", "sweep"), analyze(
        read(args.pilot / "quality.csv"), read(args.production / "quality.csv") if args.production else None
    )):
        write(args.out / f"{name}.csv", rows)


if __name__ == "__main__":
    main()
