import 'chosen-js/chosen.jquery';
import $ from 'jquery';

import * as d3 from 'd3';

import ScreenData from "./screenData";
import GenomeViewer from "./genome-viewer";
import ScreenPlot, { pvCutOff, highlightedGenes, neutral, highlight } from "./screenPlot";
import { format_pv } from './pvformat';

const positive = "#fb8", negative = "#38c", high = "#ffa82e", low = "#f442bc", notHighLow = "#444";
const cutOff = 5000;

// --------------------------------------------------------------------

class ScreenPlotRegular extends ScreenPlot {

	constructor(svg) {
		super(svg);

		this.graphType = "regular";

		if (document.graphTypeForm != null) {
			for (let btn of document.graphTypeForm.graphType) {
				if (btn.checked)
					this.graphType = btn.dataset.type;
				btn.onchange = (e) => this.selectColouring(e.target.dataset.type);
			}
		}
	}

	add(data, screenNr) {
		const result = super.add(data, screenNr);

		if (this.graphType === 'unique' || this.graphType === 'total-unique')
		{
			const options = this.getOptions();
			options.append("pv-cut-off", pvCutOff);
			options.append("single-sided", this.graphType === 'unique')
			data.loadUnique(options)
				.then(() => {
					this.recolorGenes(this.uniqueScale);
				});
		}

		this.rankRange = d3.extent(data.data.map(d => d.rank));

		return result;
	}

	getColor(/*screenNr*/) {
		return (d) => {
			const colors = d.values
				.map(d => {
					if (d.fcpv >= pvCutOff && highlightedGenes.has(d.gene))
						return highlight;

					switch (this.graphType) {
						case 'regular':
							return d.fcpv >= pvCutOff ? neutral : d.mi < 1 ? negative : positive;
						case 'high':
							return d.rank >= this.rankRange[1] - cutOff ? high : (d.fcpv >= pvCutOff ? neutral : notHighLow);
						case 'low':
							return d.rank <= /*this.rankRange[0] +*/ cutOff ? low : (d.fcpv >= pvCutOff ? neutral : notHighLow);
						case 'unique':
						case 'total-unique':
							return d.fcpv >= pvCutOff ? neutral : this.uniqueScale(d.unique);
					}
				})
				.sort()
				.reduce((a, b) => {
					if (a.indexOf(b) == -1)
						return [...a, b];
					return a;
				}, []);

			if (colors.length == 1)
				return colors[0];
			return this.getPattern(colors[0], colors[colors.length - 1]);
		}
	}

	async selectColouring(type) {
		this.graphType = type;

		if (type === "unique" || type === "total-unique") {
			const data = this.screens.values().next().value;

			const options = this.getOptions();
			options.append("pv-cut-off", pvCutOff);
			options.append("single-sided", type === "unique");
			await data.loadUnique(options);
		}

		this.plotData.selectAll("g.dot")
			.select("circle")
			.style("fill", this.getColor(0));
	}

	getOptions() {
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

		return fd;
	}

	loadScreen(screen) {
		return new Promise( (resolve, reject) => {
			let plotTitle = $(".plot-title");
			if (plotTitle.hasClass("plot-status-loading"))  // avoid multiple runs
				return;
			plotTitle.addClass("plot-status-loading").removeClass("plot-status-loaded").removeClass("plot-status-failed");

			$(".screen-name").text(screen);

			const screenData = new ScreenData(screen);

			screenData.load(this.getOptions())
				.then(() => {
					this.add(screenData, 0);
					// this.spinnerTD.classList.remove("loading");
					return screenData.data;
				})
				.then((data) => {
					// fill tables

					const fmt = d3.format(".3g");
					const fmt2 = format_pv;

					const fillTable = function (table, genes) {
						$("tr", table).remove();
						genes.forEach(d => {
							let row = $("<tr/>");
							$("<td/>").text(d.gene).appendTo(row);
							$("<td/>").text(d.low).appendTo(row);
							$("<td/>").text(d.high).appendTo(row);
							$("<td/>").text(fmt2(d.fcpv)).appendTo(row);
							$("<td/>").text(fmt(d.log2mi)).appendTo(row);
							row.appendTo(table);
						});
					};

					fillTable($("#positive-regulators"), data.filter(d => d.fcpv < pvCutOff && d.mi < 1).sort((a, b) => a.mi - b.mi));
					fillTable($("#negative-regulators"), data.filter(d => d.fcpv < pvCutOff && d.mi > 1).sort((a, b) => b.mi - a.mi));

					plotTitle.removeClass("plot-status-loading").addClass("plot-status-loaded");

					resolve(data);
				})
				.catch(err => {
					plotTitle.removeClass("plot-status-loading").addClass("plot-status-failed");
					console.log(err);

					$("#plot-status-error-text").text(err).show();

					// if (err === "invalid-credentials")
					// 	showLoginDialog(null, () => screenData.load());
					// else 
						reject(err);
				});
		});
	}

	exportCSV() {
		const screenList = document.getElementById("screenList");
		const selected = screenList.selectedOptions;
		if (selected.length === 1) {
			const screen = selected.item(0).dataset.screen;
			
			const screenData = new ScreenData(screen);

			screenData.load(this.getOptions())
				.then(() => {
					return screenData.data;
				})
				.then(data => {
					const byteArrays = [];

					const header = "gene,pv,fcpv,mi,low,high,rank\n";
					const hbytes = new Array(header.length);
					for (let i in header)
						hbytes[i] = header.charCodeAt(i);
					byteArrays.push(new Uint8Array(hbytes));

					data.map(e => [e.gene, e.pv, e.fcpv, e.mi, e.low, e.high, e.rank].join(",") + "\n")
						.forEach(l => {
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
				});
		}
	}

}

window.addEventListener('load', () => {

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

	if (typeof params["gene"] == 'string') {
		document.getElementById('highlightGene').value = params['gene'];
	}

	new GenomeViewer();

	const svg = d3.select(document.getElementById("plot"));
	const plot = new ScreenPlotRegular(svg);

	const screenList = document.getElementById("screenList");

	$(screenList).chosen().on('change', () => {
		const selected = screenList.selectedOptions;
		if (selected.length === 1) {
			const screen = selected.item(0).dataset.screen;
			plot.loadScreen(screen);
		}
	});

	$("#reload-btn").on("click", (e) => {
		if (e && e.preventDefault)
			e.preventDefault();

		const selected = screenList.selectedOptions;
		if (selected.length === 1) {
			const screen = selected.item(0).dataset.screen;
			plot.loadScreen(screen);
		}

		return false;
	});

	if (selectedScreen)
		plot.loadScreen(selectedScreen)
			.then(() => plot.highlightGene(params['gene']));
});

