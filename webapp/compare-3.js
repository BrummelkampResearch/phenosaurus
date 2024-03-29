/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * 
 * Copyright (c) 2022 NKI/AVL, Netherlands Cancer Institute
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

import 'chosen-js/chosen.jquery';

import * as d3 from 'd3';

import ScreenData from "./screenData";
import ScreenPlot, { pvCutOff, highlightedGenes } from "./screenPlot";
import { geneSelectionEditor } from './gene-selection';

const neutral = "#aaa";

const colorMap = new class ScreenColorMapSBS {

	constructor() {
		this.colorType = 'gradient';
		this.uniqueScale = d3.scaleSequential(d3.interpolatePiYG).domain([0, 9]);
	}

	async set(data) {
		this.data = data;
		this.miScale = d3.scaleSequential(d3.interpolateRdYlGn).domain(data.yRange);
		this.geneMap = new Map(
			data.data
				.filter(d => d.fcpv <= pvCutOff)
				.map(d => [d.gene, d]));
		if (this.colorType === 'unique' || this.colorType === 'total-unique')
		{
			const options = geneSelectionEditor.getOptions();
			options.append("pv-cut-off", pvCutOff);
			options.append("single-sided", this.colorType === 'unique');
			await data.loadUnique(options);
		}
	}

	getColor(plot) {
		return (d) => {

			const colors = d.values
				.map(d => {
					if (d.fcpv >= pvCutOff)
						return highlightedGenes.has(d.gene) ? "#b3ff3e" : neutral;

					switch (this.colorType) {
						case "gradient":
							return this.miScale(d.log2mi);

						case "unique":
						case 'total-unique':
							return this.uniqueScale(d.unique);
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

			return plot.getPattern(colors[0], colors[colors.length - 1]);
		};
	}

	getMappedColor(plot) {
		return (d) => {
			const g = d.values
				.map(d => this.geneMap.get(d.gene))
				.filter(d => d != null);

			if (g.length === 0)
				return d.values.indexOf(g => highlightedGenes.has(g.gene)) >= 0 ? "#b3ff3e" : neutral;

			switch (this.colorType) {
				case "gradient":
				{
					const mi = d3.extent(g.map(d => d.log2mi));``
					return plot.getPattern(this.miScale(mi[0]), this.miScale(mi[1]));
				}

				case "unique":
				case "total-unique":
						{
					const u = d3.extent(g.map(d => d.unique));
					return plot.getPattern(this.uniqueScale(u[0]), this.uniqueScale(u[1]));
				}
			}
		};
	}

	getMappedOpacity() {
		return (d) => {
			const fcpv = d3.extent(
				d.values
					.map(g => this.geneMap.get(g.gene))
					.filter(d => d != null)
					.map(d => d.fcpv));

			const myFcpv = Math.min(...d.values.map(d => d.fcpv));
			return (fcpv[0] == null || fcpv[0] > pvCutOff) && myFcpv > pvCutOff ? 0.16 : 0.66;
		}
	}

	async selectPlotColor(colorType) {
		switch (colorType) {
			case 'gradient':
				this.colorType = 'gradient';
				break;
			case 'unique':
			case 'total-unique':
			{
				this.colorType = 'unique';
				const options = geneSelectionEditor.getOptions();
				options.append("pv-cut-off", pvCutOff);
				options.append("single-sided", colorType === 'unique');
				await this.data.loadUnique(options);
				break;
			}
		}

		const btns = document.getElementById("graphColorBtns");
		btns.dispatchEvent(new Event("change-color"));
	}
}();

class ScreenPlotCompareSBSMain extends ScreenPlot {

	constructor(svg) {
		super(svg);

		this.secondaries = [];

		const btns = document.getElementById("graphColorBtns");
		btns.addEventListener("change-color", () => this.changedColorType());
	}

	add(data, screenNr) {
		colorMap.set(data);
		const result = super.add(data, screenNr);

		this.secondaries.forEach(s => s.recolorGenes());

		return result;
	}

	getColor() {
		return colorMap.getColor(this);
	}

	changedColorType() {
		this.recolorGenes();
		this.secondaries.forEach(s => s.recolorGenes());
	}
}

class ScreenPlotCompareSBSSecondary extends ScreenPlot {

	constructor(svg, main) {
		super(svg);

		main.secondaries.push(this);
	}

	add(data, screenNr) {
		return super.add(data, screenNr);
	}

	getColor() {
		return colorMap.getMappedColor(this);
	}

	getOpacity() {
		return this.presentationMode ? 1 : colorMap.getMappedOpacity();
	}
}

window.addEventListener('load', () => {

	const svg1 = d3.select("#plot-1");
	const plot1 = new ScreenPlotCompareSBSMain(svg1);

	const svg2 = d3.select("#plot-2");
	const plot2 = new ScreenPlotCompareSBSSecondary(svg2, plot1);

	$("select").chosen().on('change', function() {
		const selected = this.selectedOptions;
		if (selected.length == 1) {
			const screen = selected.item(0).dataset.screen;
			const screenData = new ScreenData(screen);
			// this.spinnerTD.classList.add("loading");

			const options = geneSelectionEditor.getOptions();

			screenData.load(options)
				.then(() => {
					switch (this.id) {
						case 'select-screen-1':
							plot1.add(screenData, this.screenNr);
							break;
						case 'select-screen-2':
							plot2.add(screenData, this.screenNr);
							break;
					}

					// this.spinnerTD.classList.remove("loading");
				})
				.catch(err => {
					// this.spinnerTD.classList.remove("loading");
					console.log(err);
					alert(err);
				});
		}
	});

	// ['gradient', 'unique'].forEach(id =>
	// 	document.getElementById(id).addEventListener("change", () => colorMap.selectPlotColor(id)));

	for (let btn of document.getElementsByClassName("graph-color-btn")) {
		if (btn.checked)
			colorMap.selectPlotColor(btn.dataset.colortype);
		btn.onchange = () => colorMap.selectPlotColor(btn.dataset.colortype);
	}

});

