import 'chosen-js/chosen.jquery';
import ScreenPlot, { neutral, highlight, pvCutOff, highlightedGenes } from './screenPlot';
import { geneSelectionEditor } from './gene-selection';
import SLDot from './sl-dot';

import * as d3 from 'd3';

import GenomeViewer from "./genome-viewer";

const radius = 5;

export let significantGenes = new Set();

/*global context_name, screenReplicates, selectedReplicate $ */

// --------------------------------------------------------------------

class ColorMap {

	constructor() {
		this.scale = d3.scaleSequential(d3.interpolateReds).domain([0, 1]);

		this.geneColorMap = new Map();
		this.type = 'raw';
	}

	getColor(d) {
		const mapped = this.geneColorMap.get(d.gene);

		if (mapped == null || mapped.binom_fdr == null || mapped.binom_fdr >= pvCutOff)
			return highlightedGenes.has(d.gene) ? highlight : neutral;

		switch (this.type) {
			case 'raw':
				return this.scale(mapped.diff);
			case 'significant':
				if (significantGenes.has(d.gene))
					return this.scale(mapped.diff);
				else if (highlightedGenes.has(d.gene))
					return highlight;
				else
					return neutral;
		}
	}

	setControl(data, control) {
		this.control = control;

		data.forEach(d => {
			const prev = this.geneColorMap.get(d.gene);
			if (prev != null) {
				prev.diff += d.senseratio - prev.control;
				prev.control = d.senseratio;
			}
			else
				this.geneColorMap.set(d.gene, {control: d.senseratio});
		});
	}

	setData(data) {
		let maxDiff = 0;
		data.forEach(d => {
			const e = this.geneColorMap.get(d.gene);
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
		significantGenes = new Set(genes);

		if (this.type === 'significant') {
			const btns = document.getElementById("graphColorBtns");
			btns.dispatchEvent(new Event("change-color"));
		}

		return significantGenes;
	}

	selectType(colortype) {
		this.type = colortype;

		const btns = document.getElementById("graphColorBtns");
		btns.dispatchEvent(new Event("change-color"));
	}
}

const colorMap = new ColorMap();

// --------------------------------------------------------------------

class SLScreenPlot extends ScreenPlot {

	constructor(svg) {
		super(svg, 'sense ratio');

		this.data = null;
		this.replicate = 0;

		this.parentColumn = svg.node().parentNode.parentNode;
		this.control = null;
		this.updateColorMap = (data) => colorMap.setData(data);

		const btns = document.getElementById("graphColorBtns");
		btns.addEventListener("change-color", () => this.updateColors());
	}

	reloadScreen(name) {
		this.loadScreen(this.name ? this.name : name);
	}

	loadScreen(name, replicate = 0) {
		return new Promise((resolve, reject) => {
			this.name = name;
			this.screen = name;	// for click-genes -> genome-viewer

			const plotTitle = this.parentColumn.getElementsByClassName("plot-title")[0];
			if (plotTitle.classList.contains("plot-status-loading"))  // avoid multiple runs
				return;
			plotTitle.classList.add("plot-status-loading");
			plotTitle.classList.remove("plot-status-loaded", "plot-status-failed");

			[...this.parentColumn.getElementsByClassName("screen-name")]
				.forEach(sn => sn.textContent = name);

			const options = geneSelectionEditor.getOptions();

			options.append("pvCutOff", pvCutOff);
			options.append("binomCutOff", document.getElementById('binom_fdr').value);
			options.append("effectSize", document.getElementById('effect-size').value);

			fetch(`${context_name}sl/screen/${name}`,
					{ credentials: "include", method: "post", body: options })
				.then(value => {
					if (value.ok)
						return value.json();
					if (value.status === 403)
						throw "invalid-credentials";
				})
				.then(data => {

					this.data = data;

					const significant = colorMap.setSignificantGenes(this.data.significant);

					this.data.replicate.forEach(r => {
						r.data.forEach(d => {
							d.sense_raw = d.sense;
							d.antisense_raw = d.antisense;
	
							d.sense = d.sense_normalized;
							d.antisense = d.antisense_normalized;
	
							d.insertions = d.sense + d.antisense;
							d.senseratio = (d.sense + 1) / (d.insertions + 2);

							d.significant = significant.has(d.gene);
						});
					})

					this.process(replicate);
					plotTitle.classList.remove("plot-status-loading");
					plotTitle.classList.add("plot-status-loaded");
					resolve();
				})
				.catch(err => {
					plotTitle.classList.remove("plot-status-loading");
					plotTitle.classList.add("plot-status-failed");
					console.log(err);
					if (err === "invalid-credentials")
						alert('session timeout, please login again');
						// showLoginDialog(null, () => this.loadScreen(this.name, this.id));
					else reject(err);
				});
		});
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

	getOpacity() {
		return (d) => {
			if (d.highlight() || d.values.findIndex(g => significantGenes.has(g.gene)) >= 0) return 1;
			if (d.significant(pvCutOff)) return this.presentationMode ? 1 : 0.66;
			return 0.16;
		};
	}

	process(replicate) {
		
		this.replicate = replicate;
		if (this.data == null || this.replicate > this.data.replicate.length)
		{
			screenPlot.selectAll("g.dot")
				.remove();
			return;
		}

		this.updateReplicateBtns(replicate);

		const data = this.data.replicate[this.replicate].data;

		this.screens.set(0, data);

		this.updateColorMap(data);

		let maxInsertions = Math.max(...data.map(d => d.insertions));
		maxInsertions = Math.ceil(Math.pow(10, Math.log10(maxInsertions) + 0.1));

		this.dotData = d3.nest()
			.key(d => [d.senseratio, d.insertions].join(":"))
			.entries(data)
			.map(d => new SLDot(d.key, d.values));

		const xRange = [1, maxInsertions];
		const yRange = [0, 1];
		const [x, y] = this.adjustAxes(xRange, yRange);

		const screenPlotID = 'plot-0';
		let screenPlot = this.plotData.select(`#${screenPlotID}`);
		if (screenPlot === null || screenPlot.empty())
		{
			screenPlot = this.plotData.append("g")
				.classed("screen-plot", true)
				.attr("id", screenPlotID);
		}

		const dots = screenPlot.selectAll("g.dot")
			.data(this.dotData, d => d.key);

		dots.exit()
			.remove();

		let gs = dots.enter()
			.append("g")
			.attr("class", "dot")
			.attr("transform", d => `translate(${x(d.x)},${y(d.y)})`);

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

		if (this.control != null)
			this.updateSignificantTable(data);

		this.highlightGenes();
	}

	updateReplicateBtns(number) {
		const replicateBtnContainer = this.parentColumn.getElementsByClassName("replicate-btn-container")[0];

		[...replicateBtnContainer.getElementsByTagName("label")]
			.forEach(r => r.remove());

		if (this.data.replicate.length > 0) {
			const btns = [];

			for (let i in this.data.replicate) {
				const replicate = this.data.replicate[i].name.replace(/replicate-/, '');
				const label = document.createElement("label");
				label.classList.add("btn", "btn-secondary");
				if (+number === +i)
					label.classList.add("active");
				const btn = document.createElement("input");
				btn.type = "radio";
				btn.name = "replicate";
				btn.autocomplete = "off";
				btn.dataset.replicate = i;
				label.appendChild(btn);
				label.appendChild(document.createTextNode(`${replicate}`));
				btn.onchange = () => this.process(i);

				btns.push(label);
			}

			btns.sort((a, b) => a.textContent > b.textContent)
				.forEach(b => replicateBtnContainer.appendChild(b));
		}
	}

	updateColors() {
		this.plotData.selectAll("g.dot")
			.select("circle")
			.style("fill", this.getColor())
			.style("opacity", this.getOpacity());
	}

	updateSignificantTable(data) {
		const table = document.getElementById("significantGenesTable");
		[...table.querySelectorAll("tr")].forEach(tr => tr.remove());
		const fmt = d3.format(".3g");

		data
			.filter(d => d.significant)
			.forEach(d => {
				const row = document.createElement("tr");
				const col = (text) => {
					const td = document.createElement("td");
					td.textContent = text;
					row.appendChild(td);
				};

				col(d.gene);
				col(d.senseratio.toFixed(2));
				col(d.insertions);
				col(fmt(d.binom_fdr));
				col(fmt(d.ref_fcpv[0]));
				col(fmt(d.ref_fcpv[1]));
				col(fmt(d.ref_fcpv[2]));
				col(fmt(d.ref_fcpv[3]));

				table.appendChild(row);
				// row.appendTo(table);

				row.addEventListener('click', () => {
					this.highlightGene(d.gene);
					this.control.highlightGene(d.gene);
				});
			});
			
		this.plotData.selectAll("g.dot")
			.filter(d => d.significantGene())
			.raise();
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

		this.loadScreen("ControlData-HAP1", 0);
	}

	updateColors() {
		if (this.dotData !== undefined) {
			this.dotData
				.forEach(d => {
					d.values.forEach(v => v.significant = significantGenes.has(v.gene));
				});
		}
		
		this.plotData
			.selectAll("g.dot")
			.filter(d => d.significantGene())
			.raise();

		super.updateColors();
	}

	reload() {
		return this.loadScreen("ControlData-HAP1", 0);
	}
}

window.addEventListener('load', () => {

	new GenomeViewer();

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

	// const [selectedID, selectedName] = $("input[name='selectedScreen']").val().split(':');
	const selectedScreen = params["screen"];

	const svg = d3.select("#plot-screen");
	const plot = new SLScreenPlot(svg);

	const controlSvg = d3.select("#plot-control");
	const controlPlot = new SLControlScreenPlot(controlSvg, plot);

	const screenList = document.getElementById("screenList");

	$(screenList).chosen().on('change', () => {
		const selected = screenList.selectedOptions;
		if (selected.length === 1) {
			const name = selected.item(0).dataset.screen;
			plot.loadScreen(name);
		}
	});

	if (typeof selectedScreen === 'string')
	{
		const r = screenReplicates.find(e => e.name === selectedScreen);
		if (r != null) {
			plot.loadScreen(selectedScreen, +selectedReplicate)
				.then(() => plot.highlightGene(params['gene']));
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
			}
		});
	}

	const reloadButton = document.getElementById("reload-btn");
	if (reloadButton != null)
		reloadButton.addEventListener('click', (evt) => {
			evt.preventDefault();

			const selected = screenList.selectedOptions;

			controlPlot
				.reload()
				.then(() => {
					if (selected.length === 1) {
						const name = selected.item(0).dataset.screen;
						plot.reloadScreen(name);
					}
				});
		})

});

