import 'bootstrap';
import 'bootstrap/js/dist/modal'
import 'chosen-js/chosen.jquery';

import * as d3 from 'd3';

import ScreenData from "./screenData";
import GenomeViewer from "./genome-viewer";
import ScreenPlot, { highlightedGenes } from "./screenPlot";

const neutral = "#aaa", positive = "#fb8", negative = "#38c", high = "#ffa82e", low = "#f442bc", notHighLow = "#444";
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
			await data.loadUnique(this.pvCutOff);

		this.rankRange = d3.extent(data.data.map(d => d.rank));

		return super.add(data, screenNr);
	}

	getColor(/*screenNr*/) {
		return (d) => {
			const colors = d.values
				.map(d => {
					if (d.fcpv >= this.pvCutOff && highlightedGenes.has(d.geneName))
						return "highlight";

					switch (this.graphType) {
						case 'regular':
							return d.fcpv >= this.pvCutOff ? neutral : d.mi < 1 ? negative : positive;
						case 'high':
							return d.rank >= this.rankRange[1] - cutOff ? high : (d.fcpv >= this.pvCutOff ? neutral : notHighLow);
						case 'low':
							return d.rank <= /*this.rankRange[0] +*/ cutOff ? low : (d.fcpv >= this.pvCutOff ? neutral : notHighLow);
						case 'unique':
							return d.fcpv >= this.pvCutOff ? neutral : this.uniqueScale(d.unique);
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
			await data.loadUnique(this.pvCutOff);
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

					fillTable($("#positive-regulators"), data.filter(d => d.fcpv < this.pvCutOff && d.mi < 1).sort((a, b) => a.mi - b.mi));
					fillTable($("#negative-regulators"), data.filter(d => d.fcpv < this.pvCutOff && d.mi > 1).sort((a, b) => b.mi - a.mi));

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

	clickGenes(d, screenNr) {
		if (d.multiDot === undefined && d.values.length === 1) {

			// default is to highlight clicked genes
			const geneName = d.values[0].geneName;
			const plot = this.svg.node();
			
			const e = new Event("clicked-gene");
			e.geneID = geneName;

			plot.dispatchEvent(e);
		}

		return super.clickGenes(d, screenNr);
	}
}

window.addEventListener('load', () => {
	const [selectedID, selectedName] = $("input[name='selectedScreen']").val().split(':');

	new GenomeViewer();

	const svg = d3.select(document.getElementById("plot"));
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

	$("#reload-btn").on("click", (e) => {
		if (e && e.preventDefault)
			e.preventDefault();

		const selected = screenList.selectedOptions;
		if (selected.length === 1) {
			const screenID = selected.item(0).dataset.screen;
			const screenName = selected.item(0).textContent;

			plot.loadScreen(screenID, screenName);
		}

		return false;
	});

	if (selectedName !== "" && selectedID !== "")
		plot.loadScreen(selectedID, selectedName)
			.then(() => plot.highlightGene());

	const svgExportBtn = document.getElementById('btn-export-svg');
	if (svgExportBtn != null)
		svgExportBtn.addEventListener('click', () => plot.exportSVG());
});

