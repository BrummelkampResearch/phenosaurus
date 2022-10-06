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

import Tooltip from './tooltip';
const tooltip = new Tooltip();

export let pvCutOff = 0.05;
let svgWidth = 1600;

const radius = 3;
const neutral = "#ccc", positive = "#fb8", negative = "#38c";

/* global screenType, screenInfo */

let s_screens = null;

export class Screens {

	static instance() {
		if (s_screens == null)
			s_screens = new Screens();
		return s_screens;
	}

	constructor() {
		this.known = new Map(screenInfo.map(v => [v.name, v.ignore]));

		this.screenIDs =
			[...this.known.keys()]
			.sort((a, b) => {
				let d = 0;
				if (this.known.get(a) != this.known.get(b))
					d = this.known.get(a) ? 1 : -1;
				if (d == 0)
					d = a.localeCompare(b);
				return d;
			});
	}

	* [Symbol.iterator]() {
		yield* this.screenIDs;
	}

	scale() {
		return (screenID) => {
			const ix = this.screenIDs.indexOf(screenID);
			if (ix < 0) {
				console.log("Screen '" + screenID + "' not found in index")
			}
			return ix;
		}
	}

	get count() {
		return this.screenIDs.length;
	}

	reorder(newOrder) {
		this.screenIDs = this.screenIDs.sort((a, b) => {
			if (this.known.get(a) != this.known.get(b))
				return this.known.get(a) ? 1 : -1;

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
		window.open(`screen?screen=${screen}&gene=${gene}`, "_blank");
	}

	static preProcessData(data) {
		data.forEach(d => {
			d.y = Math.log2(d.mi);
		});
	}

	static getDomain(data) {
		return [-6, 6, ...d3.extent(data, d => d.y)];
	}

	static getColor(d) {
		return d.fcpv >= pvCutOff ? neutral : d.mi < 1 ? negative : positive;
	}
}

class SLGeneDataTraits
{
	constructor() {
	}

	static label(d, gene) {
		return "gene: " + gene + "\n" +
			"screen: " + d.screen + "\n" +
			"sense ratio: " + d3.format(".2f")(d.sense_ratio);
	}

	static clickGene(screen, gene) {
		window.open(`screen?screen=${screen}&gene=${gene}`, "_blank");
	}

	static preProcessData(data) {
		data.forEach(d => {
			d.y = d.sense_ratio;
			d.yl = d.sense_ratio_list;
		});
	}

	static getDomain() {
		return [0, 1];
	}

	static getColor(d) {
		return (d.consistent && d.odds_ratio < 0.7) ? positive : neutral;
	}
}

// --------------------------------------------------------------------

function baseSelector() {
	switch (screenType) {
		case "ip": return IPGeneDataTraits;
		case "pa": return IPGeneDataTraits;
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

		this.gridWidth = Math.floor(this.width / Screens.instance().count);

		// this.margin.left += 25;
		this.margin.top += 2 * radius;
		// this.width -= 25;
		this.height -= 2 * radius + 10;

		this.svg.attr("transform", "translate(" + this.margin.left + "," + this.margin.top + ")");

		this.x = d3.scaleLinear()
			.domain([0, Screens.instance().count])
			.range([0, this.gridWidth * Screens.instance().count]);

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

		if (screenType === 'sl') {
			const xScale = Screens.instance().scale();
			this.controlLine = this.svg.append("rect")
				.attr("class", "control")
				.attr("x", this.x(xScale('ControlData-HAP1')))
				.attr("y", 0)
				.attr("width", this.gridWidth)
				.attr("height", this.height)
				.attr("opacity", 0.1);
		}
	}

	processData(data, gene) {
		const xScale = Screens.instance().scale();

		this.y.domain(Plot.getDomain(data));

		const yAxis = d3.axisLeft(this.y)
			.tickSizeInner(-this.width)
			.ticks(3);

		this.gY.call(yAxis);

		let dots;

		if (typeof data[0].yl !== 'undefined') {
			const d2 = [];

			data.forEach(d => {
				let r = 1;
				d.yl.forEach(dl => {
					const dd = { ...d };
					dd.y = dl;
					dd.repl = r++;
					d2.push(dd);
				})
			});

			dots = this.svg.selectAll("circle.dot")
				.data(d2, d => `${d.screen}-${d.repl}`);
		}
		else {
			dots = this.svg.selectAll("circle.dot")
				.data(data, d => d.screen);
		}
	
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
			.attr("cx", d => this.x(xScale(d.screen)) + this.gridWidth / 2)
			.attr("cy", d => this.y(d.y))
			.style("fill", d => Plot.getColor(d));
	}
	
	rearrange() {
		const xScale = Screens.instance().scale();

		this.svg.selectAll(".dot")
			.transition()
			.duration(500)
			.attr("cx", d => this.x(xScale(d.screen)) + this.gridWidth / 2);
		
		this.svg.selectAll("rect.control")
			.transition()
			.duration(500)
			.attr("x", this.x(xScale("ControlData-HAP1")));
	}
}

let s_labels = null;

export class LabelPlot extends Plot {
	constructor(td) {
		super(td, 200);
	}

	static init(td) {
		s_labels = new LabelPlot(td);
		s_labels.recreateSVG();
	}

	recreateSVG() {
		super.recreateSVG("");

		this.gX = this.svg.append("g")
			.attr("class", "axis axis--x");

		this.rearrange_()
	}

	static rearrange() {
		s_labels.rearrange_();
	}

	rearrange_() {
		const gridWidth = Math.floor(this.width / Screens.instance().count);
		this.x = d3.scaleBand()
			.domain([...Screens.instance()])
			.range([0, gridWidth * Screens.instance().count]);

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
	const hb = document.getElementById("heatmap");
	if (hb) hb.onchange = () => selectPlotType('heatmap');

	const db = document.getElementById("dotplot");
	if (db) db.onchange = () => selectPlotType('dotplot');

	// labels
	LabelPlot.init(d3.select("td.label-container"));
})
