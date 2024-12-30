/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * 
 * Copyright (c) 2022 NKI/AVL, Netherlands Cancer Institute
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

import * as d3 from 'd3';

import { geneSelectionEditor } from './gene-selection';
import { Plot, DotPlot, LabelPlot, HeatMapPlot, Screens } from './finder.js';

let nextGeneLineID = 1000;
let geneLines = [];

class GeneLine {
	constructor(hit) {
		const template = document.querySelector("#gene-line-template");
		const clone = template.content.cloneNode(true);
		const line = clone.querySelector("tr");

		const id = 'gene-' + nextGeneLineID++;
		const gene = hit.gene;

		const parent = hit.anti
			? document.querySelector("#plot > tbody.anti")
			: document.querySelector("#plot > tbody");

		line.setAttribute("id", id);
		line.geneLine = this;
		parent.appendChild(line);

		this.line = line;
		this.data = [];

		line.querySelector("td:first-of-type").innerText = gene;
		line.querySelector("td:nth-of-type(2)").innerText = parseFloat(hit.zscore).toFixed(2);
		line.querySelector("a").addEventListener("click", () => this.sort());

		const td = d3.select(line.querySelector(".svg-container"));

		this.heatMap = new HeatMapPlot(td);
		this.heatMap.recreateSVG();

		this.dotPlot = new DotPlot(td);
		this.dotPlot.recreateSVG();

		geneLines.push(this);

		this.fetchData(hit);		
	}

	async fetchData(hit) {
		const uri = `finder/${hit.gene}`;
		const options = geneSelectionEditor.getOptions();

		try {
			const response = await fetch(uri,
				{
					credentials: "include",
					method: 'post',
					body: options
				});
			
			if (response.ok && response.status === 200) {
				const data = await response.json();

				this.data = data;

				Plot.preProcessData(data);
	
				this.heatMap.processData(data, gene);
				this.dotPlot.processData(data, gene);
			}
		} catch (error) {
			console.log(error);
		}
	}

	orderedScreenIDs() {
		return this.data
			.sort((a, b) => b.mi - a.mi)
			.map(a => a.screen);
	}

	sort() {
		const newOrder = this.orderedScreenIDs();

		Screens.instance().reorder(newOrder);

		[...document.querySelectorAll("#plot > tbody:first-of-type > tr, #plot > tbody:last-of-type > tr")]
			.forEach((e) => {
				const geneLine = e.geneLine;
				geneLine.rearrange();
			});

		LabelPlot.rearrange();
	}

	rearrange() {
		this.heatMap.rearrange();
		this.dotPlot.rearrange();
	}
}

async function doSearch() {

	let plotTitle = document.querySelector(".plot-title");
	if (plotTitle.classList.contains("plot-status-loading"))  // avoid multiple runs
		return;
	
	plotTitle.style.display = "";

	const geneName = document.getElementById("gene").value;
	[...document.querySelectorAll(".gene-name")].forEach(e => e.textContent = geneName);

	plotTitle.classList.add("plot-status-loading");
	plotTitle.classList.remove("plot-status-loaded");
	plotTitle.classList.remove("plot-status-failed");
	plotTitle.classList.remove("plot-status-no-hits");

	const plot = document.getElementById("plot");
	plot.querySelector("tbody:first-of-type").replaceChildren();
	plot.querySelector("tbody:last-of-type").replaceChildren();
	plot.classList.add("no-anti");

	const options = geneSelectionEditor.getOptions();
	options.append("pv-cutoff", document.getElementById("pv-cut-off").value);
	options.append("zs-cutoff", document.getElementById("zscore-cut-off").value);

	const uri = `similar/${document.getElementById("gene").value}`;

	try {
		const response = await fetch(uri, {
			body: options,
			method: 'POST',
			credentials: "include"
		});

		if (response.ok == false) {
			err = await response.json();
			throw err.error;
		}

		const data = await response.json();

		if (data.findIndex((v) => v.anti) >= 0)
			document.getElementById("plot").classList.remove("no-anti");

		data.forEach(d => {
			new GeneLine(d);
		});

		plotTitle.classList.remove("plot-status-loading");
		plotTitle.classList.toggle("plot-status-loaded", data.length > 0);
		plotTitle.classList.toggle("plot-status-no-hits", data.length === 0);
	}
	catch (err) {
		console.log(err);
		plotTitle.classList.remove("plot-status-loading");
		plotTitle.classList.add("plot-status-failed");
	};
}

window.addEventListener('load', () => {

	const query = window.location.search;
	const params = query
		? (/^[?#]/.test(query) ? query.slice(1) : query)
			.split('&')
			.reduce((params, param) => {
				let [key, value] = param.split('=');
				params[key] = value ? decodeURIComponent(value.replace(/\+/g, ' ')) : '';
				return params;
			}, {}
			)
		: {}

	document.getElementById("gene").addEventListener("change", doSearch);
	document.getElementById("pv-cut-off").addEventListener("change", doSearch);
	document.getElementById("zscore-cut-off").addEventListener("change", doSearch);
	document.getElementById("gene").addEventListener("keydown", (e) => {
		if (e.key === 'Enter')
			doSearch();
	});

	// start search?
	if (typeof params["gene"] === 'string') {
		document.getElementById("gene").value = params["gene"];
		doSearch();
	}
});