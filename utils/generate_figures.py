#!/usr/bin/env python3
"""
generate_figures.py

Generador de figuras a partir del JSON de dependencias de RAVE Profiler.
Guarda las figuras en la estructura de carpetas:
  <DIRECTORIO_SALIDA>/<NOMBRE_ARCHIVO_SIN_EXT>/<REGION>/

Figuras disponibles:
  - Capítulo 3:
    * Figura 3.1: Distribución de distancias RAW, WAR y WAW (histogramas por bins).
    * Figura 3.2: CDF de distancias y cobertura de mecanismos (escala log2).
    * Figura 3.3: Matriz productor-consumidor para RAW (Global y Normalizada por Fila).
    * Figura 3.4: Media de dependencias concurrentes por ventana de instrucciones (Chaining / Renaming).
    * Figura 3.5: Paralelismo disponible sin dependencias próximas (si parallelism_available está presente).
    * Figura 3.6: Dimensionamiento de buffers: Capacidad requerida para P50, P90 y P99.
  - Capítulo 4:
    * Figura 4.1: Presión sobre la interfaz escalar–vectorial (frecuencia ISA y ratio de elementos activos).
    * Figura 4.2: Perfil de movimiento y comunicación dentro del vectorial (frecuencia ISA y bytes VRF).
    * Figura 4.3: Selección y efecto real de políticas tu/ta y mu/ma (selección 100% y presión de preservación).

Uso:
  python3 generate_figures.py <archivo.json> [-o <directorio>] [--o=<directorio>] [-r <regiones>]
"""

import os
import re
import sys
import json
import argparse
import numpy as np

# Backend no interactivo para entornos sin servidor gráfico X11
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker

# Etiquetas legibles para categorías productor-consumidor
PROD_LABELS = {
    "load": "Load",
    "int_arith": "Int Arith",
    "fp_arith": "FP Arith",
    "fused": "FMA / Fused",
    "permutation": "Permutation",
    "reduction": "Reduction",
    "mask": "Mask",
    "scalar_to_vector": "Scalar->Vec",
}

CONS_LABELS = {
    "store": "Store",
    "int_arith": "Int Arith",
    "fp_arith": "FP Arith",
    "fused": "FMA / Fused",
    "permutation": "Permutation",
    "reduction": "Reduction",
    "mask": "Mask",
    "vector_to_scalar": "Vec->Scalar",
}

PROD_KEYS = ["load", "int_arith", "fp_arith", "fused", "permutation", "reduction", "mask", "scalar_to_vector"]
CONS_KEYS = ["store", "int_arith", "fp_arith", "fused", "permutation", "reduction", "mask", "vector_to_scalar"]

HIST_BINS = ["1", "2", "3", "4", "5-8", "9-16", "17-32", ">32"]
CDF_POINTS = [1, 2, 4, 8, 16, 32, 64]


def load_json_resilient(file_path):
    """
    Carga un archivo JSON de forma robusta, tolerando pequeños errores de
    formato como comas tras apertura '{,' o comas finales ',}'.
    """
    with open(file_path, "r", encoding="utf-8") as f:
        content = f.read()
    try:
        return json.loads(content)
    except json.JSONDecodeError:
        cleaned = re.sub(r'\{\s*,', '{', content)
        cleaned = re.sub(r'\[\s*,', '[', cleaned)
        cleaned = re.sub(r',\s*\}', '}', cleaned)
        cleaned = re.sub(r',\s*\]', ']', cleaned)
        return json.loads(cleaned)


def extract_histogram(dep_obj):
    """Extrae los porcentajes y recuentos del histograma de un objeto depVector."""
    if not dep_obj or not isinstance(dep_obj, dict):
        return None
    
    count = dep_obj.get("count", 0)
    hist = dep_obj.get("histogram")
    
    if not hist:
        return None
    
    values = [hist.get(b, 0) for b in HIST_BINS]
    total = sum(values) if sum(values) > 0 else count
    
    if total > 0:
        percentages = [(v / total) * 100.0 for v in values]
    else:
        percentages = [0.0] * len(HIST_BINS)
        
    return percentages, values, total


def extract_cdf(dep_obj):
    """Extrae o calcula la CDF en porcentajes (0-100%)."""
    if not dep_obj or not isinstance(dep_obj, dict):
        return None
    
    count = dep_obj.get("count", 0)
    cdf = dep_obj.get("cdf")
    
    if cdf:
        percentages = []
        for p in CDF_POINTS:
            val = cdf.get(str(p), 0.0)
            if val <= 1.0 and count > 0:
                val *= 100.0
            percentages.append(val)
        return percentages

    hist = dep_obj.get("histogram")
    if hist:
        b1 = hist.get("1", 0)
        b2 = hist.get("2", 0)
        b3 = hist.get("3", 0)
        b4 = hist.get("4", 0)
        b5_8 = hist.get("5-8", 0)
        b9_16 = hist.get("9-16", 0)
        b17_32 = hist.get("17-32", 0)
        
        c1 = b1
        c2 = c1 + b2
        c4 = c2 + b3 + b4
        c8 = c4 + b5_8
        c16 = c8 + b9_16
        c32 = c16 + b17_32
        
        total = count if count > 0 else (c32 + hist.get(">32", 0))
        if total > 0:
            return [(c / total) * 100.0 for c in [c1, c2, c4, c8, c16, c32, total]]
        return [0.0] * len(CDF_POINTS)

    return None


def extract_percentiles(dep_obj):
    """Extrae percentiles (median, p90, p99) del JSON o los calcula a partir del histograma."""
    if not dep_obj or not isinstance(dep_obj, dict):
        return None
    
    pcts = dep_obj.get("percentiles")
    if pcts and isinstance(pcts, dict) and "median" in pcts and "p90" in pcts and "p99" in pcts:
        return float(pcts.get("median", 0.0)), float(pcts.get("p90", 0.0)), float(pcts.get("p99", 0.0))
    
    if dep_obj.get("count", 0) == 0:
        return 0.0, 0.0, 0.0

    hist = dep_obj.get("histogram")
    if not hist:
        return 0.0, 0.0, 0.0
    
    bin_low  = [1.0, 2.0, 3.0, 4.0, 5.0,  9.0, 17.0, 33.0, 65.0]
    bin_high = [1.0, 2.0, 3.0, 4.0, 8.0, 16.0, 32.0, 64.0, 128.0]
    bin_keys = ["1", "2", "3", "4", "5-8", "9-16", "17-32", ">32"]
    
    counts = [hist.get(b, 0) for b in bin_keys]
    cnt_arr = counts[:7] + [counts[7] * 0.5, counts[7] * 0.5] if len(counts) >= 8 else counts
    total = sum(cnt_arr)
    if total <= 0:
        return 0.0, 0.0, 0.0
    
    def calc_p(target_ratio):
        target = target_ratio * total
        cum = 0.0
        for i in range(len(cnt_arr)):
            c = cnt_arr[i]
            if c <= 0:
                continue
            if cum + c >= target:
                frac = (target - cum) / c
                if bin_low[i] == bin_high[i]:
                    return bin_low[i]
                return bin_low[i] + frac * (bin_high[i] - bin_low[i])
            cum += c
        return 128.0

    return calc_p(0.50), calc_p(0.90), calc_p(0.99)


def compute_avg_deps_per_window(dep_obj, num_instr, windows):
    """
    Calcula el número medio de dependencias concurrentes dentro de una ventana
    deslizante de tamaño W:
      media = sum_{d <= W} (W - d) * count(d) / (num_instr - W + 1)
    """
    if not dep_obj or not isinstance(dep_obj, dict):
        return None
    hist = dep_obj.get("histogram")
    if not hist:
        return None

    bin_dists = {
        "1": 1.0,
        "2": 2.0,
        "3": 3.0,
        "4": 4.0,
        "5-8": 6.5,
        "9-16": 12.5,
        "17-32": 24.5,
        ">32": 48.0
    }

    avg_values = []
    for W in windows:
        N = num_instr if num_instr > W else (W + 100)
        n_windows = max(1, N - W + 1)
        total_overlap = 0.0
        for b, dist in bin_dists.items():
            cnt = hist.get(b, 0)
            overlap = max(0.0, W - dist)
            total_overlap += overlap * cnt
        avg_values.append(total_overlap / n_windows)

    return avg_values


# ==============================================================================
# FIGURAS DEL CAPÍTULO 3
# ==============================================================================

def plot_fig_3_1(region_name, benchmark_name, vec_deps, region_dir):
    """Figura 3.1: Distribución de distancias RAW, WAR y WAW (3 paneles)."""
    raw_hist = extract_histogram(vec_deps.get("raw"))
    war_hist = extract_histogram(vec_deps.get("war"))
    waw_hist = extract_histogram(vec_deps.get("waw"))

    if not raw_hist and not war_hist and not waw_hist:
        return False

    fig, axes = plt.subplots(1, 3, figsize=(16, 5), sharey=True)
    fig.suptitle(f"Figura 3.1 — Distribución de distancias por tipo de dependencia\nBenchmark: {benchmark_name} | Región: {region_name}", fontsize=13, fontweight="bold")

    dep_types = [
        ("RAW (Read-After-Write)", raw_hist, "#1f77b4"),
        ("WAR (Write-After-Read)", war_hist, "#ff7f0e"),
        ("WAW (Write-After-Write)", waw_hist, "#2ca02c")
    ]

    for ax, (title, hist_data, color) in zip(axes, dep_types):
        if hist_data:
            pcts, counts, total = hist_data
            bars = ax.bar(HIST_BINS, pcts, color=color, edgecolor="black", alpha=0.85, width=0.6)
            ax.set_title(f"{title}\n(Total: {total:,} dependencias)", fontsize=11)
            
            for bar, pct, cnt in zip(bars, pcts, counts):
                if pct > 0:
                    ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height() + 1.0,
                            f"{pct:.1f}%\n({cnt:,})", ha="center", va="bottom", fontsize=7.5)
        else:
            ax.text(0.5, 0.5, "Sin datos de histograma", ha="center", va="center", transform=ax.transAxes, color="gray")
            ax.set_title(title, fontsize=11)

        ax.set_xlabel("Distancia (instrucciones vectoriales dinámicas)", fontsize=9.5)
        ax.set_ylim(0, 105)
        ax.yaxis.set_major_locator(ticker.MultipleLocator(20))
        ax.grid(axis="y", linestyle="--", alpha=0.5)

    axes[0].set_ylabel("Porcentaje de dependencias (%)", fontsize=10, fontweight="bold")
    plt.tight_layout()

    out_file = os.path.join(region_dir, "fig3_1_distribucion_distancias.png")
    plt.savefig(out_file, dpi=300)
    plt.close(fig)
    print(f"    [+] Generada: {out_file}")
    return True


def plot_fig_3_2(region_name, benchmark_name, vec_deps, region_dir):
    """Figura 3.2: CDF de distancias y cobertura de mecanismos."""
    raw_cdf = extract_cdf(vec_deps.get("raw"))
    war_cdf = extract_cdf(vec_deps.get("war"))
    waw_cdf = extract_cdf(vec_deps.get("waw"))

    if not raw_cdf and not war_cdf and not waw_cdf:
        return False

    fig, ax = plt.subplots(figsize=(9, 6))

    points_x = CDF_POINTS
    if raw_cdf:
        ax.plot(points_x, raw_cdf, marker="o", linewidth=2.2, label="RAW (Chaining potencial)", color="#1f77b4")
    if war_cdf:
        ax.plot(points_x, war_cdf, marker="s", linewidth=2.0, linestyle="--", label="WAR (Renaming potencial)", color="#ff7f0e")
    if waw_cdf:
        ax.plot(points_x, waw_cdf, marker="^", linewidth=2.0, linestyle="-.", label="WAW (Renaming potencial)", color="#2ca02c")

    for mark in [1, 2, 4, 8, 16]:
        ax.axvline(x=mark, color="gray", linestyle=":", alpha=0.65, linewidth=1.2)
        ax.text(mark, 102, f"{mark}", ha="center", va="bottom", fontsize=8, color="#555555")

    ax.set_xscale("log", base=2)
    ax.set_xticks(points_x)
    ax.get_xaxis().set_major_formatter(ticker.ScalarFormatter())

    ax.set_ylim(-2, 110)
    ax.set_xlabel("Distancia entre instrucciones (escala log2)", fontsize=10.5, fontweight="bold")
    ax.set_ylabel("Porcentaje acumulado de dependencias (≤ X) (%)", fontsize=10.5, fontweight="bold")
    ax.set_title(f"Figura 3.2 — CDF de distancias y cobertura de mecanismos\nBenchmark: {benchmark_name} | Región: {region_name}", fontsize=12, fontweight="bold")
    ax.grid(True, which="both", linestyle="--", alpha=0.45)
    ax.legend(loc="lower right", frameon=True, fontsize=10)

    plt.tight_layout()
    out_file = os.path.join(region_dir, "fig3_2_cdf_cobertura.png")
    plt.savefig(out_file, dpi=300)
    plt.close(fig)
    print(f"    [+] Generada: {out_file}")
    return True


def plot_fig_3_3(region_name, benchmark_name, vec_deps, region_dir):
    """Figura 3.3: Matriz productor-consumidor para dependencias RAW (Global y Normalizada por Fila)."""
    class_pairs = vec_deps.get("class_pairs")
    if not class_pairs or not isinstance(class_pairs, dict):
        return False
    
    raw_counts = class_pairs.get("raw_counts")
    if not raw_counts or not isinstance(raw_counts, dict):
        return False

    matrix = np.zeros((len(PROD_KEYS), len(CONS_KEYS)), dtype=float)
    for i, prod in enumerate(PROD_KEYS):
        row = raw_counts.get(prod, {})
        if isinstance(row, dict):
            for j, cons in enumerate(CONS_KEYS):
                matrix[i, j] = row.get(cons, 0)

    total_raw = np.sum(matrix)
    if total_raw > 0:
        matrix_global_pct = (matrix / total_raw) * 100.0
    else:
        matrix_global_pct = np.zeros_like(matrix)

    row_sums = matrix.sum(axis=1, keepdims=True)
    with np.errstate(divide="ignore", invalid="ignore"):
        matrix_row_pct = np.where(row_sums > 0, (matrix / row_sums) * 100.0, 0.0)

    prod_labels_list = [PROD_LABELS[k] for k in PROD_KEYS]
    cons_labels_list = [CONS_LABELS[k] for k in CONS_KEYS]

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(18, 8))
    fig.suptitle(f"Figura 3.3 — Matriz Productor–Consumidor RAW (Total RAW: {int(total_raw):,})\nBenchmark: {benchmark_name} | Región: {region_name}", fontsize=14, fontweight="bold")

    max_g = max(float(np.max(matrix_global_pct)), 1.0)
    im1 = ax1.imshow(matrix_global_pct, cmap="Blues", vmin=0, vmax=max_g)
    ax1.set_title("Versión A: % respecto al total de RAW del benchmark", fontsize=11, fontweight="bold")
    ax1.set_xticks(range(len(CONS_KEYS)))
    ax1.set_xticklabels(cons_labels_list, rotation=45, ha="right", fontsize=9)
    ax1.set_yticks(range(len(PROD_KEYS)))
    ax1.set_yticklabels(prod_labels_list, fontsize=9)
    ax1.set_xlabel("Familia Consumidora", fontsize=10, fontweight="bold")
    ax1.set_ylabel("Familia Productora", fontsize=10, fontweight="bold")

    for i in range(len(PROD_KEYS)):
        for j in range(len(CONS_KEYS)):
            val = matrix_global_pct[i, j]
            cnt = int(matrix[i, j])
            color = "white" if val > (np.max(matrix_global_pct) * 0.6) and val > 0 else "black"
            text = f"{val:.1f}%\n({cnt:,})" if cnt > 0 else "0"
            ax1.text(j, i, text, ha="center", va="center", color=color, fontsize=7.5)
    fig.colorbar(im1, ax=ax1, fraction=0.046, pad=0.04, label="% del total RAW")

    im2 = ax2.imshow(matrix_row_pct, cmap="YlGnBu", vmin=0, vmax=100)
    ax2.set_title("Versión B: % normalizado por fila (quién consume cada productor)", fontsize=11, fontweight="bold")
    ax2.set_xticks(range(len(CONS_KEYS)))
    ax2.set_xticklabels(cons_labels_list, rotation=45, ha="right", fontsize=9)
    ax2.set_yticks(range(len(PROD_KEYS)))
    ax2.set_yticklabels(prod_labels_list, fontsize=9)
    ax2.set_xlabel("Familia Consumidora", fontsize=10, fontweight="bold")
    ax2.set_ylabel("Familia Productora", fontsize=10, fontweight="bold")

    for i in range(len(PROD_KEYS)):
        for j in range(len(CONS_KEYS)):
            val = matrix_row_pct[i, j]
            cnt = int(matrix[i, j])
            color = "white" if val > 60 else "black"
            text = f"{val:.1f}%" if cnt > 0 else "0%"
            ax2.text(j, i, text, ha="center", va="center", color=color, fontsize=8)
    fig.colorbar(im2, ax=ax2, fraction=0.046, pad=0.04, label="% de la fila")

    plt.tight_layout()
    out_file = os.path.join(region_dir, "fig3_3_matriz_productor_consumidor.png")
    plt.savefig(out_file, dpi=300)
    plt.close(fig)
    print(f"    [+] Generada: {out_file}")
    return True


def plot_fig_3_4(region_name, benchmark_name, vec_deps, num_instr, region_dir):
    """
    Figura 3.4: Media de dependencias concurrentes por ventana de instrucciones (W).
    Mide la presión/congestión simultánea de riesgos en el hardware según el tamaño de ventana:
      media = sum_{d <= W} (W - d) * count(d) / (N - W + 1)
    """
    windows = [2, 4, 8, 16, 32, 64]
    
    raw_avg = compute_avg_deps_per_window(vec_deps.get("raw"), num_instr, windows)
    war_avg = compute_avg_deps_per_window(vec_deps.get("war"), num_instr, windows)
    waw_avg = compute_avg_deps_per_window(vec_deps.get("waw"), num_instr, windows)

    if not raw_avg and not war_avg and not waw_avg:
        return False

    fig, ax = plt.subplots(figsize=(9, 6))

    if raw_avg:
        ax.plot(windows, raw_avg, marker="o", linewidth=2.4, color="#1f77b4", label="RAW — Congestión Chaining / Wakeup")
        for x, y in zip(windows, raw_avg):
            ax.annotate(f"{y:.2f}", (x, y), textcoords="offset points", xytext=(0, 7), ha="center", fontsize=8.5, fontweight="bold")

    if war_avg:
        ax.plot(windows, war_avg, marker="s", linewidth=2.0, linestyle="--", color="#ff7f0e", label="WAR — Presión de Renaming")
        for x, y in zip(windows, war_avg):
            ax.annotate(f"{y:.2f}", (x, y), textcoords="offset points", xytext=(0, -13), ha="center", fontsize=8)

    if waw_avg:
        ax.plot(windows, waw_avg, marker="^", linewidth=2.0, linestyle="-.", color="#2ca02c", label="WAW — Presión de Renaming")
        for x, y in zip(windows, waw_avg):
            ax.annotate(f"{y:.2f}", (x, y), textcoords="offset points", xytext=(0, 7), ha="center", fontsize=8)

    ax.set_xscale("log", base=2)
    ax.set_xticks(windows)
    ax.get_xaxis().set_major_formatter(ticker.ScalarFormatter())

    max_y = max([max(lst) for lst in [raw_avg, war_avg, waw_avg] if lst] + [1.0])
    ax.set_ylim(-0.2, max_y * 1.18)
    ax.set_xlabel("Tamaño de ventana de instrucciones en vuelo (W)", fontsize=10.5, fontweight="bold")
    ax.set_ylabel("Media de dependencias activas por ventana", fontsize=10.5, fontweight="bold")
    ax.set_title(f"Figura 3.4 — Dependencias concurrentes por ventana de instrucciones\nBenchmark: {benchmark_name} | Región: {region_name} (N = {num_instr:,} instr)", fontsize=12, fontweight="bold")
    ax.grid(True, which="both", linestyle="--", alpha=0.5)
    ax.legend(loc="upper left", frameon=True, fontsize=10)

    plt.tight_layout()
    out_file = os.path.join(region_dir, "fig3_4_curva_oportunidad.png")
    plt.savefig(out_file, dpi=300)
    plt.close(fig)
    print(f"    [+] Generada: {out_file}")
    return True


def plot_fig_3_5(region_name, benchmark_name, vec_deps, region_dir):
    """Figura 3.5: Paralelismo disponible sin dependencias próximas."""
    parallelism = vec_deps.get("parallelism_available")
    if not parallelism or not isinstance(parallelism, dict):
        return False

    window_sizes = [2, 4, 8, 16]
    ilp_values = []
    for w in window_sizes:
        val = parallelism.get(str(w))
        if val is None:
            val = parallelism.get(f"window_size_{w}")
        if val is None:
            return False
        ilp_values.append(float(val))

    fig, ax = plt.subplots(figsize=(8, 5.5))
    bars = ax.bar([str(w) for w in window_sizes], ilp_values, color="#9467bd", edgecolor="black", width=0.5, alpha=0.85, label="Paralelismo medio disponible")

    # Línea de referencia ideal (ILP máximo teórico = W)
    ax.plot([str(w) for w in window_sizes], window_sizes, marker="o", linestyle="--", color="#dc2626", label="Máximo teórico (W)")

    ax.set_xlabel("Tamaño de ventana de instrucciones (W)", fontsize=10.5, fontweight="bold")
    ax.set_ylabel("Instrucciones independientes concurrentes (sin RAW)", fontsize=10.5, fontweight="bold")
    ax.set_title(f"Figura 3.5 — Paralelismo disponible sin dependencias próximas\nBenchmark: {benchmark_name} | Región: {region_name}", fontsize=12, fontweight="bold")
    ax.set_ylim(0, 18)
    ax.grid(axis="y", linestyle="--", alpha=0.5)
    ax.legend(loc="upper left", frameon=True, fontsize=9.5)

    for bar, val, w in zip(bars, ilp_values, window_sizes):
        efficiency = (val / w) * 100.0 if w > 0 else 0.0
        ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height() + 0.3,
                f"{val:.2f}\n({efficiency:.1f}%)", ha="center", va="bottom", fontsize=8.5, fontweight="bold")

    plt.tight_layout()
    out_file = os.path.join(region_dir, "fig3_5_paralelismo_disponible.png")
    plt.savefig(out_file, dpi=300)
    plt.close(fig)
    print(f"    [+] Generada: {out_file}")
    return True


def plot_fig_3_6(region_name, benchmark_name, vec_deps, region_dir):
    """
    Figura 3.6 — Dimensionamiento de Buffers: Entradas necesarias para P50, P90 y P99.
    Muestra la capacidad de cola/buffer requerida para cubrir el 50%, 90% y 99% de las dependencias.
    """
    raw_p = extract_percentiles(vec_deps.get("raw"))
    war_p = extract_percentiles(vec_deps.get("war"))
    waw_p = extract_percentiles(vec_deps.get("waw"))

    if raw_p is None and war_p is None and waw_p is None:
        return False

    categories = []
    p50_vals = []
    p90_vals = []
    p99_vals = []

    deps_data = [
        ("RAW (Chaining)", raw_p),
        ("WAR (Renaming)", war_p),
        ("WAW (Renaming)", waw_p)
    ]

    for label, p_data in deps_data:
        if p_data is not None:
            categories.append(label)
            p50_vals.append(p_data[0])
            p90_vals.append(p_data[1])
            p99_vals.append(p_data[2])

    if not categories:
        return False

    x = np.arange(len(categories))
    width = 0.24

    fig, ax = plt.subplots(figsize=(10, 6))

    bars1 = ax.bar(x - width, p50_vals, width, label="P50 (Mediana - 50% de dependencias)", color="#22c55e", edgecolor="black", alpha=0.9)
    bars2 = ax.bar(x,         p90_vals, width, label="P90 (90% de dependencias)", color="#3b82f6", edgecolor="black", alpha=0.9)
    bars3 = ax.bar(x + width, p99_vals, width, label="P99 (99% de dependencias - Caso Peor)", color="#ef4444", edgecolor="black", alpha=0.9)

    ax.set_ylabel("Capacidad de Buffer Requerida (entradas / distancia)", fontsize=10.5, fontweight="bold")
    ax.set_title(f"Figura 3.6 — Dimensionamiento de Buffers: Capacidad para P50, P90 y P99\nBenchmark: {benchmark_name} | Región: {region_name}", fontsize=12, fontweight="bold")
    ax.set_xticks(x)
    ax.set_xticklabels(categories, fontsize=10.5, fontweight="bold")
    ax.legend(loc="upper left", frameon=True, fontsize=9.5)
    ax.grid(axis="y", linestyle="--", alpha=0.5)

    for bar in list(bars1) + list(bars2) + list(bars3):
        h = bar.get_height()
        ax.text(bar.get_x() + bar.get_width() / 2, max(h, 0.0) + 0.3, f"{h:.1f}", ha="center", va="bottom", fontsize=8.5, fontweight="bold")

    max_val = max(p99_vals + [10.0])
    ax.set_ylim(0, max_val * 1.18)

    plt.tight_layout()
    out_file = os.path.join(region_dir, "fig3_6_dimensionamiento_buffers.png")
    plt.savefig(out_file, dpi=300)
    plt.close(fig)
    print(f"    [+] Generada: {out_file}")
    return True


# ==============================================================================
# FIGURAS DEL CAPÍTULO 4
# ==============================================================================

def plot_fig_4_1(region_name, benchmark_name, counters, region_dir):
    """
    Figura 4.1 — Presión sobre la interfaz escalar–vectorial.
    Panel A: Eventos por cada 1.000 instrucciones vectoriales (apilado horizontal).
    Panel B: Ratio de elementos activos que consumen escalar difundido (%).
    """
    sew_list = counters.get("instructions_previous_version", {}).get("sew", [])
    vec_instr = counters.get("instructions_previous_version", {}).get("vector", 0)
    if not sew_list or vec_instr <= 0:
        return False

    vx = sum(s.get("scalar_vector_interface", {}).get("events_count", {}).get("broadcast_vx", 0) for s in sew_list)
    vf = sum(s.get("scalar_vector_interface", {}).get("events_count", {}).get("broadcast_vf", 0) for s in sew_list)
    s2v = sum(s.get("scalar_vector_interface", {}).get("events_count", {}).get("scalar_to_vector", 0) for s in sew_list)
    v2s = sum(s.get("scalar_vector_interface", {}).get("events_count", {}).get("vector_to_scalar", 0) for s in sew_list)
    s_res = sum(s.get("scalar_vector_interface", {}).get("events_count", {}).get("scalar_result", 0) for s in sew_list)
    vset = sum(s.get("scalar_vector_interface", {}).get("events_count", {}).get("vsetvl_config", 0) for s in sew_list)

    total_events = vx + vf + s2v + v2s + s_res + vset
    if total_events == 0:
        return False

    scale = 1000.0 / vec_instr
    events = [
        ("Broadcast .vx", vx * scale, "#38bdf8"),
        ("Broadcast FP .vf", vf * scale, "#818cf8"),
        ("Escalar → Vector", s2v * scale, "#34d399"),
        ("Vector → Escalar", v2s * scale, "#fbbf24"),
        ("Resultado Escalar", s_res * scale, "#f87171"),
        ("Configuración vset*", vset * scale, "#c084fc")
    ]

    vx_elems = sum(s.get("scalar_vector_interface", {}).get("elements_affected", {}).get("broadcast_vx_consumed_elements", 0) for s in sew_list)
    vf_elems = sum(s.get("scalar_vector_interface", {}).get("elements_affected", {}).get("broadcast_vf_consumed_elements", 0) for s in sew_list)
    tot_active_elems = sum(s.get("scalar_vector_interface", {}).get("elements_affected", {}).get("total_active_elements_processed", 0) for s in sew_list)
    elem_ratio = ((vx_elems + vf_elems) / tot_active_elems * 100.0) if tot_active_elems > 0 else 0.0

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(15, 4.5), gridspec_kw={"width_ratios": [3, 1]})
    fig.suptitle(f"Figura 4.1 — Presión sobre la interfaz escalar–vectorial\nBenchmark: {benchmark_name} | Región: {region_name}", fontsize=12, fontweight="bold")

    left = 0.0
    for label, val, color in events:
        if val > 0:
            ax1.barh([benchmark_name], [val], left=left, color=color, edgecolor="black", label=f"{label} ({val:.1f})", height=0.45)
            if val > 15:
                ax1.text(left + val / 2, 0, f"{val:.1f}", ha="center", va="center", color="black", fontsize=8, fontweight="bold")
            left += val

    ax1.set_xlabel("Eventos por cada 1.000 instrucciones vectoriales", fontsize=10, fontweight="bold")
    ax1.set_xlim(0, max(left * 1.15, 10.0))
    ax1.legend(loc="upper right", bbox_to_anchor=(1.0, 1.35), ncol=3, fontsize=8.5, frameon=True)
    ax1.grid(axis="x", linestyle="--", alpha=0.5)

    bars_b = ax2.bar([benchmark_name], [elem_ratio], color="#0284c7", edgecolor="black", width=0.4)
    ax2.set_ylabel("Elementos con difusión escalar (%)", fontsize=9.5, fontweight="bold")
    ax2.set_ylim(0, 105)
    ax2.yaxis.set_major_locator(ticker.MultipleLocator(20))
    ax2.grid(axis="y", linestyle="--", alpha=0.5)
    for b in bars_b:
        ax2.text(b.get_x() + b.get_width() / 2, b.get_height() + 1.5, f"{elem_ratio:.1f}%", ha="center", va="bottom", fontsize=9, fontweight="bold")

    plt.tight_layout()
    out_file = os.path.join(region_dir, "fig4_1_interfaz_escalar_vectorial.png")
    plt.savefig(out_file, dpi=300)
    plt.close(fig)
    print(f"    [+] Generada: {out_file}")
    return True


def plot_fig_4_2(region_name, benchmark_name, counters, region_dir):
    """
    Figura 4.2 — Perfil de movimiento y comunicación dentro del vectorial.
    Panel A: Frecuencia ISA (instrucciones por cada 1.000 instrucciones vectoriales).
    Panel B: Volumen estimado (bytes leídos y escritos en VRF por op aritmética útil).
    """
    sew_list = counters.get("instructions_previous_version", {}).get("sew", [])
    vec_instr = counters.get("instructions_previous_version", {}).get("vector", 0)
    if not sew_list or vec_instr <= 0:
        return False

    cat_keys = ["slides", "gathers", "compress", "reductions", "whole_register", "mask_ops_scans", "layout_conversions"]
    cat_names = ["Slides", "Gathers", "Compress", "Reductions", "Whole-Register", "Mask / Scans", "Layout Conversions"]

    mv_instr = [sum(s.get("movement_profile", {}).get("categories", {}).get(k, {}).get("instr", 0) for s in sew_list) for k in cat_keys]
    vrf_bytes = [sum(s.get("movement_profile", {}).get("categories", {}).get(k, {}).get("vrf_bytes_read", 0) +
                     s.get("movement_profile", {}).get("categories", {}).get(k, {}).get("vrf_bytes_written", 0) for s in sew_list) for k in cat_keys]

    int_ops = sum(s.get("movement_profile", {}).get("arithmetic_useful_ops", {}).get("int_ops", 0) for s in sew_list)
    fp_ops = sum(s.get("movement_profile", {}).get("arithmetic_useful_ops", {}).get("fp_ops", 0) for s in sew_list)
    useful_ops = max(1, int_ops + fp_ops)

    scale_isa = 1000.0 / vec_instr
    freq_vals = [n * scale_isa for n in mv_instr]
    vrf_per_op = [b / useful_ops for b in vrf_bytes]

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(15, 6), sharey=True)
    fig.suptitle(f"Figura 4.2 — Perfil de movimiento y comunicación interna en VRF\nBenchmark: {benchmark_name} | Región: {region_name} (Ops útiles: {useful_ops:,})", fontsize=12, fontweight="bold")

    y_pos = np.arange(len(cat_names))

    bars1 = ax1.barh(y_pos, freq_vals, color="#38bdf8", edgecolor="black", height=0.6)
    ax1.set_yticks(y_pos)
    ax1.set_yticklabels(cat_names, fontsize=10, fontweight="bold")
    ax1.set_xlabel("Instrucciones por cada 1.000 vec instr", fontsize=10, fontweight="bold")
    ax1.set_title("Panel A: Frecuencia ISA", fontsize=11, fontweight="bold")
    ax1.grid(axis="x", linestyle="--", alpha=0.5)
    max_f = max(freq_vals + [1.0])
    ax1.set_xlim(0, max_f * 1.2)
    for b, val in zip(bars1, freq_vals):
        if val > 0:
            ax1.text(val + max_f * 0.02, b.get_y() + b.get_height() / 2, f"{val:.1f}", va="center", fontsize=8.5)

    bars2 = ax2.barh(y_pos, vrf_per_op, color="#fb923c", edgecolor="black", height=0.6)
    ax2.set_xlabel("Bytes leídos + escritos en VRF por op aritmética útil", fontsize=10, fontweight="bold")
    ax2.set_title("Panel B: Volumen estimado VRF", fontsize=11, fontweight="bold")
    ax2.grid(axis="x", linestyle="--", alpha=0.5)
    max_b = max(vrf_per_op + [1.0])
    ax2.set_xlim(0, max_b * 1.2)
    for b, val in zip(bars2, vrf_per_op):
        if val > 0:
            ax2.text(val + max_b * 0.02, b.get_y() + b.get_height() / 2, f"{val:.2f} B/op", va="center", fontsize=8.5)

    plt.tight_layout()
    out_file = os.path.join(region_dir, "fig4_2_movimiento_comunicacion_vrf.png")
    plt.savefig(out_file, dpi=300)
    plt.close(fig)
    print(f"    [+] Generada: {out_file}")
    return True


def plot_fig_4_3(region_name, benchmark_name, counters, region_dir):
    """
    Figura 4.3 — Selección y efecto real de tu/ta y mu/ma.
    Panel A: Selección de políticas (Barras al 100%).
    Panel B: Presión efectiva de preservación (Barras ordinarias, no normalizadas al 100%:
             - Elementos de tail a conservar por tu / total destino
             - Elementos desactivados por máscara por mu / total destino
             - Elementos activos escritos normalmente / total destino).
    """
    sew_list = counters.get("instructions_previous_version", {}).get("sew", [])
    if not sew_list:
        return False

    tu = sum(s.get("policies", {}).get("policy_selection", {}).get("tu_instructions", 0) for s in sew_list)
    ta = sum(s.get("policies", {}).get("policy_selection", {}).get("ta_instructions", 0) for s in sew_list)
    mu = sum(s.get("policies", {}).get("policy_selection", {}).get("mu_masked_instructions", 0) for s in sew_list)
    ma = sum(s.get("policies", {}).get("policy_selection", {}).get("ma_masked_instructions", 0) for s in sew_list)

    tail_pres = sum(s.get("policies", {}).get("preservation_pressure", {}).get("tail_preserved_elements", 0) for s in sew_list)
    mask_pres = sum(s.get("policies", {}).get("preservation_pressure", {}).get("mask_preserved_elements", 0) for s in sew_list)
    active_wr = sum(s.get("policies", {}).get("preservation_pressure", {}).get("active_written_elements", 0) for s in sew_list)
    total_dest = sum(s.get("policies", {}).get("preservation_pressure", {}).get("total_destination_elements", 0) for s in sew_list)

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 6), gridspec_kw={'width_ratios': [1, 1.45]})
    fig.suptitle(f"Figura 4.3 — Selección y efecto real de políticas tu/ta y mu/ma\nBenchmark: {benchmark_name} | Región: {region_name}", fontsize=12, fontweight="bold")

    tot_tail = tu + ta
    tu_pct = (tu / tot_tail * 100.0) if tot_tail > 0 else 0.0
    ta_pct = (ta / tot_tail * 100.0) if tot_tail > 0 else 100.0

    tot_mask = mu + ma
    mu_pct = (mu / tot_mask * 100.0) if tot_mask > 0 else 0.0
    ma_pct = (ma / tot_mask * 100.0) if tot_mask > 0 else (100.0 if tot_mask > 0 else 0.0)

    labels_a = ["Tail Policy\n(tu vs ta)", "Mask Policy\n(mu vs ma)"]
    width = 0.55

    ax1.bar(labels_a[0], [tu_pct], width, color="#ef4444", edgecolor="black", label="TU (Undisturbed)")
    ax1.bar(labels_a[0], [ta_pct], width, bottom=[tu_pct], color="#3b82f6", edgecolor="black", label="TA (Agnostic)")
    if tu_pct > 5:
        ax1.text(labels_a[0], tu_pct / 2, f"tu: {tu_pct:.1f}%", ha="center", va="center", color="white", fontweight="bold", fontsize=9)
    if ta_pct > 5:
        ax1.text(labels_a[0], tu_pct + ta_pct / 2, f"ta: {ta_pct:.1f}%", ha="center", va="center", color="white", fontweight="bold", fontsize=9)

    if tot_mask > 0:
        ax1.bar(labels_a[1], [mu_pct], width, color="#f97316", edgecolor="black", label="MU (Undisturbed)")
        ax1.bar(labels_a[1], [ma_pct], width, bottom=[mu_pct], color="#10b981", edgecolor="black", label="MA (Agnostic)")
        if mu_pct > 5:
            ax1.text(labels_a[1], mu_pct / 2, f"mu: {mu_pct:.1f}%", ha="center", va="center", color="white", fontweight="bold", fontsize=9)
        if ma_pct > 5:
            ax1.text(labels_a[1], mu_pct + ma_pct / 2, f"ma: {ma_pct:.1f}%", ha="center", va="center", color="white", fontweight="bold", fontsize=9)
    else:
        ax1.text(labels_a[1], 50, "Sin instrucciones\nenmascaradas", ha="center", va="center", color="gray", fontsize=9.5)

    ax1.set_ylabel("Distribución de Selección (%)", fontsize=10, fontweight="bold")
    ax1.set_ylim(0, 105)
    ax1.set_title("Panel A: Selección de Políticas (100%)", fontsize=11, fontweight="bold")
    ax1.legend(loc="upper right", fontsize=8.5, frameon=True)
    ax1.grid(axis="y", linestyle="--", alpha=0.5)

    # --- PANEL B: Presión Efectiva de Preservación (Barras ordinarias, no normalizadas al 100%) ---
    tot_d = total_dest if total_dest > 0 else max(1, tail_pres + mask_pres + active_wr)
    tail_ratio = (tail_pres / tot_d) * 100.0
    mask_ratio = (mask_pres / tot_d) * 100.0
    active_ratio = (active_wr / tot_d) * 100.0

    b_cats = [
        "Tail a conservar\n(tu / destino)",
        "Máscara desactivada\n(mu / destino)",
        "Activos escritos\n(normal / destino)",
    ]
    vals_b = [tail_ratio, mask_ratio, active_ratio]
    raw_b = [tail_pres, mask_pres, active_wr]
    colors_b = ["#ef4444", "#f97316", "#22c55e"]

    bars_b = ax2.bar(b_cats, vals_b, width=0.45, color=colors_b, edgecolor="black", alpha=0.85)

    ax2.set_ylabel("Porcentaje de Elementos Destino Totales (%)", fontsize=10, fontweight="bold")
    max_y = max(vals_b + [10.0])
    ax2.set_ylim(0, max(112.0, max_y * 1.22))
    ax2.set_title("Panel B: Presión Efectiva de Preservación\n(Barras ordinarias, no normalizadas)", fontsize=11, fontweight="bold")
    ax2.tick_params(axis="x", labelsize=9.5)
    ax2.grid(axis="y", linestyle="--", alpha=0.5)

    for bar, val, count in zip(bars_b, vals_b, raw_b):
        yval = bar.get_height()
        ax2.text(bar.get_x() + bar.get_width() / 2, yval + 2.0, f"{val:.1f}%\n({count:,} elems)", ha="center", va="bottom", fontsize=9.5, fontweight="bold")

    plt.tight_layout()
    out_file = os.path.join(region_dir, "fig4_3_politicas_tu_ta_mu_ma.png")
    plt.savefig(out_file, dpi=300)
    plt.close(fig)
    print(f"    [+] Generada: {out_file}")
    return True


# ==============================================================================
# PROCESAMIENTO GENERAL
# ==============================================================================

def process_json_file(file_path, base_out_dir, allowed_regions="GLOBAL_REGION"):
    """
    Procesa un archivo JSON y genera las figuras posibles a partir de sus datos
    dentro de la estructura: <base_out_dir>/<nombre_archivo_sin_ext>/<region>/
    Filtra las regiones según allowed_regions (por defecto 'GLOBAL_REGION').
    """
    print(f"\nProcesando archivo: {file_path}")
    if not os.path.isfile(file_path):
        print(f"  [!] Error: El archivo '{file_path}' no existe.")
        return

    try:
        data = load_json_resilient(file_path)
    except Exception as e:
        print(f"  [!] Error al leer el JSON '{file_path}': {e}")
        return

    # Normalizar filtro de regiones permitidas
    if isinstance(allowed_regions, str):
        if allowed_regions.strip().upper() in ["ALL", "*"]:
            filter_regions = None
        else:
            filter_regions = {r.strip().upper() for r in allowed_regions.split(",") if r.strip()}
    elif isinstance(allowed_regions, (list, set)):
        filter_regions = {str(r).strip().upper() for r in allowed_regions}
    else:
        filter_regions = {"GLOBAL_REGION"}

    # Obtener el nombre del benchmark desde "binary": split('/')[-1], split('.')[0] y TOUPPER
    binary_val = data.get("binary")
    if binary_val:
        last_component = str(binary_val).replace("\\", "/").rstrip("/").split("/")[-1]
        benchmark_name = last_component.split(".")[0].upper()
    else:
        raw_stem = os.path.splitext(os.path.basename(file_path))[0]
        benchmark_name = raw_stem.split(".")[0].upper()

    regions = data.get("regions", [])
    if not regions:
        print("  [!] Aviso: No se encontraron regiones en el JSON.")
        return

    processed_count = 0
    for idx, region in enumerate(regions):
        region_name = region.get("name") or f"region_{idx}"

        # Filtrar por regiones permitidas
        if filter_regions is not None and region_name.upper() not in filter_regions:
            continue

        counters = region.get("counters", {})
        deps = counters.get("dependencies", {})
        vec_deps = deps.get("vector", {})

        prev_instr = counters.get("instructions_previous_version", {})
        vec_instr = int(prev_instr.get("vector", 0))
        total_instr = int(prev_instr.get("total", 0))
        total_deps = vec_deps.get("total", 0)

        # Si no tiene ni dependencias vectoriales ni instrucciones vectoriales, saltar
        if vec_instr == 0 and total_deps == 0:
            continue

        processed_count += 1
        region_dir = os.path.join(base_out_dir, benchmark_name, region_name)
        os.makedirs(region_dir, exist_ok=True)

        num_instr = vec_instr if vec_instr > 0 else (total_instr or total_deps)

        print(f"  Analizando región: {region_name} ({num_instr:,} instrucciones)")
        print(f"  Carpeta destino: {region_dir}")

        # --- Figuras Capítulo 3 (Dependencias e ILP) ---
        if vec_deps:
            gen_3_1 = plot_fig_3_1(region_name, benchmark_name, vec_deps, region_dir)
            if not gen_3_1:
                print("    [o] Figura 3.1 no generada: no hay histogramas de distancias en los datos.")

            gen_3_2 = plot_fig_3_2(region_name, benchmark_name, vec_deps, region_dir)
            if not gen_3_2:
                print("    [o] Figura 3.2 no generada: no hay datos de CDF en los datos.")

            gen_3_3 = plot_fig_3_3(region_name, benchmark_name, vec_deps, region_dir)
            if not gen_3_3:
                print("    [o] Figura 3.3 no generada: no hay matriz 'class_pairs' en los datos.")

            gen_3_4 = plot_fig_3_4(region_name, benchmark_name, vec_deps, num_instr, region_dir)
            if not gen_3_4:
                print("    [o] Figura 3.4 no generada: no hay histograma para calcular dependencias concurrentes.")

            gen_3_5 = plot_fig_3_5(region_name, benchmark_name, vec_deps, region_dir)
            if not gen_3_5:
                print("    [o] Figura 3.5 omitida: 'parallelism_available' no está presente en este JSON.")

            gen_3_6 = plot_fig_3_6(region_name, benchmark_name, vec_deps, region_dir)
            if not gen_3_6:
                print("    [o] Figura 3.6 no generada: no hay percentiles ni histograma para calcular P50/P90/P99.")

        # --- Figuras Capítulo 4 (Interfaz, Movimiento, Políticas y Máscaras) ---
        gen_4_1 = plot_fig_4_1(region_name, benchmark_name, counters, region_dir)
        if not gen_4_1:
            print("    [o] Figura 4.1 no generada: sin datos en scalar_vector_interface.")

        gen_4_2 = plot_fig_4_2(region_name, benchmark_name, counters, region_dir)
        if not gen_4_2:
            print("    [o] Figura 4.2 no generada: sin datos en movement_profile.")

        gen_4_3 = plot_fig_4_3(region_name, benchmark_name, counters, region_dir)
        if not gen_4_3:
            print("    [o] Figura 4.3 no generada: sin datos en policies.")

    if processed_count == 0 and filter_regions is not None:
        print(f"  [!] Aviso: Ninguna región coincidió con las permitidas ({', '.join(sorted(filter_regions))}).")


def main():
    parser = argparse.ArgumentParser(
        description="Genera figuras de caracterización vectorial (Capítulos 3 y 4) a partir del JSON de RAVE."
    )
    parser.add_argument(
        "json_files",
        nargs="+",
        help="Uno o varios archivos JSON generados por RAVE Profiler."
    )
    parser.add_argument(
        "-o", "--o", "--output",
        dest="out_dir",
        default="figures",
        help="Carpeta base donde guardar las figuras (genera <out_dir>/<BENCHMARK>/<region>/)."
    )
    parser.add_argument(
        "-r", "--r", "--regions",
        dest="allowed_regions",
        default="GLOBAL_REGION",
        help="Regiones permitidas separadas por coma (por defecto: 'GLOBAL_REGION'). Usar 'ALL' para todas."
    )

    args = parser.parse_args()

    for json_file in args.json_files:
        process_json_file(json_file, args.out_dir, args.allowed_regions)

    print("\nProceso finalizado con éxito.")


if __name__ == "__main__":
    main()
