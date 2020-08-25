import 'bootstrap';
import 'bootstrap/js/dist/modal'
import 'chosen-js/chosen.jquery';
import * as d3 from 'd3';
import $ from 'jquery';

import {geneSelectionEditor} from './gene-selection';
import { Plot, DotPlot, LabelPlot, HeatMapPlot, Screens } from './finder.js';
import {readMyFile} from "./script";

let nextGeneLineID = 1000;
let geneLines = [];
let labels;

class GeneLine {
	constructor(gene) {
		const template = $("#plot > tbody > tr:first");
		const id = 'gene-' + nextGeneLineID++;
		const line = template.clone(true);

		line.attr("id", id)
			.prop("geneLine", this)
			.appendTo(template.parent())
			.show();

		this.line = line;
		this.row = line[0];
		this.input = $("input", line);
		this.data = [];

		$("a", line)
			.click(() => this.sort());

		this.input
			.change(() => this.changed())
			.focus();

		// const td = d3.select($("td:nth-child(2)", line)[0]);
		const td = d3.select(this.row.cells[2]);

		this.heatMap = new HeatMapPlot(td);
		this.heatMap.recreateSVG();

		this.dotPlot = new DotPlot(td);
		this.dotPlot.recreateSVG();

		if (gene === null)
			gene = this.input.val();

		if (gene !== "")
			this.setGene(gene);

		geneLines.push(this);
	}

	changed() {
		const genes = this.input.val().split(/[ \t\r\n,;]+/).filter(e => e.length > 0);

		this.input.removeClass("gene-not-found");

		if (genes.length > 0) {
			const gene = genes[0];
			this.input.val(gene);

			const options = geneSelectionEditor.getOptions();

			fetch(`finder/${gene}`, {
				method: 'post',
				credentials: "include",
				body: options
			}).then(data => {
				return data.json();
			}).then(data => {
				if (data.error != null)
					throw data.error;

				if (data.length === 0)
					this.input.addClass("gene-not-found");

				this.data = data;

				Plot.preProcessData(data);

				this.heatMap.processData(data, gene);
				this.dotPlot.processData(data, gene);

				// if (fishtail != null) {
				// 	const color = fishtail.processData(data, gene);

				// 	this.row.getElementsByClassName("swatch")[0]
				// 		.style.backgroundColor = color;

				// 	// this.spinnerTD.classList.remove("loading");
				// }
			}).catch(err => {

				// if (err === "invalid-credentials") {
				// 	showLoginDialog(null, () => this.changed());
				// }
				// else {
					this.input.addClass("gene-not-found");
					console.log(err)
				// }
			});

			genes.splice(0, 1);
			genes.forEach(id => new GeneLine(id));
		}
	}

	setGene(id) {
		this.input.val(id);
		this.changed();
	}

	orderedScreens() {
		return this.data
			.sort((a, b) => b.mi - a.mi)
			.map(a => a.screen);
	}

	sort() {
		const newOrder = this.orderedScreens();

		Screens.instance().reorder(newOrder);

		$("#plot > tbody > tr:not(:first)")
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

function addGeneLinesFromFile(evt) {
	const file = evt.target.files[0];
	if (file != null) {
		readMyFile(file)
			.then(text => {
				text.split(/[ \t\n\r]/)
					.filter(g => g.length > 0)
					.forEach(value => {
						try {
							new GeneLine(value)
						}
						catch (err) {
							console.log(err);
						}
					});
			})
			.catch(err => console.log(err));
	}
}

function selectPlotType(type) {
	switch (type) {
		case 'heatmap':
			$("#plot").removeClass("dotplot fishtail").addClass("heatmap").width("unset");
			$("#fishtailplot").hide();
			break;

		case 'dotplot':
			$("#plot").removeClass("heatmap fishtail").addClass("dotplot").width("unset");
			$("#fishtailplot").hide();
			break;
	}
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
	// document.getElementById("fishtail").onchange = () => selectPlotType('fishtail');
	
	// labels
	labels = new LabelPlot(d3.select("td.label-container"));
	labels.recreateSVG();
	
	$("#localGeneFile")
		.on("change", e => addGeneLinesFromFile(e));
	
	$("#add-gene-line")
		.on("click", () => new GeneLine());
	
	if (typeof params["gene"] === 'string') {
		new GeneLine(params["gene"]);
	}
	else  {
		new GeneLine();
	}
})
