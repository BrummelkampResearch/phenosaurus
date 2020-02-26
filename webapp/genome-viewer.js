import * as d3 from "d3";
import ContextMenu from './contextMenu';
import Tooltip from "./tooltip";

// --------------------------------------------------------------------

const tooltip = new Tooltip();

// --------------------------------------------------------------------

export class GenomeViewerContextMenu extends ContextMenu {

	constructor(viewer, menuID) {
		super(menuID ? menuID : "genome-viewer-context-menu");

		this.viewer = viewer;
		this.svg = viewer.svg.node();
	}
}

// --------------------------------------------------------------------

export default class GenomveViewer {

	constructor(svg) {

		const plot = document.getElementById("plot");
		if (plot != null) {
			plot.addEventListener("clicked-gene", (event) => this.selectedGene(event.geneID));
		}

		// create the context menu
		const contextMenuDiv = document.getElementById("genome-viewer-context-menu");
		if (contextMenuDiv)
			this.contextMenu = new GenomeViewerContextMenu(this, "genome-viewer-context-menu");
	}

	createSVG(nrOfGenes) {
		if (this.svg)
			this.svg.remove();
		
		const container = document.getElementById('genome-viewer-container');

		const boxWidth = container.clientWidth;
		const boxHeight = 60 + nrOfGenes * 7;

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


		const viewerContainer = $(this.svg.node());
		const bBoxWidth = viewerContainer.width();
		const bBoxHeight = viewerContainer.height();

		this.margin = {top: 0, right: 50, bottom: 30, left: 50};
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
			.attr("height", nrOfGenes * 7);

		// const zoom = d3.zoom()
		// 	.scaleExtent([1, 40])
		// 	.translateExtent([[0, 0], [this.width + 90, this.height + 90]])
		// 	.on("zoom", () => this.zoomed());

		// this.svg.call(zoom);

	}

	selectedGene(geneID) {
		if (this.svg)
		{
			this.svg.remove();
			this.svg = null;
		}

		const f = document.geneSelectionForm;
		const fd = new FormData(f);

		let screenID;

		const screenList = document.getElementById("screenList");
		const selected = screenList.selectedOptions;
		if (selected.length === 1) {
			screenID = selected.item(0).dataset.screen;
		}

		fd.set("screen", screenID);

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

		fetch(`ajax/gene-info/${geneID}`, { credentials: "include", method: "post", body: fd })
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
		if (this.xAxis != null)
		{
			this.plotData.attr('transform', d3.event.transform);
	
			const k = d3.event.transform.k;
	
			const x = d3.event.transform.rescaleX(this.x);
	
			this.insertionsData.selectAll("rect.ins")
				.attr("x", d => x(d));

			this.gX.call(this.xAxis.scale(x));
		}
	};

	setGene(data) {
		console.log(data);

		this.createSVG(data.genes.length);

		this.region = data;

		this.svg.select("text.x.axis-label")
			.text(`position at chromosome ${data.chrom}`);

		const x = this.adjustAxis();

		[
			{ low: false, y: 0, i: this.region.highPlus, n: "high-p", sense: this.region.geneStrand == '+' },
			{ low: false, y: 7, i: this.region.highMinus, n: "high-m", sense: this.region.geneStrand == '-' },
			{ low: true, y: 14, i: this.region.lowPlus, n: "low-p", sense: this.region.geneStrand == '+' },
			{ low: true, y: 21, i: this.region.lowMinus, n: "low-m", sense: this.region.geneStrand == '-' }
		].forEach(ii => {
			const r = this.insertionsData.selectAll(`rect.${ii.n}`)
				.data(ii.i, d => d);
		
			r.exit().remove();
			const l = r.enter()
				.append("rect")
				.attr("class", `ins ${ii.n}`)
				.attr("y", ii.y)
				.attr("height", 5)
				.attr("width", 1)
				// .attr("stroke", "#fbc")
				.attr("fill", d => {
					let color = "#888";
					if (ii.sense)
					{
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
			.attr("transform", g => `translate(0, ${30 + g.nr * 7})`)
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

	adjustAxis() {
		const xRange = [ this.region.start + 1, this.region.end + 1 ];

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
		// 	.attr("transform", d => `translate(${x(d.insertions)},${y(d.log2mi)})`);

		return x;
	}

}