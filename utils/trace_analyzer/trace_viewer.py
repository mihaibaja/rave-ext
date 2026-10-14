#!/usr/bin/env python3
"""
Interactive Trace Viewer for RAVE Dynamic Instruction Traces.

Provides:
- An offline, self-contained interactive HTML/JS dashboard.
- Live post-processing simulation sliders (Lanes & Banks).
- Instruction stream inspection, register groups, and dependency tracking.
- Local HTTP server mode or standalone HTML export mode.
- Drag-and-drop support for any .jsonl trace file.
"""

from __future__ import annotations

import argparse
import http.server
import json
import os
import socketserver
import sys
import tempfile
import threading
import webbrowser
from pathlib import Path
from typing import Any, Dict, List, Optional


HTML_TEMPLATE = r"""<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>RAVE Instruction Trace Visualizer</title>
  <style>
    :root {
      --bg-dark: #0f172a;
      --bg-card: #1e293b;
      --bg-card-hover: #334155;
      --border: #334155;
      --text-main: #f8fafc;
      --text-muted: #94a3b8;
      --accent: #38bdf8;
      --accent-hover: #0284c7;
      --green: #4ade80;
      --orange: #fb923c;
      --purple: #c084fc;
      --red: #f87171;
      --yellow: #facc15;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
      background-color: var(--bg-dark);
      color: var(--text-main);
      display: flex;
      flex-direction: column;
      height: 100vh;
      overflow: hidden;
    }
    header {
      background-color: var(--bg-card);
      border-bottom: 1px solid var(--border);
      padding: 12px 24px;
      display: flex;
      justify-content: space-between;
      align-items: center;
      flex-shrink: 0;
    }
    .header-title {
      display: flex;
      align-items: center;
      gap: 12px;
    }
    .header-title h1 {
      font-size: 1.15rem;
      font-weight: 700;
      letter-spacing: -0.02em;
      color: var(--accent);
    }
    .badge {
      background: rgba(56, 189, 248, 0.15);
      color: var(--accent);
      border: 1px solid rgba(56, 189, 248, 0.3);
      padding: 2px 8px;
      border-radius: 9999px;
      font-size: 0.75rem;
      font-weight: 600;
    }
    .header-actions {
      display: flex;
      align-items: center;
      gap: 12px;
    }
    .btn {
      background: var(--bg-card-hover);
      color: var(--text-main);
      border: 1px solid var(--border);
      padding: 6px 14px;
      border-radius: 6px;
      font-size: 0.85rem;
      font-weight: 500;
      cursor: pointer;
      transition: all 0.15s ease;
      display: inline-flex;
      align-items: center;
      gap: 6px;
    }
    .btn:hover {
      background: var(--accent);
      color: var(--bg-dark);
      border-color: var(--accent);
    }
    .btn-primary {
      background: var(--accent);
      color: var(--bg-dark);
      border-color: var(--accent);
      font-weight: 600;
    }
    .btn-primary:hover {
      background: var(--accent-hover);
    }
    /* Tabs */
    .nav-tabs {
      display: flex;
      gap: 4px;
      background: var(--bg-card);
      padding: 4px 24px 0 24px;
      border-bottom: 1px solid var(--border);
      flex-shrink: 0;
    }
    .tab-btn {
      background: transparent;
      border: none;
      color: var(--text-muted);
      padding: 8px 16px;
      font-size: 0.85rem;
      font-weight: 600;
      cursor: pointer;
      border-bottom: 2px solid transparent;
      transition: all 0.15s;
    }
    .tab-btn:hover { color: var(--text-main); }
    .tab-btn.active {
      color: var(--accent);
      border-bottom-color: var(--accent);
    }
    /* Main Layout */
    main {
      flex: 1;
      display: flex;
      overflow: hidden;
      position: relative;
    }
    .tab-pane {
      display: none;
      width: 100%;
      height: 100%;
      overflow: auto;
    }
    .tab-pane.active {
      display: flex;
    }
    /* Dashboard Tab */
    #tab-dashboard {
      flex-direction: column;
      padding: 20px 24px;
      gap: 20px;
    }
    .stats-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
      gap: 16px;
    }
    .stat-card {
      background: var(--bg-card);
      border: 1px solid var(--border);
      border-radius: 8px;
      padding: 16px;
      display: flex;
      flex-direction: column;
      gap: 6px;
    }
    .stat-card .label {
      font-size: 0.75rem;
      text-transform: uppercase;
      letter-spacing: 0.05em;
      color: var(--text-muted);
    }
    .stat-card .value {
      font-size: 1.5rem;
      font-weight: 700;
      color: var(--text-main);
    }
    .panel-row {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 20px;
    }
    .panel {
      background: var(--bg-card);
      border: 1px solid var(--border);
      border-radius: 8px;
      padding: 18px;
      display: flex;
      flex-direction: column;
      gap: 12px;
    }
    .panel-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      border-bottom: 1px solid var(--border);
      padding-bottom: 8px;
    }
    .panel-header h2 {
      font-size: 0.95rem;
      font-weight: 600;
      color: var(--accent);
    }
    .controls-row {
      display: flex;
      align-items: center;
      gap: 16px;
      font-size: 0.85rem;
    }
    .slider-group {
      display: flex;
      align-items: center;
      gap: 8px;
    }
    .slider-group input[type="range"] {
      width: 130px;
      accent-color: var(--accent);
    }
    .slider-val {
      font-weight: 700;
      color: var(--accent);
      min-width: 24px;
    }
    /* SVG Charts */
    .chart-container {
      width: 100%;
      height: 200px;
      display: flex;
      align-items: center;
      justify-content: center;
    }
    svg.chart {
      width: 100%;
      height: 100%;
    }
    /* Instructions Tab */
    #tab-instructions {
      flex-direction: row;
    }
    .inst-list-pane {
      flex: 1;
      display: flex;
      flex-direction: column;
      border-right: 1px solid var(--border);
      overflow: hidden;
    }
    .inst-toolbar {
      padding: 10px 16px;
      background: var(--bg-card);
      border-bottom: 1px solid var(--border);
      display: flex;
      align-items: center;
      gap: 12px;
      flex-shrink: 0;
    }
    .inst-toolbar input[type="text"], .inst-toolbar select {
      background: var(--bg-dark);
      color: var(--text-main);
      border: 1px solid var(--border);
      padding: 6px 10px;
      border-radius: 6px;
      font-size: 0.82rem;
    }
    .inst-toolbar input[type="text"] { flex: 1; }
    .table-container {
      flex: 1;
      overflow: auto;
    }
    table.inst-table {
      width: 100%;
      border-collapse: collapse;
      font-size: 0.82rem;
      font-family: monospace;
    }
    table.inst-table th {
      background: var(--bg-card);
      color: var(--text-muted);
      padding: 8px 12px;
      text-align: left;
      border-bottom: 1px solid var(--border);
      position: sticky;
      top: 0;
      z-index: 10;
    }
    table.inst-table td {
      padding: 7px 12px;
      border-bottom: 1px solid rgba(51, 65, 85, 0.4);
      white-space: nowrap;
    }
    table.inst-table tr {
      cursor: pointer;
      transition: background 0.1s;
    }
    table.inst-table tr:hover {
      background: var(--bg-card-hover);
    }
    table.inst-table tr.selected {
      background: rgba(56, 189, 248, 0.18);
      border-left: 3px solid var(--accent);
    }
    /* Inspector Pane */
    .inspector-pane {
      width: 440px;
      background: var(--bg-card);
      overflow-y: auto;
      padding: 16px;
      display: flex;
      flex-direction: column;
      gap: 14px;
      flex-shrink: 0;
    }
    .inspector-pane h3 {
      font-size: 0.95rem;
      color: var(--accent);
      border-bottom: 1px solid var(--border);
      padding-bottom: 6px;
    }
    .inspector-card {
      background: var(--bg-dark);
      border: 1px solid var(--border);
      border-radius: 6px;
      padding: 12px;
      display: flex;
      flex-direction: column;
      gap: 8px;
      font-size: 0.82rem;
    }
    .kv-row {
      display: flex;
      justify-content: space-between;
      gap: 12px;
    }
    .kv-label { color: var(--text-muted); }
    .kv-val { font-weight: 600; font-family: monospace; }
    .chip-container {
      display: flex;
      flex-wrap: wrap;
      gap: 6px;
    }
    .chip {
      background: rgba(56, 189, 248, 0.12);
      color: var(--accent);
      border: 1px solid rgba(56, 189, 248, 0.25);
      border-radius: 4px;
      padding: 2px 6px;
      font-size: 0.75rem;
      font-family: monospace;
    }
    .chip.dst {
      background: rgba(74, 222, 128, 0.12);
      color: var(--green);
      border-color: rgba(74, 222, 128, 0.25);
    }
    .dep-item {
      display: flex;
      justify-content: space-between;
      padding: 4px 6px;
      border-radius: 4px;
      background: rgba(255, 255, 255, 0.03);
    }
    .dep-link {
      color: var(--accent);
      text-decoration: underline;
      cursor: pointer;
    }
    .drop-overlay {
      position: absolute;
      inset: 0;
      background: rgba(15, 23, 42, 0.9);
      border: 2px dashed var(--accent);
      display: none;
      align-items: center;
      justify-content: center;
      font-size: 1.5rem;
      font-weight: 600;
      color: var(--accent);
      z-index: 999;
    }
  </style>
</head>
<body>
  <div id="drop-overlay" class="drop-overlay">Drop RAVE Trace (.jsonl) to open</div>

  <header>
    <div class="header-title">
      <h1>RAVE Trace Explorer</h1>
      <span class="badge" id="badge-version">v1.0.0</span>
      <span class="badge" id="badge-workload" style="color: var(--green); border-color: var(--green);">Ready</span>
    </div>
    <div class="header-actions">
      <input type="file" id="file-input" accept=".jsonl,.txt" style="display: none;">
      <button class="btn" onclick="document.getElementById('file-input').click()">
        📁 Open Trace (.jsonl)
      </button>
    </div>
  </header>

  <nav class="nav-tabs">
    <button class="tab-btn active" onclick="switchTab('dashboard')">Dashboard & Models</button>
    <button class="tab-btn" onclick="switchTab('instructions')">Instruction Stream</button>
  </nav>

  <main>
    <!-- TAB 1: DASHBOARD & POST-PROCESSING -->
    <div id="tab-dashboard" class="tab-pane active">
      <div class="stats-grid">
        <div class="stat-card">
          <span class="label">Total Dynamic Instructions</span>
          <span class="value" id="stat-total-inst">0</span>
        </div>
        <div class="stat-card">
          <span class="label">Vector Instructions</span>
          <span class="value" id="stat-vec-inst">0</span>
        </div>
        <div class="stat-card">
          <span class="label">Memory Operations</span>
          <span class="value" id="stat-mem-inst">0</span>
        </div>
        <div class="stat-card">
          <span class="label">Active Vector Elements</span>
          <span class="value" id="stat-active-elems">0</span>
        </div>
        <div class="stat-card">
          <span class="label">VLEN / ELEN</span>
          <span class="value" id="stat-vlen-elen">0 / 0</span>
        </div>
      </div>

      <div class="panel-row">
        <!-- Lane Model -->
        <div class="panel">
          <div class="panel-header">
            <h2>Lane Workload & Crossbar Movement</h2>
            <div class="controls-row">
              <div class="slider-group">
                <label for="lane-slider">Lanes:</label>
                <input type="range" id="lane-slider" min="1" max="32" value="4" oninput="updateLaneModel()">
                <span id="lane-val" class="slider-val">4</span>
              </div>
            </div>
          </div>
          <div class="chart-container" id="chart-lanes"></div>
          <div class="controls-row" style="justify-content: space-between; font-size: 0.78rem; color: var(--text-muted);">
            <span>Cross-lane Communications: <strong id="val-cross-lane" style="color: var(--orange);">0</strong></span>
            <span>Imbalance Ratio (Max/Min): <strong id="val-lane-imbalance" style="color: var(--text-main);">1.00</strong></span>
          </div>
        </div>

        <!-- Bank Model -->
        <div class="panel">
          <div class="panel-header">
            <h2>Memory Bank Access & Conflicts</h2>
            <div class="controls-row">
              <div class="slider-group">
                <label for="bank-slider">Banks:</label>
                <input type="range" id="bank-slider" min="2" max="32" value="8" oninput="updateBankModel()">
                <span id="bank-val" class="slider-val">8</span>
              </div>
              <div class="slider-group">
                <label for="bank-width-select">Width:</label>
                <select id="bank-width-select" onchange="updateBankModel()" style="background:var(--bg-dark); color:var(--text-main); border:1px solid var(--border); padding:2px 6px; border-radius:4px;">
                  <option value="4">4B</option>
                  <option value="8" selected>8B</option>
                  <option value="16">16B</option>
                </select>
              </div>
            </div>
          </div>
          <div class="chart-container" id="chart-banks"></div>
          <div class="controls-row" style="justify-content: space-between; font-size: 0.78rem; color: var(--text-muted);">
            <span>Evaluated Memory Instructions: <strong id="val-mem-eval" style="color: var(--text-main);">0</strong></span>
            <span>Total Bank Conflicts: <strong id="val-conflicts" style="color: var(--red);">0</strong></span>
          </div>
        </div>
      </div>
    </div>

    <!-- TAB 2: INSTRUCTION STREAM -->
    <div id="tab-instructions" class="tab-pane">
      <div class="inst-list-pane">
        <div class="inst-toolbar">
          <input type="text" id="search-input" placeholder="Search opcode, register, PC (e.g. vle32, v8, 0x101)..." oninput="filterTable()">
          <select id="category-select" onchange="filterTable()">
            <option value="ALL">All Categories</option>
            <option value="VECTOR_LOAD">Vector Load</option>
            <option value="VECTOR_STORE">Vector Store</option>
            <option value="VECTOR_ARITH">Vector Arith</option>
            <option value="VECTOR_GATHER">Vector Gather</option>
            <option value="VECTOR_SLIDE">Vector Slide</option>
            <option value="VECTOR_COMPRESS">Vector Compress</option>
            <option value="VSETVL">VSETVL</option>
            <option value="SCALAR">Scalar</option>
          </select>
          <label style="font-size: 0.8rem; display: flex; align-items: center; gap: 4px; cursor: pointer;">
            <input type="checkbox" id="check-masked" onchange="filterTable()"> Masked only
          </label>
        </div>
        <div class="table-container">
          <table class="inst-table">
            <thead>
              <tr>
                <th>Seq#</th>
                <th>PC</th>
                <th>Instruction</th>
                <th>Category</th>
                <th>Dst</th>
                <th>Srcs</th>
                <th>VL</th>
                <th>SEW</th>
                <th>LMUL</th>
                <th>Active</th>
                <th>Aux</th>
              </tr>
            </thead>
            <tbody id="inst-tbody"></tbody>
          </table>
        </div>
      </div>

      <!-- Detail Inspector -->
      <div class="inspector-pane" id="inspector-pane">
        <h3>Instruction Detail</h3>
        <div class="inspector-card" id="detail-summary">
          <p style="color: var(--text-muted);">Select an instruction to inspect details.</p>
        </div>

        <h3>Register Groups</h3>
        <div class="inspector-card" id="detail-regs">
          <div class="kv-row"><span class="kv-label">Destination:</span><div class="chip-container" id="detail-dst-chips">-</div></div>
          <div class="kv-row"><span class="kv-label">Sources:</span><div class="chip-container" id="detail-src-chips">-</div></div>
        </div>

        <h3>Dependencies & Producers</h3>
        <div class="inspector-card" id="detail-deps">
          <p style="color: var(--text-muted);">No dependencies.</p>
        </div>

        <h3>Auxiliary Records</h3>
        <div class="inspector-card" id="detail-aux">
          <p style="color: var(--text-muted);">No auxiliary records.</p>
        </div>
      </div>
    </div>
  </main>

  <script>
    // Embedded trace payload injected by Python server or file exporter
    let traceData = EMBEDDED_TRACE_DATA_PLACEHOLDER;

    let headerRecord = null;
    let instructions = [];
    let selectedSeqno = null;

    function init() {
      setupDragAndDrop();
      document.getElementById('file-input').addEventListener('change', handleFileSelect);
      if (traceData && traceData.length > 0) {
        parseTraceLines(traceData);
      }
    }

    function switchTab(tabId) {
      document.querySelectorAll('.nav-tabs .tab-btn').forEach(btn => btn.classList.remove('active'));
      document.querySelectorAll('.tab-pane').forEach(p => p.classList.remove('active'));
      if (tabId === 'dashboard') {
        document.querySelectorAll('.nav-tabs .tab-btn')[0].classList.add('active');
        document.getElementById('tab-dashboard').classList.add('active');
      } else {
        document.querySelectorAll('.nav-tabs .tab-btn')[1].classList.add('active');
        document.getElementById('tab-instructions').classList.add('active');
      }
    }

    function setupDragAndDrop() {
      const overlay = document.getElementById('drop-overlay');
      window.addEventListener('dragover', (e) => { e.preventDefault(); overlay.style.display = 'flex'; });
      overlay.addEventListener('dragleave', (e) => { e.preventDefault(); overlay.style.display = 'none'; });
      overlay.addEventListener('drop', (e) => {
        e.preventDefault();
        overlay.style.display = 'none';
        if (e.dataTransfer.files.length > 0) {
          loadFile(e.dataTransfer.files[0]);
        }
      });
    }

    function handleFileSelect(e) {
      if (e.target.files.length > 0) {
        loadFile(e.target.files[0]);
      }
    }

    function loadFile(file) {
      const reader = new FileReader();
      reader.onload = (e) => {
        const text = e.target.result;
        const lines = text.split(/\r?\n/).filter(l => l.trim().length > 0);
        parseTraceLines(lines);
      };
      reader.readAsText(file);
    }

    function parseTraceLines(lines) {
      headerRecord = null;
      instructions = [];
      const instMap = new Map();

      for (let i = 0; i < lines.length; i++) {
        let obj;
        try {
          obj = typeof lines[i] === 'string' ? JSON.parse(lines[i]) : lines[i];
        } catch (e) {
          continue;
        }
        if (!obj || !obj.record_type) continue;

        if (obj.record_type === 'header') {
          headerRecord = obj;
        } else if (obj.record_type === 'inst') {
          obj.aux_records = [];
          instructions.push(obj);
          instMap.set(obj.seqno, obj);
        } else if (obj.record_type === 'aux') {
          const parent = instMap.get(obj.seqno);
          if (parent) {
            parent.aux_records.push(obj);
          }
        }
      }

      updateHeaderUI();
      renderDashboard();
      renderInstructionTable();
      if (instructions.length > 0) {
        selectInstruction(instructions[0].seqno);
      }
    }

    function updateHeaderUI() {
      if (headerRecord) {
        document.getElementById('badge-version').textContent = 'Schema v' + headerRecord.schema_version;
        document.getElementById('badge-workload').textContent = headerRecord.workload || 'Trace';
        document.getElementById('stat-vlen-elen').textContent = `${headerRecord.vlen} / ${headerRecord.elen}`;
      }
    }

    function renderDashboard() {
      document.getElementById('stat-total-inst').textContent = instructions.length;
      const vecCount = instructions.filter(i => (i.decoded_op || '').startsWith('VECTOR_')).length;
      document.getElementById('stat-vec-inst').textContent = vecCount;
      const memCount = instructions.filter(i => (i.decoded_op || '').includes('LOAD') || (i.decoded_op || '').includes('STORE')).length;
      document.getElementById('stat-mem-inst').textContent = memCount;
      const activeSum = instructions.reduce((acc, i) => acc + (i.active_elements || 0), 0);
      document.getElementById('stat-active-elems').textContent = activeSum.toLocaleString();

      updateLaneModel();
      updateBankModel();
    }

    function updateLaneModel() {
      const numLanes = parseInt(document.getElementById('lane-slider').value, 10);
      document.getElementById('lane-val').textContent = numLanes;

      const laneLoads = new Array(numLanes).fill(0);
      let crossLaneMoves = 0;

      for (const inst of instructions) {
        if (!inst.decoded_op || !inst.decoded_op.startsWith('VECTOR_') || !inst.active_elements) continue;
        const active = inst.active_elements;
        const vstart = inst.vstart || 0;
        for (let i = vstart; i < vstart + active; i++) {
          laneLoads[i % numLanes]++;
        }

        const gatherAux = inst.aux_records.find(a => a.aux_type === 'gather_indices');
        if (gatherAux && gatherAux.indices) {
          gatherAux.indices.forEach((srcElem, dstElem) => {
            if ((dstElem % numLanes) !== (srcElem % numLanes)) crossLaneMoves++;
          });
        }
        const slideAux = inst.aux_records.find(a => a.aux_type === 'slide_compress');
        if (slideAux && slideAux.offset !== undefined) {
          const off = slideAux.offset;
          for (let d = 0; d < active; d++) {
            if ((d % numLanes) !== ((d + off) % numLanes)) crossLaneMoves++;
          }
        }
      }

      document.getElementById('val-cross-lane').textContent = crossLaneMoves.toLocaleString();
      const maxL = Math.max(...laneLoads, 1);
      const minL = Math.min(...laneLoads, 1);
      document.getElementById('val-lane-imbalance').textContent = (maxL / minL).toFixed(2);

      renderBarChart('chart-lanes', laneLoads.map((v, i) => ({ label: 'L' + i, val: v })), '#38bdf8');
    }

    function updateBankModel() {
      const numBanks = parseInt(document.getElementById('bank-slider').value, 10);
      document.getElementById('bank-val').textContent = numBanks;
      const bankWidth = parseInt(document.getElementById('bank-width-select').value, 10);

      const bankHits = new Array(numBanks).fill(0);
      let totalMemInsts = 0;
      let totalConflicts = 0;

      for (const inst of instructions) {
        const memAux = inst.aux_records.find(a => a.aux_type === 'mem_addrs');
        if (!memAux || !memAux.deltas || memAux.deltas.length === 0) continue;

        totalMemInsts++;
        const instHits = new Map();
        for (const delta of memAux.deltas) {
          const bankId = Math.floor(Math.abs(delta) / bankWidth) % numBanks;
          bankHits[bankId]++;
          instHits.set(bankId, (instHits.get(bankId) || 0) + 1);
        }
        for (const count of instHits.values()) {
          if (count > 1) totalConflicts += count - 1;
        }
      }

      document.getElementById('val-mem-eval').textContent = totalMemInsts;
      document.getElementById('val-conflicts').textContent = totalConflicts.toLocaleString();

      renderBarChart('chart-banks', bankHits.map((v, i) => ({ label: 'B' + i, val: v })), '#f87171');
    }

    function renderBarChart(containerId, data, color) {
      const container = document.getElementById(containerId);
      if (!data || data.length === 0) {
        container.innerHTML = '<span style="color:var(--text-muted);font-size:0.8rem;">No data</span>';
        return;
      }
      const maxVal = Math.max(...data.map(d => d.val), 1);
      const w = 400;
      const h = 180;
      const pad = 30;
      const barW = Math.max(4, Math.floor((w - pad * 2) / data.length) - 3);

      let svg = `<svg class="chart" viewBox="0 0 ${w} ${h}">`;
      svg += `<line x1="${pad}" y1="${h - 25}" x2="${w - 10}" y2="${h - 25}" stroke="var(--border)" stroke-width="1"/>`;

      data.forEach((d, idx) => {
        const barH = (d.val / maxVal) * (h - 50);
        const x = pad + idx * ((w - pad * 2) / data.length);
        const y = h - 25 - barH;
        svg += `<rect x="${x}" y="${y}" width="${barW}" height="${barH}" rx="2" fill="${color}" opacity="0.85">
                  <title>${d.label}: ${d.val}</title>
                </rect>`;
        if (data.length <= 16) {
          svg += `<text x="${x + barW / 2}" y="${h - 10}" fill="var(--text-muted)" font-size="9" text-anchor="middle">${d.label}</text>`;
        }
      });
      svg += `</svg>`;
      container.innerHTML = svg;
    }

    function renderInstructionTable() {
      const tbody = document.getElementById('inst-tbody');
      tbody.innerHTML = '';

      instructions.forEach(inst => {
        const tr = document.createElement('tr');
        tr.id = 'row-' + inst.seqno;
        tr.onclick = () => selectInstruction(inst.seqno);

        const dstStr = (inst.dst || []).join(', ');
        const srcStr = (inst.srcs || []).join(', ');
        const auxLen = (inst.aux_records || []).length;
        const auxBadge = auxLen > 0 ? `<span class="badge" style="background:rgba(192,132,252,0.15);color:var(--purple);border-color:var(--purple);">${auxLen}</span>` : '-';

        tr.innerHTML = `
          <td style="color:var(--text-muted);">${inst.seqno}</td>
          <td>${inst.pc}</td>
          <td style="color:var(--accent);font-weight:600;">${inst.asm || inst.op}</td>
          <td style="color:var(--text-muted);font-size:0.75rem;">${inst.decoded_op}</td>
          <td style="color:var(--green);">${dstStr || '-'}</td>
          <td style="color:var(--orange);">${srcStr || '-'}</td>
          <td>${inst.vl !== undefined ? inst.vl : '-'}</td>
          <td>${inst.sew || '-'}</td>
          <td>${inst.lmul || '-'}</td>
          <td>${inst.active_elements !== undefined ? inst.active_elements : '-'}</td>
          <td>${auxBadge}</td>
        `;
        tbody.appendChild(tr);
      });
    }

    function filterTable() {
      const q = document.getElementById('search-input').value.toLowerCase();
      const cat = document.getElementById('category-select').value;
      const maskedOnly = document.getElementById('check-masked').checked;

      instructions.forEach(inst => {
        const row = document.getElementById('row-' + inst.seqno);
        if (!row) return;

        let visible = true;
        if (q) {
          const fullText = `${inst.seqno} ${inst.pc} ${inst.asm} ${inst.op} ${(inst.dst||[]).join(' ')} ${(inst.srcs||[]).join(' ')}`.toLowerCase();
          if (!fullText.includes(q)) visible = false;
        }
        if (cat !== 'ALL') {
          if (!inst.decoded_op || !inst.decoded_op.includes(cat)) visible = false;
        }
        if (maskedOnly && !inst.masked) {
          visible = false;
        }

        row.style.display = visible ? '' : 'none';
      });
    }

    function selectInstruction(seqno) {
      selectedSeqno = seqno;
      document.querySelectorAll('#inst-tbody tr').forEach(r => r.classList.remove('selected'));
      const row = document.getElementById('row-' + seqno);
      if (row) row.classList.add('selected');

      const inst = instructions.find(i => i.seqno === seqno);
      if (!inst) return;

      // Summary Card
      document.getElementById('detail-summary').innerHTML = `
        <div class="kv-row"><span class="kv-label">SeqNo:</span><span class="kv-val">${inst.seqno}</span></div>
        <div class="kv-row"><span class="kv-label">Hart / Thread:</span><span class="kv-val">${inst.hart}</span></div>
        <div class="kv-row"><span class="kv-label">Program Counter:</span><span class="kv-val" style="color:var(--accent);">${inst.pc}</span></div>
        <div class="kv-row"><span class="kv-label">Binary:</span><span class="kv-val">${inst.binary}</span></div>
        <div class="kv-row"><span class="kv-label">Assembly:</span><span class="kv-val" style="color:var(--green);">${inst.asm}</span></div>
        <div class="kv-row"><span class="kv-label">Decoded Op:</span><span class="kv-val">${inst.decoded_op}</span></div>
        <div class="kv-row"><span class="kv-label">VL / SEW / LMUL:</span><span class="kv-val">${inst.vl} / ${inst.sew}b / ${inst.lmul}</span></div>
        <div class="kv-row"><span class="kv-label">VTA / VMA / vstart:</span><span class="kv-val">${inst.vta} / ${inst.vma} / ${inst.vstart}</span></div>
        <div class="kv-row"><span class="kv-label">Masked:</span><span class="kv-val" style="color:${inst.masked ? 'var(--orange)' : 'var(--text-muted)'};">${inst.masked ? 'Yes (v0.t)' : 'No'}</span></div>
        <div class="kv-row"><span class="kv-label">Active Elements:</span><span class="kv-val">${inst.active_elements}</span></div>
      `;

      // Regs Card
      const dstChips = (inst.dst || []).map(r => `<span class="chip dst">${r}</span>`).join('') || '<span style="color:var(--text-muted);">-</span>';
      const srcChips = (inst.srcs || []).map(r => `<span class="chip">${r}</span>`).join('') || '<span style="color:var(--text-muted);">-</span>';
      document.getElementById('detail-dst-chips').innerHTML = dstChips;
      document.getElementById('detail-src-chips').innerHTML = srcChips;

      // Dependencies Card
      const depsContainer = document.getElementById('detail-deps');
      if (inst.deps && inst.deps.length > 0) {
        depsContainer.innerHTML = inst.deps.map(d => `
          <div class="dep-item">
            <span><strong>${d.operand}</strong> (${d.dep_type})</span>
            <span>Producer: <a class="dep-link" onclick="jumpToSeqno(${d.producer_seqno})">#${d.producer_seqno}</a> (dist: ${d.distance})</span>
          </div>
        `).join('');
      } else {
        depsContainer.innerHTML = '<p style="color:var(--text-muted);">No dependencies recorded.</p>';
      }

      // Aux Card
      const auxContainer = document.getElementById('detail-aux');
      if (inst.aux_records && inst.aux_records.length > 0) {
        auxContainer.innerHTML = inst.aux_records.map(a => {
          let extra = '';
          if (a.aux_type === 'mask_bitmap') {
            extra = `<div>Bitmap: <code style="color:var(--orange);">${a.mask_bitmap}</code> (Active: ${a.active_mask_count}/${a.total_elements})</div>`;
          } else if (a.aux_type === 'mem_addrs') {
            const deltasPreview = (a.deltas || []).slice(0, 8).join(', ') + ((a.deltas || []).length > 8 ? '...' : '');
            extra = `<div>Base: <code>${a.base_addr}</code> | Stride: ${a.stride_bytes}B</div>
                     <div>Deltas: [${deltasPreview}]</div>`;
          } else if (a.aux_type === 'gather_indices') {
            const idxPreview = (a.indices || []).slice(0, 8).join(', ') + ((a.indices || []).length > 8 ? '...' : '');
            extra = `<div>Gather Indices: [${idxPreview}] (Range: ${a.min_index} - ${a.max_index})</div>`;
          } else if (a.aux_type === 'slide_compress') {
            extra = `<div>Type: ${a.sub_type} ${a.offset !== undefined ? '| Offset: ' + a.offset : ''} ${a.compacted_count !== undefined ? '| Compacted: ' + a.compacted_count : ''}</div>`;
          }
          return `
            <div style="border-bottom:1px solid rgba(255,255,255,0.06); padding-bottom:6px;">
              <span class="badge" style="margin-bottom:4px; display:inline-block;">${a.aux_type}</span>
              ${extra}
            </div>
          `;
        }).join('');
      } else {
        auxContainer.innerHTML = '<p style="color:var(--text-muted);">No auxiliary records.</p>';
      }
    }

    function jumpToSeqno(targetSeqno) {
      switchTab('instructions');
      const row = document.getElementById('row-' + targetSeqno);
      if (row) {
        row.scrollIntoView({ behavior: 'smooth', block: 'center' });
        selectInstruction(targetSeqno);
      }
    }

    window.addEventListener('DOMContentLoaded', init);
  </script>
</body>
</html>
"""


def load_trace_json_lines(trace_path: str) -> List[Dict[str, Any]]:
    """Loads all lines from a trace file into an array of dicts."""
    records = []
    with open(trace_path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line:
                try:
                    records.append(json.loads(line))
                except json.JSONDecodeError:
                    continue
    return records


def generate_html_content(trace_records: Optional[List[Dict[str, Any]]] = None) -> str:
    """Generates the interactive HTML page with optional embedded trace records."""
    if trace_records is None:
        payload_js = "[]"
    else:
        payload_js = json.dumps(trace_records)

    return HTML_TEMPLATE.replace("EMBEDDED_TRACE_DATA_PLACEHOLDER", payload_js)


def run_local_server(html_content: str, port: int = 8080, open_browser: bool = True) -> None:
    """Runs a local lightweight HTTP server serving the interactive visualizer."""
    class TraceHTTPHandler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.end_headers()
            self.wfile.write(html_content.encode("utf-8"))

        def log_message(self, format, *args):
            # Silence standard HTTP access logging to keep terminal tidy
            pass

    server_address = ("127.0.0.1", port)
    # Try finding an open port if requested port is busy
    for p in range(port, port + 100):
        try:
            httpd = socketserver.TCPServer(("127.0.0.1", p), TraceHTTPHandler)
            port = p
            break
        except OSError:
            continue
    else:
        print(f"Error: Could not bind server on ports {port}-{port+100}", file=sys.stderr)
        return

    url = f"http://127.0.0.1:{port}"
    print(f"\n=======================================================")
    print(f" RAVE Instruction Trace Visualizer running at:")
    print(f" => {url}")
    print(f" (Press Ctrl+C to stop the server)")
    print(f"=======================================================\n")

    if open_browser:
        threading.Timer(0.4, lambda: webbrowser.open(url)).start()

    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping visualizer server...")
    finally:
        httpd.server_close()


def main():
    parser = argparse.ArgumentParser(
        description="Launch an interactive visualizer for RAVE instruction traces."
    )
    parser.add_argument("trace_file", nargs="?", default=None, help="Path to RAVE .jsonl trace file")
    parser.add_argument("--port", type=int, default=8080, help="Port for the local visualizer server (default: 8080)")
    parser.add_argument("--export", type=str, default=None, help="Export a standalone HTML file instead of starting server")
    parser.add_argument("--no-browser", action="store_true", help="Do not automatically open the default web browser")
    args = parser.parse_args()

    records = None
    if args.trace_file:
        if not os.path.exists(args.trace_file):
            print(f"Error: File not found: {args.trace_file}", file=sys.stderr)
            sys.exit(1)
        records = load_trace_json_lines(args.trace_file)

    html_content = generate_html_content(records)

    if args.export:
        out_path = Path(args.export).resolve()
        with open(out_path, "w", encoding="utf-8") as f:
            f.write(html_content)
        print(f"[OK] Exported interactive visualizer to: {out_path}")
    else:
        run_local_server(html_content, port=args.port, open_browser=not args.no_browser)


if __name__ == "__main__":
    main()
