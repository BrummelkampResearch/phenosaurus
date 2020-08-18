import 'bootstrap';
import 'bootstrap/js/dist/modal'
import 'bootstrap/dist/css/bootstrap.min.css';
import '@fortawesome/fontawesome-free/css/all.min.css';
import 'chosen-js/chosen.jquery';
import 'bootstrap4c-chosen/dist/css/component-chosen.min.css';

import $ from 'jquery';

import * as d3 from 'd3';

import Tooltip from './tooltip';
import {readMyFile} from "./script";
import ScreenPlot from "./screenPlot";
import ScreenData from "./screenData";
// import {showLoginDialog} from "./index";

/* global screenType, context_name */

let pvCutOff = 0.05;
let nextGeneLineID = 1000;
let svgWidth = 1600;
let fishtail = null;
let genelines = [];

const radius = 3;
const neutral = "#ccc", positive = "#fb8", negative = "#38c";

// New style gene finder layout

class Screens {
	constructor() {
		const e = Array.from(document.getElementsByClassName("screen-id"));
		this.known = new Map(e.map(v => [v.dataset.id, v.dataset.name]));
		this.screenIDs = Array.from(this.known.keys()).map(k => +k);
	}

	* [Symbol.iterator]() {
		yield* this.screenIDs;
	}

	name(screenID) {
		return this.known.get(screenID);
	}

	*names() {
		for (let i of this.screenIDs)
			yield this.known.get(""+i);
	}

	scale() {
		return (screenID) => {
			const ix = this.screenIDs.indexOf(screenID);
			if (ix < 0) {
				alert("Screen '" + screenID + "' not found in index")
			}
			return ix;
		}
	}

	get count() {
		return this.screenIDs.length;
	}

	reorder(newOrder) {
		this.screenIDs = this.screenIDs.sort((a, b) => {
			const ai = newOrder.indexOf(a);
			const bi = newOrder.indexOf(b);

			if (ai === bi)
				return 0;
			else if (ai < 0) {
				return 1;
			}
			else if (bi < 0) {
				return -1;
			}
			else {
				return ai - bi;
			}
		});
	}

	labelX(x) {
		return this.screenIDs[x];
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
			"screen: " + d.screenName + "\n" +
			"mutational index: " + d3.format(".2f")(d.mi);
	}

	static clickGene(screenName, geneID, replicate) {
		window.open(`./simpleplot?screen=${screenName}&highlightGene=${geneID}`, "_blank");
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

	static label(d, geneID) {
		return "gene: " + geneID + "\n" +
			"screen: " + d.screenName + "\n" +
			"sense ratio: " + d3.format(".2f")(d.mi) + "\n" +
			"replicate: " + d.replicate;
	}

	static clickGene(screenName, geneID, replicate) {
		window.open(`./slscreen?screen=${screenName}&highlightGene=${geneID}&replicate=${replicate}`, "_blank");
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
		case "ip": return IPGeneDataTraits;
		case "sl": return SLGeneDataTraits;
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

	processData(data, geneID) {
		const tiles = this.svg.selectAll(".tile")
			.data(data, d => d.screen);

		tiles.exit().remove();

		const xScale = screens.scale();
		this.x = d3.scaleLinear()
			.domain([0, screens.count])
			.range([0, this.width]);

		const yScale = d3.scaleSequential(d3.interpolateViridis).domain(Plot.getDomain(data));

		tiles.enter()
			.append("rect")
			.attr("class", "tile")
			.attr("x", d => this.x(xScale(d.screen)))
			.attr("rx", 3)
			.attr("ry", 3)
			.attr("class", "tile bordered")
			.attr("width", this.gridWidth)
			.attr("height", this.height - 2)

			.on("mouseover", d => tooltip.show(Plot.label(d, geneID), d3.event.pageX + 5, d3.event.pageY - 5))
			.on("mouseout", () => tooltip.hide())
			.on("click", d => Plot.clickGene(d.screenName, geneID, d.replicate))

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
			.on("click", d => Plot.clickGene(d.screenName, geneID, d.replicate))
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
			.domain([...screens.names()])
			.range([0, this.width]);

		const xAxis = d3.axisBottom(this.x);

		this.gX.call(xAxis)
			.selectAll("text")
			.attr("class", "axis-label")
			.style("text-anchor", "end")
			.attr("dx", "-.8em")
			.attr("dy", ".15em")
			.attr("transform", "rotate(-60)");
	}
}

// Fishtail like plot for the selected genes
class FishTailGeneFinderPlot extends ScreenPlot {
	constructor(svg) {
		super(svg);

		this.nextScreenNr = 1;
		this.geneMap = new Map();

		genelines.forEach(gl => gl.changed());
	}

	processData(data, geneID) {

		const screenNr = this.nextScreenNr;
		++this.nextScreenNr;

		this.geneMap.set(screenNr, geneID);

		const screenData = new ScreenData(`Fishtail-like plot for gene ${geneID}`, geneID);
		screenData.process(data);

		data.forEach(d => d.fcpv = 1e-12);

		return this.add(screenData, screenNr);
	}

	dblClickGenes(d, screenNr) {
		const screenID = d.values[0].screenName;
		const geneName = this.geneMap.get(screenNr);
		Plot.clickGene(screenID, geneName, d.replicate);
	}

	mouseOver(d, screenNr) {
		const geneName = this.geneMap.get(screenNr);
		tooltip.show(Plot.label(d.values[0], geneName), d3.event.pageX + 5, d3.event.pageY - 5);
	}

	mouseOut(/*d, screenNr*/) {
		tooltip.hide();
	}
}

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

		genelines.push(this);
	}

	changed() {
		const geneIDs = this.input.val().split(/[ \t\r\n,;]+/).filter(e => e.length > 0);

		this.input.removeClass("gene-not-found");

		if (geneIDs.length > 0) {
			const geneID = geneIDs[0];
			this.input.val(geneID);

			fetch(`${context_name}/${screenType}/finder/${geneID}`, {
				method: 'post',
				credentials: "include"
			}).then(data => {
				return data.json();
			}).then(data => {
				if (data.error != null)
					throw data.error;

				if (data.length === 0)
					this.input.addClass("gene-not-found");

				this.data = data;

				Plot.preProcessData(data);

				this.heatMap.processData(data, geneID);
				this.dotPlot.processData(data, geneID);

				if (fishtail != null) {
					const color = fishtail.processData(data, geneID);

					this.row.getElementsByClassName("swatch")[0]
						.style.backgroundColor = color;

					// this.spinnerTD.classList.remove("loading");
				}
			}).catch(err => {

				// if (err === "invalid-credentials") {
				// 	showLoginDialog(null, () => this.changed());
				// }
				// else {
					this.input.addClass("gene-not-found");
					console.log(err)
				// }
			});

			geneIDs.splice(0, 1);
			geneIDs.forEach(id => new GeneLine(id));
		}
	}

	setGene(id) {
		this.input.val(id);
		this.changed();
	}

	orderedScreenIDs() {
		return this.data
			.sort((a, b) => b.mi - a.mi)
			.map(a => a.screen);
	}

	sort() {
		const newOrder = this.orderedScreenIDs();

		screens.reorder(newOrder);

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

		case 'fishtail':
			$("#plot").removeClass("heatmap dotplot").addClass("fishtail").width("10em");
			$("#fishtailplot").show();

			if (fishtail == null) {
				const svg = d3.select("#ft-plot");
				fishtail = new FishTailGeneFinderPlot(svg)
			}
			break;
	}
}

$(function () {

	document.getElementById("heatmap").onchange = () => selectPlotType('heatmap');
	document.getElementById("dotplot").onchange = () => selectPlotType('dotplot');
	document.getElementById("fishtail").onchange = () => selectPlotType('fishtail');

	// labels = new ScreenLabels();
	screens = new Screens();

	const genes = Array.from(document.getElementsByClassName("gene-id")).map(e => e.dataset.id);
	genes.forEach(value => new GeneLine(value));

	// labels
	labels = new LabelPlot(d3.select("td.label-container"));
	labels.recreateSVG();

	$("#localGeneFile")
		.on("change", e => addGeneLinesFromFile(e));

	$("#add-gene-line")
		.on("click", () => new GeneLine());

	if (genes.length === 0) {
		new GeneLine();
	}
});


