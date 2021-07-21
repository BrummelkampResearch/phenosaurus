import * as d3 from "d3";
import ScreenColorMap from "./screenColorMap";
import {gene} from "./geneInfo";
import DotContextMenu from './dot-context-menu';
import MultiDot from './multidot';

export const radius = 5;
export const neutral = "#aaa", highlight = "#b3ff3e";

const colorMap = new ScreenColorMap();
export const highlightedGenes = new Set();

export let pvCutOff = 0.05;

// --------------------------------------------------------------------

export default class ScreenPlot {

	constructor(svg, yLabel = 'mutational index') {
		this.svg = svg;
		this.graphType = "regular";
		this.screens = new Map();
		this.patterns = new Set();
		this.presentationMode = false;
		this.showAllLabels = false;
		this.uniqueScale = d3.scaleSequential(d3.interpolateViridis).domain([0, 9]);

		this.svg.node().addEventListener('wheel', (evt) => {
			evt.stopPropagation();
			evt.preventDefault();
			return false;
		}, false);

		const showGridLinesCB = document.getElementById("show-gridlines");
		if (showGridLinesCB) {
			showGridLinesCB.addEventListener("change", () => this.toggleGridLines());
			this.svg.node().classList.toggle("show-gridlines", showGridLinesCB.checked);
		}
		else this.svg.node().classList.add("show-gridlines");

		const showLabelsCB = document.getElementById("show-labels");
		if (showLabelsCB) {
			showLabelsCB.addEventListener("change", () => this.toggleLabels());
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
		pvCutOff = +pvCutOffEdit.value;

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

		const plotContainerNode = this.svg.node();
		const bBoxWidth = plotContainerNode.clientWidth;
		const bBoxHeight = plotContainerNode.clientHeight;

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
			.text(yLabel);

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
			.scaleExtent([1, 8])
			.translateExtent([[0, 0], [this.width + 90, this.height + 90]])
			.filter(() => d3.event.ctrlKey)
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

	recolorGenes(uniqueScale) {
		this.uniqueScale = uniqueScale || d3.scaleSequential(d3.interpolateViridis).domain([0, 9]);

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

		if (this.xAxis != null && this.yAxis != null)
		{
			this.plotData.attr('transform', d3.event.transform);
	
			const k = d3.event.transform.k;
	
			const x = this.x;
			const y = this.y;
	
			[...this.screens.keys()].forEach(sn => {
				const screenPlotData = this.plotData.select(`#plot-${sn}`);
	
				screenPlotData.selectAll("g.dot")
					.attr('transform', d => `translate(${x(d.x)},${y(d.y)}) scale(${1/k})`);
			});
	
			this.gX.call(this.xAxis.scale(d3.event.transform.rescaleX(this.x)));
			this.gY.call(this.yAxis.scale(d3.event.transform.rescaleY(this.y)));
		}
	}

	getColor(screenNr) {
		return (d) => {
			const color = colorMap.get(screenNr);
			const colors = d.values
				.map(d =>
					d.fcpv >= pvCutOff
						? highlightedGenes.has(d.gene)
							? highlight 
							: neutral
						: color)
				.reduce((a, b) => {
					return (a.indexOf(b) === -1)
						? [...a, b]
						: a;
				}, []);

			return colors.length === 1
				? colors[0]
				: this.getPattern(colors[0], colors[colors.length - 1]);
		}
	}

	getOpacity() {
		return (d) => {
			if (d.highlight()) return 1;
			if (d.significant(pvCutOff)) return this.presentationMode ? 1 : 0.66;
			return 0.16;
		};
	}

	cleanUp() {
	}

	add(data, screenNr) {
		this.cleanUp();

		this.screen = data.screen;
		this.screens.set(screenNr, data);

		const [x, y] = this.adjustAxes();

		colorMap.add(screenNr);

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
			.attr("transform", d => `translate(${x(d.x)},${y(d.y)})`);

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
			const gene = d.values[0].gene;
			if (highlightedGenes.has(gene))
				highlightedGenes.delete(gene);
			else
				highlightedGenes.add(gene);

			const highlightGene = document.getElementById("highlightGene");
			if (highlightGene != null)
				highlightGene.dispatchEvent(new Event('highlight-gene'));

			// dispatch another event for the genome-viewer to pick up
			const plot = this.svg.node();

			const e = new Event("clicked-gene");
			e.gene = gene;
			e.screen = this.screen;
			if (this.replicate !== null)
				e.replicate = this.replicate;

			plot.dispatchEvent(e);
		} else {
			const clickedCircle = d3.event.target;

			d.multiDot = new MultiDot(clickedCircle, d, this, screenNr);
			handled = true;
		}

		return handled;
	}

	dblClickGenes(d) {
		const evt = d3.event.sourceEvent;
		if (evt != null) {
			evt.stopPropagation();
			evt.preventDefault();
		}

		const genes = d.values.map(g => g.gene).join(';');

		if (d3.event.ctrlKey || d3.event.altKey)
			window.open("https://www.genecards.org/cgi-bin/carddisp.pl?gene=" + genes, "_blank");
		else
			window.open("finder?gene=" + genes, "_blank");
	}

	mouseOver(d) {
		if (d.multiDot === undefined)
			gene.set(d.values);
	}

	mouseOut(d) {
		if (d.multiDot === undefined)
			gene.set([]);
	}

	adjustAxes(xRange, yRange) {
		if (xRange === undefined || yRange === undefined) {
			const screens = [...this.screens.values()];
	
			xRange = [
				screens.map(s => s.xRange[0]).reduce((previousValue, currentValue) => previousValue < currentValue ? previousValue : currentValue, 1),
				screens.map(s => s.xRange[1]).reduce((previousValue, currentValue) => previousValue > currentValue ? previousValue : currentValue, 1)
			];
	
			yRange = [
				screens.map(s => s.yRange[0]).reduce((previousValue, currentValue) => previousValue < currentValue ? previousValue : currentValue, 0),
				screens.map(s => s.yRange[1]).reduce((previousValue, currentValue) => previousValue > currentValue ? previousValue : currentValue, 0)
			];
		}

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
			.attr("transform", d => `translate(${x(d.x)},${y(d.y)})`);

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
		this.plotData.selectAll("g.dot")
			.select("text")
			.remove();

		[...this.screens.keys()].forEach(sn => {
			const screenPlotData = this.plotData.select(`#plot-${sn}`);

			// regular dots
			screenPlotData.selectAll("g.dot")
				.filter(d => d.multiDot === undefined && d.highlight())
				.raise()
				.classed("highlight", true)
				.append("text")
				.attr("class", "label")
				.text(d => d.label())
				.filter(d => d.subdot)
				.attr("x", d => d.tx)
				.attr("y", d => d.ty)
				.attr("text-anchor", d => d.anchor);
		});
	}

	toggleGridLines() {
		this.svg.node().classList.toggle("show-gridlines");
	}

	toggleLabels() {
		this.plotData.selectAll("g.dot")
			.select("text")
			.remove();

		if ((this.showAllLabels = !this.showAllLabels))
		{
			[...this.screens.keys()].forEach(sn => {
				const screenPlotData = this.plotData.select(`#plot-${sn}`);
	
				// regular dots
				screenPlotData.selectAll("g.dot")
					.filter(d => d.multiDot === undefined && d.significant(pvCutOff))
					.append("text")
					.attr("class", "label")
					.text(d => d.values.map(d => d.gene).join(", "))
					.filter(d => d.subdot)
					.attr("x", d => d.tx)
					.attr("y", d => d.ty)
					.attr("text-anchor", d => d.anchor);
			});
		}
	}

	togglePresentationMode() {
		this.presentationMode = ! this.presentationMode;
		this.svg.node().classList.toggle("presentation");
		this.recolorGenes();
	}

	clearHighlight() {
		highlightedGenes.clear();

		const selected = this.plotData.selectAll("g.dot.highlight")
		.classed("highlight", false);
	
	selected
		.select("circle")
		.style("fill", this.getColor())
		.style("opacity", this.getOpacity());
	
	selected
		.select("text")
		.remove();
	}

	highlightGene(gene) {
		if (gene === undefined) {
			gene = document.getElementById('highlightGene').value;
		}

		const genes = gene
			.split(/[ \t\r\n,;]+/)
			.filter(id => id.length > 0);

		genes
			.forEach(value => highlightedGenes.add(value));
		const geneSet = new Set(genes);

		[...this.screens.keys()].forEach(sn => {
			const screenPlotData = this.plotData.select(`#plot-${sn}`);

			const selected = screenPlotData.selectAll("g.dot")
			.filter(d => d.multiDot === undefined && d.values.filter(g => geneSet.has(g.gene)).length > 0)
			.classed("highlight", true)
				.raise();

			selected
			.append("text")
			.attr("class", "label")
			.text(d => d.label())
			.filter(d => d.subdot)
			.attr("x", d => d.tx)
			.attr("y", d => d.ty)
			.attr("text-anchor", d => d.anchor);

			selected
				.select("circle")
				.style("fill", this.getColor(sn))
				.attr("r", 10 * radius)
				.style("opacity", this.presentationMode ? 1 : 0.66)
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
		if (!source.match(/^<svg[^>]+xmlns="http:\/\/www\.w3\.org\/2000\/svg"/))
			source = source.replace(/^<svg/, '<svg xmlns="http://www.w3.org/2000/svg"');
		if (!source.match(/^<svg[^>]+"http:\/\/www\.w3\.org\/1999\/xlink"/))
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

	exportCSV() {

	}

	createContextMenu() {
		const contextMenuDiv = document.getElementById("plot-context-menu");
		if (contextMenuDiv)
			this.contextMenu = new DotContextMenu(this, "plot-context-menu");
	}
}
