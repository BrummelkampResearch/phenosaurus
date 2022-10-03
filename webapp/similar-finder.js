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
import $ from 'jquery';

import {geneSelectionEditor} from './gene-selection';
import { Plot, DotPlot, LabelPlot, HeatMapPlot, Screens } from './finder.js';

let nextGeneLineID = 1000;
let geneLines = [];

class GeneLine {
	constructor(hit) {
		const template = $("#plot > tbody:first > tr:first");
		const id = 'gene-' + nextGeneLineID++;
		const line = template.clone(true);
		const gene = hit.gene;

		const parent = hit.anti ? $("#plot > tbody.anti") : template.parent();

		line.attr("id", id)
			.prop("geneLine", this)
			.appendTo(parent)
			.show();

		this.line = line;
		this.row = line[0];
		this.data = [];

		this.row.cells[0].innerText = gene;
		this.row.cells[1].innerText = parseFloat(hit.zscore).toFixed(2);

		$("a", line)
			.click(() => this.sort());

		// const td = d3.select($("td:nth-child(2)", line)[0]);
		const td = d3.select(this.row.querySelector(".svg-container"));

		this.heatMap = new HeatMapPlot(td);
		this.heatMap.recreateSVG();

		this.dotPlot = new DotPlot(td);
		this.dotPlot.recreateSVG();

		geneLines.push(this);

		const uri = `finder/${hit.gene}`;
		const options = geneSelectionEditor.getOptions();

		fetch(uri,
			{
				credentials: "include",
				method: 'post',
				body: options
			})
			.then(data => {
				if (data.ok)
					return data.json();

				if (data.status == 403)
					throw "invalid-credentials";
			})
			.then(data => {
				this.data = data;

				Plot.preProcessData(data);

				this.heatMap.processData(data, gene);
				this.dotPlot.processData(data, gene);
			})
			.catch(err => {
				console.log(err)
			});
	}

	orderedScreenIDs() {
		return this.data
			.sort((a, b) => b.mi - a.mi)
			.map(a => a.screen);
	}

	sort() {
		const newOrder = this.orderedScreenIDs();

		Screens.instance().reorder(newOrder);

		$("#plot > tbody:first > tr:not(:first), #plot > tbody:last > tr")
			.each((i, e) => {
				const geneLine = $(e).prop("geneLine");
				geneLine.rearrange();
			});

		LabelPlot.rearrange();
	}

	rearrange() {
		this.heatMap.rearrange();
		this.dotPlot.rearrange();
	}
}

function doSearch() {

	let plotTitle = $(".plot-title");
	if (plotTitle.hasClass("plot-status-loading"))  // avoid multiple runs
		return;

	const geneName = document.getElementById("gene").value;
	$(".gene-name").text(geneName);
	plotTitle.addClass("plot-status-loading")
		.removeClass("plot-status-loaded")
		.removeClass("plot-status-failed")
		.removeClass("plot-status-no-hits");

	$("#plot > tbody:first > tr:not(:first-child)").remove();
	$("#plot > tbody:last > tr").remove();
	document.getElementById("plot").classList.add("no-anti");

	const options = geneSelectionEditor.getOptions();
	options.append("pv-cutoff", document.getElementById("pv-cut-off").value);
	options.append("zs-cutoff", document.getElementById("zscore-cut-off").value);

	const uri = `similar/${document.getElementById("gene").value}`;

	fetch(uri, {
		body: options,
		method: 'POST',
		credentials: "include"
	})
	.then(response => {
		if (response.ok)
			return response.json();

		response.json()
			.then(err => { throw err.error; })
			.catch(err => { throw err; });
	})
	.then(data => {
		if (data.findIndex((v) => v.anti) >= 0)
			document.getElementById("plot").classList.remove("no-anti");

		data.forEach(d => {
			new GeneLine(d);
		});

		plotTitle.removeClass("plot-status-loading")
			.toggleClass("plot-status-loaded", data.length > 0)
			.toggleClass("plot-status-no-hits", data.length === 0);
	})
	.catch(err => {
		console.log(err);
		plotTitle.removeClass("plot-status-loading").addClass("plot-status-failed");
	});
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

	document.getElementById("gene").onchange = doSearch;
	document.getElementById("pv-cut-off").onchange = () => {
		doSearch();
	};
	document.getElementById("zscore-cut-off").onchange = doSearch;
	document.getElementById("gene").onkeydown = (e) => {
		if (e.key === 'Enter')
			doSearch();
	};

	// start search?
	if (typeof params["gene"] === 'string') {
		document.getElementById("gene").value = params["gene"];
		doSearch();
	}
});