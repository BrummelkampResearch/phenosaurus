import 'bootstrap';
import 'bootstrap/js/dist/modal'
import 'chosen-js/chosen.jquery';
import * as d3 from 'd3';
import $ from 'jquery';

import {geneSelectionEditor} from './gene-selection';
import { Plot, DotPlot, LabelPlot, HeatMapPlot, Screens } from './finder.js';

let nextGeneLineID = 1000;
let geneLines = [];
let labels;

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

		if (labels !== null) {
			labels.rearrange();
		}
	}

	rearrange() {
		this.heatMap.rearrange();
		this.dotPlot.rearrange();
	}
}

function selectPlotType(type) {
	switch (type) {
		case 'heatmap':
			$("#plot").removeClass("dotplot").addClass("heatmap");
			break;

		case 'dotplot':
			$("#plot").removeClass("heatmap").addClass("dotplot");
			break;
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

	document.getElementById("heatmap").onchange = () => selectPlotType('heatmap');
	document.getElementById("dotplot").onchange = () => selectPlotType('dotplot');

	document.getElementById("gene").onchange = doSearch;
	document.getElementById("pv-cut-off").onchange = () => {
		doSearch();
	};
	document.getElementById("zscore-cut-off").onchange = doSearch;
	document.getElementById("gene").onkeydown = (e) => {
		if (e.key === 'Enter')
			doSearch();
	};

	// labels
	labels = new LabelPlot(d3.select("td.label-container"));
	labels.recreateSVG();

	// start search?
	if (typeof params["gene"] === 'string') {
		document.getElementById("gene").value = params["gene"];
		doSearch();
	}
});