# evaluates a series of the standalone benchmark (MyVVPP/tests/bench.sh)
# usage: python3 measurements/analyze.py <series.csv> <table.tex> <figure-dir> [<gpu-series.csv>]
# the optional second series adds the GPU times (column gpu_ms) to the table and the figure
import csv
import statistics
import sys
import math

import matplotlib
matplotlib.use("pgf")
import matplotlib.pyplot as plt
from scipy import stats

# the effects in the order of the chain, with their names in the effect table of chapter 4;
# legacy series without inversion remain readable
EFFECTS = [
    ("chromatic", "Chromatic aberration"),
    ("vignette", "Vignette"),
    ("tonemap", "Tonemapping"),
    ("colorgrade", "Color grading"),
    ("segmentation", "Hue segmentation"),
    ("highlight", "Color highlight"),
    ("greyscale", "Grayscale"),
    ("invert", "Invert"),
    ("solarize", "Solarization"),
    ("sabattier", "Sabattier effect"),
    ("emboss", "Emboss"),
    ("sobel", "Sobel edge detection"),
    ("speedlines", "Speed lines"),
    ("filmgrain", "Film grain"),
    ("dither", "Dithering"),
]


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


# one table row, one cell "mean +- half-width" per image size
def table_row(label, values_per_size):
    cells = []
    for values in values_per_size:
        mean, half = mean_ci(values)
        cells.append(f"${mean:.3f} \\pm {half:.3f}$")
    return "\t\t" + label + " & " + " & ".join(cells) + " \\\\"


# the call times of the main series, and the GPU times of the second series if one is given
series = [("Call time", load(sys.argv[1], "mean_ms"))]
if len(sys.argv) > 4:
    series.append(("GPU time", load(sys.argv[4], "gpu_ms")))
times = series[0][1]
if not times:
    raise ValueError("Empty series")
if not any("invert" in configs for configs in times.values()):
    EFFECTS = [(name, label) for name, label in EFFECTS if name != "invert"]
sizes = sorted(times)
rounds = sorted(times[sizes[0]]["empty"])
expected = {"empty", "full"} | {name for name, _ in EFFECTS}
if len(rounds) < 2 or rounds != list(range(1, len(rounds) + 1)):
    raise ValueError("Expected at least two complete consecutive rounds")
for _, data in series:
    if sorted(data) != sizes:
        raise ValueError("Both series need the same image sizes")
    for configs in data.values():
        if set(configs) != expected or any(sorted(runs) != rounds for runs in configs.values()):
            raise ValueError("Incomplete or mismatched configuration grid")
print(f"{len(rounds)} rounds, {len(sizes)} image sizes, {len(series)} series")


# per round: time of a configuration minus the empty chain of the same round
def minus_empty(data, size, config):
    return [data[size][config][r] - data[size]["empty"][r] for r in rounds]


# per round: the sum of the differences of all single effects
def sum_of_effects(data, size):
    sums = []
    for r in rounds:
        total = 0.0
        for name, label in EFFECTS:
            total += data[size][name][r] - data[size]["empty"][r]
        sums.append(total)
    return sums


# the table for chapter 5, included there with \input, in the grid format of the thesis; with a second series the
# columns come in two groups, the call time and the GPU time
columns = [(data, s) for _, data in series for s in sizes]
size_labels = [f"$\\mathbf{{{w} \\times {h}}}$" for w, h in sizes]
lines = []
lines.append("\\begin{tabular}{|l|" + "r|" * len(columns) + "}")
lines.append("\t\t\\hline")
if len(series) == 1:
    lines.append("\t\t\\textbf{Configuration} & " + " & ".join(size_labels) + " \\\\")
else:
    lines.append("\t\t & " + " & ".join(f"\\multicolumn{{{len(sizes)}}}{{c|}}{{\\textbf{{{name}}}}}" for name, _ in series)
                 + " \\\\")
    lines.append(f"\t\t\\cline{{2-{len(columns) + 1}}}")
    lines.append("\t\t\\textbf{Configuration} & " + " & ".join(size_labels * len(series)) + " \\\\")
lines.append("\t\t\\hline")
lines.append(table_row("Empty chain $T(E)$", [[data[s]["empty"][r] for r in rounds] for data, s in columns]))
lines.append("\t\t\\hline")
for name, label in EFFECTS:
    lines.append(table_row(label, [minus_empty(data, s, name) for data, s in columns]))
    lines.append("\t\t\\hline")
lines.append(table_row("Sum of the single effects", [sum_of_effects(data, s) for data, s in columns]))
lines.append("\t\t\\hline")
lines.append(table_row("Full chain $T(C) - T(E)$", [minus_empty(data, s, "full") for data, s in columns]))
lines.append("\t\t\\hline")
lines.append("\t\\end{tabular}")
with open(sys.argv[2], "w") as file:
    file.write("\n".join(lines) + "\n")
print(f"table written to {sys.argv[2]}")

# collected in the loop below: per series and size the deviation of the full chain from the prediction in every round,
# in percent of the prediction, for the figure
deviations = {}

# numbers for the text: single effects per size, and the full chain against the additive prediction
for series_name, data in series:
    pvalues = []
    # check without the assumption of normally distributed differences: Wilcoxon signed-rank test
    wilcoxon = []
    for size in sizes:
        megapixels = size[0] * size[1] / 1e6
        effect_means = {}
        for name, label in EFFECTS:
            effect_means[label] = statistics.mean(minus_empty(data, size, name))
        average = statistics.mean(effect_means.values())
        cheapest = min(effect_means, key=effect_means.get)
        costliest = max(effect_means, key=effect_means.get)

        sums = sum_of_effects(data, size)
        predicted = []
        gap = []
        for i, r in enumerate(rounds):
            prediction = data[size]["empty"][r] + sums[i]
            predicted.append(prediction)
            gap.append(data[size]["full"][r] - prediction)
        gap_mean, gap_half = mean_ci(gap)
        pvalues.append(float(stats.ttest_1samp(gap, 0.0).pvalue))
        wilcoxon.append(float(stats.wilcoxon(gap).pvalue))
        predicted_mean = statistics.mean(predicted)

        deviations[series_name, size] = [100 * g / p for g, p in zip(gap, predicted)]

        print(f"\n{series_name}, {size[0]} x {size[1]}")
        print(f"  empty chain     {statistics.mean([data[size]['empty'][r] for r in rounds]):.3f} ms")
        print(f"  single effects  mean {average:.3f} ms, {average / megapixels:.3f} ms per megapixel")
        print(f"                  cheapest {cheapest} {effect_means[cheapest]:.3f} ms, "
              f"costliest {costliest} {effect_means[costliest]:.3f} ms")
        print(f"  full chain      {statistics.mean([data[size]['full'][r] for r in rounds]):.3f} ms, "
              f"predicted {predicted_mean:.3f} ms")
        print(f"  difference      {gap_mean:+.3f} +- {gap_half:.3f} ms ({gap_mean / predicted_mean * 100:+.1f} %)")

    # Holm correction across the resolutions of one series; the confidence intervals stay pointwise.
    adjusted = [0.0] * len(pvalues)
    previous = 0.0
    for rank, index in enumerate(sorted(range(len(pvalues)), key=pvalues.__getitem__)):
        previous = max(previous, min(1.0, (len(pvalues) - rank) * pvalues[index]))
        adjusted[index] = previous
    for size, raw, corrected in zip(sizes, pvalues, adjusted):
        print(f"Additivity, {series_name}, {size}: p={raw:.6g}, Holm p={corrected:.6g}")
    previous = 0.0
    for rank, index in enumerate(sorted(range(len(wilcoxon)), key=wilcoxon.__getitem__)):
        previous = max(previous, min(1.0, (len(wilcoxon) - rank) * wilcoxon[index]))
        print(f"Additivity, {series_name}, {sizes[index]}: Wilcoxon p={wilcoxon[index]:.6g}, Holm p={previous:.6g}")

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
# per series the color of its rounds: blue for the call time, orange for the GPU time
SERIES_COLORS = {"Call time": "#2a78d6", "GPU time": "#eb6834"}
GRID = "#e1e0d9"

# H1: deviation of the full chain from the prediction in every round, in percent of the prediction, and the mean
# with its confidence interval; the dashed line at zero is the prediction itself
fig, ax = plt.subplots(figsize=FIGSIZE, layout="constrained")
h1 = ax.axhline(0, color="#52514e", linewidth=0.8, linestyle=(0, (4, 3)))
handles = []
labels = []
# within a resolution, each series takes its own band, the rounds side by side in their order and the mean right of them
band = 0.8 / len(series)
for j, (series_name, _) in enumerate(series):
    for i, size in enumerate(sizes):
        values = deviations[series_name, size]
        left = i - 0.4 + j * band
        x = [left + 0.05 + 0.55 * band * k / (len(values) - 1) for k in range(len(values))]
        rounds_marker, = ax.plot(x, values, "o", color=SERIES_COLORS[series_name], markersize=4.5,
                                 markeredgecolor="white", markeredgewidth=0.6, linestyle="none")
        mean, half = mean_ci(values)
        mean_marker = ax.errorbar([left + 0.8 * band], [mean], yerr=[half], fmt="D", color="#0b0b0b", markersize=5,
                                  capsize=4, linewidth=1.2)
    handles.append(rounds_marker)
    labels.append(f"{series_name}, single round" if len(series) > 1 else "Single round")
ax.set_xticks(range(len(sizes)), [f"${w} \\times {h}$" for w, h in sizes])
ax.set_xlim(-0.6, len(sizes) - 0.4)
# room above the points for the legend, and below for the lowest round (-33 % at 1920 x 1080)
ax.set_ylim(-40, 40)
ax.set_yticks(range(-40, 41, 10))
ax.set_ylabel("Deviation (\\%)")
ax.grid(axis="y", color=GRID, linewidth=0.5)
ax.set_axisbelow(True)
# no background patch, its invisible edge would be written as transparency (PDF/A)
ax.patch.set_visible(False)
ax.legend(handles + [mean_marker, h1],
          labels + ["Mean with 95\\,\\% confidence interval", "Sum of the single effects (H1)"],
          loc="upper right")
fig.savefig(f"{sys.argv[3]}/eval_additivity.pgf")
print(f"figure written to {sys.argv[3]}")
