import * as d3 from 'd3';

import Tooltip from './tooltip';
const tooltip = new Tooltip();

export let pvCutOff = 0.05;
let svgWidth = 1600;

const radius = 3;
const neutral = "#ccc", positive = "#fb8", negative = "#38c";

/* global screenType, screenNames */

let s_screens = null;

export class Screens {

	static instance() {
		if (s_screens == null)
			s_screens = new Screens();
		return s_screens;
	}

	constructor() {
		this.known = new Map(screenNames.map(v => [v, v]));
		// this.screenIDs = Array.from(this.known.keys()).map(k => +k);
		this.screenIDs = [...this.known.keys()];
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

// --------------------------------------------------------------------

class IPGeneDataTraits
{
	constructor() {
	}

	static label(d, gene) {
		return "gene: " + gene + "\n" +
			"screen: " + d.screen + "\n" +
			"mutational index: " + d3.format(".2f")(d.mi);
	}

	static clickGene(screen, gene) {
		window.open(`screen?screen=${screen}&highlightGene=${gene}`, "_blank");
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
		window.open(`screen?screen=${screen}&highlightGene=${gene}&replicate=${replicate}`, "_blank");
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

export class Plot extends baseSelector() {
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

export class HeatMapPlot extends Plot {

	constructor(td) {
		super(td, 15);
	}

	recreateSVG() {
		super.recreateSVG("heatmap");
		this.gridWidth = Math.floor(this.width / Screens.instance().count);
	}

	processData(data, gene) {
		const tiles = this.svg.selectAll(".tile")
			.data(data, d => d.screen);

		tiles.exit().remove();

		const xScale = Screens.instance().scale();
		this.x = d3.scaleLinear()
			.domain([0, Screens.instance().count])
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
		const xScale = Screens.instance().scale();

		this.svg.selectAll(".tile")
			.transition()
			.duration(500)
			.attr("x", d => this.x(xScale(d.screen)));
	}
}

export class DotPlot extends Plot {
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
			.domain([0, Screens.instance().count])
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

	processData(data, gene) {
		const xScale = Screens.instance().scale();

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
			.on("mouseover", d => tooltip.show(Plot.label(d, gene), d3.event.pageX + 5, d3.event.pageY - 5))
			.on("mouseout", () => tooltip.hide())
			.on("click", d => Plot.clickGene(d.screen, gene, d.replicate))
			.merge(dots)
			.attr("cx", d => this.x(xScale(d.screen)))
			.attr("cy", d => this.y(d.y))
			.style("fill", d => {
				return d.fcpv >= pvCutOff ? neutral : d.mi < 1 ? negative : positive;
			});
	}
	
	rearrange() {
		const xScale = Screens.instance().scale();

		this.svg.selectAll(".dot")
			.transition()
			.duration(500)
			.attr("cx", d => this.x(xScale(d.screen)));
	}
}

export class LabelPlot extends Plot {
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
		this.x = d3.scaleBand()
			.domain([...Screens.instance().names()])
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

// // Fishtail like plot for the selected genes
// class FishTailGeneFinderPlot extends ScreenPlot {
// 	constructor(svg) {
// 		super(svg);

// 		this.nextScreenNr = 1;
// 		this.geneMap = new Map();

// 		genelines.forEach(gl => gl.changed());
// 	}

// 	processData(data, gene) {

// 		const screenNr = this.nextScreenNr;
// 		++this.nextScreenNr;

// 		this.geneMap.set(screenNr, gene);

// 		const screenData = new ScreenData(`Fishtail-like plot for gene ${gene}`, gene);
// 		screenData.process(data);

// 		data.forEach(d => d.fcpv = 1e-12);

// 		return this.add(screenData, screenNr);
// 	}

// 	dblClickGenes(d, screenNr) {
// 		const screen = d.values[0].screen;
// 		const gene = this.geneMap.get(screenNr);
// 		Plot.clickGene(screen, gene, d.replicate);
// 	}

// 	mouseOver(d, screenNr) {
// 		const gene = this.geneMap.get(screenNr);
// 		tooltip.show(Plot.label(d.values[0], gene), d3.event.pageX + 5, d3.event.pageY - 5);
// 	}

// 	mouseOut(/*d, screenNr*/) {
// 		tooltip.hide();
// 	}
// }
