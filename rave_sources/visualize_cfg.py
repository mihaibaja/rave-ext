#!/usr/bin/env python3
"""
visualize_cfg.py

Visualizador Web Interactivo para el Grafo de Control (CFG / Bloques Básicos y Aristas) de RAVE.
Permite inspeccionar:
- Grafo dirigido jerárquico de Bloques Básicos (nodos) y Transiciones (aristas con frecuencias).
- Al hacer clic en cualquier bloque básico, visualiza el código ensamblador que representa,
  cruzándolo con la traza de instrucciones (rave_inst_trace.jsonl).
- Soporta cargar la traza por línea de comandos (--trace <fichero.jsonl>) o directamente
  arrastrándola / seleccionándola en la interfaz web.

Uso:
    python3 visualize_cfg.py [fichero_bb.json] [--trace rave_inst_trace.jsonl] [--port 8080]
"""

import sys
import os
import json
import argparse
import webbrowser
from http.server import HTTPServer, SimpleHTTPRequestHandler
import urllib.parse

HTML_TEMPLATE = r"""<!DOCTYPE html>
<html lang="es">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>RAVE - Visualizador de Grafo de Control (CFG) y Código</title>
    <!-- Vis.js Network para grafos interactivos -->
    <script type="text/javascript" src="https://unpkg.com/vis-network/standalone/umd/vis-network.min.js"></script>
    <style>
        :root {
            --bg-color: #0f172a;
            --surface-color: #1e293b;
            --surface-hover: #334155;
            --border-color: #475569;
            --text-main: #f8fafc;
            --text-muted: #94a3b8;
            --accent-blue: #38bdf8;
            --accent-green: #4ade80;
            --accent-amber: #fbbf24;
            --accent-rose: #f43f5e;
            --accent-purple: #c084fc;
        }

        * {
            box-sizing: border-box;
            margin: 0;
            padding: 0;
        }

        body {
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
            background-color: var(--bg-color);
            color: var(--text-main);
            display: flex;
            height: 100vh;
            overflow: hidden;
        }

        /* Barra Lateral Izquierda */
        #sidebar {
            width: 440px;
            min-width: 380px;
            background-color: var(--surface-color);
            border-right: 1px solid var(--border-color);
            display: flex;
            flex-direction: column;
            z-index: 10;
        }

        #header {
            padding: 14px 18px;
            border-bottom: 1px solid var(--border-color);
            background: linear-gradient(135deg, #1e293b 0%, #0f172a 100%);
        }

        #header h1 {
            font-size: 1.15rem;
            color: var(--accent-blue);
            display: flex;
            align-items: center;
            gap: 8px;
        }

        #header p {
            font-size: 0.78rem;
            color: var(--text-muted);
            margin-top: 3px;
        }

        /* Controles y Carga de Ficheros */
        #controls {
            padding: 12px 18px;
            border-bottom: 1px solid var(--border-color);
            display: flex;
            flex-direction: column;
            gap: 8px;
            background-color: rgba(15, 23, 42, 0.4);
        }

        .upload-row {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 8px;
        }

        .btn {
            background-color: var(--surface-hover);
            color: var(--text-main);
            border: 1px solid var(--border-color);
            padding: 6px 10px;
            border-radius: 6px;
            font-size: 0.78rem;
            cursor: pointer;
            transition: all 0.2s;
            font-weight: 500;
            text-align: center;
        }

        .btn:hover {
            background-color: #475569;
            border-color: var(--accent-blue);
        }

        .btn.active {
            background-color: #0284c7;
            border-color: var(--accent-blue);
            color: white;
        }

        .file-upload-btn {
            background-color: #0369a1;
            color: white;
            border: none;
            padding: 6px 8px;
            border-radius: 6px;
            font-size: 0.75rem;
            cursor: pointer;
            font-weight: 600;
            text-align: center;
            display: block;
            width: 100%;
        }

        .file-upload-btn:hover {
            background-color: #0284c7;
        }

        input[type="file"] {
            display: none;
        }

        /* Resumen global */
        #summary-stats {
            padding: 10px 18px;
            display: grid;
            grid-template-columns: 1fr 1fr 1fr;
            gap: 8px;
            border-bottom: 1px solid var(--border-color);
            font-size: 0.8rem;
        }

        .stat-card {
            background-color: rgba(15, 23, 42, 0.6);
            padding: 6px 10px;
            border-radius: 6px;
            border: 1px solid rgba(255, 255, 255, 0.05);
        }

        .stat-card .val {
            font-size: 1rem;
            font-weight: bold;
            color: var(--accent-blue);
            margin-top: 2px;
        }

        .stat-card .lbl {
            font-size: 0.68rem;
            color: var(--text-muted);
            text-transform: uppercase;
        }

        /* Pestañas de la barra lateral */
        .tab-bar {
            display: flex;
            border-bottom: 1px solid var(--border-color);
            background: rgba(15, 23, 42, 0.3);
        }

        .tab-btn {
            flex: 1;
            padding: 9px;
            text-align: center;
            font-size: 0.8rem;
            font-weight: 600;
            background: transparent;
            border: none;
            color: var(--text-muted);
            cursor: pointer;
            border-bottom: 2px solid transparent;
        }

        .tab-btn.active {
            color: var(--accent-blue);
            border-bottom: 2px solid var(--accent-blue);
            background: rgba(56, 189, 248, 0.05);
        }

        /* Paneles de Contenido */
        #pane-container {
            flex: 1;
            overflow-y: auto;
            padding: 14px 18px;
        }

        .tab-content {
            display: none;
        }

        .tab-content.active {
            display: block;
        }

        /* Visor de Código Ensamblador */
        .code-viewer {
            background-color: #0b1120;
            border: 1px solid #1e293b;
            border-radius: 8px;
            overflow: hidden;
            font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", monospace;
            font-size: 0.78rem;
            margin-top: 10px;
        }

        .code-header {
            background-color: #1e293b;
            padding: 6px 12px;
            color: var(--text-muted);
            font-size: 0.72rem;
            border-bottom: 1px solid #334155;
            display: flex;
            justify-content: space-between;
        }

        .code-table {
            width: 100%;
            border-collapse: collapse;
        }

        .code-row {
            border-bottom: 1px solid rgba(255, 255, 255, 0.03);
            transition: background 0.15s;
        }

        .code-row:hover {
            background-color: rgba(56, 189, 248, 0.08);
        }

        .code-row.vector-row {
            background-color: rgba(34, 197, 94, 0.04);
        }

        .code-cell {
            padding: 5px 8px;
            vertical-align: top;
            white-space: nowrap;
        }

        .pc-cell {
            color: var(--accent-purple);
            font-weight: 600;
            width: 80px;
        }

        .bin-cell {
            color: #64748b;
            width: 75px;
        }

        .asm-cell {
            color: var(--text-main);
            font-weight: 500;
        }

        .asm-mnemonic {
            color: var(--accent-blue);
            font-weight: bold;
        }

        .asm-mnemonic.vec {
            color: var(--accent-green);
        }

        .badge-cell {
            text-align: right;
            padding-right: 8px;
        }

        .badge {
            display: inline-block;
            font-size: 0.65rem;
            padding: 1px 5px;
            border-radius: 4px;
            background: #334155;
            color: #cbd5e1;
            margin-left: 2px;
        }

        .badge-vec {
            background: #14532d;
            color: #86efac;
            border: 1px solid #22c55e;
        }

        /* Cajas de detalles y leyendas */
        .detail-box {
            background-color: rgba(15, 23, 42, 0.6);
            border: 1px solid var(--border-color);
            border-radius: 8px;
            padding: 12px;
            margin-bottom: 12px;
        }

        .detail-box h3 {
            font-size: 0.95rem;
            margin-bottom: 8px;
            color: var(--accent-blue);
            display: flex;
            justify-content: space-between;
        }

        .detail-row {
            display: flex;
            justify-content: space-between;
            font-size: 0.8rem;
            margin-bottom: 5px;
        }

        .detail-row .val {
            font-family: monospace;
            font-weight: bold;
        }

        /* Área Principal del Grafo */
        #graph-container {
            flex: 1;
            height: 100vh;
            position: relative;
            background: radial-gradient(circle, #1e293b 10%, #0f172a 90%);
        }

        #network {
            width: 100%;
            height: 100%;
        }

        #hint-overlay {
            position: absolute;
            bottom: 14px;
            right: 14px;
            background: rgba(15, 23, 42, 0.85);
            border: 1px solid var(--border-color);
            padding: 6px 12px;
            border-radius: 6px;
            font-size: 0.72rem;
            color: var(--text-muted);
            pointer-events: none;
        }
    </style>
</head>
<body>

    <!-- Panel Lateral Izquierdo -->
    <div id="sidebar">
        <div id="header">
            <h1><span>⚡</span> RAVE CFG & Code Inspector</h1>
            <p id="sub-title">Cargando datos del profiler...</p>
        </div>

        <div id="controls">
            <div class="upload-row">
                <label class="file-upload-btn" for="bb-file-input">📂 Grafo (BB JSON)</label>
                <input type="file" id="bb-file-input" accept=".json">

                <label class="file-upload-btn" for="trace-file-input" style="background-color: #0f766e;">📜 Traza (.jsonl)</label>
                <input type="file" id="trace-file-input" accept=".jsonl,.json">
            </div>

            <div style="display: flex; justify-content: space-between; align-items: center; margin-top: 4px;">
                <span style="font-size: 0.75rem; color: var(--text-muted);">Disposición:</span>
                <div style="display: flex; gap: 4px;">
                    <button id="btn-hierarchical" class="btn active" onclick="setLayout(true)">Jerárquico</button>
                    <button id="btn-physics" class="btn" onclick="setLayout(false)">Física</button>
                    <button class="btn" onclick="network.fit({animation: true})">🎯 Centrar</button>
                </div>
            </div>
            <div id="trace-status" style="font-size: 0.72rem; color: var(--accent-amber); margin-top: 2px;">
                Cargando traza de instrucciones...
            </div>
        </div>

        <div id="summary-stats">
            <div class="stat-card">
                <div class="lbl">Bloques Únicos</div>
                <div id="stat-total-bbs" class="val">-</div>
            </div>
            <div class="stat-card">
                <div class="lbl">Aristas (Caminos)</div>
                <div id="stat-total-edges" class="val">-</div>
            </div>
            <div class="stat-card">
                <div class="lbl">% Vectorial</div>
                <div id="stat-vec-ratio" class="val" style="color: var(--accent-green);">-</div>
            </div>
        </div>

        <!-- Pestañas: Código Ensamblador vs Métricas -->
        <div class="tab-bar">
            <button id="tab-code-btn" class="tab-btn active" onclick="switchTab('code')">💻 Código Ensamblador</button>
            <button id="tab-metrics-btn" class="tab-btn" onclick="switchTab('metrics')">📊 Métricas del Bloque</button>
        </div>

        <div id="pane-container">
            <!-- Pestaña 1: Código Ensamblador del Bloque -->
            <div id="tab-code" class="tab-content active">
                <div id="code-content">
                    <p style="color: var(--text-muted); font-size: 0.85rem; text-align: center; margin-top: 40px;">
                        👉 <b>Haz clic en cualquier bloque básico del grafo</b> para desplegar el código ensamblador que lo compone.
                    </p>
                </div>
            </div>

            <!-- Pestaña 2: Métricas Detalladas -->
            <div id="tab-metrics" class="tab-content">
                <div id="metrics-content">
                    <p style="color: var(--text-muted); font-size: 0.85rem; text-align: center; margin-top: 40px;">
                        Selecciona un nodo o arista en el grafo para inspeccionar su perfil de ejecución.
                    </p>
                </div>
            </div>
        </div>
    </div>

    <!-- Área Principal de Red (Grafo) -->
    <div id="graph-container">
        <div id="network"></div>
        <div id="hint-overlay">Rueda: Zoom | Arrastrar: Mover | Clic: Abrir código del bloque</div>
    </div>

    <script>
        // Datos iniciales inyectados desde Python
        let cfgData = __INITIAL_DATA__;
        let traceByPc = __TRACE_DATA__; // { pc_int: { pc, asm, op, binary, vl, sew, lmul, deps } }
        let network = null;
        let isHierarchical = true;
        let selectedNode = null;

        function updateTraceStatus() {
            const count = Object.keys(traceByPc).length;
            const el = document.getElementById('trace-status');
            if (count > 0) {
                el.innerText = `✓ Traza activa: ${count.toLocaleString()} instrucciones indexadas por PC`;
                el.style.color = "var(--accent-green)";
            } else {
                el.innerText = "⚠ Sin traza cargada. Carga un rave_inst_trace.jsonl para ver el código.";
                el.style.color = "var(--accent-amber)";
            }
        }

        function switchTab(tabName) {
            document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
            document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));

            if (tabName === 'code') {
                document.getElementById('tab-code-btn').classList.add('active');
                document.getElementById('tab-code').classList.add('active');
            } else {
                document.getElementById('tab-metrics-btn').classList.add('active');
                document.getElementById('tab-metrics').classList.add('active');
            }
        }

        function initGraph(data) {
            cfgData = data;
            const container = document.getElementById('network');
            const nodes = data.nodes || [];
            const edges = data.edges || [];

            let totalDynInsns = 0;
            let totalDynVec = 0;
            let maxExec = 1;

            nodes.forEach(n => {
                const total = n.metrics ? n.metrics.instructions.total : (n.num_insns || 0);
                const vec = n.metrics ? n.metrics.instructions.vector : (n.num_vec_insns || 0);
                const exec = n.metrics ? n.metrics.execution_count : (n.exec_count || 1);
                totalDynInsns += (total * exec);
                totalDynVec += (vec * exec);
                if (exec > maxExec) maxExec = exec;
            });

            document.getElementById('stat-total-bbs').innerText = nodes.length;
            document.getElementById('stat-total-edges').innerText = edges.length;
            const globalVecPct = totalDynInsns > 0 ? ((totalDynVec / totalDynInsns) * 100).toFixed(1) : "0.0";
            document.getElementById('stat-vec-ratio').innerText = globalVecPct + "%";
            document.getElementById('sub-title').innerText = `${nodes.length} bloques | ${edges.length} aristas`;

            // Preparar Nodos Vis.js
            const visNodes = nodes.map(n => {
                const total = n.metrics ? n.metrics.instructions.total : (n.num_insns || 0);
                const vec = n.metrics ? n.metrics.instructions.vector : (n.num_vec_insns || 0);
                const exec = n.metrics ? n.metrics.execution_count : (n.exec_count || 1);
                const ratio = total > 0 ? (vec / total) : 0;
                const dynShare = totalDynInsns > 0 ? ((total * exec) / totalDynInsns * 100) : 0;

                const startPc = n.pc ? n.pc.start : (n.start_pc || "");
                const endPc = n.pc ? n.pc.end : (n.end_pc || "");

                // Coloreado según densidad vectorial
                let bg = "#1e3a8a"; // Escalar
                let border = "#3b82f6";
                if (ratio > 0.5) {
                    bg = "#14532d"; // Vectorial
                    border = "#22c55e";
                } else if (ratio > 0) {
                    bg = "#713f12"; // Mezcla
                    border = "#eab308";
                }

                if (dynShare >= 5.0) border = "#f43f5e"; // Hotspot

                const label = `BB #${n.id}\n[${startPc.slice(-6)}..${endPc.slice(-6)}]\n${total} ins (${vec}v)\n${exec.toLocaleString()}x`;

                return {
                    id: n.id,
                    label: label,
                    shape: 'box',
                    margin: 8,
                    color: {
                        background: bg,
                        border: border,
                        highlight: { background: '#0284c7', border: '#38bdf8' }
                    },
                    font: { color: '#f8fafc', face: 'monospace', size: 11, bold: true },
                    borderWidth: dynShare >= 5.0 ? 3 : 1.5,
                    title: `BB #${n.id}: ${startPc} - ${endPc}`,
                    rawNode: n,
                    dynShare: dynShare
                };
            });

            // Preparar Aristas Vis.js
            let maxEdgeCount = 1;
            edges.forEach(e => { if (e.count > maxEdgeCount) maxEdgeCount = e.count; });

            const visEdges = edges.map((e, idx) => {
                const width = Math.max(1.2, Math.min(7, (e.count / maxEdgeCount) * 7));
                const isHeavy = (e.count / maxEdgeCount) > 0.2;

                return {
                    id: `edge_${idx}`,
                    from: e.src,
                    to: e.dst,
                    arrows: 'to',
                    width: width,
                    color: { color: isHeavy ? '#f43f5e' : '#64748b', highlight: '#38bdf8' },
                    label: e.count > 1 ? `${e.count.toLocaleString()}x` : '',
                    font: { color: '#94a3b8', size: 9, align: 'middle', background: 'rgba(15, 23, 42, 0.7)' },
                    smooth: { type: 'cubicBezier', forceDirection: isHierarchical ? 'vertical' : 'none', roundness: 0.3 },
                    rawEdge: e
                };
            });

            const graphData = {
                nodes: new vis.DataSet(visNodes),
                edges: new vis.DataSet(visEdges)
            };

            const options = {
                layout: {
                    hierarchical: isHierarchical ? {
                        direction: 'UD',
                        sortMethod: 'directed',
                        levelSeparation: 110,
                        nodeSpacing: 160,
                        treeSpacing: 180
                    } : false
                },
                physics: {
                    enabled: !isHierarchical,
                    barnesHut: { gravitationalConstant: -3000, springLength: 120, springConstant: 0.04 }
                },
                interaction: { hover: true, keyboard: true }
            };

            if (network) network.destroy();
            network = new vis.Network(container, graphData, options);

            // Clic en Nodo -> Mostrar Código y Métricas
            network.on("selectNode", function (params) {
                const nodeId = params.nodes[0];
                const found = visNodes.find(n => n.id === nodeId);
                if (found) {
                    selectedNode = found;
                    renderNodeCode(found.rawNode);
                    renderNodeMetrics(found.rawNode, found.dynShare);
                }
            });

            // Clic en Arista
            network.on("selectEdge", function (params) {
                if (params.nodes.length === 0 && params.edges.length > 0) {
                    const edgeId = params.edges[0];
                    const found = visEdges.find(e => e.id === edgeId);
                    if (found) renderEdgeMetrics(found.rawEdge);
                }
            });
        }

        // Renderiza el código ensamblador de un bloque básico seleccionado
        function renderNodeCode(n) {
            const startStr = n.pc ? n.pc.start : (n.start_pc || "0");
            const endStr = n.pc ? n.pc.end : (n.end_pc || "0");
            const startInt = parseInt(startStr, 16);
            const endInt = parseInt(endStr, 16);

            const minPc = Math.min(startInt, endInt);
            const maxPc = Math.max(startInt, endInt);

            // Filtrar instrucciones de la traza dentro del rango del bloque
            let matchedInsts = [];
            for (let [pcKey, inst] of Object.entries(traceByPc)) {
                const p = parseInt(pcKey, 10);
                if (p >= minPc && p <= maxPc) {
                    matchedInsts.push({ pcInt: p, ...inst });
                }
            }

            matchedInsts.sort((a, b) => a.pcInt - b.pcInt);

            let html = `
                <div class="detail-box">
                    <h3>Bloque Básico #${n.id} <span style="font-size: 0.75rem; color: var(--text-muted); font-family: monospace;">${startStr} .. ${endStr}</span></h3>
                    <div style="font-size: 0.78rem; color: var(--text-muted); margin-bottom: 6px;">
                        Instrucciones en el bloque: <b>${n.metrics ? n.metrics.instructions.total : n.num_insns}</b> |
                        Coincidencias en traza: <b style="color: var(--accent-green);">${matchedInsts.length}</b>
                    </div>
                </div>
            `;

            if (matchedInsts.length > 0) {
                html += `
                    <div class="code-viewer">
                        <div class="code-header">
                            <span>DIRECCIÓN | BINARIO | ENSAMBLADOR</span>
                            <span>PARÁMETROS RVV</span>
                        </div>
                        <table class="code-table">
                `;

                matchedInsts.forEach(inst => {
                    const isVec = (inst.op && inst.op.startsWith('v')) || (inst.asm && inst.asm.trim().startsWith('v'));
                    const mnemClass = isVec ? "asm-mnemonic vec" : "asm-mnemonic";

                    // Formatear el mnemónico y operandos
                    let asmText = inst.asm || inst.op || "";
                    let mnem = "";
                    let rest = "";
                    const parts = asmText.trim().split(/\s+/);
                    if (parts.length > 0) {
                        mnem = parts[0];
                        rest = asmText.slice(asmText.indexOf(mnem) + mnem.length);
                    }

                    let badges = "";
                    if (inst.vl !== undefined && inst.vl !== null) badges += `<span class="badge badge-vec">vl:${inst.vl}</span>`;
                    if (inst.sew) badges += `<span class="badge">sew:${inst.sew}</span>`;
                    if (inst.lmul) badges += `<span class="badge">lmul:${inst.lmul}</span>`;

                    html += `
                        <tr class="code-row ${isVec ? 'vector-row' : ''}">
                            <td class="code-cell pc-cell">${inst.pc}</td>
                            <td class="code-cell bin-cell">${inst.binary ? inst.binary.replace('0x','') : ''}</td>
                            <td class="code-cell asm-cell"><span class="${mnemClass}">${mnem}</span> <span style="color: #cbd5e1;">${rest}</span></td>
                            <td class="code-cell badge-cell">${badges}</td>
                        </tr>
                    `;
                });

                html += `
                        </table>
                    </div>
                `;
            } else {
                html += `
                    <div style="background: rgba(15, 23, 42, 0.6); padding: 20px; border-radius: 8px; border: 1px dashed var(--border-color); text-align: center; font-size: 0.8rem; color: var(--text-muted); margin-top: 10px;">
                        <p style="margin-bottom: 6px;">⚠ No se encontraron instrucciones en este rango [${startStr} - ${endStr}] dentro de la traza cargada.</p>
                        <p style="font-size: 0.72rem;">Asegúrate de que <code>rave_inst_trace.jsonl</code> corresponde a la misma ejecución del benchmark o carga la traza con el botón superior.</p>
                    </div>
                `;
            }

            document.getElementById('code-content').innerHTML = html;
        }

        // Renderiza las métricas en la pestaña de métricas
        function renderNodeMetrics(n, dynShare) {
            const total = n.metrics ? n.metrics.instructions.total : (n.num_insns || 0);
            const vec = n.metrics ? n.metrics.instructions.vector : (n.num_vec_insns || 0);
            const scalar = total - vec;
            const exec = n.metrics ? n.metrics.execution_count : (n.exec_count || 1);
            const ratio = total > 0 ? (vec / total * 100).toFixed(1) : 0;
            const startPc = n.pc ? n.pc.start : (n.start_pc || "");
            const endPc = n.pc ? n.pc.end : (n.end_pc || "");

            const html = `
                <div class="detail-box">
                    <h3>Bloque Básico #${n.id} <span style="color: var(--accent-rose); font-size: 0.8rem;">${dynShare.toFixed(2)}% del cómputo</span></h3>
                    <div class="detail-row"><span style="color: var(--text-muted);">PC Inicial:</span><span class="val">${startPc}</span></div>
                    <div class="detail-row"><span style="color: var(--text-muted);">PC Final:</span><span class="val">${endPc}</span></div>
                    <div class="detail-row"><span style="color: var(--text-muted);">Total Instrucciones:</span><span class="val">${total}</span></div>
                    <div class="detail-row"><span style="color: var(--text-muted);">Inst. Escalares:</span><span class="val">${scalar}</span></div>
                    <div class="detail-row"><span style="color: var(--text-muted);">Inst. Vectoriales:</span><span class="val" style="color: var(--accent-green);">${vec} (${ratio}%)</span></div>
                    <div class="detail-row"><span style="color: var(--text-muted);">Veces Ejecutado:</span><span class="val">${exec.toLocaleString()}</span></div>
                    <div class="detail-row"><span style="color: var(--text-muted);">Inst. Dinámicas:</span><span class="val">${(total * exec).toLocaleString()}</span></div>
                </div>
            `;
            document.getElementById('metrics-content').innerHTML = html;
        }

        function renderEdgeMetrics(e) {
            switchTab('metrics');
            const html = `
                <div class="detail-box">
                    <h3>Transición (Arista)</h3>
                    <div class="detail-row"><span style="color: var(--text-muted);">Bloque Origen:</span><span class="val">BB #${e.src}</span></div>
                    <div class="detail-row"><span style="color: var(--text-muted);">Bloque Destino:</span><span class="val">BB #${e.dst}</span></div>
                    <div class="detail-row"><span style="color: var(--text-muted);">Frecuencia de Salto:</span><span class="val" style="color: var(--accent-rose);">${e.count.toLocaleString()} veces</span></div>
                </div>
            `;
            document.getElementById('metrics-content').innerHTML = html;
        }

        function setLayout(hierarchical) {
            isHierarchical = hierarchical;
            document.getElementById('btn-hierarchical').classList.toggle('active', hierarchical);
            document.getElementById('btn-physics').classList.toggle('active', !hierarchical);
            initGraph(cfgData);
        }

        // Cargar otro BB JSON
        document.getElementById('bb-file-input').addEventListener('change', function (e) {
            const file = e.target.files[0];
            if (!file) return;
            const reader = new FileReader();
            reader.onload = function (event) {
                try {
                    initGraph(JSON.parse(event.target.result));
                } catch (err) {
                    alert("Error en el formato JSON: " + err.message);
                }
            };
            reader.readAsText(file);
        });

        // Cargar traza de instrucciones (.jsonl)
        document.getElementById('trace-file-input').addEventListener('change', function (e) {
            const file = e.target.files[0];
            if (!file) return;
            const reader = new FileReader();
            reader.onload = function (event) {
                const lines = event.target.result.split('\n');
                let count = 0;
                traceByPc = {};
                lines.forEach(line => {
                    line = line.trim();
                    if (!line) return;
                    try {
                        const rec = JSON.parse(line);
                        if (rec.record_type === "inst" && rec.pc) {
                            const pcInt = parseInt(rec.pc, 16);
                            traceByPc[pcInt] = rec;
                            count++;
                        }
                    } catch (e) {}
                });
                updateTraceStatus();
                if (selectedNode) renderNodeCode(selectedNode.rawNode);
            };
            reader.readAsText(file);
        });

        window.addEventListener('load', () => {
            updateTraceStatus();
            initGraph(cfgData);
        });
    </script>
</body>
</html>
"""


def load_inst_trace(trace_path):
    """Carga y parsea el archivo JSON Lines de la traza de instrucciones."""
    trace_by_pc = {}
    if not os.path.isfile(trace_path):
        return trace_by_pc

    print(f"[INFO] Leyendo traza de instrucciones desde '{trace_path}'...")
    try:
        with open(trace_path, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    record = json.loads(line)
                    if record.get("record_type") == "inst" and "pc" in record:
                        pc_str = record["pc"]
                        pc_int = int(pc_str, 16)
                        if pc_int not in trace_by_pc:
                            trace_by_pc[pc_int] = {
                                "pc": pc_str,
                                "asm": record.get("asm", record.get("op", "")),
                                "op": record.get("op", ""),
                                "binary": record.get("binary", ""),
                                "vl": record.get("vl", None),
                                "sew": record.get("sew", None),
                                "lmul": record.get("lmul", None),
                                "deps": record.get("deps", [])
                            }
                except json.JSONDecodeError:
                    continue
        print(f"[OK] Traza cargada: {len(trace_by_pc)} direcciones de instrucción únicas indexadas.")
    except Exception as e:
        print(f"[AVISO] Error al leer la traza '{trace_path}': {e}")

    return trace_by_pc


class CFGServer(SimpleHTTPRequestHandler):
    def __init__(self, *args, json_data=None, trace_data=None, **kwargs):
        self.json_data = json_data
        self.trace_data = trace_data
        super().__init__(*args, **kwargs)

    def do_GET(self):
        parsed_path = urllib.parse.urlparse(self.path)
        if parsed_path.path in ["/", "/index.html"]:
            self.send_response(200)
            self.send_header("Content-type", "text/html; charset=utf-8")
            self.end_headers()
            data_str = json.dumps(self.json_data if self.json_data else {"nodes": [], "edges": []})
            trace_str = json.dumps(self.trace_data if self.trace_data else {})
            html_content = HTML_TEMPLATE.replace("__INITIAL_DATA__", data_str).replace("__TRACE_DATA__", trace_str)
            self.wfile.write(html_content.encode("utf-8"))
        else:
            self.send_error(404, "Recurso no encontrado")

    def log_message(self, format, *args):
        pass


def main():
    parser = argparse.ArgumentParser(description="Visualizador Web de CFG y Código Ensamblador de RAVE")
    parser.add_argument("json_file", nargs="?", default="rave_bb.json", help="Ruta al JSON de bloques básicos (por defecto: rave_bb.json)")
    parser.add_argument("--trace", "-t", default=None, help="Ruta a la traza rave_inst_trace.jsonl")
    parser.add_argument("--port", type=int, default=8080, help="Puerto del servidor local (por defecto: 8080)")
    parser.add_argument("--no-browser", action="store_true", help="No abrir automáticamente el navegador")

    args = parser.parse_args()

    # Cargar datos de bloques básicos
    cfg_data = {"nodes": [], "edges": []}
    if os.path.isfile(args.json_file):
        try:
            with open(args.json_file, "r", encoding="utf-8") as f:
                cfg_data = json.load(f)
            print(f"[OK] Archivo '{args.json_file}' cargado ({len(cfg_data.get('nodes', []))} nodos, {len(cfg_data.get('edges', []))} aristas).")
        except Exception as e:
            print(f"[AVISO] No se pudo leer '{args.json_file}': {e}")
    else:
        print(f"[INFO] '{args.json_file}' no encontrado aún. Puedes cargarlo desde la interfaz web.")

    # Localizar y cargar traza de instrucciones
    trace_path = args.trace
    if not trace_path:
        for candidate in ["rave_inst_trace.jsonl", "../rave_inst_trace.jsonl"]:
            if os.path.isfile(candidate):
                trace_path = candidate
                break

    trace_data = load_inst_trace(trace_path) if trace_path else {}

    # Iniciar servidor local
    port = args.port
    handler = lambda *a, **k: CFGServer(*a, json_data=cfg_data, trace_data=trace_data, **k)

    server = None
    for p in range(port, port + 10):
        try:
            server = HTTPServer(("127.0.0.1", p), handler)
            port = p
            break
        except OSError:
            continue

    if not server:
        print(f"Error: Puertos {port}-{port+9} ocupados.", file=sys.stderr)
        sys.exit(1)

    url = f"http://localhost:{port}"
    print("\n" + "=" * 65)
    print("🚀 Visualizador Web RAVE CFG & Inspector de Código en marcha")
    print(f"👉 URL: {url}")
    print("Presiona Ctrl+C para detener el servidor.")
    print("=" * 65 + "\n")

    if not args.no_browser:
        webbrowser.open(url)

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nServidor web detenido.")
        server.server_close()


if __name__ == "__main__":
    main()
