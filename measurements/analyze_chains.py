# evaluates the chain series (measurements/chains-2026-11-06): every chain against the sum of its single effects
# usage: python3 measurements/analyze_chains.py <series.csv> <gpu-series.csv> <table.tex> <figure-dir>
# the first series gives the call times (column mean_ms), the second one the GPU times (column gpu_ms)
import csv
import math
import statistics
import sys

import matplotlib
matplotlib.use("pgf")
import matplotlib.pyplot as plt
from scipy import stats

EFFECTS = ["chromatic", "vignette", "tonemap", "colorgrade", "segmentation", "highlight", "greyscale", "invert",
           "solarize", "sabattier", "emboss", "sobel", "speedlines", "filmgrain", "dither"]
# the chains with their effects and their names in the table; invert<n> runs the same shader n times
CHAINS = [
    ("vsg3", ["chromatic", "vignette", "filmgrain"], "Example in VSG"),
    ("vve5", ["chromatic", "vignette", "tonemap", "greyscale", "filmgrain"], "Examples in VVE"),
    ("full", EFFECTS, "Full chain"),
    ("invert3", ["invert"] * 3, "Invert"),
    ("invert5", ["invert"] * 5, "Invert"),
    ("invert15", ["invert"] * 15, "Invert"),
]
# chains with the same number of passes, one with different effects and one with the invert shader in every pass
PAIRS = [("vsg3", "invert3"), ("vve5", "invert5"), ("full", "invert15")]


# reads one column of the CSV into times[(width, height)][config][round] = mean of that run in ms
def load(path, field):
    times = {}
    with open(path) as file:
        for line in csv.DictReader(file):
            size = (int(line["width"]), int(line["height"]))
            runs = times.setdefault(size, {}).setdefault(line["config"], {})
            round_id = int(line["round"])
            value = float(line[field])
            if round_id in runs or not math.isfinite(value) or value <= 0:
                raise ValueError("Duplicate or invalid observation")
            runs[round_id] = value
    return times


# mean over the rounds and half-width of its 95 % confidence interval
def mean_ci(values):
    n = len(values)
    half = stats.t.ppf(0.975, n - 1) * statistics.stdev(values) / n ** 0.5
    return statistics.mean(values), half


# Holm correction of a list of p-values, in the same order
def holm(pvalues):
    adjusted = [0.0] * len(pvalues)
    previous = 0.0
    for rank, index in enumerate(sorted(range(len(pvalues)), key=pvalues.__getitem__)):
        previous = max(previous, min(1.0, (len(pvalues) - rank) * pvalues[index]))
        adjusted[index] = previous
    return adjusted


series = [("Call time", load(sys.argv[1], "mean_ms")), ("GPU time", load(sys.argv[2], "gpu_ms"))]
sizes = sorted(series[0][1])
rounds = sorted(series[0][1][sizes[0]]["empty"])
expected = {"empty"} | set(EFFECTS) | {name for name, _, _ in CHAINS}
if len(rounds) < 2 or rounds != list(range(1, len(rounds) + 1)):
    raise ValueError("Expected at least two complete consecutive rounds")
for _, data in series:
    if sorted(data) != sizes:
        raise ValueError("Both series need the same image sizes")
    for configs in data.values():
        if set(configs) != expected or any(sorted(runs) != rounds for runs in configs.values()):
            raise ValueError("Incomplete or mismatched configuration grid")
print(f"{len(rounds)} rounds, {len(sizes)} image sizes")


# per round: the chain, its prediction from the empty chain and the single effects of the same round, and the
# deviation of the chain from the prediction in ms and in percent of the prediction
def deviation(data, size, chain, effects):
    measured, predicted = [], []
    for r in rounds:
        empty = data[size]["empty"][r]
        measured.append(data[size][chain][r])
        predicted.append(empty + sum(data[size][e][r] - empty for e in effects))
    gap = [m - p for m, p in zip(measured, predicted)]
    return measured, predicted, gap, [100 * g / p for g, p in zip(gap, predicted)]


# all numbers per kind of time, size and chain; the p-values are corrected per kind of time over all chains and sizes
results = {}
for kind, data in series:
    keys = [(size, name) for size in sizes for name, _, _ in CHAINS]
    for size in sizes:
        for name, effects, _ in CHAINS:
            measured, predicted, gap, percent = deviation(data, size, name, effects)
            results[kind, size, name] = dict(
                passes=len(effects), empty=statistics.mean(data[size]["empty"][r] for r in rounds),
                chain=statistics.mean(measured), predicted=statistics.mean(predicted), gap=gap, percent=percent,
                p=float(stats.ttest_1samp(gap, 0.0).pvalue), p_wilcoxon=float(stats.wilcoxon(gap).pvalue),
                negative=sum(g < 0 for g in gap))
    for test in ["p", "p_wilcoxon"]:
        for key, corrected in zip(keys, holm([results[kind, s, n][test] for s, n in keys])):
            results[kind, key[0], key[1]][test + "_holm"] = corrected

    print(f"\n{kind}")
    for size in sizes:
        empty = results[kind, size, "vsg3"]["empty"]
        print(f"  {size[0]} x {size[1]}, empty chain {empty:.3f} ms")
        for name, effects, _ in CHAINS:
            r = results[kind, size, name]
            gap_mean, gap_half = mean_ci(r["gap"])
            pct_mean, pct_half = mean_ci(r["percent"])
            print(f"    {name:9} chain - empty {r['chain'] - r['empty']:.3f}, sum of effects {r['predicted'] - r['empty']:.3f},"
                  f" deviation {gap_mean:+.3f} +- {gap_half:.3f} ms ({pct_mean:+.1f} +- {pct_half:.1f} %),"
                  f" per further pass {gap_mean / (r['passes'] - 1):+.4f} ms, negative in {r['negative']} of {len(rounds)},"
                  f" Holm p={r['p_holm']:.3g}, Wilcoxon Holm p={r['p_wilcoxon_holm']:.3g}")

    # the deviation of a chain with different effects minus that of the invert chain with the same number of passes
    pair_keys = [(size, mixed, invert) for size in sizes for mixed, invert in PAIRS]
    differences = [[a - b for a, b in zip(results[kind, s, m]["gap"], results[kind, s, i]["gap"])] for s, m, i in pair_keys]
    corrected = holm([float(stats.ttest_1samp(d, 0.0).pvalue) for d in differences])
    corrected_w = holm([float(stats.wilcoxon(d).pvalue) for d in differences])
    for (size, mixed, invert), d, p, pw in zip(pair_keys, differences, corrected, corrected_w):
        mean, half = mean_ci(d)
        print(f"  {size[0]} x {size[1]}: deviation of {mixed} minus {invert} {mean:+.3f} +- {half:.3f} ms,"
              f" Holm p={p:.3g}, Wilcoxon Holm p={pw:.3g}")


# the table for chapter 5 in the grid format of the thesis: per chain the deviation from the sum of its effects in
# percent of the prediction, for both kinds of time and both sizes
def cell(kind, size, name):
    mean, half = mean_ci(results[kind, size, name]["percent"])
    return f"${mean:.1f} \\pm {half:.1f}$"


size_labels = [f"$\\mathbf{{{w} \\times {h}}}$" for w, h in sizes]
lines = []
lines.append("\\begin{tabular}{|l|r|" + "r|" * (2 * len(sizes)) + "}")
lines.append("\t\t\\hline")
lines.append("\t\t & & " + " & ".join(f"\\multicolumn{{{len(sizes)}}}{{c|}}{{\\textbf{{{kind}}}}}" for kind, _ in series)
             + " \\\\")
lines.append(f"\t\t\\cline{{3-{2 * len(sizes) + 2}}}")
lines.append("\t\t\\textbf{Chain} & \\textbf{Passes} & " + " & ".join(size_labels * len(series)) + " \\\\")
lines.append("\t\t\\hline")
for name, effects, label in CHAINS:
    cells = [cell(kind, size, name) for kind, _ in series for size in sizes]
    lines.append(f"\t\t{label} & {len(effects)} & " + " & ".join(cells) + " \\\\")
    lines.append("\t\t\\hline")
lines.append("\t\\end{tabular}")
with open(sys.argv[3], "w") as file:
    file.write("\n".join(lines) + "\n")
print(f"\ntable written to {sys.argv[3]}")

# figure style of chapter 5: every diagram 15 x 6.5 cm, text set by LaTeX in the font of the thesis at the size of
# the captions (11 pt), legend at the upper right, no transparency (PDF/A); plot code written with Claude
plt.rcParams.update({
    "font.size": 11,
    "font.family": "serif",
    "pgf.rcfonts": False,
    "pgf.texsystem": "pdflatex",
    "pgf.preamble": "\\usepackage[T1]{fontenc}",
    "axes.spines.top": False,
    "axes.spines.right": False,
    "axes.edgecolor": "#c3c2b7",
    "xtick.color": "#c3c2b7",
    "ytick.color": "#c3c2b7",
    "xtick.labelcolor": "#0b0b0b",
    "ytick.labelcolor": "#0b0b0b",
    "legend.frameon": False,
})
FIGSIZE = (15 / 2.54, 6.5 / 2.54)
GRID = "#e1e0d9"
# per size one color, as blue and orange in Figure 5.2; filled markers for different effects, open ones for invert
SIZE_COLORS = {sizes[0]: "#2a78d6", sizes[1]: "#eb6834"}

# GPU time: the deviation of each chain from the sum of its effects in ms over the number of passes, with the mean
# and its confidence interval; a chain of one pass is its own prediction and lies at zero
fig, ax = plt.subplots(figsize=FIGSIZE, layout="constrained")
ax.axhline(0, color="#52514e", linewidth=0.8, linestyle=(0, (4, 3)))
handles, labels = [], []
for size in sizes:
    for chains, filled, offset, text in [([n for n, _, _ in CHAINS if not n.startswith("invert")], True, -0.12,
                                           "different effects"),
                                          ([n for n, _, _ in CHAINS if n.startswith("invert")], False, 0.12, "invert")]:
        x = [1 + offset]
        y = [0.0]
        yerr = [0.0]
        for name in chains:
            mean, half = mean_ci(results["GPU time", size, name]["gap"])
            x.append(results["GPU time", size, name]["passes"] + offset)
            y.append(mean)
            yerr.append(half)
        color = SIZE_COLORS[size]
        handle = ax.errorbar(x, y, yerr=yerr, fmt="o-" if filled else "s--", color=color, markersize=5,
                             markerfacecolor=color if filled else "white", markeredgewidth=1.2, capsize=3,
                             linewidth=1.0)
        handles.append(handle)
        labels.append(f"${size[0]} \\times {size[1]}$, {text}")
ax.set_xticks([1, 3, 5, 15])
ax.set_xlim(0, 16)
ax.set_xlabel("Passes")
# room above zero for the legend; all deviations of the series of 2026-11-06 lie between -0.6 and 0 ms
ax.set_ylim(-0.65, 0.65)
ax.set_yticks([-0.6, -0.4, -0.2, 0.0, 0.2, 0.4, 0.6])
ax.set_ylabel("Deviation (ms)")
ax.grid(axis="y", color=GRID, linewidth=0.5)
ax.set_axisbelow(True)
# no background patch, its invisible edge would be written as transparency (PDF/A)
ax.patch.set_visible(False)
ax.legend(handles, labels, loc="upper right")
fig.savefig(f"{sys.argv[4]}/eval_chains.pgf")
print(f"figure written to {sys.argv[4]}")
