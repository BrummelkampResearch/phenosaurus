import 'chosen-js/chosen.jquery';

import * as d3 from 'd3';

import {pvCutOff, setPvCutOff, highlightedGenes, DotContextMenu} from "./screenPlot";
import {MultiDot} from "./screenPlot";
import {showLoginDialog} from "./index";
import {gene} from "./geneInfo";

const radius = 5;
const screenReplicatesMap = new Map();
const neutral = "#bbb", highlightColor = "#b3ff3e";

let effectSize = 0.2;

// --------------------------------------------------------------------

class ColorMap {

	constructor() {
		this.scale = d3.scaleSequential(d3.interpolateReds).domain([0, 1]);

		this.geneColorMap = new Map();
		this.type = 'raw';
		this.significantGenes = new Set();
	}

	getColor(d) {
		const mapped = this.geneColorMap.get(d.geneID);

		if (mapped == null || mapped.binom_fdr == null || mapped.binom_fdr >= pvCutOff)
			return highlightedGenes.has(d.geneName) ? highlightColor : neutral;

		// if (mapped == null || mapped.binom_fdr == null || mapped.binom_fdr >= pvCutOff)
		// 	return neutral;
		//
		// if (/*d.binom_fdr > pvCutOff && */highlightedGenes.has(d.geneName))
		// 	return highlightColor;

		switch (this.type) {
			case 'raw':
				return this.scale(mapped.diff);
			case 'significant':
				if (this.significantGenes.has(d.geneName))
					return this.scale(mapped.diff);
				else
					return neutral;
		}
	}

	setControl(data, control) {
		this.control = control;

		data.forEach(d => {
			const prev = this.geneColorMap.get(d.geneID);
			if (prev != null) {
				prev.diff += d.senseratio - prev.control;
				prev.control = d.senseratio;
			}
			else
				this.geneColorMap.set(d.geneID, {control: d.senseratio});
		});
	}

	setData(data) {
		let maxDiff = 0;
		data.forEach(d => {
			const e = this.geneColorMap.get(d.geneID);
			if (e != null) {
				e.binom_fdr = d.binom_fdr;
				e.diff = e.control - d.senseratio;

				if (maxDiff < e.diff)
					maxDiff = e.diff;
			}
		});

		this.scale = d3.scaleSequential(d3.interpolateReds).domain([0, maxDiff]);

		this.control.updateColors();
	}

	setSignificantGenes(genes) {
		this.significantGenes = new Set(genes);

		if (this.type === 'significant') {
			const btns = document.getElementById("graphColorBtns");
			btns.dispatchEvent(new Event("change-color"));
		}
	}

	selectType(colortype) {
		this.type = colortype;

		const btns = document.getElementById("graphColorBtns");
		btns.dispatchEvent(new Event("change-color"));
	}
}

const colorMap = new ColorMap();

// --------------------------------------------------------------------

class SLScreenPlot {

	constructor(svg) {
		this.svg = svg;
		this.screens = new Map();
		this.patterns = new Set();
		this.parentColumn = svg.node().parentNode.parentNode;
		this.control = null;
		this.updateColorMap = (data) => colorMap.setData(data);

		const btns = document.getElementById("graphColorBtns");
		btns.addEventListener("change-color", () => this.updateColors());

		this.svg.node().addEventListener('wheel', (evt) => {
			evt.stopPropagation();
			evt.preventDefault();
			return false;
		}, false);

		const showGridLinesCB = document.getElementById("show-gridlines");
		if (showGridLinesCB) {
			showGridLinesCB.addEventListener("change", e => this.toggleGridLines());
			this.svg.node().classList.toggle("show-gridlines", showGridLinesCB.checked);
		} else this.svg.node().classList.add("show-gridlines");

		const showLabelsCB = document.getElementById("show-labels");
		if (showLabelsCB) {
			showLabelsCB.addEventListener("change", e => this.toggleLabels());
			this.svg.node().classList.toggle("show-labels", showLabelsCB.checked);
		} else this.svg.node().classList.remove("show-labels");

		const highlightGene = document.getElementById("highlightGene");
		if (highlightGene != null) {
			highlightGene.addEventListener("change", () => this.highlightGene(highlightGene.value));
			highlightGene.addEventListener("highlight-gene", () => this.highlightGenes());

			const btnHighlightGene = document.getElementById("btn-highlight-gene");
			if (btnHighlightGene != null)
				btnHighlightGene.addEventListener('click', () => this.highlightGene(highlightGene.value));

			const clearHighlightBtn = document.getElementById("btn-clear-highlight");
			if (clearHighlightBtn != null) {
				highlightedGenes.clear();
				clearHighlightBtn.addEventListener('click', () => this.clearHighlight());
			}
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
			.text("sense-ratio");

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

	selectReplicate(replicate) {
		this.loadScreen(this.name, replicate);
	}

	loadScreen(name, replicate = 1) {
		return new Promise((resolve, reject) => {
			this.name = name;
			this.replicateNr = replicate;

			// colorMap.add(replicate);

			this.updateReplicateBtns(replicate);

			const plotTitle = this.parentColumn.getElementsByClassName("plot-title")[0];
			if (plotTitle.classList.contains("plot-status-loading"))  // avoid multiple runs
				return;
			plotTitle.classList.add("plot-status-loading");
			plotTitle.classList.remove("plot-status-loaded", "plot-status-failed");

			[...this.parentColumn.getElementsByClassName("screen-name")]
				.forEach(sn => sn.textContent = name);

			const f = document.geneSelectionForm;
			const fd = new FormData(f);

			// if (f["read-length"])
			// 	fd.set("read-length", f["read-length"].value + 0);
			// fd.set("read-length", 50);

			const geneStartOffset = parseInt(document.getElementById('geneStartOffset').value);

			let geneStart = document.getElementById("geneStartType").value;
			if (geneStartOffset > 0)
				geneStart += "+" + geneStartOffset;
			else if (geneStartOffset < 0)
				geneStart += geneStartOffset;

			fd.append("gene-start", geneStart);

			const geneEndOffset = parseInt(document.getElementById('geneEndOffset').value);

			let geneEnd = document.getElementById("geneEndType").value;
			if (geneEndOffset > 0)
				geneEnd += "+" + geneEndOffset;
			else if (geneEndOffset < 0)
				geneEnd += geneEndOffset;

			fd.append("gene-end", geneEnd);

			fetch(`${context_name}sl/screenData/${name}/${replicate}`,
					{ credentials: "include", method: "post", body: fd })
				.then(value => {
					if (value.ok)
						return value.json();
					if (value.status === 403)
						throw "invalid-credentials";
				})
				.then(data => {
					this.process(data, name, replicate);
					plotTitle.classList.remove("plot-status-loading");
					plotTitle.classList.add("plot-status-loaded");
					resolve(data);
				})
				.catch(err => {
					plotTitle.classList.remove("plot-status-loading");
					plotTitle.classList.add("plot-status-failed");
					console.log(err);
					if (err === "invalid-credentials")
						showLoginDialog(null, () => this.loadScreen(this.name, this.id));
					else reject(err);
				});
		});
	}

	loadSignificantGenes() {
	
		const f = document.geneSelectionForm;
		const fd = new FormData(f);

		const geneStartOffset = parseInt(document.getElementById('geneStartOffset').value);

		let geneStart = document.getElementById("geneStartType").value;
		if (geneStartOffset > 0)
			geneStart += "+" + geneStartOffset;
		else if (geneStartOffset < 0)
			geneStart += geneStartOffset;

		fd.append("gene-start", geneStart);

		const geneEndOffset = parseInt(document.getElementById('geneEndOffset').value);

		let geneEnd = document.getElementById("geneEndType").value;
		if (geneEndOffset > 0)
			geneEnd += "+" + geneEndOffset;
		else if (geneEndOffset < 0)
			geneEnd += geneEndOffset;

		fd.append("gene-end", geneEnd);
	
		fetch(`${context_name}sl/significantGenes/${this.name}/${this.replicateNr}?pvCutOff=${pvCutOff}&binomCutOff=${pvCutOff}&effectSize=${effectSize}`,
			{ credentials: "include", method: "post", body: fd })
			.then(value => value.json())
			.then(value => this.setSignificantGenes(value))
			.catch(reason => console.log(reason));
	}

	process(data) {
		console.log(data);

		this.screenData = data;

		this.updateColorMap(data);

		this.dotData = d3.nest()
			.key(d => [d.senseratio, d.insertions].join(":"))
			.entries(data);
		this.dotData
			.forEach(d => {
				d.senseratio = d.values[0].senseratio;
				d.insertions = d.values[0].insertions;
			});

		const [x, y] = this.adjustAxes();

		// clean up everything to make life easier
		this.plotData.selectAll("g.dot").remove();

		const dots = this.plotData.selectAll("g.dot")
			.data(this.dotData, d => d.key);

		dots.exit()
			.remove();

		let gs = dots.enter()
			.append("g")
			.attr("class", "dot")
			.attr("transform", d => `translate(${x(d.insertions)},${y(d.senseratio)})`);

		gs.append("circle")
			.attr("r", radius)
			.on("mouseover", d => this.mouseOver(d))
			.on("mouseout", d => this.mouseOut(d))
			.on("click", d => this.clickGenes(d))
			.on("dblclick", d => this.dblClickGenes(d));

		gs.merge(dots)
			.select("circle")
			.style("fill", this.getColor())
			.style("opacity", this.getOpacity());

		gs.append("text")
			.attr("class", "label");

		gs.merge(dots)
			.select("text")
			.filter(d => d.values.length === 1 && d.values[0].binom_fdr < pvCutOff)
			.text(d => d.values[0].geneName)
			.classed("significant", true);

		this.highlightGenes();
	}

	adjustAxes() {

		let maxInsertions = d3.max(this.screenData, d => d.insertions);
		maxInsertions = Math.ceil(Math.pow(10, Math.log10(maxInsertions) + 0.1));

		const xRange = [ 1, maxInsertions ];

		const yRange = [ 0, 1 ];

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
			.attr("transform", d => `translate(${x(d.insertions)},${y(d.senseratio)})`);

		return [x, y];
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

		this.plotData.selectAll("g.dot")
			.attr('transform', d => `translate(${x(d.insertions)},${y(d.senseratio)}) scale(${1/k})`);

		this.gX.call(this.xAxis.scale(d3.event.transform.rescaleX(this.x)));
		this.gY.call(this.yAxis.scale(d3.event.transform.rescaleY(this.y)));
	};

	getColor() {
		return (d) => {
			const colors = d.values
				.map(d => colorMap.getColor(d))
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
			if (d.values.findIndex(d => highlightedGenes.has(d.geneName)) !== -1 || colorMap.significantGenes.has(d.geneName))
				return 1;
			else if (Math.min(...d.values.map(d => d.binom_fdr)) <= pvCutOff)
				return 0.5;
			else return 0.1;
		};
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

	dblClickGenes(d) {
		const evt = d3.event.sourceEvent;
		if (evt != null) {
			evt.stopPropagation();
			evt.preventDefault();
		}

		const geneNames = d.values.map(g => g.geneName).join(';');

		if (d3.event.ctrlKey || d3.event.altKey)
			window.open("https://www.genecards.org/cgi-bin/carddisp.pl?gene=" + geneNames, "_blank");
		else
			window.open("./geneFinder?screenType=SL&gene=" + geneNames, "_blank");
	}

	mouseOver(d) {
		if (d.multiDot === undefined)
			gene.setSL(d.values);
	}

	mouseOut(d) {
		if (d.multiDot === undefined)
			gene.setSL([]);
	}

	highlightGenes() {
		this.plotData.selectAll(".highlight")
			.classed("highlight", false);

		// regular dots
		let selected = this.plotData.selectAll("g.dot")
			.filter(d => d.values.length === 1 && highlightedGenes.has(d.values[0].geneName))
			.classed("highlight", true)
			.raise();

		selected
			.select("text")
			.raise();

		selected
			.select("circle")
			.style("fill", this.getColor());

		// multi dots
		selected = this.plotData.selectAll("g.dot")
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

	highlightGene(geneName) {
		const geneNames = geneName
			.split(/[ \t\r\n,;]+/)
			.filter(id => id.length > 0);

		geneNames
			.forEach(value => highlightedGenes.add(value));
		const geneNameSet = new Set(geneNames);

		const selected = this.plotData.selectAll("g.dot")
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
			.style("fill", this.getColor())
			.attr("r", 10 * radius)
			.style("opacity", 0.66)
			.transition()
			.delay(1000)
			.duration(1500)
			.attr("r", radius)
			.style("opacity", this.getOpacity());
	}


	createContextMenu() {
		const contextMenuDiv = document.getElementById("plot-context-menu");
		if (contextMenuDiv)
			this.contextMenu = new DotContextMenu(this, "plot-context-menu");
	}

	updateReplicateBtns(number) {
		this.replicateNr = number;

		const replicateBtnContainer = this.parentColumn.getElementsByClassName("replicate-btn-container")[0];

		const replicates = screenReplicatesMap.get(this.name);

		if (replicates != null) {

			[...replicateBtnContainer.getElementsByTagName("label")]
				.forEach(r => r.remove());

			for (let replicate of replicates) {
				const label = document.createElement("label");
				label.classList.add("btn", "btn-secondary");
				if (number === replicate)
					label.classList.add("active");
				const btn = document.createElement("input");
				btn.type = "radio";
				btn.name = "replicate";
				btn.autocomplete = "off";
				btn.dataset.replicate = replicate;
				label.appendChild(btn);
				label.appendChild(document.createTextNode(`${replicate}`));
				replicateBtnContainer.appendChild(label);
				btn.onchange = () => this.selectReplicate(replicate);
			}
		}
	}

	updateColors() {
		const significantGenes = colorMap.significantGenes;

		this.plotData.selectAll("g.dot")
			.select("circle")
			.style("fill", this.getColor())
			.style("opacity", this.getOpacity())
			.filter(d => significantGenes.has(d.geneName))
			.raise();
	}

	setSignificantGenes(genes) {

		colorMap.setSignificantGenes(genes);

		const table = document.getElementById("significantGenesTable");
		$("tr", table).remove();
		const fmt = d3.format(".3g");

		this.screenData
			.filter(d => colorMap.significantGenes.has(d.geneName))
			.forEach(d => {
				let row = $("<tr/>");
				$("<td/>").text(d.geneName).appendTo(row);
				$("<td/>").text(d.senseratio).appendTo(row);
				$("<td/>").text(d.insertions).appendTo(row);
				$("<td/>").text(fmt(d.binom_fdr)).appendTo(row);
				$("<td/>").text(fmt(d.ref_fcpv[0])).appendTo(row);
				$("<td/>").text(fmt(d.ref_fcpv[1])).appendTo(row);
				$("<td/>").text(fmt(d.ref_fcpv[2])).appendTo(row);
				$("<td/>").text(fmt(d.ref_fcpv[3])).appendTo(row);
				row.appendTo(table);

				row[0].onclick = () => {
					this.highlightGene(d.geneName);
					this.control.highlightGene(d.geneName);
				};
			});
	}

	setPvCutOff(pv) {
		setPvCutOff(pv);
		this.loadSignificantGenes();
	}

	setEffectSize(es) {
		effectSize = es;
		this.loadSignificantGenes();
	}
}

class SLControlScreenPlot extends SLScreenPlot {
	constructor(svg, plot) {
		super(svg);

		this.updateColorMap = (data) => colorMap.setControl(data, this);
		plot.control = this;

		const controlData = screenReplicates.find(e => e.name === 'ControlData-HAP1');

		if (controlData == null)
			throw "Missing control data set";

		this.loadScreen("ControlData-HAP1", 1);
	}

	getColor() {
		return (d) => {
			const colors = d.values
				.map(d => colorMap.getColor(d))
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
}

window.addEventListener('load', () => {
	screenReplicates.forEach(o => screenReplicatesMap.set(o.name, o.replicates));

	const svg = d3.select("#plot-screen");
	const plot = new SLScreenPlot(svg);

	const controlSvg = d3.select("#plot-control");
	try {
		new SLControlScreenPlot(controlSvg, plot);
	}
	catch (err) {
		alert(err);
	}

	const screenList = document.getElementById("screenList");

	$(screenList).chosen().change(() => {
		const selected = screenList.selectedOptions;
		if (selected.length === 1) {
			const name = selected.item(0).dataset.screen;
			const replicates = screenReplicatesMap.get(name);

			plot.loadScreen(name, replicates[0])
				.then(data => plot.loadSignificantGenes());
		}
	});

	if (selectedScreen != null)
	{
		const r = screenReplicates.find(e => e.name === selectedScreen);
		if (r != null) {
			plot.loadScreen(r.name, selectedScreen, +selectedReplicate)
				.then(() => plot.highlightGene(selectedGene));
		}
	}

	for (let btn of document.getElementsByClassName("graph-color-btn")) {
		if (btn.checked)
			colorMap.selectType(btn.dataset.colortype);
		btn.onchange = () => colorMap.selectType(btn.dataset.colortype);
	}

	const pvCutOffEdit = document.getElementById("pv-cut-off");
	if (pvCutOffEdit != null) {
		pvCutOffEdit.addEventListener("change", () => {
			const pv = pvCutOffEdit.value;

			if (isNaN(pv)) {
				pvCutOffEdit.classList.add("error");
			} else {
				pvCutOffEdit.classList.remove("error");
				plot.setPvCutOff(pv);
			}
		});
	}

	const effectSizeEdit = document.getElementById("effect-size");
	if (effectSizeEdit != null) {
		effectSizeEdit.addEventListener("change", () => {
			const es = effectSizeEdit.value;

			if (isNaN(es)) {
				effectSizeEdit.classList.add("error");
			} else {
				effectSizeEdit.classList.remove("error");
				plot.setEffectSize(es);
			}
		});
	}

});

