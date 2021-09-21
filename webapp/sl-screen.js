import ScreenPlot, { neutral, highlight, pvCutOff, highlightedGenes } from './screenPlot';
import { geneSelectionEditor } from './gene-selection';
import SLDot from './sl-dot';
import { gene } from "./geneInfo";

import * as d3 from 'd3';
import { format_pv } from './pvformat';

import GenomeViewer from "./genome-viewer";

const radius = 5;

let binomCutOff = 0.05, oddsRatioCutOff = 0.8;

const SLType = {
	None: 0,
	SyntheticLethal: 1,
	SuppressedEssential: 2,
	FitnessEnhancer: 3
};

/*global context_name, screenReplicates, selectedReplicate $ */

// --------------------------------------------------------------------

class ColorMap {

	constructor() {
		this.scale = [];
		this.geneColorMap = new Map();
	}

	getColor(d) {
		const mapped = this.geneColorMap.get(d.gene);

		if (mapped == null)
			return highlightedGenes.has(d.gene) ? highlight : neutral;

		return mapped;
	}

	setData(data) {
		let minOddsRatio = [100, 100, 100], maxOddsRatio = [0, 0, 0];
		data.filter(d => d.significant)
			.forEach(d => {
				let t = d.type;

				if (d.odds_ratio) {
					if (minOddsRatio[t - 1] > d.odds_ratio)
						minOddsRatio[t - 1] = d.odds_ratio;

					if (maxOddsRatio[t - 1] < d.odds_ratio)
						maxOddsRatio[t - 1] = d.odds_ratio;
				}
			});

		this.scale = [
			d3.scaleSequentialLog(d3.interpolateReds).domain([minOddsRatio[0], maxOddsRatio[0]]),
			d3.scaleSequentialLog(d3.interpolateBlues).domain([minOddsRatio[1], maxOddsRatio[1]]),
			d3.scaleSequentialLog(d3.interpolatePurples).domain([minOddsRatio[2], maxOddsRatio[2]])
		];

		this.geneColorMap = new Map();

		data.filter(d => d.significant)
			.forEach(d => {
				const type = d.type;
				const scale = this.scale[type - 1];
				this.geneColorMap.set(d.gene, scale(maxOddsRatio[type - 1] - d.odds_ratio));
			});
	}

	significant(gene) {
		return this.geneColorMap.has(gene);
	}
}

const colorMap = new ColorMap();

// --------------------------------------------------------------------

class SLScreenPlot extends ScreenPlot {

	constructor(svg, screenList) {
		super(svg, 'sense ratio');

		this.screenList = screenList;

		this.data = null;
		this.replicate = 0;

		this.parentColumn = svg.node().parentNode.parentNode;
		this.control = null;
		this.updateColorMap = (data) => colorMap.setData(data);

		screenList.addEventListener('change', () => {
			const selected = screenList.selectedOptions;
			if (selected.length === 1) {
				const name = selected.item(0).dataset.screen;
				this.loadScreen(name);
			}
		});

		const binomCutOffEdit = document.getElementById("binom_fdr");
		if (binomCutOffEdit != null) {
			binomCutOffEdit.addEventListener("change", () => {
				const pv = binomCutOffEdit.value;

				if (isNaN(pv)) {
					binomCutOffEdit.classList.add("error");
				}
				else {
					binomCutOffEdit.classList.remove("error");
					this.setBinomCutOff(pv);
				}
			});
		}
		binomCutOff = +binomCutOffEdit.value;

		const oddsRatioCutOffEdit = document.getElementById("odds-ratio");
		if (oddsRatioCutOffEdit != null) {
			oddsRatioCutOffEdit.addEventListener("change", () => {
				const pv = oddsRatioCutOffEdit.value;

				if (isNaN(pv)) {
					oddsRatioCutOffEdit.classList.add("error");
				}
				else {
					oddsRatioCutOffEdit.classList.remove("error");
					this.setOddsRatioCutOff(pv);
				}
			});
		}
		oddsRatioCutOff = +oddsRatioCutOffEdit.value;
	}

	createLegend() {
		const m = { top: 5, left: 0, bottom: 0, right: 5 };
		const lh = 16;
		const w = 120, h = 3 * lh + m.top + m.bottom;

		this.legendG = this.svg.append('g')
			.attr('class', 'legend')
			.attr("transform", `translate(${this.width + this.margin.left - m.right - w}, ${this.margin.top + m.top})`);

		this.legendG.append('svg:rect')
			.attr('x', 0.5)
			.attr('y', 0.5)
			.attr('height', h)
			.attr('width', w)
			.style('fill', 'white')
			.style('stroke', 'black')
			.style('stroke-width', 1);

		const c = [
			{ color: d3.interpolateReds(0.75), label: 'synthetic lethal' },
			{ color: d3.interpolateBlues(0.75), label: 'suppressed essential' },
			{ color: d3.interpolatePurples(0.75), label: 'fitness enhancer' }
		]

		for (let ci in c) {
			ci = +ci;
			const cc = c[ci];
			this.legendG.append('circle')
				.attr('cx', radius * 2)
				.attr('cy', m.top + (ci + 0.5) * lh)
				.attr('r', radius)
				.style('fill', cc.color);
			this.legendG.append('text')
				.attr('x', radius * 4)
				.attr('y', m.top + (ci + 0.75) * lh)
				.text(cc.label);
		}
	}

	reloadScreen(name) {
		this.loadScreen(this.name ? this.name : name);
	}

	loadScreen(name, replicate = 0) {
		this.screenList.selectedIndex = [...this.screenList.options].map(option => option.label).indexOf(name);

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

			if (this.control != null)
				options.append("control", this.control.name);

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
			if (d.highlight() || d.values.findIndex(g => colorMap.significant(g.gene)) >= 0) return 1;
			if (d.significant(binomCutOff)) return this.presentationMode ? 1 : 0.66;
			return 0.16;
		};
	}

	mouseOver(d) {
		if (d.multiDot === undefined)
			gene.setSL(d.values);
	}

	process(replicate) {

		this.replicate = replicate;
		if (this.data == null || this.replicate > this.data[0].replicate.length) {
			screenPlot.selectAll("g.dot")
				.remove();
			return;
		}

		this.updateReplicateBtns(replicate);

		const data = [];
		this.data
			.filter(d => d.replicate[replicate].sense_normalized && d.replicate[replicate].antisense_normalized)
			.forEach(d => {
				const sense = d.replicate[replicate].sense_normalized;
				const antisense = d.replicate[replicate].antisense_normalized;
				const insertions = sense + antisense;
				const sense_ratio = (1 + sense) / (2 + insertions);
				const odds_ratio =
					d.odds_ratio > 1 ? 1 / d.odds_ratio : d.odds_ratio;

				const maxPV = Math.max(...d.replicate.map(r => Math.max(...r.ref_pv)));
				const maxBinom = Math.max(...d.replicate.map(r => r.binom_fdr));

				data.push({
					gene: d.gene,
					sense: sense,
					antisense: antisense,
					insertions: insertions,
					sense_ratio: sense_ratio,
					aggr_sense_ratio: d.sense_ratio,
					odds_ratio: odds_ratio,
					pv: maxPV,
					binom_fdr: maxBinom,

					consistent: d.consistent,

					control_binom: d.control_binom,
					control_sense_ratio: d.control_sense_ratio,

					replicate: d.replicate,

					get significant() {
						// return this.consistent && this.pv < pvCutOff && this.binom_fdr < binomCutOff;
						return this.consistent && this.pv < pvCutOff && this.odds_ratio < oddsRatioCutOff && this.type != SLType.None;
					},

					get type() {
						if (this.aggr_sense_ratio < this.control_sense_ratio && this.aggr_sense_ratio < 0.5 && this.binom_fdr < binomCutOff)
							return SLType.SyntheticLethal;

						if (this.control_binom < binomCutOff && this.control_sense_ratio < 0.5 && (this.binom_fdr >= binomCutOff || this.aggr_sense_ratio < 0.5))
							return SLType.SuppressedEssential;

						if (this.aggr_sense_ratio > 0.5 && this.binom_fdr < binomCutOff && (this.control_sense_ratio < 0.5 || this.control_binom > binomCutOff))
							return SLType.FitnessEnhancer;

						return SLType.None;
					}
				});
			});

		// const data = this.data[0].replicate[this.replicate].data;

		this.screens.set(0, data);

		let maxInsertions = Math.max(...data.map(d => d.insertions));
		maxInsertions = Math.ceil(Math.pow(10, Math.log10(maxInsertions) + 0.1));

		this.dotData = d3.nest()
			.key(d => [d.sense_ratio, d.insertions].join(":"))
			.entries(data)
			.map(d => new SLDot(d.key, d.values));

		const xRange = [1, maxInsertions];
		const yRange = [0, 1];
		const [x, y] = this.adjustAxes(xRange, yRange);

		const screenPlotID = 'plot-0';
		let screenPlot = this.plotData.select(`#${screenPlotID}`);
		if (screenPlot === null || screenPlot.empty()) {
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
			this.updateSignificantTable();

		this.highlightGenes();
		this.recolorGenes();
	}

	updateReplicateBtns(number) {
		const replicateBtnContainer = this.parentColumn.getElementsByClassName("replicate-btn-container")[0];

		[...replicateBtnContainer.getElementsByTagName("label")]
			.forEach(r => r.remove());

		if (this.data[0].replicate.length > 0) {
			const btns = [];

			for (let i = 0; i < this.data[0].replicate.length; ++i) {
				const replicate = i + 1;
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
				btn.addEventListener('change', () => this.process(i));

				btns.push(label);
			}

			btns.sort((a, b) => a.textContent > b.textContent)
				.forEach(b => replicateBtnContainer.appendChild(b));
		}
	}

	recolorGenes(uniqueScale) {
		if (this.control != null)
			colorMap.setData(this.screens.get(0));

		super.recolorGenes(uniqueScale);

		this.plotData.selectAll("g.dot")
			.select("circle")
			.style("fill", this.getColor())
			.style("opacity", this.getOpacity());

		this.plotData.selectAll("g.dot")
			.filter(d => d.highlight() || d.values.findIndex(g => colorMap.significant(g.gene)) >= 0)
			.raise();

		if (this.control != null)
			this.control.recolorGenes(uniqueScale);
	}

	setPvCutOff(pv) {
		if (this.control != null)
		{
			super.setPvCutOff(+pv);
			this.recolorGenes();
	
			this.updateSignificantTable();
		}
	}

	setBinomCutOff(binom) {
		if (this.control != null)
		{
			binomCutOff = +binom;
			this.recolorGenes();
	
			this.updateSignificantTable();
		}
	}

	setOddsRatioCutOff(or) {
		if (this.control != null)
		{
			oddsRatioCutOff = +or;
			this.recolorGenes();
	
			this.updateSignificantTable();
		}
	}

	updateSignificantTable() {
		const table = document.getElementById("significantGenesTable");
		[...table.querySelectorAll("tr")].forEach(tr => tr.remove());
		const fmt2 = format_pv;
		const fmt3 = d3.format(".3g");

		this.screens.get(0)
			.filter(d => d.significant)
			.sort((a, b) => a.gene > b.gene)
			.forEach(d => {
				const row = document.createElement("tr");
				const col = (text) => {
					const td = document.createElement("td");
					td.textContent = text;
					row.appendChild(td);
				};

				col(d.gene);
				col(d.odds_ratio ? fmt2(d.odds_ratio) : '');
				col(fmt2(d.sense_ratio));
				col(`${d.sense}/${d.antisense}`);
				col(fmt3(d.replicate[this.replicate].binom_fdr));
				col(fmt2(d.aggr_sense_ratio));
				col(fmt2(d.control_binom));
				col(fmt2(d.control_sense_ratio));

				col(fmt2(d.replicate[this.replicate].ref_pv[0]));
				col(fmt2(d.replicate[this.replicate].ref_pv[1]));
				col(fmt2(d.replicate[this.replicate].ref_pv[2]));
				col(fmt2(d.replicate[this.replicate].ref_pv[3]));

				table.appendChild(row);
				// row.appendTo(table);

				row.addEventListener('click', () => {
					this.highlightGene(d.gene);
					this.control.highlightGene(d.gene);
				});
			});
	}

	exportCSV() {

		const byteArrays = [];

		const header = [
			"gene",
			"sense",
			"antisense",
			"sense_normalized",
			"antisense_normalized",
			"binom_fdr",
			"odds_ratio",
			"aggr_odds_ratio",
			"ref_pv_1",
			"ref_pv_2",
			"ref_pv_3",
			"ref_pv_4",
			"cntl_binom_fdr",
			"cntl_senseratio"
		];
		const hbytes = new Array(header.length);
		const hl = header.join('\t') + '\n';
		for (let i in hl)
			hbytes[i] = hl.charCodeAt(i);
		byteArrays.push(new Uint8Array(hbytes));

		const data = this.screens.get(0);
		data.forEach(d => {
			const values = [
				d.gene,												// gene
				d.replicate[this.replicate].sense,					// sense
				d.replicate[this.replicate].antisense,				// antisense
				d.replicate[this.replicate].sense_normalized,		// sense_normalized
				d.replicate[this.replicate].antisense_normalized,	// antisense_normalized
				d.binom_fdr,										// binom_fdr
				d.odds_ratio,										// odds_ratio
				d.aggr_sense_ratio,									// aggr_sense_ratio
				d.replicate[this.replicate].ref_pv[0],				// ref_pv_1
				d.replicate[this.replicate].ref_pv[1],				// ref_pv_2
				d.replicate[this.replicate].ref_pv[2],				// ref_pv_3
				d.replicate[this.replicate].ref_pv[3],				// ref_pv_4
				d.control_binom,									// control_binom
				d.control_sense_ratio								// control_sense_ratio
			];

			const l = values.join('\t') + '\n';

			const bytes = new Array(l.length);
			for (let i in l)
				bytes[i] = l.charCodeAt(i);
			byteArrays.push(new Uint8Array(bytes));
		});

		const blob = new Blob(byteArrays, { type: 'text/plain' });
		const url = window.URL.createObjectURL(blob);
		const a = document.createElement('a');
		a.href = url;
		a.download = `Raw_data_for_${screen}.csv`;
		document.body.appendChild(a); // we need to append the element to the dom -> otherwise it will not work in firefox
		a.click();
		a.remove();
	}
}

class SLControlScreenPlot extends SLScreenPlot {
	constructor(svg, plot, screenList) {
		super(svg, screenList);

		this.updateColorMap = (data) => {};
		plot.control = this;
	}

	reload() {
		return this.loadScreen(this.name, 0);
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
	const selectedControl = params["control"] || 'ControlData-HAP1';
	const selectedReplicate = params["replicate"] || 1;

	const screenList = document.getElementById("screenList");

	const svg = d3.select("#plot-screen");
	const plot = new SLScreenPlot(svg, screenList, selectedScreen);

	const controlList = document.getElementById("screenListControl");

	const controlSvg = d3.select("#plot-control");
	const controlPlot = new SLControlScreenPlot(controlSvg, plot, controlList);

	controlPlot.loadScreen(selectedControl)
		.then(() => {
			if (typeof selectedScreen === 'string') {
				const r = screenReplicates.find(e => e.name === selectedScreen);
				if (r != null) {
					plot.loadScreen(selectedScreen, +selectedReplicate)
						.then(() => plot.highlightGene(params['gene']));
				}
			}
		});

	const oddsRatioEdit = document.getElementById("odds-ratio");
	if (oddsRatioEdit != null) {
		oddsRatioEdit.addEventListener("change", () => {
			const es = oddsRatioEdit.value;

			if (isNaN(es)) {
				oddsRatioEdit.classList.add("error");
			} else {
				oddsRatioEdit.classList.remove("error");
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

