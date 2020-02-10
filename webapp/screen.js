import 'bootstrap';
import 'bootstrap/js/dist/modal'
import 'bootstrap/dist/css/bootstrap.min.css';
import '@fortawesome/fontawesome-free/css/all.min.css';
import 'chosen-js/chosen.jquery';
import 'bootstrap4c-chosen/dist/css/component-chosen.min.css';

import * as d3 from 'd3';

import ScreenData from "./screenData";
import {pvCutOff, highlightedGenes, DotContextMenu} from "./screenPlot";
import ScreenPlot from "./screenPlot";
import {showLoginDialog} from "./index";

const neutral = "#aaa", positive = "#fb8", negative = "#38c", high = "#ffa82e", low = "#f442bc", notHighLow = "#444";
const uniqueScale = d3.scaleSequential(d3.interpolatePiYG).domain([0, 9]);
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

	async add(data, screenNr) {

		if (this.graphType === 'unique')
			await data.loadUnique();

		this.rankRange = d3.extent(data.data.map(d => d.rank));

		return super.add(data, screenNr);
	}

	getColor(screenNr) {
		return (d) => {
			const colors = d.values
				.map(d => {
					if (d.fcpv >= pvCutOff && highlightedGenes.has(d.geneName))
						return "#b3ff3e";

					switch (this.graphType) {
						case 'regular':
							return d.fcpv >= pvCutOff ? neutral : d.mi < 1 ? negative : positive;
							break;
						case 'high':
							return d.rank >= this.rankRange[1] - cutOff ? high : (d.fcpv >= pvCutOff ? neutral : notHighLow);
							break;
						case 'low':
							return d.rank <= /*this.rankRange[0] +*/ cutOff ? low : (d.fcpv >= pvCutOff ? neutral : notHighLow);
							break;
						case 'unique':
							return d.fcpv >= pvCutOff ? neutral : uniqueScale(d.unique);
							break;
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

		if (type === "unique") {
			const data = this.screens.values().next().value;
			await data.loadUnique();
		}

		this.plotData.selectAll("g.dot")
			.select("circle")
			.style("fill", this.getColor(0));
	}

	loadScreen(screenID, screenName) {
		return new Promise( (resolve, reject) => {
			let plotTitle = $(".plot-title");
			if (plotTitle.hasClass("plot-status-loading"))  // avoid multiple runs
				return;
			plotTitle.addClass("plot-status-loading").removeClass("plot-status-loaded").removeClass("plot-status-failed");

			$(".screen-name").text(screenName);

			const screenData = new ScreenData(screenName, screenID);

			const f = document.geneSelectionForm;
			const fd = new FormData(f);

			fd.set("read-length", f["read-length"].value + 0);

			const geneStartOffset = document.getElementById('geneStartOffset').value + 0;

			let geneStart = document.getElementById("geneStartType").value;
			if (geneStartOffset > 0)
				geneStart += "+" + geneStartOffset;
			else if (geneStartOffset < 0)
				geneStart += geneStartOffset;

			fd.append("gene-start", geneStart);

			const geneEndOffset = document.getElementById('geneEndOffset').value + 0;

			let geneEnd = document.getElementById("geneEndType").value;
			if (geneEndOffset > 0)
				geneEnd += "+" + geneEndOffset;
			else if (geneEndOffset < 0)
				geneEnd += geneEndOffset;

			fd.append("gene-end", geneEnd);

			screenData.load(fd)
				.then(() => {
					this.add(screenData, 0);
					// this.spinnerTD.classList.remove("loading");
					return screenData.data;
				})
				.then((data) => {
					// fill tables

					const fmt = d3.format(".3g");

					const fillTable = function (table, genes) {
						$("tr", table).remove();
						genes.forEach(d => {
							let row = $("<tr/>");
							$("<td/>").text(d.geneName).appendTo(row);
							$("<td/>").text(d.low).appendTo(row);
							$("<td/>").text(d.high).appendTo(row);
							$("<td/>").text(fmt(d.fcpv)).appendTo(row);
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

					if (err === "invalid-credentials")
						showLoginDialog(null, () => screenData.load());
					else reject(err);
				});
		});

	}
}

$(function () {
	const [selectedID, selectedName] = $("input[name='selectedScreen']").val().split(':');

	const svg = d3.select("svg");
	const plot = new ScreenPlotRegular(svg);

	const screenList = document.getElementById("screenList");

	$(screenList).chosen().change(() => {
		const selected = screenList.selectedOptions;
		if (selected.length === 1) {
			const screenID = selected.item(0).dataset.screen;
			const screenName = selected.item(0).textContent;

			plot.loadScreen(screenID, screenName);
		}
	});

	if (selectedName !== "" && selectedID !== "")
		plot.loadScreen(selectedID, selectedName)
			.then(() => plot.highlightGene());

	const svgExportBtn = document.getElementById('btn-export-svg');
	if (svgExportBtn != null)
		svgExportBtn.addEventListener('click', () => plot.exportSVG());
});

