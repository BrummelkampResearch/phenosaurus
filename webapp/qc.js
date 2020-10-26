import "core-js/stable";
import "regenerator-runtime/runtime";

import '@fortawesome/fontawesome-free/css/all.min.css';
import 'bootstrap';
import 'bootstrap/js/dist/modal'
import 'bootstrap/dist/css/bootstrap.min.css';
import * as d3 from 'd3';
import 'chosen-js/chosen.jquery';
import 'bootstrap4c-chosen/dist/css/component-chosen.min.css';

/*global chromosomes screens $*/

class CanvasPlot {
	constructor(chromosome, zoomLevel) {
		if (chromosomes.indexOf(chromosome) != -1)
			this.chromosome = chromosome;
		else
			this.chromosome = null;
		this.zoomLevel = zoomLevel;

		this.plotContainer = d3.select(".plot-container");

		this.skipScreenList = [];

		// this.winsorize = document.getElementById("winsorize");	
		// // this.winsorize.addEventListener('change', () => this.update());
		// $(this.winsorize).on("change", () => this.update());

		this.graphType = "heatmap";

		for (let btn of document.graphTypeForm.graphType) {
			if (btn.checked)
				this.graphType = btn.dataset.type;
			btn.onchange = (e) => this.selectGraphType(e.target.dataset.type);
		}

		const boxWidth = $(this.plotContainer.node()).width();

		this.margin = {top: 30, right: 50, bottom: 40, left: 250};
		this.width = boxWidth - this.margin.left - this.margin.right;

		this.update();
	}

	update() {
		let plotTitle = document.getElementsByClassName("plot-title")[0];
		if (plotTitle.classList.contains("plot-status-loading"))  // avoid multiple runs
			return;

		[...document.getElementsByClassName("chr-name")]
			.forEach(e => e.textContent = this.chromosome ? this.chromosome : "all chromosomes");

		plotTitle.classList.toggle("plot-status-loading", true);
		plotTitle.classList.toggle("plot-status-loaded", false);
		plotTitle.classList.toggle("plot-status-failed", false);

		const cellWidth = 5;
		const graphWidth = this.width * this.zoomLevel;
		const requestedBinCount = Math.floor(graphWidth / cellWidth);

		const fd = new FormData();
		fd.append('requestedBinCount', requestedBinCount);
		if (this.chromosome != null)
			fd.append('chr', this.chromosome);
		if (this.skipScreenList.length > 0)
			fd.append('skip', this.skipScreenList.join(';'));
		// fd.append('winsorize', this.winsorize.checked ? 0.9 : 0.0);
		fd.append("cluster", true);

		fetch(`qc/${this.graphType}`, { credentials: "include", body: fd, method: 'POST'})
			.then(response => {
				if (response.ok)
					return response.json();

				plotTitle.classList.toggle("plot-status-loading", false);
				plotTitle.classList.toggle("plot-status-loaded", false);
				plotTitle.classList.toggle("plot-status-failed", true);
			})
			.then(data => {
				this.init(data);

				plotTitle.classList.toggle("plot-status-loading", false);
				plotTitle.classList.toggle("plot-status-loaded", true);
				plotTitle.classList.toggle("plot-status-failed", false);
			})
			.catch(err => {
				console.log(err);

				plotTitle.classList.toggle("plot-status-loading", false);
				plotTitle.classList.toggle("plot-status-loaded", false);
				plotTitle.classList.toggle("plot-status-failed", true);
			});
	}
	
	init(info)
	{
        console.log(info);

        const data = Object.entries(info.data)
            .map(d => { return { "screen": d[0], "zscores": d[1] }});

		const chromStarts = new Map(info.chromosomeStarts.map(v => [v.start, v.chrom]));

		const binCount = info.binCount;
		const screenCount = info.screens.length;
		const plotHeight = screenCount * 14;

		this.height = plotHeight - this.margin.top - this.margin.bottom;
		const graphWidth = this.width * this.zoomLevel;

		this.plotContainer.selectAll(".plot").remove();

		const graphDiv = this.plotContainer.append("div")
			.classed("plot", true)
			.style("position", "relative");

		const svg = graphDiv.append('svg')
			.style('position', 'absolute')
			.attr("width", graphWidth)
			.attr("height", plotHeight);

		const g = svg.append("g")
			.attr("transform", `translate(${this.margin.left},${this.margin.top})`);
		const xAxisG = g.append('g')
			.classed('x', true)
			.attr('transform', `translate(0,${this.height})`);

		const yAxisG = g.append('g')
			.classed('y', true);

		const x = d3.scaleLinear()
			.rangeRound([0, graphWidth])
            .domain([0, binCount]);
        
		const y = d3.scaleBand()
			.range([0, this.height])
			.domain(info.screens)
			.padding(0.25, 0.01);
	
		const xAxis = d3.axisBottom(x)
				.tickSize(-this.height);

		if (info.chromosomeStarts.length > 1)
		{
			xAxis
				.tickValues(info.chromosomeStarts.map(v => v.start))
				.tickFormat(v => chromStarts.get(v));

			xAxisG.call(g => {
				g.call(xAxis)
					.selectAll("text")
					.attr("class", "axis-label")
					.style("text-anchor", "end")
					.attr("dx", "-.8em")
					.attr("dy", ".15em")
					.attr("transform", "rotate(-60)");
				// g.select(".domain").remove();
				// g.selectAll("line")
				// 	.attr("stroke", "#ddd");
				});
		
		}
		else
		{
			const binBaseCount = info.chromosomeStarts[0].binBaseCount;
			xAxis.tickFormat(v => d3.format(",.0f")(v * binBaseCount));
			xAxisG.call(xAxis);
		}

		const yAxis = d3.axisLeft(y);

		yAxisG.call(yAxis);
        
		graphDiv.selectAll('canvas').remove();
		const canvas = graphDiv.append('canvas')
			.attr("class", "plot")
			.attr("width", graphWidth)
			.attr("height", plotHeight)
			.style('position', 'absolute');

		const context = canvas.node().getContext("2d");

        const cellWidth = (x(binCount) - x(0)) / binCount;
		
        const colorScale = d3.scaleSequential(d3.interpolateRdBu)
            .domain([-5, 5]);

		data.forEach(d => {
			d.zscores.forEach((s, i) => {
				context.fillStyle = colorScale(-s);
				context.fillRect(this.margin.left + x(i), this.margin.top + y(d.screen), cellWidth, y.bandwidth());
			});
		});

		console.log(`laatste x: ${x(data[0].zscores.length -1)}`);
	}

	showChromosome(chr) {
		if (chromosomes.indexOf(chr) != -1)
			this.chromosome = chr;
		else
			this.chromosome = null;
		this.update();
	}

	zoomPlot(zoomLevel) {
		this.zoomLevel = +zoomLevel;

		this.update();
	}

	setScreenSkipList(skip) {
		this.skipScreenList = skip;
		this.update();
	}

	async selectGraphType(type) {
		this.graphType = type;
		this.update();
	}

}

window.addEventListener('load', () => {

	const chromSelector = document.getElementById('chromosomeList');
	const zoomSelector = document.getElementById('zoomLevel');

	const plot = new CanvasPlot(
		chromSelector.options[chromSelector.selectedIndex].value,
		zoomSelector.options[zoomSelector.selectedIndex].value);

	chromSelector.addEventListener('change', () => {
		const selected = chromSelector.selectedOptions;
		if (selected.length == 1) {
			const chr = selected.item(0).dataset.chr;
			plot.showChromosome(chr);
		}
	});

	zoomSelector.addEventListener('change', () => {
		const selected = zoomSelector.selectedOptions;
		if (selected.length == 1) {
			plot.zoomPlot(selected.item(0).value);
		}
	});

	const updateScreensBtn = document.getElementById('updateSelectedScreens');
	updateScreensBtn.addEventListener('click', () => {
		const screenList = screens.filter(s => ! document.getElementById(`screen-${s}`).checked);
		console.log(screenList);
		plot.setScreenSkipList(screenList);
		$('#selectScreensModal').modal('hide');
	});
});