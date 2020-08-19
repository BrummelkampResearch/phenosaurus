import $ from 'jquery';
import 'bootstrap';
import 'bootstrap/js/dist/modal'
import * as d3 from 'd3';
import 'chosen-js/chosen.jquery';

import { geneSelectionEditor } from './gene-selection';

import Tooltip from './tooltip';
// import {readMyFile} from "./script";
// import ScreenPlot from "./screenPlot";
// import ScreenData from "./screenData";
// import {showLoginDialog} from "./index";

let pvCutOff = 0.05;
let nextGeneLineID = 1000;
let svgWidth = 1600;
let genelines = [];

let screenType = 'IP';

const radius = 3;
const neutral = "#ccc", positive = "#fb8", negative = "#38c";

// New style gene finder layout

/* global screenNames, selectedGene, context_name */

class Screens {
	constructor() {
		this.update();
	}

	update() {
		this.screens = screenNames;
	}

	* [Symbol.iterator]() {
		yield* this.screens;
	}

	scale() {
		return (screen) => {
			const ix = this.screens.indexOf(screen);
			if (ix < 0) {
				alert("Screen '" + screen + "' not found in index")
			}
			return ix;
		}
	}

	get count() {
		return this.screens.length;
	}

	reorder(newOrder) {
		this.screens = newOrder;
	}

	labelX(x) {
		return this.screens[x];
	}
}

let screens = null, labels = null;

const tooltip = new Tooltip();

// --------------------------------------------------------------------

class IPGeneDataTraits
{
	constructor() {
	}

	static label(d, geneID) {
		return "gene: " + geneID + "\n" +
			"screen: " + d.screen + "\n" +
			"mutational index: " + d3.format(".2f")(Math.log2(d.mi));
	}

	static clickGene(screen, gene) {
		window.open(`../ip/screen?screen=${screen}&highlightGene=${gene}`, "_blank");
	}

	static preProcessData(data) {
		data.forEach(d => {
			d.y = Math.log2(d.mi);
		});
	}

	static getDomain(data) {
		return [-6, 6, ...d3.extent(data, d => d.y)];
	}
}

class SLGeneDataTraits
{
	constructor() {
	}

	static label(d, gene) {
		return "gene: " + gene + "\n" +
			"screen: " + d.screen + "\n" +
			"sense ratio: " + d3.format(".2f")(d.mi) + "\n" +
			"replicate: " + d.replicate;
	}

	static clickGene(screen, gene, replicate) {
		window.open(`../sl/screen?screen=${screen}&highlightGene=${gene}&replicate=${replicate}`, "_blank");
	}

	static preProcessData(data) {
		data.forEach(d => {
			d.y = d.mi;
		});
	}

	static getDomain() {
		return [0, 1];
	}
}

// --------------------------------------------------------------------

function baseSelector() {
	switch (screenType) {
		case "IP": return IPGeneDataTraits;
		case "SL": return SLGeneDataTraits;
		default: throw "Invalid screen type";
	}
}

// --------------------------------------------------------------------

class Plot extends baseSelector() {
	constructor(td, height) {
		super();

		this.td = td;
		this.container = td;
		this.initialtHeight = height;
		this.height = height;
	}

	recreateSVG(className) {
		if (this.plotSVG != null)
			this.plotSVG.remove();

		this.plotSVG = this.td.append("svg")
			.attr("width", svgWidth)
			.attr("height", this.initialtHeight)
			.attr("class", className)

			.attr("preserveAspectRatio", "xMinYMin meet")
			.attr("viewBox", `0 0 ${svgWidth} ${this.initialtHeight}`)
			.classed("svg-content", true);
		this.margin = {top: 0, right: 10, bottom: 0, left: 55};

		this.height = this.initialtHeight;
		this.width = svgWidth - this.margin.left - this.margin.right;

		this.svg = this.plotSVG.append("g")
			.attr("transform", "translate(" + this.margin.left + "," + this.margin.top + ")");
	}
}

class HeatMapPlot extends Plot {

	constructor(td) {
		super(td, 15);
	}

	recreateSVG() {
		super.recreateSVG("heatmap");
		this.gridWidth = Math.floor(this.width / screens.count);
	}

	processData(data, gene) {
		const tiles = this.svg.selectAll(".tile")
			.data(data, d => d.screen);

		tiles.exit().remove();

		const xScale = screens.scale();
		this.x = d3.scaleLinear()
			.domain([0, screens.count])
			.range([0, this.width]);

		const yScale = d3.scaleSequential(d3.interpolatePiYG).domain(Plot.getDomain(data));

		tiles.enter()
			.append("rect")
			.attr("class", "tile")
			.attr("x", d => this.x(xScale(d.screen)))
			.attr("rx", 3)
			.attr("ry", 3)
			.attr("class", "tile bordered")
			.attr("width", this.gridWidth)
			.attr("height", this.height - 2)

			.on("mouseover", d => tooltip.show(Plot.label(d, gene), d3.event.pageX + 5, d3.event.pageY - 5))
			.on("mouseout", () => tooltip.hide())
			.on("click", d => Plot.clickGene(d.screen, gene, d.replicate))

			.merge(tiles)
			.style("fill", d => yScale(d.y));
	}

	rearrange() {
		const xScale = screens.scale();

		this.svg.selectAll(".tile")
			.transition()
			.duration(500)
			.attr("x", d => this.x(xScale(d.screen)));
	}
}

class DotPlot extends Plot {
	constructor(td) {
		super(td, 150);
	}

	recreateSVG() {
		super.recreateSVG("dotplot show-gridlines");

		// this.margin.left += 25;
		this.margin.top += 2 * radius;
		// this.width -= 25;
		this.height -= 2 * radius + 10;

		this.svg.attr("transform", "translate(" + this.margin.left + "," + this.margin.top + ")");

		this.x = d3.scaleLinear()
			.domain([0, screens.count - 1])
			.range([0, this.width]);

		this.y = d3.scaleLinear()
			.range([this.height, 0]);

		const xAxis = d3.axisBottom(this.x)
			.tickSizeInner(-this.height)
			.tickFormat(() => '');

		this.svg.append("g")
			.attr("class", "axis axis--x")
			.attr("transform", "translate(0," + this.height + ")")
			.call(xAxis);

		this.gY = this.svg.append("g")
			.attr("class", "axis axis--y");
	}

	processData(data, geneID) {
		const xScale = screens.scale();

		this.y.domain(Plot.getDomain(data));

		const yAxis = d3.axisLeft(this.y)
			.tickSizeInner(-this.width)
			.ticks(3);

		this.gY.call(yAxis);

		const dots = this.svg.selectAll("circle.dot")
			.data(data, d => d.screen);

		dots.exit()
			.remove();

		dots.enter()
			.append("circle")
			.attr("class", "dot")
			.attr("r", radius)
			.on("mouseover", d => tooltip.show(Plot.label(d, geneID), d3.event.pageX + 5, d3.event.pageY - 5))
			.on("mouseout", () => tooltip.hide())
			.on("click", d => Plot.clickGene(d.screen, geneID, d.replicate))
			.merge(dots)
			.attr("cx", d => this.x(xScale(d.screen)))
			.attr("cy", d => this.y(d.y))
			.style("fill", d => {
				return d.fcpv >= pvCutOff ? neutral : d.mi < 1 ? negative : positive;
			});
	}

	rearrange() {
		const xScale = screens.scale();

		this.svg.selectAll(".dot")
			.transition()
			.duration(500)
			.attr("cx", d => this.x(xScale(d.screen)));
	}
}

class LabelPlot extends Plot {
	constructor(td) {
		super(td, 200);
	}

	recreateSVG() {
		super.recreateSVG("");

		this.gX = this.svg.append("g")
			.attr("class", "axis axis--x");

		this.rearrange()
	}

	rearrange() {
		this.x = d3.scalePoint()
			.domain([...screens])
			.range([0, this.width]);

		const xAxis = d3.axisBottom(this.x);

		this.gX.call(xAxis)
			.selectAll("text")
			.attr("class", "axis-label")
			.style("text-anchor", "end")
			.style("font-size", "xx-small")
			.attr("dx", "-.8em")
			.attr("dy", ".15em")
			.attr("transform", "rotate(-60)");
	}
}

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

		genelines.push(this);

		const uri = `${context_name}ip/finder/${hit.gene}`;
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

		screens.reorder(newOrder);

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

	const uri = `${context_name}/ip/similar/${document.getElementById("gene").value}`;

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

	// labels = new ScreenLabels();
	screens = new Screens();

	document.getElementById("heatmap").onchange = () => selectPlotType('heatmap');
	document.getElementById("dotplot").onchange = () => selectPlotType('dotplot');

	document.getElementById("gene").onchange = doSearch;
	document.getElementById("pv-cut-off").onchange = () => {
		pvCutOff = document.getElementById("pv-cut-off").value;
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
	if (selectedGene != null)
		doSearch();
});