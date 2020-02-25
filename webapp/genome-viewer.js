import * as d3 from "d3";
import ContextMenu from './contextMenu';

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
		this.svg = svg;

		this.svg.node().addEventListener('wheel', (evt) => {
			evt.stopPropagation();
			evt.preventDefault();
			return false;
		}, false);

		const plot = document.getElementById("plot");
		if (plot != null) {
			plot.addEventListener("clicked-gene", (event) => this.selectedGene(event.geneID));
		}

		const viewerContainer = $(this.svg.node());
		const bBoxWidth = viewerContainer.width();
		const bBoxHeight = viewerContainer.height();

		this.margin = {top: 0, right: 50, bottom: 25, left: 50};
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
			.attr("height", 20);

		// const zoom = d3.zoom()
		// 	.scaleExtent([1, 40])
		// 	.translateExtent([[0, 0], [this.width + 90, this.height + 90]])
		// 	.on("zoom", () => this.zoomed());

		// this.svg.call(zoom);

		// create the context menu
		const contextMenuDiv = document.getElementById("genome-viewer-context-menu");
		if (contextMenuDiv)
			this.contextMenu = new GenomeViewerContextMenu(this, "genome-viewer-context-menu");
	}

	selectedGene(geneID) {
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

		this.region = data;

		const x = this.adjustAxis();

		[
			{ low: false, y: 0, i: this.region.highPlus, n: "high-p", sense: this.region.geneStrand == '+' },
			{ low: false, y: 5, i: this.region.highMinus, n: "high-m", sense: this.region.geneStrand == '-' },
			{ low: true, y: 10, i: this.region.lowPlus, n: "low-p", sense: this.region.geneStrand == '+' },
			{ low: true, y: 15, i: this.region.lowMinus, n: "low-m", sense: this.region.geneStrand == '-' }
		].forEach(ii => {
			const r = this.insertionsData.selectAll(`rect.${ii.n}`)
				.data(ii.i, d => d);
		
			r.exit().remove();
			let l = r.enter()
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
		})


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