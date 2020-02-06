import * as d3 from "d3";
import ScreenColorMap from "./screenColorMap";
import {gene} from "./geneInfo";
import ContextMenu from './contextMenu';

const radius = 5;
export let pvCutOff = 0.05;
const neutral = "#aaa";

const colorMap = new ScreenColorMap();
export const highlightedGenes = new Set();

// --------------------------------------------------------------------

export function setPvCutOff(v) {
	pvCutOff = v;
}

// --------------------------------------------------------------------

export class DotContextMenu extends ContextMenu {

	constructor(plot, menuID) {
		super(menuID ? menuID : "plot-context-menu");

		this.plot = plot;
		this.svg = plot.svg.node();
	}

	clickIsInsideTarget(e) {
		let el = e.srcElement || e.target;
		let values = null;

		while (el != null && el !== this.svg) {
			if (el.tagName === "g" && el.classList && el.classList.contains("dot")) {
				const data = d3.select(el).data();
				values = data[0].values;
				break;
			}

			el = el.parentNode;
		}

		if (el != null) {
			let action = this.menu.getElementsByClassName("gene-finder-action");
			if (action && action.length === 1)
				action[0].classList.toggle("disabled", values == null);

			action = this.menu.getElementsByClassName("gene-cards-action");
			if (action && action.length === 1)
				action[0].classList.toggle("disabled", values == null || values.length !== 1);
		}

		return el;
	}

	handleSelect(target, action) {
		const data = d3.select(target).data();
		const v = data[0] ? data[0].values : null;
		const geneNames = v ? v.map(g => g.geneName).join(';') : null;

		switch (action) {
			case 'gene-finder':
				window.open("./screen-query/?screenType=IP&gene=" + geneNames, "_blank");
				break;

			case 'gene-cards':
				window.open("https://www.genecards.org/cgi-bin/carddisp.pl?gene=" + geneNames, "_blank");
				break;

			case 'show-labels':
				this.plot.toggleLabels();
				break;

			case 'show-gridlines':
				this.plot.toggleGridLines();
				break;

			case 'export-svg':
				this.plot.exportSVG();
				break;

			default:
				super.handleSelect(target, action);
		}
	}
}

// --------------------------------------------------------------------

export class MultiDot {
	constructor(circle, data, plot, screenNr) {
		this.g = circle.parentNode;
		this.circle = circle;
		this.data = data;
		this.plot = plot;
		this.screenNr = screenNr;

		const padding = 3;

		const N = data.values.length;
		const A = (Math.PI * 2) / N;
		const R = ((2 * radius + padding) / 2) / Math.sin(Math.PI - (A / 2));

		this.dR = R;

		const subData = data.values
			.map(g => {
				const ix = data.values.indexOf(g);
				const h = A * ix - Math.PI / 2;

				return {
					gene: g,
					values: [g],
					ix: ix,
					cx: Math.cos(h) * R,
					cy: Math.sin(h) * R
				};
			});

		d3.select(circle)
			.raise()
			.transition()
			.duration(500)
			.attr("r", (R + radius + padding))
			.style("stroke", "#ddd")
			.style("stroke-width", 1)
			.style("fill", "white")
			.style("opacity", 1);


		const dots = d3.select(this.g)
			.classed("highlight", false)
			.raise()
			.selectAll("g.dot")
			.data(subData, d => d.gene.key);

		dots.exit()
			.remove();

		let gs = dots.enter()
			.append("g")
			.attr("class", "dot")
			.classed("highlight", d => highlightedGenes.has(d.gene.geneName))
			// .attr("transform", d => `translate(${d.cx},${d.cy})`)
		;

		gs.append("circle")
			.classed("sub-dot", true)
			.attr("cx", 0)
			.attr("cy", 0)
			.attr("r", radius)
			.style("fill", this.getColor())
			.on("mouseover", d => gene.set(d.values))
			.on("mouseout", d => gene.set([]))
			.on("click", d => plot.clickGenes(d, screenNr))
			.on("dblclick", d => plot.dblClickGenes(d, screenNr))
			.transition()
			.duration(500)
			.attr("cx", d => d.cx)
			.attr("cy", d => d.cy);

		gs.append("text")
			.attr("x", d => d.cx < 0 ? d.cx - radius : d.cx + radius)
			.attr("y", d => d.cy)
			.attr("class", "label sub-dot")
			.attr("text-anchor", d => d.cx < 0 ? "end" : "begin")
			.classed("significant", d => d.gene.fcpv < pvCutOff)
			.text(d => d.gene.geneName)
			.style("opacity", 0)
			.transition()
			.delay(500)
			.style("opacity", 1);
	}

	hide() {
		d3.select(this.g)
			.classed("highlight", d => d.values.indexOf(g => highlightedGenes.has(g.geneName)) >= 0)
			.selectAll("g.dot")
			.remove();

		d3.select(this.circle)
			.transition()
			.duration(250)
			.style("stroke-width", 0)
			.style("fill", neutral)
			.style("opacity", this.plot.getOpacity(this.screenNr))
			.attr("r", radius)
			.on("end", (d, ix, c) => {
				d3.select(c[ix])
					.style("fill", this.plot.getColor());
			});

		d3.select(this.g)
			.classed("highlight", true)
			.select("text")
			.text("")
			.transition()
			.delay(500)
			.text(d => d.values
				.map(d => d.geneName)
				.filter(g => highlightedGenes.has(g))
				.join(", "));
	}

	getColor() {
		const plotGetColor = this.plot.getColor();
		const v = this.data.values;

		return (d) => {
			return plotGetColor({
				values: [v[d.ix]]
			});
		};
	}
}

// --------------------------------------------------------------------

export default class ScreenPlot {

	constructor(svg) {
		this.svg = svg;
		this.graphType = "regular";
		this.screens = new Map();
		this.patterns = new Set();

		this.svg.node().addEventListener('wheel', (evt) => {
			evt.stopPropagation();
			evt.preventDefault();
			return false;
		}, false);

		const showGridLinesCB = document.getElementById("show-gridlines");
		if (showGridLinesCB) {
			showGridLinesCB.addEventListener("change", e => this.toggleGridLines());
			this.svg.node().classList.toggle("show-gridlines", showGridLinesCB.checked);
		}
		else this.svg.node().classList.add("show-gridlines");

		const showLabelsCB = document.getElementById("show-labels");
		if (showLabelsCB) {
			showLabelsCB.addEventListener("change", e => this.toggleLabels());
			this.svg.node().classList.toggle("show-labels", showLabelsCB.checked);
		}
		else this.svg.node().classList.remove("show-labels");

		const pvCutOffEdit = document.getElementById("pv-cut-off");
		if (pvCutOffEdit != null) {
			pvCutOffEdit.addEventListener("change", () => {
				const pv = pvCutOffEdit.value;

				if (isNaN(pv)) {
					pvCutOffEdit.classList.add("error");
				}
				else {
					pvCutOffEdit.classList.remove("error");
					this.setPvCutOff(pv);
				}
			});
		}

		const highlightGene = document.getElementById("highlightGene");
		if (highlightGene != null)
		{
			highlightGene.addEventListener("change", () => this.highlightGene());
			highlightGene.addEventListener("highlight-gene", () => this.highlightGenes());
		}
		const btnHighlightGene = document.getElementById("btn-highlight-gene");
		if (btnHighlightGene != null)
			btnHighlightGene.addEventListener('click', () => this.highlightGene());

		const clearHighlightBtn = document.getElementById("btn-clear-highlight");
		if (clearHighlightBtn != null) {
			highlightedGenes.clear();
			clearHighlightBtn.addEventListener('click', () => this.clearHighlight());
		}

		const plotContainer = $(this.svg.node());
		const bBoxWidth = plotContainer.width();
		const bBoxHeight = plotContainer.height();

		this.margin = {top: 30, right: 50, bottom: 30, left: 50};
		this.width = bBoxWidth - this.margin.left - this.margin.right;
		this.height = bBoxHeight - this.margin.top - this.margin.bottom;

		this.defs = this.svg.append('defs');

		this.defs
			.append("svg:clipPath")
			.attr("id", "clip")
			.append("svg:rect")
			.attr("x", 1)
			.attr("y", 1)
			.attr("width", this.width - 2)
			.attr("height", this.height - 2);

		this.svg.append("text")
			.attr("class", "x axis-label")
			.attr("text-anchor", "end")
			.attr("x", this.width + this.margin.left)
			.attr("y", this.height + this.margin.top + this.margin.bottom)
			.text("insertions");

		this.svg.append("text")
			.attr("class", "y axis-label")
			.attr("text-anchor", "end")
			.attr("x", -this.margin.top)
			.attr("y", 6)
			.attr("dy", ".75em")
			.attr("transform", "rotate(-90)")
			.text("mutational index");

		this.g = this.svg.append("g")
			.attr("transform", "translate(" + this.margin.left + "," + this.margin.top + ")");

		this.gX = this.g.append("g")
			.attr("class", "axis axis--x")
			.attr("transform", "translate(0," + this.height + ")");

		this.gY = this.g.append("g")
			.attr("class", "axis axis--y");

		this.plot = this.g.append("g")
			.attr("class", "plot")
			.attr("width", this.width)
			.attr("height", this.height)
			.attr("clip-path", "url(#clip)");

		this.plotData = this.plot.append('g')
			.attr("width", this.width)
			.attr("height", this.height);

		const zoom = d3.zoom()
			.scaleExtent([1, 40])
			.translateExtent([[0, 0], [this.width + 90, this.height + 90]])
			.on("zoom", () => this.zoomed());

		this.svg.call(zoom);

		// create the context menu
		this.createContextMenu();
	}

	getPattern(color1, color2) {
		const c2h = (c) => {
			const r = Math.abs(c).toString(16);
			return r.length === 1 ? "0" + r : r;
		};

		const rx = /rgb\((\d+), *(\d+), *(\d+)\)/;

		const c = [color1, color2]
			.map(c => {
				const m = c.match(rx);
				return m ? c2h(m[1]) + c2h(m[2]) + c2h(m[3]) : c.substr(1);
			});

		if (c[0] === c[1])
			return "#" + c[0];

		const patternID = c.join("_");

		if (this.patterns.has(patternID) === false) {

			const pattern = this.defs
				.append("pattern")
				.attr("id", patternID)
				.attr("width", radius)
				.attr("height", radius)
				.attr("patternUnits" , "userSpaceOnUse")
				.attr("patternTransform", "rotate(45)");

			pattern.append("rect")
				.attr("width", radius / 2)
				.attr("height", radius)
				.attr("x", 0)
				.attr("y", 0)
				.style("fill", color1);

			pattern.append("rect")
				.attr("width", radius / 2)
				.attr("height", radius)
				.attr("x", radius / 2)
				.attr("y", 0)
				.style("fill", color2);

			this.patterns.add(patternID);
		}

		return `url(#${patternID})`;
	}

	recolorGenes() {
		[...this.screens.keys()].forEach(sn => {
			const screenPlotData = this.plotData.select(`#plot-${sn}`);

			screenPlotData.selectAll("g.dot")
				.select("circle")
				.style("fill", this.getColor(sn))
				.style("opacity", this.getOpacity(sn));
		});
	}

	zoomed() {
		const evt = d3.event.sourceEvent;
		if (evt != null) {
			evt.stopPropagation();
			evt.preventDefault();
		}

		this.plotData.attr('transform', d3.event.transform);

		const k = d3.event.transform.k;

		const x = this.x;
		const y = this.y;

		[...this.screens.keys()].forEach(sn => {
			const screenPlotData = this.plotData.select(`#plot-${sn}`);

			screenPlotData.selectAll("g.dot")
				// .select("circle")
				.attr('transform', d => `translate(${x(d.insertions)},${y(d.log2mi)}) scale(${1/k})`);
		});

		this.gX.call(this.xAxis.scale(d3.event.transform.rescaleX(this.x)));
		this.gY.call(this.yAxis.scale(d3.event.transform.rescaleY(this.y)));
	};

	getColor(screenNr) {
		return (d) => {
			const color = colorMap.get(screenNr);
			const colors = d.values
				.map(d =>
					d.fcpv >= pvCutOff ?
						highlightedGenes.has(d.geneName) ? "#b3ff3e" : neutral :
						color)
				.reduce((a, b) => {
					if (a.indexOf(b) === -1)
						return [...a, b];
					return a;
				}, []);

			if (colors.length === 1) {
				return colors[0];
			}
			return this.getPattern(colors[0], colors[colors.length - 1]);
		}
	}

	getOpacity() {
		return (d) => {

			if (d.values.findIndex(d => highlightedGenes.has(d.geneName)) !== -1)
				return 1;
			else if (Math.min(...d.values.map(d => d.fcpv)) < pvCutOff)
				return 0.66;
			else return 0.16;
		};
	}

	cleanUp() {
	}

	add(data, screenNr) {
		this.cleanUp();

		this.screens.set(screenNr, data);

		const [x, y] = this.adjustAxes();

		colorMap.add(screenNr);
		const color = this.getColor(screenNr);

		const screenPlotID = `plot-${screenNr}`;

		let screenPlot = this.plotData.select(`#${screenPlotID}`);
		if (screenPlot === null || screenPlot.empty())
		{
			screenPlot = this.plotData.append("g")
				.classed("screen-plot", true)
				.attr("id", screenPlotID);
		}

		const dots = screenPlot.selectAll("g.dot")
			.data(data.dotData, d => d.key);

		dots.exit()
			.remove();

		let gs = dots.enter()
			.append("g")
			.attr("class", "dot")
			.attr("transform", d => `translate(${x(d.insertions)},${y(d.log2mi)})`);

		gs.append("circle")
			.attr("r", radius)
			.on("mouseover", d => this.mouseOver(d, screenNr))
			.on("mouseout", d => this.mouseOut(d, screenNr))
			.on("click", d => this.clickGenes(d, screenNr))
			.on("dblclick", d => this.dblClickGenes(d, screenNr));

		gs.merge(dots)
			.select("circle")
			.style("fill", this.getColor(screenNr))
			.style("opacity", this.getOpacity(screenNr));

		gs.append("text")
			.attr("class", "label");

		gs.merge(dots)
			.select("text")
			.filter(d => d.values.length === 1 && d.values[0].fcpv < pvCutOff)
			.text(d => d.values[0].geneName)
			.classed("significant", true);

		return colorMap.get(screenNr);
	}

	clickGenes(d, screenNr) {
		let handled = false;

		if (d.multiDot !== undefined) {
			d.multiDot.hide();
			delete d.multiDot;
			handled = true;
		} else if (d.values.length === 1) {

			// default is to highlight clicked genes
			const geneName = d.values[0].geneName;
			if (highlightedGenes.has(geneName))
				highlightedGenes.delete(geneName);
			else
				highlightedGenes.add(geneName);

			const highlightGene = document.getElementById("highlightGene");
			if (highlightGene != null)
				highlightGene.dispatchEvent(new Event('highlight-gene'));
		} else {
			const clickedCircle = d3.event.target;

			d.multiDot = new MultiDot(clickedCircle, d, this, screenNr);
			handled = true;
		}

		return handled;
	}

	dblClickGenes(d, screenNr) {
		const evt = d3.event.sourceEvent;
		if (evt != null) {
			evt.stopPropagation();
			evt.preventDefault();
		}

		const geneNames = d.values.map(g => g.geneName).join(';');

		if (d3.event.ctrlKey || d3.event.altKey)
			window.open("https://www.genecards.org/cgi-bin/carddisp.pl?gene=" + geneNames, "_blank");
		else
			window.open("./screen-query/?screenType=IP&gene=" + geneNames, "_blank");
	}

	mouseOver(d, screenNr) {
		if (d.multiDot === undefined)
			gene.set(d.values);
	}

	mouseOut(d, screenNr) {
		if (d.multiDot === undefined)
			gene.set([]);
	}

	adjustAxes() {
		const screens = [...this.screens.values()];

		const xRange = [
			screens.map(s => s.xRange[0]).reduce((previousValue, currentValue) => previousValue < currentValue ? previousValue : currentValue, 1),
			screens.map(s => s.xRange[1]).reduce((previousValue, currentValue) => previousValue > currentValue ? previousValue : currentValue, 1)
		];

		const yRange = [
			screens.map(s => s.yRange[0]).reduce((previousValue, currentValue) => previousValue < currentValue ? previousValue : currentValue, 0),
			screens.map(s => s.yRange[1]).reduce((previousValue, currentValue) => previousValue > currentValue ? previousValue : currentValue, 0)
		];

		const x = d3.scaleLog()
			.domain(xRange)
			.range([0, this.width]);
		this.x = x;

		const y = d3.scaleLinear()
			.domain(yRange)
			.range([this.height, 0]);
		this.y = y;

		const xAxis = d3.axisBottom(x)
			.tickSizeInner(-this.height)
			.tickArguments([15, ".0f"]);
		this.xAxis = xAxis;

		const yAxis = d3.axisLeft(y)
			.tickSizeInner(-this.width);
		this.yAxis = yAxis;

		this.gX.call(xAxis);
		this.gY.call(yAxis);

		// adjust current dots for new(?) axes
		this.plotData.selectAll("g.dot")
			.attr("transform", d => `translate(${x(d.insertions)},${y(d.log2mi)})`);

		return [x, y];
	}

	remove(screenNr) {
		if (this.screens.has(screenNr)) {
			this.cleanUp();

			const screenPlotID = `plot-${screenNr}`;

			this.plotData.select(`#${screenPlotID}`).remove();

			this.screens.delete(screenNr);

			this.adjustAxes();
		}
	}

	setPvCutOff(pv) {
		pvCutOff = pv;
		this.recolorGenes();
	}

	highlightGenes() {
		this.plotData.selectAll(".highlight")
			.classed("highlight", false);

		[...this.screens.keys()].forEach(sn => {
			const screenPlotData = this.plotData.select(`#plot-${sn}`);

			// regular dots
			let selected = screenPlotData.selectAll("g.dot")
				.filter(d => d.values.length === 1 && highlightedGenes.has(d.values[0].geneName))
				.classed("highlight", true)
				.raise();

			selected
				.select("text")
				.raise();

			selected
				.select("circle")
				.style("fill", this.getColor(sn));

			// multi dots
			selected = screenPlotData.selectAll("g.dot")
				.filter(d => d.values.length > 1 && d.values.findIndex(g => highlightedGenes.has(g.geneName)) >= 0)
				.classed("highlight", d => d.multiDot == null)
				.raise();

			selected
				.select("text")
				.raise()
				.text(d => d.values
					.map(d => d.geneName)
					.filter(g => highlightedGenes.has(g))
					.join(", "));
		});
	}

	toggleGridLines() {
		this.svg.node().classList.toggle("show-gridlines");
	}

	toggleLabels() {
		this.svg.node().classList.toggle("show-labels");
	}

	clearHighlight() {
		highlightedGenes.clear();

		this.plotData.selectAll("g.dot.highlight")
			.classed("highlight", false)
			.style("fill", this.getColor())
			.style("opacity", this.getOpacity())
			.select("text")
			.text("");
	}

	highlightGene() {
		const geneNames = $("#highlightGene").val()
			.split(/[ \t\r\n,;]+/)
			.filter(id => id.length > 0);

		geneNames
			.forEach(value => highlightedGenes.add(value));
		const geneNameSet = new Set(geneNames);

		[...this.screens.keys()].forEach(sn => {
			const screenPlotData = this.plotData.select(`#plot-${sn}`);

			const selected = screenPlotData.selectAll("g.dot")
				.filter(d => d.values.filter(g => geneNameSet.has(g.geneName)).length > 0)
				.classed("highlight", true)
				.raise();

			selected
				.select("text")
				.text(d => d.values
					.map(d => d.geneName)
					.filter(g => highlightedGenes.has(g))
					.join(", "));

			selected
				.select("circle")
				.style("fill", this.getColor(sn))
				.attr("r", 10 * radius)
				.style("opacity", 0.66)
				.transition()
				.delay(1000)
				.duration(1500)
				.attr("r", radius)
				.style("opacity", this.getOpacity(sn));
		});
	}

	exportSVG() {
		//get svg source.
		const svg = this.svg.node();
		const serializer = new XMLSerializer();
		let source = serializer.serializeToString(svg);

		//add name spaces.
		if (!source.match(/^<svg[^>]+xmlns="http\:\/\/www\.w3\.org\/2000\/svg"/))
			source = source.replace(/^<svg/, '<svg xmlns="http://www.w3.org/2000/svg"');
		if (!source.match(/^<svg[^>]+"http\:\/\/www\.w3\.org\/1999\/xlink"/))
			source = source.replace(/^<svg/, '<svg xmlns:xlink="http://www.w3.org/1999/xlink"');

		//add xml declaration
		source = '<?xml version="1.0" standalone="no"?>\r\n' + source;

		//convert svg source to URI data scheme.
		const url = "data:image/svg+xml;charset=utf-8," + encodeURIComponent(source);

		const link = document.createElement("a");
		link.href = url;
		link.download = this.screenID + "-plot.svg";
		document.body.appendChild(link);
		link.click();
		document.body.removeChild(link);
	}

	createContextMenu() {
		const contextMenuDiv = document.getElementById("plot-context-menu");
		if (contextMenuDiv)
			this.contextMenu = new DotContextMenu(this, "plot-context-menu");
	}
};
