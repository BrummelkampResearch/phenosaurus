import 'chosen-js/chosen.jquery';
import ScreenPlot from './screenPlot';

import * as d3 from 'd3';

import {pvCutOff, highlightedGenes } from "./screenPlot";
import SLDot from './sl-dot';

const radius = 5;
const screenReplicatesMap = new Map();
const neutral = "#bbb", highlightColor = "#b3ff3e";

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
				if (d.significant)
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

class SLScreenPlot extends ScreenPlot {

	constructor(svg) {
		super(svg, 'sense ratio');

		this.parentColumn = svg.node().parentNode.parentNode;
		this.control = null;
		this.updateColorMap = (data) => colorMap.setData(data);

		const btns = document.getElementById("graphColorBtns");
		btns.addEventListener("change-color", () => this.updateColors());
	}

	selectReplicate(replicate) {
		this.loadScreen(this.name, replicate);
	}

	reloadScreen(name) {
		this.loadScreen(this.name ? this.name : name, this.replicateNr);
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

	process(data) {
		console.log(data);

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

		if (this.control != null)
		{
			colorMap.setSignificantGenes(data.filter(d => d.significant).map(d => d.geneName));
			this.updateSignificantTable(data);
		}

		this.highlightGenes();
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
				if (+number === +replicate)
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
		this.plotData.selectAll("g.dot")
			.select("circle")
			.style("fill", this.getColor())
			.style("opacity", this.getOpacity());
	}

	updateSignificantTable(data) {
		const table = document.getElementById("significantGenesTable");
		$("tr", table).remove();
		const fmt = d3.format(".3g");

		data
			.filter(d => d.significant)
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
			
		this.plotData.selectAll("g.dot")
			.filter(d => d.significantGene(colorMap.significantGenes))
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
	new SLControlScreenPlot(controlSvg, plot);

	const screenList = document.getElementById("screenList");

	$(screenList).chosen().change(() => {
		const selected = screenList.selectedOptions;
		if (selected.length === 1) {
			const name = selected.item(0).dataset.screen;
			const replicates = screenReplicatesMap.get(name);

			plot.loadScreen(name, replicates[0]);
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
			if (selected.length === 1) {
				const name = selected.item(0).dataset.screen;
				plot.reloadScreen(name);
			}
		})

});

