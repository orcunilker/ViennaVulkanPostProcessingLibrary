# evaluates a series of the engine measurement copy (MyVVEV3 examples/postprocessing_measure/measure.sh)
# usage: python3 measurements/analyze_engine.py <series.csv> <table.tex> <figure-dir> [<gpu-series.csv>]
# the optional second series adds the post-processing GPU time (column gpu_post_ms) to the table
import csv
import statistics
import sys
import math

import matplotlib
matplotlib.use("pgf")
import matplotlib.pyplot as plt
from scipy import stats

# the three configurations with their names in the thesis and their colors in the figure
CHAINS = [
    ("direct", "Direct copy $T(D)$", "Direct copy", "#2a78d6"),
    ("empty", "Empty chain $T(E)$", "Empty chain", "#eb6834"),
    ("full", "Full chain $T(C)$", "Full chain", "#1baf7a"),
]

# the differences of chapter 5: configuration, reference, name in the thesis
DIFFERENCES = [
    ("empty", "direct", "$T(E) - T(D)$"),
    ("full", "empty", "$T(C) - T(E)$"),
    ("full", "direct", "$T(C) - T(D)$"),
]


# reads one column of the CSV into times[chain][round] = mean of that run in ms
def load(path, field):
    times = {}
    with open(path) as file:
        for line in csv.DictReader(file):
            runs = times.setdefault(line["chain"], {})
            round_id = int(line["round"])
            value = float(line[field])
            if round_id in runs or not math.isfinite(value) or value <= 0:
                raise ValueError("Duplicate or invalid observation")
            runs[round_id] = value
    return times


# mean and half-width of its 95 % confidence interval
def mean_ci(values):
    n = len(values)
    half = stats.t.ppf(0.975, n - 1) * statistics.stdev(values) / n ** 0.5
    return statistics.mean(values), half


# the frame times of the main series, and the post-processing GPU times of the second series if one is given
series = [("Frame time", load(sys.argv[1], "mean_ms"))]
if len(sys.argv) > 4:
    series.append(("Post-processing GPU time", load(sys.argv[4], "gpu_post_ms")))
times = series[0][1]
for _, data in series:
    if set(data) != {name for name, _, _, _ in CHAINS}:
        raise ValueError("Expected direct, empty and full configurations")
round_ids = sorted(times["direct"])
if len(round_ids) < 2 or round_ids != list(range(1, len(round_ids) + 1)):
    raise ValueError("Expected at least two complete consecutive rounds")
if any(sorted(runs) != round_ids for _, data in series for runs in data.values()):
    raise ValueError("Incomplete paired rounds")
print(", ".join(f"{name} {len(times[name])} runs" for name, _, _, _ in CHAINS))


# Per complete round: time of a configuration minus its reference.
def paired(data, config, reference):
    rounds = sorted(set(data[config]) & set(data[reference]))
    return [data[config][r] - data[reference][r] for r in rounds]


# Two-sided paired comparisons, with Holm correction across the three contrasts of each series.
holm = {}
for series_name, data in series:
    pvalues = [float(stats.ttest_1samp(paired(data, c, r), 0.0).pvalue) for c, r, _ in DIFFERENCES]
    adjusted = [0.0] * len(pvalues)
    previous = 0.0
    for rank, index in enumerate(sorted(range(len(pvalues)), key=pvalues.__getitem__)):
        previous = max(previous, min(1.0, (len(pvalues) - rank) * pvalues[index]))
        adjusted[index] = previous
    holm[series_name] = adjusted
    for (config, reference, _), raw, corrected in zip(DIFFERENCES, pvalues, adjusted):
        print(f"{series_name}, {config} - {reference}: p={raw:.6g}, Holm p={corrected:.6g}")
    # check without the assumption of normally distributed differences: Wilcoxon signed-rank test, Holm-corrected
    wilcoxon = [float(stats.wilcoxon(paired(data, c, r)).pvalue) for c, r, _ in DIFFERENCES]
    previous = 0.0
    for rank, index in enumerate(sorted(range(len(wilcoxon)), key=wilcoxon.__getitem__)):
        previous = max(previous, min(1.0, (len(wilcoxon) - rank) * wilcoxon[index]))
        print(f"{series_name}, {DIFFERENCES[index][0]} - {DIFFERENCES[index][1]}: Wilcoxon p={wilcoxon[index]:.6g}, "
              f"Holm p={previous:.6g}")

# a p-value for the table, values below 0.001 only as "< 0.001", because 20 pairs carry no more precision
def p_text(p):
    if p >= 0.001:
        return f"${p:.2g}$"
    return "$< 0.001$"


# a mean with the half-width of its confidence interval; a half-width that rounds to zero is written as "< 0.001"
def ci_text(mean, half, sign=""):
    if round(half, 3) == 0:
        return f"${mean:{sign}.3f} \\pm {{<}}\\,0.001$"
    return f"${mean:{sign}.3f} \\pm {half:.3f}$"


# the table for chapter 5, included there with \input, in the grid format of the thesis; with a second series the
# columns come in two groups, the frame time and the post-processing GPU time
lines = []
lines.append("\\begin{tabular}{|l|" + "r|r|" * len(series) + "}")
lines.append("\t\t\\hline")
if len(series) > 1:
    lines.append("\t\t & " + " & ".join(f"\\multicolumn{{2}}{{c|}}{{\\textbf{{{name}}}}}" for name, _ in series) + " \\\\")
    lines.append(f"\t\t\\cline{{2-{2 * len(series) + 1}}}")
lines.append("\t\t\\textbf{Configuration} & " + " & ".join(["\\textbf{Mean in ms} & \\textbf{Holm} $p$"] * len(series)) + " \\\\")
lines.append("\t\t\\hline")
for name, label, _, _ in CHAINS:
    cells = []
    for series_name, data in series:
        mean, half = mean_ci(list(data[name].values()))
        cells.append(f"{ci_text(mean, half)} & --")
        print(f"{series_name}, {name:7} mean {mean:.3f} +- {half:.3f} ms, "
              f"min {min(data[name].values()):.3f}, max {max(data[name].values()):.3f}")
    lines.append(f"\t\t{label} & " + " & ".join(cells) + " \\\\")
    lines.append("\t\t\\hline")
for index, (config, reference, label) in enumerate(DIFFERENCES):
    cells = []
    for series_name, data in series:
        values = paired(data, config, reference)
        mean, half = mean_ci(values)
        base = statistics.mean(data[reference].values())
        cells.append(f"{ci_text(mean, half, '+')} & {p_text(holm[series_name][index])}")
        print(f"{series_name}, {config} - {reference}: {len(values)} rounds, mean {mean:+.3f} +- {half:.3f} ms "
              f"({mean / base * 100:+.1f} +- {half / base * 100:.1f} %), sd {statistics.stdev(values):.3f}")
    lines.append(f"\t\t{label} & " + " & ".join(cells) + " \\\\")
    lines.append("\t\t\\hline")
lines.append("\t\\end{tabular}")
with open(sys.argv[2], "w") as file:
    file.write("\n".join(lines) + "\n")
print(f"table written to {sys.argv[2]}")

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
# a marker shape per configuration, so the lines stay apart without color
MARKERS = {"direct": "o", "empty": "s", "full": "^"}

# frame time of every run against its round, one line per configuration; the axis starts at zero, so the
# distance between the lines can be compared with the frame time itself
fig, ax = plt.subplots(figsize=FIGSIZE, layout="constrained")
for name, _, label, color in CHAINS:
    rounds = sorted(times[name])
    ax.plot(rounds, [times[name][r] for r in rounds], color=color, linewidth=1.5, marker=MARKERS[name],
            markersize=4.5, markeredgecolor="white", markeredgewidth=0.5, label=label)
last = max(max(times[name]) for name, _, _, _ in CHAINS)
ax.set_xlim(0.5, last + 0.5)
# rounds are whole numbers, so no ticks like 2.5
ax.set_xticks([1] + list(range(5, last + 1, 5)))
# room above the highest line for the legend, without a gridline through it
ax.set_ylim(0, 4.8)
ax.set_yticks([0, 1, 2, 3, 4])
ax.set_xlabel("Round")
ax.set_ylabel("Frame time (ms)")
ax.grid(axis="y", color=GRID, linewidth=0.5)
ax.set_axisbelow(True)
# no background patch, its invisible edge would be written as transparency (PDF/A)
ax.patch.set_visible(False)
ax.legend(loc="upper right", ncols=3)
fig.savefig(f"{sys.argv[3]}/eval_engine_rounds.pgf")
print(f"figure written to {sys.argv[3]}")
