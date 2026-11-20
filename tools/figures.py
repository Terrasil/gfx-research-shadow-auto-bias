"""Render manuscript figures and table rows directly from the recorded benchmark."""
import argparse
import csv
import math
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from PIL import Image

LABELS = {"constant": "Constant", "slope": "Slope scale", "receiver": "Receiver plane",
          "numeric": "Numerics", "footprint": "Footprint", "full": "Auto"}
COLORS = {"constant": "#77756d", "slope": "#984b17", "receiver": "#185e89",
          "numeric": "#84558c", "footprint": "#087e66", "full": "#b73725"}


def load_csv(path):
    with path.open(newline="", encoding="utf-8") as source:
        return list(csv.DictReader(source))


def comparison(pilot, figs, docs):
    path = pilot / "captures" / "ridge-sun18-az37-n512-w8-overview"
    order = [("constant", "Constant (default)"), ("footprint", "Footprint"),
             ("full", "Auto"), ("reference", "Ray reference")]
    fig, axes = plt.subplots(2, 4, figsize=(8.8, 3.3), gridspec_kw={"height_ratios": [1, 1]})
    for i, (name, label) in enumerate(order):
        for row, mode in enumerate(("shaded", "errors")):
            with Image.open(path / f"{name}_{mode}.ppm") as image:
                axes[row,i].imshow(image)
            axes[row,i].axis("off")
        axes[0,i].set_title(label, fontsize=11, pad=5)
    fig.subplots_adjust(left=0.001,right=0.999,wspace=0.018,hspace=0.018,top=0.91,bottom=0.004)
    fig.savefig(figs / "comparison.pdf", dpi=160)
    fig.savefig(docs / "comparison.png", dpi=165)
    plt.close(fig)


def terrain(heightmaps, docs):
    fig, axes = plt.subplots(1, 4, figsize=(8.6, 2.2))
    for label, ax in zip(("ramp","hills","terraces","ridge"), axes):
        with Image.open(heightmaps / f"{label}.pgm") as source:
            data = np.asarray(source, dtype=float)/1024.0-16.0
        picture = ax.imshow(data,cmap="terrain",vmin=-16,vmax=32)
        ax.set_title(label.capitalize(),fontsize=10,pad=3)
        ax.axis("off")
    fig.subplots_adjust(left=0.006,right=0.918,top=0.88,bottom=0.04,wspace=0.045)
    cax = fig.add_axes([0.931,0.137,0.017,0.594])
    cbar = fig.colorbar(picture,cax=cax,ticks=[-16,0,16,32])
    cbar.set_label("Height (m)",fontsize=7)
    cbar.ax.tick_params(labelsize=7)
    fig.savefig(docs / "heightmaps.png", dpi=160)
    plt.close(fig)


def tradeoffs(rows, summary, figs, docs):
    profiles = ["pilot"] + (["production"] if any(r["profile"] == "production" for r in rows) else [])
    fig, axes = plt.subplots(1, len(profiles), figsize=(7.25, 2.8), squeeze=False, layout="constrained")
    for ax, profile in zip(axes[0], profiles):
        for method, label in LABELS.items():
            group = sorted((r for r in rows if r["profile"] == profile and r["method"] == method),
                           key=lambda r: float(r["parameter"]))
            x = [float(r["missed_shadow_pct"]) for r in group]
            y = [float(r["false_shadow_pct"]) for r in group]
            ax.plot(x,y,".-",color=COLORS[method],lw=1.12,ms=3.6,label=label)
            default = next(r for r in summary if r["profile"] == profile and r["method"] == method
                           and r["arm"] == "default" and r["scene"] == r["ring"] == r["warp"] == "all")
            ax.scatter(float(default["missed_shadow_pct"]),float(default["false_shadow_pct"]),
                       s=24,facecolor="white",edgecolor=COLORS[method],zorder=5)
        ax.set_title("256²–512² shadow maps" if profile == "pilot" else "2048² shadow maps",fontsize=10)
        ax.set_xlabel("Missed shadow (%)",fontsize=9)
        ax.set_ylabel("False shadow (%)",fontsize=9)
        ax.set_xscale("symlog",linthresh=2)
        ax.set_yscale("symlog",linthresh=0.2)
        ax.set_xlim(left=0)
        ax.set_ylim(bottom=0)
        ax.grid(True,which="major",color="#d9deda",lw=0.5)
        ax.tick_params(labelsize=8)
    handles, labels = axes[0,0].get_legend_handles_labels()
    fig.legend(handles, labels, fontsize=7, frameon=False, ncol=6,
               loc="lower center", bbox_to_anchor=(0.5, -0.045))
    fig.savefig(figs / "tradeoff.pdf", bbox_inches="tight", pad_inches=0.035)
    fig.savefig(docs / "tradeoff.png", dpi=170, bbox_inches="tight", pad_inches=0.035)
    plt.close(fig)


def tables(rows, data):
    order = [("constant","default"),("constant","calibrated"),("slope","calibrated"),
             ("receiver","calibrated"),("numeric","default"),("footprint","default"),("full","default")]
    for profile in ("pilot","production"):
        relevant = [r for r in rows if r["profile"] == profile and r["scene"] == r["ring"] == r["warp"] == "all"]
        if not relevant:
            continue
        lines = []
        for method, arm in order:
            r = next(row for row in relevant if row["method"] == method and row["arm"] == arm)
            label = LABELS[method]
            if method == "constant":
                label += " (default)" if arm == "default" else " (cal.)"
            metrics = [float(r[metric]) for metric in
                       ("false_shadow_pct","missed_shadow_pct","missed_contact_pct","balanced_error_pct")]
            txt = " & ".join([label,*(f"{v:.2f}" if math.isfinite(v) else "--" for v in metrics)])
            lines.append(txt+r" \\")
        (data / f"{profile}_rows.tex").write_text("\n".join(lines)+"\n",encoding="utf-8")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("pilot", type=Path)
    p.add_argument("analysis", type=Path)
    p.add_argument("--paper", type=Path, required=True)
    p.add_argument("--docs", type=Path, default=Path("docs/assets"))
    p.add_argument("--heightmaps", type=Path, default=Path("heightmaps"))
    a = p.parse_args()
    for folder in (a.docs, a.paper/"figures", a.paper/"data"):
        folder.mkdir(parents=True,exist_ok=True)
    summary = load_csv(a.analysis/"summary.csv")
    comparison(a.pilot,a.paper/"figures",a.docs)
    terrain(a.heightmaps,a.docs)
    tradeoffs(load_csv(a.analysis/"sweep.csv"),summary,a.paper/"figures",a.docs)
    tables(summary,a.paper/"data")


if __name__ == "__main__":
    main()
