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

import * as d3 from "d3";
import ContextMenu from './context-menu';
import Tooltip from "./tooltip";

/*global context_name */

// --------------------------------------------------------------------

const tooltip = new Tooltip();

const INSERTION_STRIP_HEIGHT = 10;

// --------------------------------------------------------------------

function getRandomInt(max) {
	return Math.floor(Math.random() * max);
}

// --------------------------------------------------------------------

export class GenomeViewerContextMenu extends ContextMenu {

	constructor(viewer, menuID) {

		const container = document.getElementById('genome-viewer-container');

		super(menuID ? menuID : "genome-viewer-context-menu", container);

		this.viewer = viewer;
	}

	clickIsInsideTarget(e) {
		let el = e.srcElement || e.target;

		const container = document.getElementById('genome-viewer-container');

		while (el != null && el !== container) {
			el = el.parentNode;
		}

		return el;
	}

	handleSelect(target, action) {
		switch (action) {

			case 'export-svg':
				this.viewer.exportSVG();
				break;

			default:
				super.handleSelect(target, action);
		}
	}
}

// --------------------------------------------------------------------

export default class GenomeViewer {

	constructor(svg) {

		this.svg = svg;

		[...document.querySelectorAll("svg.fishtail")]
			.forEach(plot => plot.addEventListener("clicked-gene", (event) => this.selectedGene(event)));

		[...document.querySelectorAll("svg.fishtail")]
			.forEach(plot => plot.addEventListener("reset-screen", () => {
				if (this.svg) {
					this.svg.remove();
					this.svg = null;
				}
			}));

		// create the context menu
		const contextMenuDiv = document.getElementById("genome-viewer-context-menu");
		if (contextMenuDiv)
			this.contextMenu = new GenomeViewerContextMenu(this, "genome-viewer-context-menu");
	}

	createSVG(nrOfGenes, nrOfInsertionLines) {
		if (this.svg)
			this.svg.remove();

		const container = document.getElementById('genome-viewer-container');

		const boxWidth = container.clientWidth;
		const boxHeight = 32 + nrOfInsertionLines * INSERTION_STRIP_HEIGHT + nrOfGenes * INSERTION_STRIP_HEIGHT;

		const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");

		svg.setAttributeNS(null, "viewBox", "0 0 " + boxWidth + " " + boxHeight);
		svg.setAttributeNS(null, "width", boxWidth);
		svg.setAttributeNS(null, "height", boxHeight);
		svg.style.display = "block";

		container.appendChild(svg);

		this.svg = d3.select(svg);

		this.svg.node().addEventListener('wheel', (evt) => {
			evt.stopPropagation();
			evt.preventDefault();
			return false;
		}, false);


		const viewerContainer = this.svg.node();
		const bBoxWidth = viewerContainer.clientWidth;
		const bBoxHeight = viewerContainer.clientWidth;

		this.margin = { top: 0, right: 50, bottom: 30, left: 50 };
		this.width = bBoxWidth - this.margin.left - this.margin.right;
		this.height = bBoxHeight - this.margin.top - this.margin.bottom;

		this.defs = this.svg.append('defs');

		this.defs
			.append("svg:clipPath")
			.attr("id", "gv-clip")
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
			.text("position");

		this.svg.append("text")
			.attr("class", "y axis-label")
			.attr("text-anchor", "end")
			.attr("x", -this.margin.top)
			.attr("y", 6)
			.attr("dy", ".75em")
			.attr("transform", "rotate(-90)")
			.text("insertions");

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
			.attr("clip-path", "url(#gv-clip)");

		this.plotData = this.plot.append('g')
			.attr("width", this.width)
			.attr("height", this.height);

		this.insertionsData = this.plotData.append('g')
			.attr("width", this.width)
			.attr("height", 25);

		this.genesData = this.plotData.append('g')
			.attr("width", this.width)
			.attr("height", nrOfGenes * INSERTION_STRIP_HEIGHT);

		// const zoom = d3.zoom()
		// 	.scaleExtent([1, 40])
		// 	.translateExtent([[0, 0], [this.width + 90, this.height + 90]])
		// 	.on("zoom", () => this.zoomed());

		// this.svg.call(zoom);

	}

	selectedGene(event) {
		const gene = event.gene;

		if (this.svg) {
			this.svg.remove();
			this.svg = null;
		}

		const f = document.geneSelectionForm;
		const fd = new FormData(f);

		let screenID = event.screen;

		if (screenID === null) {
			const screenList = document.getElementById("screenList");
			const selected = screenList.selectedOptions;
			if (selected.length === 1) {
				screenID = selected.item(0).dataset.screen;
			}
		}

		fd.set("screen", screenID);
		if (event.replicate != null)
			fd.set("replicate", event.replicate);

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

		fetch(`gene-info/${gene}`, { credentials: "include", method: "post", body: fd })
			.then(data => {
				if (data.ok)
					return data.json();
				if (data.status == 403)
					throw "invalid-credentials";
				throw data.json();
			})
			.then(data => {
				this.setGene(data);
			})
			.catch(err => {
				alert('error, see console');
				console.log(err);
			});
	}

	zoomed() {
		if (this.xAxis != null) {
			this.plotData.attr('transform', d3.event.transform);

			const x = d3.event.transform.rescaleX(this.x);

			this.insertionsData.selectAll("rect.ins")
				.attr("x", d => x(d));

			this.gX.call(this.xAxis.scale(x));
		}
	}

	setGene(data) {
		this.createSVG(data.genes.length, data.insertions.length);

		this.region = data;

		this.svg.select("text.x.axis-label")
			.text(`position at chromosome ${data.chrom}`);

		const f = document.geneSelectionForm;
		const direction = f['direction'] ? f['direction'].value : 'sense';

		const x = this.adjustAxis();

		let Y = -INSERTION_STRIP_HEIGHT;

		// this.insertionsData.selectAll('rect.area').remove();
		// data.area.forEach(a => {
		// 	for (let r = 0; r < data.insertions.length; ++r)
		// 	{
		// 		this.insertionsData
		// 			.append("rect")
		// 			.attr("class", "area")
		// 			.attr("x", x(a.start))
		// 			.attr("y", r * INSERTION_STRIP_HEIGHT)
		// 			.attr("width", x(a.end) - x(a.start))
		// 			.attr("height", INSERTION_STRIP_HEIGHT - 1)
		// 			.attr("fill", "#eee")
		// 			.attr("opacity", 0.5);
		// 	}
		// });

		this.insertionsData.selectAll('g.ins').remove();

		data.insertions.sort((a, b) => {
			let d = a.name.localeCompare(b.name);
			if (d == 0)
				d = -a.strand.localeCompare(b.strand);
			return d > 0;
		}).map(d => {
			return {
				name: d.name,
				strand: d.strand,
				low: d.name === "low",
				high: d.name === "high",
				y: (Y += INSERTION_STRIP_HEIGHT),
				i: d.pos,
				n: `${d.name}-${d.strand === '+' ? 'p' : 'm'}`,
				sense: data.geneStrand === d.strand
			};
		}).forEach(ii => {

			const g = this.insertionsData
				.append("g")
				.attr("class", "ins")
				.attr("transform", `translate(0, ${ii.y})`)
				.on("mouseover", () => tooltip.show(`${ii.name} ${ii.strand}`, d3.event.pageX + 5, d3.event.pageY - 5))
				.on("mouseout", () => tooltip.hide());

			data.area.forEach(a => {
				g.append("rect")
					.attr("class", "area")
					.attr("x", x(a.start))
					.attr("width", x(a.end) - x(a.start))
					.attr("height", INSERTION_STRIP_HEIGHT - 1)
					.attr("fill", "#eee")
					.attr("opacity", 0.5);
			});

			const r = g.selectAll(`rect.${ii.n}`)
				.data(ii.i, d => d);

			r.exit().remove();
			const l = r.enter()
				.append("rect")
				.attr("class", `ins ${ii.n}`)
				.attr("y", () => getRandomInt(INSERTION_STRIP_HEIGHT - 3))
				.attr("height", 2)
				.attr("width", 2)
				.attr("fill", d => {
					let color = "#888";
					if (direction == 'both' || (direction == 'sense' && ii.sense) || (direction == 'antisense' && !ii.sense)) {
						this.region.area.forEach(a => {
							if (d >= a.start && d < a.end)
								color = ii.low ? "#3bc" : "#fb8";
						});
					}
					return color;
				});

			l.merge(r)
				.attr("x", d => x(d + 1));
		});

		// number the genes to get an id
		let nr = 0;
		data.genes.forEach(g => g.nr = nr++);

		const g = this.genesData.selectAll("g.gene")
			.data(data.genes, g => g.nr);

		g.exit().remove();
		const gl = g.enter()
			.append("g")
			.attr("class", "gene")
			.attr("transform", g => `translate(0, ${Y + 12 + g.nr * INSERTION_STRIP_HEIGHT})`)
			.on("mouseover", g => tooltip.show(g.name, d3.event.pageX + 5, d3.event.pageY - 5))
			.on("mouseout", () => tooltip.hide());

		gl.append("line")
			.attr("class", "direction")
			.style("stroke", "#777")
			.style("stroke-width", 1)
			.attr("shape-rendering", "crispEdges")
			.attr("y1", 3)
			.attr("y2", 3)
			.attr("marker-end", g => `url(#gene-head-${g.nr})`);

		gl.append("marker")
			.attr("id", g => `gene-head-${g.nr}`)
			.attr("orient", "auto")
			.attr("markerWidth", 3)
			.attr("markerHeight", 6)
			.attr("refX", 3)
			.attr("refY", 3)
			.append("path")
			.attr("d", "M0,0 V6 L3,3 Z")
			.style("fill", "#777");

		gl.merge(g)
			.select("line")
			.attr("x1", gene => gene.strand == '+' ? x(gene.txStart + 1) : x(gene.txEnd + 1))
			.attr("x2", gene => gene.strand == '-' ? x(gene.txStart + 1) - 8 : x(gene.txEnd + 1) + 8);

		[
			{ f: "exons", cl: "exon", c: "#daa520", y: 1, h: 5 },
			{ f: "utr3", cl: "utr-3", c: "#13728c", y: 2, h: 3 },
			{ f: "utr5", cl: "utr-5", c: "#13728c", y: 2, h: 3 },
		].forEach(ii => {
			const r = gl.merge(g)
				.selectAll(`rect.${ii.cl}`)
				.data(gene => gene[ii.f]);

			r.exit().remove();
			r.enter()
				.append("rect")
				.attr("class", ii.cl)
				.attr("x", e => x(e.start))
				.attr("y", ii.y)
				.attr("width", e => x(e.end) - x(e.start))
				.attr("height", ii.h)
				.attr("shape-rendering", "crispEdges")
				.style("fill", ii.c);
		});

	}

	recolor() {
		const f = document.geneSelectionForm;
		const direction = f['direction'].value;

		[
			{ low: false, y: 0 * INSERTION_STRIP_HEIGHT, i: this.region.highPlus, n: "high-p", sense: this.region.geneStrand == '+' },
			{ low: false, y: 1 * INSERTION_STRIP_HEIGHT, i: this.region.highMinus, n: "high-m", sense: this.region.geneStrand == '-' },
			{ low: true, y: 2 * INSERTION_STRIP_HEIGHT, i: this.region.lowPlus, n: "low-p", sense: this.region.geneStrand == '+' },
			{ low: true, y: 3 * INSERTION_STRIP_HEIGHT, i: this.region.lowMinus, n: "low-m", sense: this.region.geneStrand == '-' }
		].forEach(ii => {
			this.insertionsData.selectAll(`rect.${ii.n}`)
				.attr("fill", d => {
					let color = "#888";
					if (direction == 'both' || (direction == 'sense' && ii.sense) || (direction == 'antisense' && !ii.sense)) {
						this.region.area.forEach(a => {
							if (d >= a.start && d < a.end)
								color = ii.low ? "#3bc" : "#fb8";
						});
					}
					return color;
				});
		});
	}

	adjustAxis() {
		const xRange = [this.region.start + 1, this.region.end + 1];

		const x = d3.scaleLinear()
			.domain(xRange)
			.range([0, this.width]);
		this.x = x;

		const xAxis = d3.axisBottom(x)
			.tickSizeInner(-this.height)
			.tickArguments([15, ".0f"]);
		this.xAxis = xAxis;

		this.gX.call(xAxis);

		// // adjust current dots for new(?) axes
		// this.plotData.selectAll("g.dot")
		// 	.attr("transform", d => `translate(${x(d.x)},${y(d.y)})`);

		return x;
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

}